/**
 * @file audio_seqplayer_mm.c
 *
 * ComboShip: Majora's Mask's sequence-script interpreter, running inside Ocarina of Time's audio engine.
 *
 * MM transformation forms in Hyrule play MM's own sound effects natively: MM's sfx sequence (Sequence_0, read from
 * MM's resident ResourceManager) runs on an extra OOT sequence player (SEQ_PLAYER_COMBO_MM_SFX) whose `mmMode` flag
 * routes its sequence, channel and layer scripts here instead of to audio_seqplayer.c. MM's script language is a
 * superset of OOT's with different meanings for a few opcodes (channel 0xA0-0xA8 sfx-state/bit ops, 0xBB-0xBE comb
 * filter / start position / custom function, layer 0xF0/0xF1, seq 0xC2/0xC3, "delay 0" continues, a step2/step3 loop
 * per layer tick...), so this is a faithful port of mm/src/audio/lib/seqplayer.c onto OOT's structs: MM-only fields
 * live in the extension members appended to SequencePlayer/SequenceChannel/SequenceLayer (z64audio.h).
 *
 * Everything below a layer (note allocation, envelopes, vibrato/portamento, synthesis, the mixer) is OOT's own engine;
 * the few note-level MM rules (per-layer reverb/gain/vibrato selection, start sample position) are applied in
 * audio_playback.c / audio_effects.c behind SEQ_PLAYER_IS_MM(). Vanilla players 0-3 never reach this file.
 *
 * MM field -> OOT field: channel muted -> stopSomething2, muteFlags -> muteBehavior, targetReverbVol -> reverb,
 * gain -> unk_0C, combFilterSize -> unk_0F, combFilterGain -> unk_20, vibrato.vibratoDepth* -> vibratoExtent*,
 * stereoData -> stereo, seqScriptIO -> soundScriptIO, adsr.decayIndex -> adsr.releaseRate; layer muted ->
 * stopSomething, tunedSample -> sound, bend -> unk_34, unk_0A -> mmFlags; player bend -> unk_34, applyBend -> unk_0b1,
 * tempoChange -> unk_0C, storedFadeTimer -> fadeTimerUnkEu, unk_16 -> mmUnk16, maxTempo -> tempoInternalToExternal.
 */
#include <libultraship/libultra.h>
#include "global.h"

#include "soh/ResourceManagerHelpers.h"

extern char** sequenceMap;
extern size_t sequenceMapSize;

#define PROCESS_SCRIPT_END -1
#define SEQ_IO_VAL_NONE -1

#define PORTAMENTO_IS_SPECIAL(x) ((x).mode & 0x80)
#define PORTAMENTO_MODE(x) ((x).mode & ~0x80)
#define PORTAMENTO_MODE_OFF 0
#define PORTAMENTO_MODE_1 1
#define PORTAMENTO_MODE_2 2
#define PORTAMENTO_MODE_3 3
#define PORTAMENTO_MODE_4 4
#define PORTAMENTO_MODE_5 5

#define MUTE_FLAGS_STOP_LAYER (1 << 4)
#define MUTE_FLAGS_STOP_NOTES (1 << 6)
#define MUTE_FLAGS_STOP_SCRIPT (1 << 7)

#define SEQPLAYER_STATE_0 0
#define SEQPLAYER_STATE_FADE_IN 1
#define SEQPLAYER_STATE_FADE_OUT 2

// MM SequenceLayer.unk_0A bits (SequenceLayer.mmFlags)
#define MM_LAYER_REVERB_FROM_CHANNEL (1 << 2)
#define MM_LAYER_USE_CHANNEL_VIBRATO (1 << 3)
#define MM_LAYER_GAIN_FROM_CHANNEL (1 << 9)

u8 AudioSeq_ScriptReadU8(SeqScriptState* state);
s16 AudioSeq_ScriptReadS16(SeqScriptState* state);
u16 AudioSeq_ScriptReadCompressedU16(SeqScriptState* state);
void AudioSeq_SeqLayerDisable(SequenceLayer* layer);
void AudioSeq_SeqLayerFree(SequenceChannel* channel, s32 layerIdx);
void AudioSeq_SequenceChannelDisable(SequenceChannel* channel);
void AudioSeq_SequenceChannelEnable(SequencePlayer* seqPlayer, u8 channelIdx, void* script);
void AudioSeq_SequencePlayerSetupChannels(SequencePlayer* seqPlayer, u16 channelBits);
void AudioSeq_SequencePlayerDisable(SequencePlayer* seqPlayer);
void* AudioSeq_AudioListPopBack(AudioListItem* list);
void* AudioHeap_SearchCaches(s32 tableType, s32 cache, s32 id);
s32 AudioLoad_SlowLoadSample(s32 fontId, s32 instId, s8* isDone);

// Debug trace (COMBO_DEBUG_MMSEQ=<channel mask hex>): logs every script command of the traced MM sfx channels and
// their layers. Test harness only.
#include <stdio.h>
#include <stdlib.h>
static s32 sMmSeqTraceMask = -1;
static s32 AudioSeqMm_Trace(SequenceChannel* channel) {
    if (sMmSeqTraceMask < 0) {
        const char* env = getenv("COMBO_DEBUG_MMSEQ");
        sMmSeqTraceMask = (env != NULL) ? (s32)strtol(env, NULL, 16) : 0;
    }
    return (sMmSeqTraceMask >> channel->channelIndex) & 1;
}
#define MMSEQ_TRACE(channel, fmt, ...)                                                       \
    do {                                                                                     \
        if (AudioSeqMm_Trace(channel)) {                                                     \
            fprintf(stderr, "[mmseq ch%d] " fmt "\n", (channel)->channelIndex, __VA_ARGS__); \
        }                                                                                    \
    } while (0)

// audio_mm_sfx.c: MM's AUDIO_CUSTOM_FUNCTION_SEQ_0 (AudioSfx_SetFreqAndStereoBits).
u32 AudioMmSfx_SetFreqAndStereoBits(u8 seqScriptValIn, SequenceChannel* channel);
u8* AudioMmSfx_GetChannelState(s32 channelIndex);

/**
 * MM's sSeqInstructionArgsTable (0xA0-0xFF), bitpacked abcUUUnn: n = arg count, a/b/c = arg is s16.
 */
#define CMD_ARGS_0() 0
#define CMD_ARGS_1(arg0Type) (((sizeof(arg0Type) - 1) << 7) | 1)
#define CMD_ARGS_2(arg0Type, arg1Type) (((sizeof(arg0Type) - 1) << 7) | ((sizeof(arg1Type) - 1) << 6) | 2)
#define CMD_ARGS_3(arg0Type, arg1Type, arg2Type) \
    (((sizeof(arg0Type) - 1) << 7) | ((sizeof(arg1Type) - 1) << 6) | ((sizeof(arg2Type) - 1) << 5) | 3)

static const u8 sMmSeqInstructionArgsTable[] = {
    CMD_ARGS_1(s16),        // 0xA0 (channel: read from SfxChannelState using arg)
    CMD_ARGS_0(),           // 0xA1 (channel: read from SfxChannelState using unk_22)
    CMD_ARGS_1(s16),        // 0xA2 (channel: write to SfxChannelState using arg)
    CMD_ARGS_0(),           // 0xA3 (channel: write to SfxChannelState using unk_22)
    CMD_ARGS_1(u8),         // 0xA4 (channel: surround effect index)
    CMD_ARGS_0(),           // 0xA5 (channel: value += channelIndex)
    CMD_ARGS_2(u8, s16),    // 0xA6 (channel: write into sequence script, offset by channelIndex)
    CMD_ARGS_1(u8),         // 0xA7 (channel: shift value)
    CMD_ARGS_2(s16, s16),   // 0xA8 (channel: random range large)
    CMD_ARGS_0(),           // 0xA9
    CMD_ARGS_0(),           // 0xAA
    CMD_ARGS_0(),           // 0xAB
    CMD_ARGS_0(),           // 0xAC
    CMD_ARGS_0(),           // 0xAD
    CMD_ARGS_0(),           // 0xAE
    CMD_ARGS_0(),           // 0xAF
    CMD_ARGS_1(s16),        // 0xB0 (channel: set filter)
    CMD_ARGS_0(),           // 0xB1 (channel: clear filter)
    CMD_ARGS_1(s16),        // 0xB2 (channel: dynread sequence large)
    CMD_ARGS_1(u8),         // 0xB3 (channel: load filter)
    CMD_ARGS_0(),           // 0xB4 (channel: set dyntable large)
    CMD_ARGS_0(),           // 0xB5 (channel: read dyntable large)
    CMD_ARGS_0(),           // 0xB6 (channel: read dyntable)
    CMD_ARGS_1(s16),        // 0xB7 (channel: random large)
    CMD_ARGS_1(u8),         // 0xB8 (channel: random)
    CMD_ARGS_1(u8),         // 0xB9 (channel: set velocity random variance)
    CMD_ARGS_1(u8),         // 0xBA (channel: set gatetime random variance)
    CMD_ARGS_2(u8, s16),    // 0xBB (channel: comb filter)
    CMD_ARGS_1(s16),        // 0xBC (channel: add large)
    CMD_ARGS_1(s16),        // 0xBD (channel: start sample position)
    CMD_ARGS_1(u8),         // 0xBE (channel: custom function)
    CMD_ARGS_0(),           // 0xBF
    CMD_ARGS_0(),           // 0xC0
    CMD_ARGS_1(u8),         // 0xC1 (channel: set instrument)
    CMD_ARGS_1(s16),        // 0xC2 (channel: set dyntable)
    CMD_ARGS_0(),           // 0xC3 (channel: large notes off)
    CMD_ARGS_0(),           // 0xC4 (channel: large notes on)
    CMD_ARGS_0(),           // 0xC5 (channel: dyn set dyntable)
    CMD_ARGS_1(u8),         // 0xC6 (channel: set soundFont)
    CMD_ARGS_2(u8, s16),    // 0xC7 (channel: write into sequence script)
    CMD_ARGS_1(u8),         // 0xC8 (channel: subtract -> set value)
    CMD_ARGS_1(u8),         // 0xC9 (channel: `bit and` -> set value)
    CMD_ARGS_1(u8),         // 0xCA (channel: set mute behavior)
    CMD_ARGS_1(s16),        // 0xCB (channel: read sequence -> set value)
    CMD_ARGS_1(u8),         // 0xCC (channel: set value)
    CMD_ARGS_1(u8),         // 0xCD (channel: disable channel)
    CMD_ARGS_1(s16),        // 0xCE (channel: set unk_22)
    CMD_ARGS_1(s16),        // 0xCF (channel: write large into sequence script)
    CMD_ARGS_1(u8),         // 0xD0 (channel: stereo headset effects)
    CMD_ARGS_1(u8),         // 0xD1 (channel: set note allocation policy)
    CMD_ARGS_1(u8),         // 0xD2 (channel: set sustain)
    CMD_ARGS_1(u8),         // 0xD3 (channel: large bend pitch)
    CMD_ARGS_1(u8),         // 0xD4 (channel: set reverb)
    CMD_ARGS_1(u8),         // 0xD5
    CMD_ARGS_1(u8),         // 0xD6
    CMD_ARGS_1(u8),         // 0xD7 (channel: set vibrato rate)
    CMD_ARGS_1(u8),         // 0xD8 (channel: set vibrato depth)
    CMD_ARGS_1(u8),         // 0xD9 (channel: set decay index)
    CMD_ARGS_1(s16),        // 0xDA (channel: set envelope)
    CMD_ARGS_1(u8),         // 0xDB (channel: transpose)
    CMD_ARGS_1(u8),         // 0xDC (channel: set pan mix)
    CMD_ARGS_1(u8),         // 0xDD (channel: set pan)
    CMD_ARGS_1(s16),        // 0xDE (channel: set freqscale)
    CMD_ARGS_1(u8),         // 0xDF (channel: set volume)
    CMD_ARGS_1(u8),         // 0xE0 (channel: set volume scale)
    CMD_ARGS_3(u8, u8, u8), // 0xE1 (channel: set vibratorate linear)
    CMD_ARGS_3(u8, u8, u8), // 0xE2 (channel: set vibrato depth linear)
    CMD_ARGS_1(u8),         // 0xE3 (channel: set vibrato delay)
    CMD_ARGS_0(),           // 0xE4 (channel: dyncall)
    CMD_ARGS_1(u8),         // 0xE5 (channel: set reverb index)
    CMD_ARGS_1(u8),         // 0xE6 (channel: set book offset)
    CMD_ARGS_1(s16),        // 0xE7 (channel: read channel params from sequence)
    CMD_ARGS_3(u8, u8, u8), // 0xE8 (channel: set channel params)
    CMD_ARGS_1(u8),         // 0xE9 (channel: set note priority)
    CMD_ARGS_0(),           // 0xEA (channel: stop script)
    CMD_ARGS_2(u8, u8),     // 0xEB (channel: set soundFont and instrument)
    CMD_ARGS_0(),           // 0xEC (channel: reset vibrato)
    CMD_ARGS_1(u8),         // 0xED (channel: set hilo gain)
    CMD_ARGS_1(u8),         // 0xEE (channel: small bend pitch)
    CMD_ARGS_2(s16, u8),    // 0xEF
    CMD_ARGS_0(),           // 0xF0 (channel: unreserve notes)
    CMD_ARGS_1(u8),         // 0xF1 (channel: reserve notes)
    // Control flow instructions (>= 0xF2) can only have 0 or 1 args
    CMD_ARGS_1(u8),  // 0xF2 (branch relative if less than zero)
    CMD_ARGS_1(u8),  // 0xF3 (branch relative if equal to zero)
    CMD_ARGS_1(u8),  // 0xF4 (jump relative)
    CMD_ARGS_1(s16), // 0xF5 (branch if greater than or equal to zero)
    CMD_ARGS_0(),    // 0xF6 (break)
    CMD_ARGS_0(),    // 0xF7 (loop end)
    CMD_ARGS_1(u8),  // 0xF8 (loop)
    CMD_ARGS_1(s16), // 0xF9 (branch if less than zero)
    CMD_ARGS_1(s16), // 0xFA (branch if equal to zero)
    CMD_ARGS_1(s16), // 0xFB (jump)
    CMD_ARGS_1(s16), // 0xFC (call and jump to a function)
    CMD_ARGS_0(),    // 0xFD (delay n frames)
    CMD_ARGS_0(),    // 0xFE (delay 1 frame)
    CMD_ARGS_0(),    // 0xFF (end script)
};

static u16 AudioSeqMm_GetScriptControlFlowArgument(SeqScriptState* state, u8 cmd) {
    u8 highBits = sMmSeqInstructionArgsTable[cmd - 0xA0];
    u8 lowBits = highBits & 3;
    u16 cmdArg = 0;

    if (lowBits == 1) {
        if (!(highBits & 0x80)) {
            cmdArg = AudioSeq_ScriptReadU8(state);
        } else {
            cmdArg = AudioSeq_ScriptReadS16(state);
        }
    }
    return cmdArg;
}

static s32 AudioSeqMm_HandleScriptFlowControl(SequencePlayer* seqPlayer, SeqScriptState* state, s32 cmd, s32 cmdArg) {
    switch (cmd) {
        case 0xFF: // end script
            if (state->depth == 0) {
                return PROCESS_SCRIPT_END;
            }
            state->pc = state->stack[--state->depth];
            break;

        case 0xFD: // delay n frames
            return AudioSeq_ScriptReadCompressedU16(state);

        case 0xFE: // delay 1 frame
            return 1;

        case 0xFC: // call and jump to a function
            state->stack[state->depth++] = state->pc;
            state->pc = seqPlayer->seqData + (u16)cmdArg;
            break;

        case 0xF8: // loop
            state->remLoopIters[state->depth] = cmdArg;
            state->stack[state->depth++] = state->pc;
            break;

        case 0xF7: // loop end
            state->remLoopIters[state->depth - 1]--;
            if (state->remLoopIters[state->depth - 1] != 0) {
                state->pc = state->stack[state->depth - 1];
            } else {
                state->depth--;
            }
            break;

        case 0xF6: // break
            state->depth--;
            break;

        case 0xF5: // branch if greater than or equal to zero
        case 0xF9: // branch if less than zero
        case 0xFA: // branch if equal to zero
        case 0xFB: // jump
            if ((cmd == 0xFA) && (state->value != 0)) {
                break;
            }
            if ((cmd == 0xF9) && (state->value >= 0)) {
                break;
            }
            if ((cmd == 0xF5) && (state->value < 0)) {
                break;
            }
            state->pc = seqPlayer->seqData + (u16)cmdArg;
            break;

        case 0xF2: // branch relative if less than zero
        case 0xF3: // branch relative if equal to zero
        case 0xF4: // jump relative
            if ((cmd == 0xF3) && (state->value != 0)) {
                break;
            }
            if ((cmd == 0xF2) && (state->value >= 0)) {
                break;
            }
            state->pc += (s8)(cmdArg & 0xFF);
            break;
    }

    return 0;
}

/**
 * MM AudioScript_SeqChannelSetLayer: OOT's version plus MM's per-layer reverb/surround/flags/vibrato/bend.
 */
static s32 AudioSeqMm_SeqChannelSetLayer(SequenceChannel* channel, s32 layerIndex) {
    SequenceLayer* layer;

    if (channel->layers[layerIndex] == NULL) {
        layer = AudioSeq_AudioListPopBack(&gAudioContext.layerFreeList);
        channel->layers[layerIndex] = layer;
        if (layer == NULL) {
            channel->layers[layerIndex] = NULL;
            return -1;
        }
    } else {
        Audio_SeqLayerNoteDecay(channel->layers[layerIndex]);
    }

    layer = channel->layers[layerIndex];

    layer->channel = channel;
    layer->adsr = channel->adsr;
    layer->adsr.releaseRate = 0;
    layer->targetReverbVol = channel->reverb;
    layer->enabled = true;
    layer->finished = false;
    layer->stopSomething = false;
    layer->continuousNotes = false;
    layer->bit3 = false;
    layer->ignoreDrumPan = false;
    layer->bit1 = false;
    layer->notePropertiesNeedInit = false;
    layer->gateTime = 0x80;
    layer->surroundEffectIndex = 0x80;
    layer->stereo.asByte = 0;
    layer->portamento.mode = PORTAMENTO_MODE_OFF;
    layer->scriptState.depth = 0;
    layer->pan = 0x40;
    layer->transposition = 0;
    layer->delay = 0;
    layer->gateDelay = 0;
    layer->delay2 = 0;
    layer->note = NULL;
    layer->instrument = NULL;
    layer->instOrWave = 0xFF;
    layer->mmFlags = 0xFFFF;
    // MM's per-layer VibratoSubStruct is never changed by a script, so it always holds MM's defaults (rate 0x800,
    // depth 0, no delay): a layer that leaves the channel vibrato (layer opcode 0xF0) gets none (audio_effects.c).
    layer->freqScale = 1.0f;
    layer->unk_34 = 1.0f; // bend
    layer->velocitySquare2 = 0.0f;

    return 0;
}

static u8 AudioSeqMm_GetInstrument(SequenceChannel* channel, u8 instId, Instrument** instOut, AdsrSettings* adsr) {
    Instrument* inst = Audio_GetInstrumentInner(channel->fontId, instId);

    if (inst == NULL) {
        *instOut = NULL;
        return 0;
    }

    adsr->envelope = inst->envelope;
    adsr->releaseRate = inst->releaseRate;

    *instOut = inst;

    // temporarily offset instrument id by 2 so that instId 0, 1 can be reserved by drums and sfxs respectively.
    instId += 2;

    return instId;
}

static void AudioSeqMm_SetInstrument(SequenceChannel* channel, u8 instId) {
    if (instId >= 0x80) {
        // Synthetic Waves
        channel->instOrWave = instId;
        channel->instrument = NULL;
    } else if (instId == 0x7F) {
        // Drums
        channel->instOrWave = 0;
        channel->instrument = (Instrument*)1; // invalid pointer, never dereferenced
    } else if (instId == 0x7E) {
        // Sfxs
        channel->instOrWave = 1;
        channel->instrument = (Instrument*)2; // invalid pointer, never dereferenced
    } else {
        // Instruments
        if ((channel->instOrWave = AudioSeqMm_GetInstrument(channel, instId, &channel->instrument, &channel->adsr)) ==
            0) {
            channel->hasInstrument = false;
            return;
        }
    }

    channel->hasInstrument = true;
}

static void AudioSeqMm_SetChannelPriorities(SequenceChannel* channel, u8 priority) {
    if ((priority & 0xF) != 0) {
        channel->notePriority = priority & 0xF;
    }

    priority = priority >> 4;
    if (priority != 0) {
        channel->someOtherPriority = priority;
    }
}

// ---- layer ---------------------------------------------------------------------------------------------------------

static void AudioSeqMm_SeqLayerProcessScriptStep1(SequenceLayer* layer) {
    if (!layer->continuousNotes) {
        Audio_SeqLayerNoteDecay(layer);
    } else if ((layer->note != NULL) && (layer->note->playbackState.wantedParentLayer == layer)) {
        Audio_SeqLayerNoteDecay(layer);
    }

    if ((PORTAMENTO_MODE(layer->portamento) == PORTAMENTO_MODE_1) ||
        (PORTAMENTO_MODE(layer->portamento) == PORTAMENTO_MODE_2)) {
        layer->portamento.mode = PORTAMENTO_MODE_OFF;
    }
    layer->notePropertiesNeedInit = true;
}

static s32 AudioSeqMm_SeqLayerProcessScriptStep5(SequenceLayer* layer, s32 sameTunedSample) {
    Note* note;

    if ((layer->continuousNotes == true) && (layer->bit1 == true)) {
        return 0;
    }

    if ((layer->continuousNotes == true) && (layer->note != NULL) && layer->bit3 && (sameTunedSample == true) &&
        (layer->note->playbackState.parentLayer == layer)) {
        if (layer->sound == NULL) {
            Audio_InitSyntheticWave(layer->note, layer);
        }
    } else {
        if (!sameTunedSample) {
            Audio_SeqLayerNoteDecay(layer);
        }

        layer->note = Audio_AllocNote(layer);

        if (layer->note != NULL) {
            note = layer->note;

            if (note->playbackState.parentLayer == layer) {
                Audio_NoteVibratoInit(note);
            }
        }
    }

    if ((layer->note != NULL) && (layer->note->playbackState.parentLayer == layer)) {
        note = layer->note;

        Audio_NotePortamentoInit(note);
    }

    return 0;
}

static s32 AudioSeqMm_SeqLayerProcessScriptStep2(SequenceLayer* layer) {
    SequenceChannel* channel = layer->channel;
    SeqScriptState* state = &layer->scriptState;
    SequencePlayer* seqPlayer = channel->seqPlayer;
    u8 cmd;
    u8 cmdArg8;
    u16 cmdArg16;
    u16 velocity;

    while (true) {
        cmd = AudioSeq_ScriptReadU8(state);

        // Note Commands, processed in step 3
        if (cmd <= 0xC0) {
            return cmd;
        }

        // Control Flow Commands
        if (cmd >= 0xF2) {
            cmdArg16 = AudioSeqMm_GetScriptControlFlowArgument(state, cmd);

            if (AudioSeqMm_HandleScriptFlowControl(seqPlayer, state, cmd, cmdArg16) == 0) {
                continue;
            }
            AudioSeq_SeqLayerDisable(layer);
            return PROCESS_SCRIPT_END;
        }

        switch (cmd) {
            case 0xC1: // layer: set short note velocity
            case 0xCA: // layer: set pan
                cmdArg8 = *(state->pc++);
                if (cmd == 0xC1) {
                    layer->velocitySquare = SQ(cmdArg8) / SQ(127.0f);
                } else {
                    layer->pan = cmdArg8;
                }
                break;

            case 0xC9: // layer: set short note gatetime
            case 0xC2: // layer: set transposition in semitones
                cmdArg8 = *(state->pc++);
                if (cmd == 0xC9) {
                    layer->gateTime = cmdArg8;
                } else {
                    layer->transposition = cmdArg8;
                }
                break;

            case 0xC4: // layer: continuous notes on
            case 0xC5: // layer: continuous notes off
                if (cmd == 0xC4) {
                    layer->continuousNotes = true;
                } else {
                    layer->continuousNotes = false;
                }
                layer->bit1 = false;
                Audio_SeqLayerNoteDecay(layer);
                break;

            case 0xC3: // layer: set short note default delay
                cmdArg16 = AudioSeq_ScriptReadCompressedU16(state);
                layer->shortNoteDefaultDelay = cmdArg16;
                break;

            case 0xC6: // layer: set instrument
                cmd = AudioSeq_ScriptReadU8(state);
                if (cmd >= 0x7E) {
                    if (cmd == 0x7E) {
                        // Sfxs
                        layer->instOrWave = 1;
                    } else if (cmd == 0x7F) {
                        // Drums
                        layer->instOrWave = 0;
                    } else {
                        // Synthetic Wave
                        layer->instOrWave = cmd;
                        layer->instrument = NULL;
                    }

                    if (cmd == 0xFF) {
                        layer->adsr.releaseRate = 0;
                    }
                } else {
                    // Instrument
                    if ((layer->instOrWave =
                             AudioSeqMm_GetInstrument(channel, cmd, &layer->instrument, &layer->adsr)) == 0) {
                        layer->instOrWave = 0xFF;
                    }
                }
                break;

            case 0xC7: // layer: enable portamento
                layer->portamento.mode = AudioSeq_ScriptReadU8(state);

                cmd = AudioSeq_ScriptReadU8(state);
                cmd += channel->transposition;
                cmd += layer->transposition;
                cmd += seqPlayer->transposition;

                if (cmd >= 0x80) {
                    cmd = 0;
                }

                layer->portamentoTargetNote = cmd;

                // If special, the next param is u8 instead of var
                if (PORTAMENTO_IS_SPECIAL(layer->portamento)) {
                    layer->portamentoTime = *(state->pc++);
                    break;
                }

                cmdArg16 = AudioSeq_ScriptReadCompressedU16(state);
                layer->portamentoTime = cmdArg16;
                break;

            case 0xC8: // layer: disable portamento
                layer->portamento.mode = PORTAMENTO_MODE_OFF;
                break;

            case 0xCB: // layer: set envelope and decay index
                cmdArg16 = AudioSeq_ScriptReadS16(state);
                layer->adsr.envelope = (AdsrEnvelope*)(seqPlayer->seqData + cmdArg16);
                // fallthrough
            case 0xCF: // layer: set decay index
                layer->adsr.releaseRate = AudioSeq_ScriptReadU8(state);
                break;

            case 0xCC: // layer: ignore drum pan
                layer->ignoreDrumPan = true;
                break;

            case 0xCD: // layer: stereo effects
                layer->stereo.asByte = AudioSeq_ScriptReadU8(state);
                break;

            case 0xCE: // layer: bend pitch
                cmdArg8 = AudioSeq_ScriptReadU8(state);
                layer->unk_34 = gBendPitchTwoSemitonesFrequencies[(u8)(cmdArg8 + 0x80)];
                break;

            case 0xF0: // layer: clear MM layer flags (reverb/vibrato/gain no longer follow the channel)
                cmdArg16 = AudioSeq_ScriptReadS16(state);
                layer->mmFlags &= (cmdArg16 ^ 0xFFFF);
                break;

            case 0xF1: // layer: surround effect index
                layer->surroundEffectIndex = AudioSeq_ScriptReadU8(state);
                break;

            default:
                switch (cmd & 0xF0) {
                    case 0xD0: // layer: set short note velocity from table
                        velocity = seqPlayer->shortNoteVelocityTable[cmd & 0xF];
                        layer->velocitySquare = SQ(velocity) / SQ(127.0f);
                        break;

                    case 0xE0: // layer: set short note gatetime from table
                        layer->gateTime = seqPlayer->shortNoteGateTimeTable[cmd & 0xF];
                        break;
                }
        }
    }
}

static s32 AudioSeqMm_SeqLayerProcessScriptStep4(SequenceLayer* layer, s32 cmd) {
    s32 sameTunedSample = true;
    s32 instOrWave;
    s32 speed;
    f32 temp_f14;
    f32 temp_f2;
    Portamento* portamento = &layer->portamento;
    f32 freqScale;
    f32 freqScale2;
    SoundFontSound* tunedSample;
    Instrument* instrument;
    Drum* drum;
    SoundFontSound* soundEffect;
    SequenceChannel* channel;
    SequencePlayer* seqPlayer;
    u8 semitone = cmd;
    u16 sfxId;
    s32 semitone2;
    s32 velocity;
    f32 time;
    f32 tuning;
    s32 speed2;

    instOrWave = layer->instOrWave;
    channel = layer->channel;
    seqPlayer = channel->seqPlayer;

    if (instOrWave == 0xFF) {
        if (!channel->hasInstrument) {
            return PROCESS_SCRIPT_END;
        }
        instOrWave = channel->instOrWave;
    }

    switch (instOrWave) {
        case 0:
            // Drums
            semitone += channel->transposition + layer->transposition;
            layer->semitone = semitone;

            drum = Audio_GetDrum(channel->fontId, semitone);
            if (drum == NULL) {
                layer->stopSomething = true;
                layer->delay2 = layer->delay;
                return PROCESS_SCRIPT_END;
            }

            tunedSample = &drum->sound;
            layer->adsr.envelope = drum->envelope;
            layer->adsr.releaseRate = drum->releaseRate;
            if (!layer->ignoreDrumPan) {
                layer->pan = drum->pan;
            }

            layer->sound = tunedSample;
            layer->freqScale = tunedSample->tuning;
            break;

        case 1:
            // Sfxs
            layer->semitone = semitone;
            sfxId = (layer->transposition << 6) + semitone;

            soundEffect = Audio_GetSfx(channel->fontId, sfxId);
            MMSEQ_TRACE(channel, "layer sfx font %u id %u -> %p", channel->fontId, sfxId, (void*)soundEffect);
            if (soundEffect == NULL) {
                layer->stopSomething = true;
                layer->delay2 = layer->delay + 1;
                return PROCESS_SCRIPT_END;
            }

            tunedSample = soundEffect;
            layer->sound = tunedSample;
            layer->freqScale = tunedSample->tuning;
            break;

        default:
            semitone += seqPlayer->transposition + channel->transposition + layer->transposition;
            semitone2 = semitone;

            layer->semitone = semitone;
            if (semitone >= 0x80) {
                layer->stopSomething = true;
                return PROCESS_SCRIPT_END;
            }

            if (layer->instOrWave == 0xFF) {
                instrument = channel->instrument;
            } else {
                instrument = layer->instrument;
            }

            if (layer->portamento.mode != PORTAMENTO_MODE_OFF) {
                velocity = (semitone > layer->portamentoTargetNote) ? semitone : layer->portamentoTargetNote;

                if (instrument != NULL) {
                    tunedSample = Audio_InstrumentGetSound(instrument, velocity);
                    sameTunedSample = (layer->sound == tunedSample);
                    layer->sound = tunedSample;
                    tuning = tunedSample->tuning;
                } else {
                    layer->sound = NULL;
                    tuning = 1.0f;
                    if (instOrWave >= 0xC0) {
                        layer->sound = &gAudioContext.synthesisReverbs[instOrWave - 0xC0].sound;
                    }
                }

                temp_f2 = gNoteFrequencies[semitone2] * tuning;
                temp_f14 = gNoteFrequencies[layer->portamentoTargetNote] * tuning;

                switch (PORTAMENTO_MODE(*portamento)) {
                    case PORTAMENTO_MODE_1:
                    case PORTAMENTO_MODE_3:
                    case PORTAMENTO_MODE_5:
                        freqScale2 = temp_f2;
                        freqScale = temp_f14;
                        break;

                    case PORTAMENTO_MODE_2:
                    case PORTAMENTO_MODE_4:
                        freqScale = temp_f2;
                        freqScale2 = temp_f14;
                        break;

                    default:
                        freqScale = temp_f2;
                        freqScale2 = temp_f2;
                        break;
                }

                portamento->extent = (freqScale2 / freqScale) - 1.0f;

                if (PORTAMENTO_IS_SPECIAL(*portamento)) {
                    speed = seqPlayer->tempo * 0x8000 / gAudioContext.tempoInternalToExternal;
                    if (layer->delay != 0) {
                        speed = speed * 0x100 / (layer->delay * layer->portamentoTime);
                    }
                } else {
                    speed = 0x20000 / (layer->portamentoTime * gAudioContext.audioBufferParameters.updatesPerFrame);
                }

                if (speed >= 0x7FFF) {
                    speed = 0x7FFF;
                } else if (speed < 1) {
                    speed = 1;
                }

                portamento->speed = speed;
                portamento->cur = 0;
                layer->freqScale = freqScale;
                if (PORTAMENTO_MODE(*portamento) == PORTAMENTO_MODE_5) {
                    layer->portamentoTargetNote = semitone;
                }
                break;
            }

            MMSEQ_TRACE(channel, "layer inst %p semitone %d font %u", (void*)instrument, semitone, channel->fontId);
            if (instrument != NULL) {
                tunedSample = Audio_InstrumentGetSound(instrument, semitone);
                sameTunedSample = (tunedSample == layer->sound);
                layer->sound = tunedSample;
                layer->freqScale = gNoteFrequencies[semitone2] * tunedSample->tuning;
            } else {
                layer->sound = NULL;
                layer->freqScale = gNoteFrequencies[semitone2];
                if (instOrWave >= 0xC0) {
                    layer->sound = &gAudioContext.synthesisReverbs[instOrWave - 0xC0].sound;
                }
            }
            break;
    }

    layer->delay2 = layer->delay;
    layer->freqScale *= layer->unk_34;

    if (layer->delay == 0) {
        if (layer->sound != NULL) {
            time = layer->sound->sample->loop->loopEnd;
        } else {
            time = 0.0f;
        }
        time *= seqPlayer->tempo;
        time *= gAudioContext.unk_2870;
        time /= layer->freqScale;
        if (time > 0x7FFE) {
            time = 0x7FFE;
        }

        layer->gateDelay = 0;
        layer->delay = (u16)(s32)time + 1;

        if (layer->portamento.mode != PORTAMENTO_MODE_OFF) {
            if (PORTAMENTO_IS_SPECIAL(*portamento)) {
                speed2 = seqPlayer->tempo * 0x8000 / gAudioContext.tempoInternalToExternal;
                speed2 = speed2 * 0x100 / (layer->delay * layer->portamentoTime);
                if (speed2 >= 0x7FFF) {
                    speed2 = 0x7FFF;
                } else if (speed2 < 1) {
                    speed2 = 1;
                }
                portamento->speed = speed2;
            }
        }
    }
    return sameTunedSample;
}

static s32 AudioSeqMm_SeqLayerProcessScriptStep3(SequenceLayer* layer, s32 cmd) {
    SeqScriptState* state = &layer->scriptState;
    u16 delay;
    s32 velocity;
    SequenceChannel* channel = layer->channel;
    SequencePlayer* seqPlayer = channel->seqPlayer;
    s32 intDelta;
    f32 floatDelta;

    if (cmd == 0xC0) { // layer: delay
        layer->delay = AudioSeq_ScriptReadCompressedU16(state);
        layer->stopSomething = true;
        layer->bit1 = false;
        return PROCESS_SCRIPT_END;
    }

    layer->stopSomething = false;

    if (channel->largeNotes == true) {
        switch (cmd & 0xC0) {
            case 0x00: // layer: large note 0
                delay = AudioSeq_ScriptReadCompressedU16(state);
                velocity = *(state->pc++);
                layer->gateTime = *(state->pc++);
                layer->lastDelay = delay;
                break;

            case 0x40: // layer: large note 1
                delay = AudioSeq_ScriptReadCompressedU16(state);
                velocity = *(state->pc++);
                layer->gateTime = 0;
                layer->lastDelay = delay;
                break;

            case 0x80: // layer: large note 2
                delay = layer->lastDelay;
                velocity = *(state->pc++);
                layer->gateTime = *(state->pc++);
                break;
        }

        if ((velocity > 0x7F) || (velocity < 0)) {
            velocity = 0x7F;
        }
        layer->velocitySquare = SQ((f32)velocity) / SQ(127.0f);
        cmd -= (cmd & 0xC0);
    } else {
        switch (cmd & 0xC0) {
            case 0x00: // layer: small note 0
                delay = AudioSeq_ScriptReadCompressedU16(state);
                layer->lastDelay = delay;
                break;

            case 0x40: // layer: small note 1
                delay = layer->shortNoteDefaultDelay;
                break;

            case 0x80: // layer: small note 2
                delay = layer->lastDelay;
                break;
        }
        cmd -= (cmd & 0xC0);
    }

    if (channel->velocityRandomVariance != 0) {
        floatDelta = layer->velocitySquare * (gAudioContext.audioRandom % channel->velocityRandomVariance) / 100.0f;
        if ((gAudioContext.audioRandom & 0x8000) != 0) {
            floatDelta = -floatDelta;
        }

        layer->velocitySquare2 = layer->velocitySquare + floatDelta;

        if (layer->velocitySquare2 < 0.0f) {
            layer->velocitySquare2 = 0.0f;
        } else if (layer->velocitySquare2 > 1.0f) {
            layer->velocitySquare2 = 1.0f;
        }
    } else {
        layer->velocitySquare2 = layer->velocitySquare;
    }

    layer->delay = delay;
    layer->gateDelay = (layer->gateTime * delay) >> 8;

    if (channel->gateTimeRandomVariance != 0) {
        //! @bug should probably be gateTimeRandomVariance (as in MM and OOT)
        intDelta = (layer->gateDelay * (gAudioContext.audioRandom % channel->velocityRandomVariance)) / 100;
        if ((gAudioContext.audioRandom & 0x4000) != 0) {
            intDelta = -intDelta;
        }

        layer->gateDelay += intDelta;
        if (layer->gateDelay < 0) {
            layer->gateDelay = 0;
        } else if (layer->gateDelay > layer->delay) {
            layer->gateDelay = layer->delay;
        }
    }

    if ((seqPlayer->muted && (channel->muteBehavior & (MUTE_FLAGS_STOP_NOTES | MUTE_FLAGS_STOP_LAYER))) ||
        channel->stopSomething2) {
        layer->stopSomething = true;
        return PROCESS_SCRIPT_END;
    }

    if (seqPlayer->skipTicks != 0) {
        layer->stopSomething = true;
        return PROCESS_SCRIPT_END;
    }

    return cmd;
}

static void AudioSeqMm_SeqLayerProcessScript(SequenceLayer* layer) {
    s32 cmd;

    if (!layer->enabled) {
        return;
    }

    if (layer->delay > 1) {
        layer->delay--;
        if (!layer->stopSomething && (layer->delay <= layer->gateDelay)) {
            Audio_SeqLayerNoteDecay(layer);
            layer->stopSomething = true;
        }
        return;
    }

    AudioSeqMm_SeqLayerProcessScriptStep1(layer);

    do {
        cmd = AudioSeqMm_SeqLayerProcessScriptStep2(layer);
        if (cmd == PROCESS_SCRIPT_END) {
            return;
        }

        cmd = AudioSeqMm_SeqLayerProcessScriptStep3(layer, cmd);

    } while ((cmd == -1) && (layer->delay == 0));

    if (cmd != PROCESS_SCRIPT_END) {
        // returns `sameTunedSample` instead of a command
        cmd = AudioSeqMm_SeqLayerProcessScriptStep4(layer, cmd);
    }

    if (cmd != PROCESS_SCRIPT_END) {
        AudioSeqMm_SeqLayerProcessScriptStep5(layer, cmd);
    }

    if (layer->stopSomething == true) {
        if ((layer->note != NULL) || layer->continuousNotes) {
            Audio_SeqLayerNoteDecay(layer);
        }
    }
}

// ---- channel -------------------------------------------------------------------------------------------------------

// MM's per-sequence font table lookup (channel opcodes 0xC6/0xEB): the sequence's font list, newest first. The MM
// sequence registered for this player carries OOT font ids (ComboMmAudio.cpp remaps them at registration).
static s32 AudioSeqMm_LookupSeqFont(SequencePlayer* seqPlayer, u16 index, u16* fontOut) {
    SequenceData sDat;

    if ((seqPlayer->seqId >= sequenceMapSize + 0xF) || (sequenceMap[seqPlayer->seqId] == NULL)) {
        return false;
    }
    sDat = ResourceMgr_LoadSeqByName(sequenceMap[seqPlayer->seqId]);
    // The game would sometimes do negative array lookups, rejected by AudioHeap_SearchCaches (2S2H's guard).
    if (index >= sDat.numFonts) {
        return false;
    }
    *fontOut = sDat.fonts[sDat.numFonts - index - 1];
    return true;
}

static void AudioSeqMm_SequenceChannelProcessScript(SequenceChannel* channel) {
    s32 i;
    u8* data;
    u32 rand;
    SequencePlayer* seqPlayer;

    if (channel->stopScript) {
        goto exit_loop;
    }

    seqPlayer = channel->seqPlayer;
    if (seqPlayer->muted && (channel->muteBehavior & MUTE_FLAGS_STOP_SCRIPT)) {
        return;
    }

    if (channel->delay >= 2) {
        channel->delay--;
        goto exit_loop;
    }

    while (true) {
        SeqScriptState* scriptState = &channel->scriptState;
        s32 param;
        s16 temp1;
        u16 cmdArgU16;
        u32 cmdArgs[3];
        s8 cmdArgS8;
        u8* tracePc = scriptState->pc;
        u8 cmd = AudioSeq_ScriptReadU8(scriptState);
        u8 lowBits;
        u8 highBits;
        s32 delay;
        s32 temp2;
        u8 phi_v0_3;
        u8 new_var;
        u8 depth;
        u8* seqData = seqPlayer->seqData;
        u32 new_var2;

        // Commands 0xA0 - 0xFF
        if (cmd >= 0xA0) {
            highBits = sMmSeqInstructionArgsTable[cmd - 0xA0];
            lowBits = highBits & 3;

            // read in arguments for the instruction
            for (i = 0; i < lowBits; i++, highBits <<= 1) {
                if (!(highBits & 0x80)) {
                    cmdArgs[i] = AudioSeq_ScriptReadU8(scriptState);
                } else {
                    cmdArgs[i] = AudioSeq_ScriptReadS16(scriptState);
                }
            }

            MMSEQ_TRACE(channel, "%04X: %02X %X %X value %d", (u32)(tracePc - seqPlayer->seqData), cmd,
                        (lowBits > 0) ? cmdArgs[0] : 0, (lowBits > 1) ? cmdArgs[1] : 0, scriptState->value);
            // Control Flow Commands
            if (cmd >= 0xF2) {
                delay = AudioSeqMm_HandleScriptFlowControl(seqPlayer, scriptState, cmd, cmdArgs[0]);

                if (delay != 0) {
                    if (delay == PROCESS_SCRIPT_END) {
                        AudioSeq_SequenceChannelDisable(channel);
                    } else {
                        channel->delay = delay;
                    }
                    break;
                }
                continue;
            }

            switch (cmd) {
                case 0xEA: // channel: stop script
                    channel->stopScript = true;
                    goto exit_loop;

                case 0xF1: // channel: reserve notes
                    Audio_NotePoolClear(&channel->notePool);
                    cmd = (u8)cmdArgs[0];
                    Audio_NotePoolFill(&channel->notePool, cmd);
                    break;

                case 0xF0: // channel: unreserve notes
                    Audio_NotePoolClear(&channel->notePool);
                    break;

                case 0xC2: // channel: set dyntable
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->dynTable = (void*)&seqPlayer->seqData[cmdArgU16];
                    break;

                case 0xC5: // channel: dyn set dyntable
                    if (scriptState->value != -1) {
                        data = (*channel->dynTable)[scriptState->value];
                        cmdArgU16 = (u16)((data[0] << 8) + data[1]);
                        scriptState->pc = (void*)&seqPlayer->seqData[cmdArgU16];
                    }
                    break;

                case 0xEB: { // channel: set soundFont and instrument
                    u16 result = (u16)cmdArgs[0];
                    s32 found = true;

                    if (seqPlayer->defaultFont != FONT_ID_NONE) {
                        found = AudioSeqMm_LookupSeqFont(seqPlayer, result, &result);
                    }
                    if (found && AudioHeap_SearchCaches(FONT_TABLE, CACHE_EITHER, result)) {
                        channel->fontId = result;
                    }

                    cmdArgs[0] = cmdArgs[1];
                }
                    // fallthrough
                case 0xC1: // channel: set instrument
                    cmd = (u8)cmdArgs[0];
                    AudioSeqMm_SetInstrument(channel, cmd);
                    break;

                case 0xC3: // channel: large notes off
                    channel->largeNotes = false;
                    break;

                case 0xC4: // channel: large notes on
                    channel->largeNotes = true;
                    break;

                case 0xDF: // channel: set volume
                    cmd = (u8)cmdArgs[0];
                    channel->volume = (s32)cmd / 127.0f;
                    channel->changes.s.volume = true;
                    break;

                case 0xE0: // channel: set volume scale
                    cmd = (u8)cmdArgs[0];
                    channel->volumeScale = (f32)(s32)cmd / 128.0f;
                    channel->changes.s.volume = true;
                    break;

                case 0xDE: // channel: set freqscale
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->freqScale = (f32)(s32)cmdArgU16 / 0x8000;
                    channel->changes.s.freqScale = true;
                    break;

                case 0xD3: // channel: large bend pitch
                    cmd = (u8)cmdArgs[0];
                    cmd += 0x80;
                    channel->freqScale = gBendPitchOneOctaveFrequencies[cmd];
                    channel->changes.s.freqScale = true;
                    break;

                case 0xEE: // channel: small bend pitch
                    cmd = (u8)cmdArgs[0];
                    cmd += 0x80;
                    channel->freqScale = gBendPitchTwoSemitonesFrequencies[cmd];
                    channel->changes.s.freqScale = true;
                    break;

                case 0xDD: // channel: set pan
                    cmd = (u8)cmdArgs[0];
                    channel->newPan = cmd;
                    channel->changes.s.pan = true;
                    break;

                case 0xDC: // channel: set pan mix
                    cmd = (u8)cmdArgs[0];
                    channel->panChannelWeight = cmd;
                    channel->changes.s.pan = true;
                    break;

                case 0xDB: // channel: transpose
                    cmdArgS8 = (s8)cmdArgs[0];
                    channel->transposition = cmdArgS8;
                    break;

                case 0xDA: // channel: set envelope
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->adsr.envelope = (AdsrEnvelope*)&seqPlayer->seqData[cmdArgU16];
                    break;

                case 0xD9: // channel: set decay index
                    cmd = (u8)cmdArgs[0];
                    channel->adsr.releaseRate = cmd;
                    break;

                case 0xD8: // channel: set vibrato depth
                    cmd = (u8)cmdArgs[0];
                    channel->vibratoExtentTarget = cmd * 8;
                    channel->vibratoExtentStart = 0;
                    channel->vibratoExtentChangeDelay = 0;
                    break;

                case 0xD7: // channel: set vibrato rate
                    cmd = (u8)cmdArgs[0];
                    channel->vibratoRateChangeDelay = 0;
                    channel->vibratoRateTarget = cmd * 32;
                    channel->vibratoRateStart = cmd * 32;
                    break;

                case 0xE2: // channel: set vibrato depth linear
                    cmd = (u8)cmdArgs[0];
                    channel->vibratoExtentStart = cmd * 8;
                    cmd = (u8)cmdArgs[1];
                    channel->vibratoExtentTarget = cmd * 8;
                    cmd = (u8)cmdArgs[2];
                    channel->vibratoExtentChangeDelay = cmd * 16;
                    break;

                case 0xE1: // channel: set vibratorate linear
                    cmd = (u8)cmdArgs[0];
                    channel->vibratoRateStart = cmd * 32;
                    cmd = (u8)cmdArgs[1];
                    channel->vibratoRateTarget = cmd * 32;
                    cmd = (u8)cmdArgs[2];
                    channel->vibratoRateChangeDelay = cmd * 16;
                    break;

                case 0xE3: // channel: set vibrato delay
                    cmd = (u8)cmdArgs[0];
                    channel->vibratoDelay = cmd * 16;
                    break;

                case 0xD4: // channel: set reverb volume
                    cmd = (u8)cmdArgs[0];
                    channel->reverb = cmd;
                    break;

                case 0xC6: { // channel: set soundFont
                    u16 result = (u16)cmdArgs[0];

                    if (seqPlayer->defaultFont != FONT_ID_NONE) {
                        if (!AudioSeqMm_LookupSeqFont(seqPlayer, result, &result)) {
                            break;
                        }
                    }

                    if (AudioHeap_SearchCaches(FONT_TABLE, CACHE_EITHER, result)) {
                        channel->fontId = result;
                    }
                    break;
                }

                case 0xC7: // channel: write into sequence script
                    cmd = (u8)cmdArgs[0];
                    cmdArgU16 = (u16)cmdArgs[1];
                    seqData = &seqPlayer->seqData[cmdArgU16];
                    seqData[0] = (u8)scriptState->value + cmd;
                    break;

                case 0xC8: // channel: subtract -> set value
                case 0xCC: // channel: set value
                case 0xC9: // channel: `bit and` -> set value
                    cmdArgS8 = (s8)cmdArgs[0];

                    if (cmd == 0xC8) {
                        scriptState->value -= cmdArgS8;
                    } else if (cmd == 0xCC) {
                        scriptState->value = cmdArgS8;
                    } else {
                        scriptState->value &= cmdArgS8;
                    }
                    break;

                case 0xCD: // channel: disable channel
                    cmd = (u8)cmdArgs[0];
                    AudioSeq_SequenceChannelDisable(seqPlayer->channels[cmd]);
                    break;

                case 0xCA: // channel: set mute behavior
                    cmd = (u8)cmdArgs[0];
                    channel->muteBehavior = cmd;
                    channel->changes.s.volume = true;
                    break;

                case 0xCB: // channel: read sequence -> set value
                    cmdArgU16 = (u16)cmdArgs[0];
                    scriptState->value = *(seqPlayer->seqData + (u32)(cmdArgU16 + scriptState->value));
                    break;

                case 0xCE: // channel: set unk_22
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->unk_22 = cmdArgU16;
                    break;

                case 0xCF: // channel: write large into sequence script
                    cmdArgU16 = (u16)cmdArgs[0];
                    seqData = &seqPlayer->seqData[cmdArgU16];
                    seqData[0] = (channel->unk_22 >> 8) & 0xFF;
                    seqData[1] = channel->unk_22 & 0xFF;
                    break;

                case 0xD0: // channel: stereo headset effects
                    cmd = (u8)cmdArgs[0];
                    if (cmd & 0x80) {
                        channel->stereoHeadsetEffects = true;
                    } else {
                        channel->stereoHeadsetEffects = false;
                    }
                    channel->stereo.asByte = cmd & 0x7F;
                    break;

                case 0xD1: // channel: set note allocation policy
                    cmd = (u8)cmdArgs[0];
                    channel->noteAllocPolicy = cmd;
                    break;

                case 0xD2: // channel: set sustain
                    cmd = (u8)cmdArgs[0];
                    channel->adsr.sustain = cmd;
                    break;

                case 0xE5: // channel: set reverb index
                    cmd = (u8)cmdArgs[0];
                    channel->reverbIndex = cmd;
                    break;

                case 0xE4: // channel: dyncall
                    if (scriptState->value != -1) {
                        data = (*channel->dynTable)[scriptState->value];
                        depth = scriptState->depth;
                        //! @bug: Missing a stack depth check here (as in MM)
                        scriptState->stack[depth] = scriptState->pc;
                        scriptState->depth++;
                        cmdArgU16 = (u16)((data[0] << 8) + data[1]);
                        scriptState->pc = seqPlayer->seqData + cmdArgU16;
                    }
                    break;

                case 0xE6: // channel: set book offset
                    cmd = (u8)cmdArgs[0];
                    channel->bookOffset = cmd;
                    break;

                case 0xE7: // channel: read channel params from sequence
                    cmdArgU16 = (u16)cmdArgs[0];
                    data = &seqPlayer->seqData[cmdArgU16];
                    channel->muteBehavior = *data++;
                    channel->noteAllocPolicy = *data++;
                    AudioSeqMm_SetChannelPriorities(channel, *data++);
                    channel->transposition = (s8)*data++;
                    channel->newPan = *data++;
                    channel->panChannelWeight = *data++;
                    channel->reverb = *data++;
                    channel->reverbIndex = *data++;
                    //! @bug: Not marking reverb state as changed (as in MM)
                    channel->changes.s.pan = true;
                    break;

                case 0xE8: // channel: set channel params
                    channel->muteBehavior = cmdArgs[0];
                    channel->noteAllocPolicy = cmdArgs[1];
                    cmd = (u8)cmdArgs[2];
                    AudioSeqMm_SetChannelPriorities(channel, cmd);
                    channel->transposition = (s8)AudioSeq_ScriptReadU8(scriptState);
                    channel->newPan = AudioSeq_ScriptReadU8(scriptState);
                    channel->panChannelWeight = AudioSeq_ScriptReadU8(scriptState);
                    channel->reverb = AudioSeq_ScriptReadU8(scriptState);
                    channel->reverbIndex = AudioSeq_ScriptReadU8(scriptState);
                    //! @bug: Not marking reverb state as changed (as in MM)
                    channel->changes.s.pan = true;
                    break;

                case 0xEC: // channel: reset vibrato
                    channel->vibratoExtentTarget = 0;
                    channel->vibratoExtentStart = 0;
                    channel->vibratoExtentChangeDelay = 0;
                    channel->vibratoRateTarget = 0;
                    channel->vibratoRateStart = 0;
                    channel->vibratoRateChangeDelay = 0;
                    channel->filter = NULL;
                    channel->unk_0C = 0; // gain
                    channel->adsr.sustain = 0;
                    channel->velocityRandomVariance = 0;
                    channel->gateTimeRandomVariance = 0;
                    channel->unk_0F = 0; // combFilterSize
                    channel->unk_20 = 0; // combFilterGain
                    channel->bookOffset = 0;
                    channel->startSamplePos = 0;
                    channel->unk_E0 = 0;
                    channel->freqScale = 1.0f;
                    break;

                case 0xE9: // channel: set note priority
                    AudioSeqMm_SetChannelPriorities(channel, (u8)cmdArgs[0]);
                    break;

                case 0xED: // channel: set hilo gain
                    cmd = (u8)cmdArgs[0];
                    channel->unk_0C = cmd;
                    break;

                case 0xB0: // channel: set filter
                    cmdArgU16 = (u16)cmdArgs[0];
                    data = seqPlayer->seqData + cmdArgU16;
                    channel->filter = (s16*)data;
                    break;

                case 0xB1: // channel: clear filter
                    channel->filter = NULL;
                    break;

                case 0xB3: // channel: load filter
                    cmd = cmdArgs[0];

                    if (channel->filter != NULL) {
                        lowBits = (cmd >> 4) & 0xF; // LowPassCutoff
                        cmd &= 0xF;                 // HighPassCutoff
                        AudioHeap_LoadFilter(channel->filter, lowBits, cmd);
                    }
                    break;

                case 0xB2: // channel: dynread sequence large
                    cmdArgU16 = (u16)cmdArgs[0];
                    channel->unk_22 = BE16SWAP(*(u16*)(seqPlayer->seqData + (u32)(cmdArgU16 + scriptState->value * 2)));
                    break;

                case 0xB4: // channel: set dyntable large
                    channel->dynTable = (void*)&seqPlayer->seqData[channel->unk_22];
                    break;

                case 0xB5: // channel: read dyntable large
                    channel->unk_22 = BE16SWAP(((u16*)(channel->dynTable))[scriptState->value]);
                    break;

                case 0xB6: // channel: read dyntable
                    scriptState->value = (*channel->dynTable)[0][scriptState->value];
                    break;

                case 0xB7: // channel: random large
                    channel->unk_22 = (cmdArgs[0] == 0) ? (gAudioContext.audioRandom & 0xFFFF)
                                                        : (gAudioContext.audioRandom % cmdArgs[0]);
                    break;

                case 0xB8: // channel: random value
                    scriptState->value = (cmdArgs[0] == 0) ? (gAudioContext.audioRandom & 0xFFFF)
                                                           : (gAudioContext.audioRandom % cmdArgs[0]);
                    break;

                case 0xA8: // channel: random range large
                    rand = Audio_NextRandom();
                    channel->unk_22 = (cmdArgs[0] == 0) ? (rand & 0xFFFF) : (rand % cmdArgs[0]);
                    channel->unk_22 += cmdArgs[1];
                    temp2 = (channel->unk_22 / 0x100) + 0x80;
                    param = channel->unk_22 % 0x100;
                    channel->unk_22 = (temp2 << 8) | param;
                    break;

                case 0xB9: // channel: set velocity random variance
                    channel->velocityRandomVariance = cmdArgs[0];
                    break;

                case 0xBA: // channel: set gatetime random variance
                    channel->gateTimeRandomVariance = cmdArgs[0];
                    break;

                case 0xBB:                        // channel: comb filter
                    channel->unk_0F = cmdArgs[0]; // combFilterSize
                    channel->unk_20 = cmdArgs[1]; // combFilterGain
                    break;

                case 0xBC: // channel: add large
                    channel->unk_22 += cmdArgs[0];
                    break;

                case 0xBD: // channel: start sample position
                    channel->startSamplePos = cmdArgs[0];
                    break;

                case 0xBE: // channel: custom function
                    // MM registers AUDIO_CUSTOM_FUNCTION_SEQ_0 = AudioSfx_SetFreqAndStereoBits for the sfx player;
                    // SEQ_1 (ambience random bend) belongs to MM's ambience player and is not set here.
                    if (cmdArgs[0] == 0) {
                        scriptState->value = AudioMmSfx_SetFreqAndStereoBits(scriptState->value, channel);
                    }
                    break;

                case 0xA0: // channel: read from SfxChannelState using arg
                case 0xA1: // channel: read from SfxChannelState using unk_22
                case 0xA2: // channel: write to SfxChannelState using arg
                case 0xA3: // channel: write to SfxChannelState using unk_22
                    if ((cmd == 0xA0) || (cmd == 0xA2)) {
                        cmdArgU16 = (u16)cmdArgs[0];
                    } else {
                        cmdArgU16 = channel->unk_22;
                    }

                    if (channel->sfxState != NULL) {
                        if ((cmd == 0xA0) || (cmd == 0xA1)) {
                            scriptState->value = channel->sfxState[cmdArgU16];
                        } else {
                            channel->sfxState[cmdArgU16] = scriptState->value;
                        }
                    }
                    break;

                case 0xA4: // channel: surround effect index
                    channel->surroundEffectIndex = cmdArgs[0];
                    break;

                case 0xA5: // channel: value += channelIndex
                    scriptState->value += channel->channelIndex;
                    break;

                case 0xA6: // channel: write into sequence script, offset by channelIndex
                    cmd = (u8)cmdArgs[0];
                    cmdArgU16 = (u16)cmdArgs[1];
                    seqData = seqPlayer->seqData + (u32)(cmdArgU16 + channel->channelIndex);
                    seqData[0] = (u8)scriptState->value + cmd;
                    break;

                case 0xA7: // channel: shift value
                    new_var2 = (cmdArgs[0] & 0x80);
                    new_var = (scriptState->value & 0x80);

                    if (!new_var2) {
                        phi_v0_3 = scriptState->value << (cmdArgs[0] & 0xF);
                    } else {
                        phi_v0_3 = scriptState->value >> (cmdArgs[0] & 0xF);
                    }

                    if (cmdArgs[0] & 0x40) {
                        phi_v0_3 &= (u8)~0x80;
                        phi_v0_3 |= new_var;
                    }

                    scriptState->value = phi_v0_3;
                    break;
            }
            continue;
        }

        MMSEQ_TRACE(channel, "%04X: %02X value %d", (u32)(tracePc - seqPlayer->seqData), cmd, scriptState->value);
        // Commands 0x70 - 0x9F
        if (cmd >= 0x70) {
            lowBits = cmd & 0x7;

            if (((cmd & 0xF8) != 0x70) && (lowBits >= 4)) {
                lowBits = 0;
            }

            switch (cmd & 0xF8) {
                case 0x80: // channel: test layer is finished
                    if (channel->layers[lowBits] != NULL) {
                        scriptState->value = channel->layers[lowBits]->finished;
                    } else {
                        scriptState->value = -1;
                    }
                    break;

                case 0x88: // channel: set layer
                    cmdArgU16 = AudioSeq_ScriptReadS16(scriptState);
                    if (!AudioSeqMm_SeqChannelSetLayer(channel, lowBits)) {
                        channel->layers[lowBits]->scriptState.pc = &seqPlayer->seqData[cmdArgU16];
                    }
                    break;

                case 0x90: // channel: free layer
                    AudioSeq_SeqLayerFree(channel, lowBits);
                    break;

                case 0x98: // channel: dynset layer
                    if ((scriptState->value != -1) && (AudioSeqMm_SeqChannelSetLayer(channel, lowBits) != -1)) {
                        data = (*channel->dynTable)[scriptState->value];
                        cmdArgU16 = (data[0] << 8) + data[1];
                        channel->layers[lowBits]->scriptState.pc = &seqPlayer->seqData[cmdArgU16];
                    }
                    break;

                case 0x70: // channel: io write value
                    channel->soundScriptIO[lowBits] = scriptState->value;
                    break;

                case 0x78: // channel: set layer relative
                    temp1 = AudioSeq_ScriptReadS16(scriptState);
                    if (!AudioSeqMm_SeqChannelSetLayer(channel, lowBits)) {
                        channel->layers[lowBits]->scriptState.pc = &scriptState->pc[temp1];
                    }
                    break;
            }
            continue;
        }

        // Commands 0x00 - 0x6F
        lowBits = cmd & 0xF;

        switch (cmd & 0xF0) {
            case 0x00: // channel: delay short (MM: a zero delay keeps reading)
                channel->delay = lowBits;
                if (lowBits == 0) {
                    break;
                }
                goto exit_loop;

            case 0x10: // channel: load sample
                if (lowBits < 8) {
                    channel->soundScriptIO[lowBits] = SEQ_IO_VAL_NONE;
                    if (AudioLoad_SlowLoadSample(channel->fontId, scriptState->value,
                                                 &channel->soundScriptIO[lowBits]) == -1) {}
                } else {
                    lowBits -= 8;
                    channel->soundScriptIO[lowBits] = SEQ_IO_VAL_NONE;
                    if (AudioLoad_SlowLoadSample(channel->fontId, channel->unk_22 + 0x100,
                                                 &channel->soundScriptIO[lowBits]) == -1) {}
                }
                break;

            case 0x60: // channel: io read value
                scriptState->value = channel->soundScriptIO[lowBits];
                if (lowBits < 2) {
                    channel->soundScriptIO[lowBits] = SEQ_IO_VAL_NONE;
                }
                break;

            case 0x50: // channel: io read value subtract
                scriptState->value -= channel->soundScriptIO[lowBits];
                break;

            case 0x20: // channel: start channel
                cmdArgU16 = AudioSeq_ScriptReadS16(scriptState);
                AudioSeq_SequenceChannelEnable(seqPlayer, lowBits, &seqPlayer->seqData[cmdArgU16]);
                break;

            case 0x30: // channel: io write value 2
                cmd = AudioSeq_ScriptReadU8(scriptState);
                seqPlayer->channels[lowBits]->soundScriptIO[cmd] = scriptState->value;
                break;

            case 0x40: // channel: io read value 2
                cmd = AudioSeq_ScriptReadU8(scriptState);
                scriptState->value = seqPlayer->channels[lowBits]->soundScriptIO[cmd];
                break;
        }
    }
exit_loop:

    for (i = 0; i < ARRAY_COUNT(channel->layers); i++) {
        if (channel->layers[i] != NULL) {
            AudioSeqMm_SeqLayerProcessScript(channel->layers[i]);
        }
    }
}

// ---- sequence player -----------------------------------------------------------------------------------------------

void AudioSeqMm_SequencePlayerProcessSequence(SequencePlayer* seqPlayer) {
    u8 cmd;
    u8 cmdLowBits;
    SeqScriptState* seqScript = &seqPlayer->scriptState;
    s16 tempS;
    u16 temp;
    s32 i;
    s32 value;
    u8* data1;
    u8* data2;
    u8* data3;
    u8* data4;
    s32 tempoChange;
    s32 j;
    SequenceChannel* channel;
    u16* new_var;
    s32 delay;

    if (!seqPlayer->enabled) {
        return;
    }

    // MM points each sfx channel at its SfxChannelState (AUDIOCMD_CHANNEL_SET_SFX_STATE) right after starting the
    // sequence; channel resets clear it, so attach before any script runs. Only the sfx player: an MM music sequence
    // on another player (the fanfare player) has no sfx state, as in MM.
    for (j = 0; j < ARRAY_COUNT(seqPlayer->channels); j++) {
        if (seqPlayer != &gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX]) {
            break;
        }
        if (IS_SEQUENCE_CHANNEL_VALID(seqPlayer->channels[j]) && (seqPlayer->channels[j]->sfxState == NULL)) {
            seqPlayer->channels[j]->sfxState = AudioMmSfx_GetChannelState(j);
        }
    }

    if (!AudioLoad_IsSeqLoadComplete(seqPlayer->seqId) || !AudioLoad_IsFontLoadComplete(seqPlayer->defaultFont)) {
        AudioSeq_SequencePlayerDisable(seqPlayer);
        return;
    }

    AudioLoad_SetSeqLoadStatus(seqPlayer->seqId, 2);
    AudioLoad_SetFontLoadStatus(seqPlayer->defaultFont, 2);

    if (seqPlayer->muted && (seqPlayer->muteBehavior & MUTE_FLAGS_STOP_SCRIPT)) {
        return;
    }

    seqPlayer->scriptCounter++;

    tempoChange = seqPlayer->tempo + (s16)seqPlayer->unk_0C;
    if (tempoChange > (s32)gAudioContext.tempoInternalToExternal) {
        tempoChange = gAudioContext.tempoInternalToExternal;
    }

    seqPlayer->tempoAcc += tempoChange;

    if (seqPlayer->tempoAcc < gAudioContext.tempoInternalToExternal) {
        return;
    }

    seqPlayer->tempoAcc -= (u16)gAudioContext.tempoInternalToExternal;
    seqPlayer->mmUnk16++;

    if (seqPlayer->stopScript == true) {
        return;
    }

    if (seqPlayer->delay > 1) {
        seqPlayer->delay--;
    } else {
        seqPlayer->recalculateVolume = true;

        while (true) {
            cmd = AudioSeq_ScriptReadU8(seqScript);

            // 0xF2 and above are "flow control" commands, including termination.
            if (cmd >= 0xF2) {
                delay = AudioSeqMm_HandleScriptFlowControl(
                    seqPlayer, seqScript, cmd, AudioSeqMm_GetScriptControlFlowArgument(&seqPlayer->scriptState, cmd));

                if (delay != 0) {
                    if (delay == -1) {
                        AudioSeq_SequencePlayerDisable(seqPlayer);
                    } else {
                        seqPlayer->delay = delay;
                    }
                    break;
                }
                continue;
            }

            // Commands 0xC0 - 0xF1
            if (cmd >= 0xC0) {
                switch (cmd) {
                    case 0xF1: // seqPlayer: reserve notes
                        Audio_NotePoolClear(&seqPlayer->notePool);
                        cmd = AudioSeq_ScriptReadU8(seqScript);
                        Audio_NotePoolFill(&seqPlayer->notePool, cmd);
                        break;

                    case 0xF0: // seqPlayer: unreserve notes
                        Audio_NotePoolClear(&seqPlayer->notePool);
                        break;

                    case 0xDF: // seqPlayer: transpose
                        seqPlayer->transposition = 0;
                        // fallthrough
                    case 0xDE: // seqPlayer: transpose relative
                        seqPlayer->transposition += (s8)AudioSeq_ScriptReadU8(seqScript);
                        break;

                    case 0xDD: // seqPlayer: set tempo
                        seqPlayer->tempo = AudioSeq_ScriptReadU8(seqScript) * TATUMS_PER_BEAT;
                        if (seqPlayer->tempo > gAudioContext.tempoInternalToExternal) {
                            seqPlayer->tempo = gAudioContext.tempoInternalToExternal;
                        }

                        if ((s16)seqPlayer->tempo <= 0) {
                            seqPlayer->tempo = 1;
                        }
                        break;

                    case 0xDC: // seqPlayer: add tempo
                        seqPlayer->unk_0C = (s8)AudioSeq_ScriptReadU8(seqScript) * TATUMS_PER_BEAT;
                        break;

                    case 0xDA: // seqPlayer: change volume
                        cmd = AudioSeq_ScriptReadU8(seqScript);
                        temp = AudioSeq_ScriptReadS16(seqScript);
                        switch (cmd) {
                            case SEQPLAYER_STATE_0:
                            case SEQPLAYER_STATE_FADE_IN:
                                if (seqPlayer->state != SEQPLAYER_STATE_FADE_OUT) {
                                    seqPlayer->fadeTimerUnkEu = temp;
                                    seqPlayer->state = cmd;
                                }
                                break;

                            case SEQPLAYER_STATE_FADE_OUT:
                                seqPlayer->fadeTimer = temp;
                                seqPlayer->state = cmd;
                                seqPlayer->fadeVelocity = (0.0f - seqPlayer->fadeVolume) / (s32)seqPlayer->fadeTimer;
                                break;
                        }
                        break;

                    case 0xDB: // seqPlayer: set volume
                        value = AudioSeq_ScriptReadU8(seqScript);
                        switch (seqPlayer->state) {
                            case SEQPLAYER_STATE_FADE_IN:
                                seqPlayer->state = SEQPLAYER_STATE_0;
                                seqPlayer->fadeVolume = 0.0f;
                                // fallthrough
                            case SEQPLAYER_STATE_0:
                                seqPlayer->fadeTimer = seqPlayer->fadeTimerUnkEu;
                                if (seqPlayer->fadeTimerUnkEu != 0) {
                                    seqPlayer->fadeVelocity =
                                        ((value / 127.0f) - seqPlayer->fadeVolume) / (s32)seqPlayer->fadeTimer;
                                } else {
                                    seqPlayer->fadeVolume = value / 127.0f;
                                }
                                break;

                            case SEQPLAYER_STATE_FADE_OUT:
                                break;
                        }
                        break;

                    case 0xD9: // seqPlayer: set volume scale
                        seqPlayer->fadeVolumeScale = (s8)AudioSeq_ScriptReadU8(seqScript) / 127.0f;
                        break;

                    case 0xD7: // seqPlayer: initialize channels
                        temp = AudioSeq_ScriptReadS16(seqScript);
                        AudioSeq_SequencePlayerSetupChannels(seqPlayer, temp);
                        break;

                    case 0xD6: // seqPlayer: disable channels
                        AudioSeq_ScriptReadS16(seqScript);
                        break;

                    case 0xD5: // seqPlayer: set mute scale
                        seqPlayer->muteVolumeScale = (s8)AudioSeq_ScriptReadU8(seqScript) / 127.0f;
                        break;

                    case 0xD4: // seqPlayer: mute
                        seqPlayer->muted = true;
                        break;

                    case 0xD3: // seqPlayer: set mute behavior
                        seqPlayer->muteBehavior = AudioSeq_ScriptReadU8(seqScript);
                        break;

                    case 0xD1: // seqPlayer: set short note gatetime table
                    case 0xD2: // seqPlayer: set short note velocity table
                        temp = AudioSeq_ScriptReadS16(seqScript);
                        data3 = &seqPlayer->seqData[temp];
                        if (cmd == 0xD2) {
                            seqPlayer->shortNoteVelocityTable = data3;
                        } else {
                            seqPlayer->shortNoteGateTimeTable = data3;
                        }
                        break;

                    case 0xD0: // seqPlayer: set note allocation policy
                        seqPlayer->noteAllocPolicy = AudioSeq_ScriptReadU8(seqScript);
                        break;

                    case 0xCE: // seqPlayer: random value
                        cmd = AudioSeq_ScriptReadU8(seqScript);
                        if (cmd == 0) {
                            seqScript->value = (gAudioContext.audioRandom >> 2) & 0xFF;
                        } else {
                            seqScript->value = (gAudioContext.audioRandom >> 2) % cmd;
                        }
                        break;

                    case 0xCD: // seqPlayer: dyncall
                        temp = AudioSeq_ScriptReadS16(seqScript);
                        if ((seqScript->value != -1) && (seqScript->depth != 3)) {
                            data1 = seqPlayer->seqData + (u32)(temp + (seqScript->value << 1));
                            seqScript->stack[seqScript->depth] = seqScript->pc;
                            seqScript->depth++;
                            temp = (data1[0] << 8) + data1[1];
                            seqScript->pc = &seqPlayer->seqData[temp];
                        }
                        break;

                    case 0xCC: // seqPlayer: set value
                        seqScript->value = AudioSeq_ScriptReadU8(seqScript);
                        break;

                    case 0xC9: // seqPlayer: `bit and` -> set value
                        seqScript->value &= AudioSeq_ScriptReadU8(seqScript);
                        break;

                    case 0xC8: // seqPlayer: subtract -> set value
                        seqScript->value -= AudioSeq_ScriptReadU8(seqScript);
                        break;

                    case 0xC7: // seqPlayer: write into sequence script
                        cmd = AudioSeq_ScriptReadU8(seqScript);
                        temp = AudioSeq_ScriptReadS16(seqScript);
                        data2 = &seqPlayer->seqData[temp];
                        *data2 = (u8)seqScript->value + cmd;
                        break;

                    case 0xC2: // seqPlayer: dynamic jump
                        temp = AudioSeq_ScriptReadS16(seqScript);
                        if (seqScript->value != -1) {
                            data4 = seqPlayer->seqData + (u32)(temp + (seqScript->value << 1));

                            temp = (data4[0] << 8) + data4[1];
                            seqScript->pc = &seqPlayer->seqData[temp];
                        }
                        break;

                    case 0xC6: // seqPlayer: stop script
                        seqPlayer->stopScript = true;
                        return;

                    case 0xC5: // seqPlayer: set unk_16
                        seqPlayer->mmUnk16 = AudioSeq_ScriptReadS16(seqScript);
                        break;

                    case 0xEF: // seqPlayer:
                        AudioSeq_ScriptReadS16(seqScript);
                        AudioSeq_ScriptReadU8(seqScript);
                        break;

                    case 0xC4: // seqPlayer: start sequence
                        cmd = AudioSeq_ScriptReadU8(seqScript);
                        if (cmd == 0xFF) {
                            cmd = seqPlayer->playerIdx;
                            if (seqPlayer->state == SEQPLAYER_STATE_FADE_OUT) {
                                break;
                            }
                        }

                        cmdLowBits = AudioSeq_ScriptReadU8(seqScript);
                        // MM's sequence numbers mean nothing to OOT's players: only a restart of this player itself.
                        if (cmd == (u8)seqPlayer->playerIdx) {
                            AudioLoad_SyncInitSeqPlayer(cmd, seqPlayer->seqId, 0);
                            return;
                        }
                        break;

                    case 0xC3: // seqPlayer: mute channels from table
                        temp = AudioSeq_ScriptReadS16(seqScript);
                        if (seqScript->value != -1) {
                            new_var = (u16*)(seqPlayer->seqData + (u32)(temp + seqScript->value * 2));
                            temp = *new_var;

                            for (i = 0; i < ARRAY_COUNT(seqPlayer->channels); i++) {
                                seqPlayer->channels[i]->stopSomething2 = temp & 1;
                                temp = temp >> 1;
                            }
                        }
                        break;
                }
                continue;
            }

            // Commands 0x00 - 0xBF
            cmdLowBits = cmd & 0x0F;

            switch (cmd & 0xF0) {
                case 0x00: // seqPlayer: test channel disabled
                    seqScript->value = seqPlayer->channels[cmdLowBits]->enabled ^ 1;
                    break;

                case 0x50: // seqPlayer: io read value subtract
                    seqScript->value -= seqPlayer->soundScriptIO[cmdLowBits];
                    break;

                case 0x70: // seqPlayer: io write value
                    seqPlayer->soundScriptIO[cmdLowBits] = seqScript->value;
                    break;

                case 0x80: // seqPlayer: io read value
                    seqScript->value = seqPlayer->soundScriptIO[cmdLowBits];
                    if (cmdLowBits < 2) {
                        seqPlayer->soundScriptIO[cmdLowBits] = SEQ_IO_VAL_NONE;
                    }
                    break;

                case 0x40: // seqPlayer: disable channel
                    AudioSeq_SequenceChannelDisable(seqPlayer->channels[cmdLowBits]);
                    break;

                case 0x90: // seqPlayer: start channel
                    temp = AudioSeq_ScriptReadS16(seqScript);
                    AudioSeq_SequenceChannelEnable(seqPlayer, cmdLowBits, (void*)&seqPlayer->seqData[temp]);
                    break;

                case 0xA0: // seqPlayer: start channel relative
                    tempS = AudioSeq_ScriptReadS16(seqScript);
                    AudioSeq_SequenceChannelEnable(seqPlayer, cmdLowBits, (void*)&seqScript->pc[tempS]);
                    break;

                case 0xB0: // seqPlayer: load sequence (MM's slow sequence loads are not used by the sfx sequence)
                    AudioSeq_ScriptReadU8(seqScript);
                    AudioSeq_ScriptReadS16(seqScript);
                    break;

                case 0x60: // seqPlayer: async load (likewise)
                    AudioSeq_ScriptReadU8(seqScript);
                    AudioSeq_ScriptReadU8(seqScript);
                    break;
            }
        }
    }

    for (j = 0; j < ARRAY_COUNT(seqPlayer->channels); j++) {
        channel = seqPlayer->channels[j];
        if (channel->enabled) {
            AudioSeqMm_SequenceChannelProcessScript(channel);
        }
    }
}
