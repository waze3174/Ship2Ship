/**
 * @file audio_mm_sfx.c
 *
 * ComboShip: Majora's Mask's sound-effect front end, driving the native MM sfx player (SEQ_PLAYER_COMBO_MM_SFX) in
 * Ocarina of Time's audio engine. A port of mm/src/audio/sfx.c (request queue, 7 sfx banks, channel allocation by
 * importance/distance) and the sfx half of mm/src/audio/code_8019AF00.c (AudioSfx_SetProperties: distance volume,
 * reverb, pan, frequency, filters; the SfxChannelState the sfx sequence reads; custom function 0), with MM's own
 * parameter tables (audio_mm_sfx_params.c) and MM's channel layout / io-port encoding for MM's Sequence_0.
 *
 * Runs on OOT's game thread from Audio_Update (code_800EC960.c), queuing channel io writes into OOT's audio command
 * queue exactly as MM's AUDIOCMD_CHANNEL_SET_IO does. Environment inputs come from OOT's side (its scene reverb, its
 * underwater filters, its sound mode), so an MM sound sits in Hyrule's acoustics the way OOT's own sfx do.
 *
 * Game code uses the C API at the bottom (Combo_MmSfx_*). Every position the front end keys on is owned here: a caller
 * pointer is only an identity key and is copied, never kept.
 */
#include <libultraship/libultra.h>
#include "global.h"

#include <math.h>
#include <stdlib.h>

#ifdef COMBO_BUILD
#include "soh/Enhancements/randomizer/ComboForms.h"
extern PlayState* gPlayState;
#endif
#include <libultraship/bridge/consolevariablebridge.h>

// ---- MM definitions (mm/include/sfx.h), prefixed to stay clear of OOT's names ------------------------------------

typedef struct {
    u8 importance;
    u8 flags;
    u16 params;
} MmSfxParams;

extern const MmSfxParams* const gMmSfxParams[7];
extern const u16 gMmSfxParamsCount[7];

#define MMSFX_BANK_SHIFT(sfxId) (((sfxId) >> 12) & 0xFF)
#define MMSFX_BANK_MASK(sfxId) ((sfxId)&0xF000)
#define MMSFX_INDEX(sfxId) ((sfxId)&0x3FF)
#define MMSFX_BANK(sfxId) MMSFX_BANK_SHIFT(MMSFX_BANK_MASK(sfxId))
#define MMSFX_FLAG_MASK 0xC00
#define MMSFX_NONE 0

#define MMSFX_PARAM_DIST_RANGE_MASK_UPPER 4
#define MMSFX_PARAM_DIST_RANGE_MASK 7
#define MMSFX_FLAG_LOWER_VOLUME_BGM (1 << 3)
#define MMSFX_FLAG_PRIORITY_NO_DIST (1 << 4)
#define MMSFX_FLAG_BLOCK_EQUAL_IMPORTANCE (1 << 5)
#define MMSFX_PARAM_RAND_FREQ_RAISE_SHIFT 6
#define MMSFX_PARAM_RAND_FREQ_RAISE_MASK (3 << MMSFX_PARAM_RAND_FREQ_RAISE_SHIFT)
#define MMSFX_FLAG_8 (1 << 8)
#define MMSFX_FLAG_SURROUND_LOWPASS_FILTER (1 << 9)
#define MMSFX_PARAM_RAND_FREQ_SCALE (1 << 11)
#define MMSFX_FLAG_REVERB_NO_DIST (1 << 12)
#define MMSFX_FLAG_VOLUME_NO_DIST (1 << 13)
#define MMSFX_PARAM_RAND_FREQ_LOWER (1 << 14)
#define MMSFX_FLAG_FREQ_NO_DIST (1 << 15)
#define MMSFX_FLAG2_FORCE_RESET (1 << 0)
#define MMSFX_FLAG2_SURROUND_NO_HIGHPASS_FILTER (1 << 5)
#define MMSFX_FLAG2_APPLY_LOWPASS_FILTER (1 << 7)

enum { MMBANK_PLAYER, MMBANK_ITEM, MMBANK_ENV, MMBANK_ENEMY, MMBANK_SYSTEM, MMBANK_OCARINA, MMBANK_VOICE, MMBANK_MAX };

enum {
    MMSFX_STATE_EMPTY,
    MMSFX_STATE_QUEUED,
    MMSFX_STATE_READY,
    MMSFX_STATE_PLAYING_REFRESH,
    MMSFX_STATE_PLAYING,
    MMSFX_STATE_PLAYING_ONE_FRAME
};

enum { MMSOUNDMODE_STEREO, MMSOUNDMODE_HEADSET, MMSOUNDMODE_SURROUND_EXTERNAL, MMSOUNDMODE_MONO };

#define MMSFX_CHANNEL_OCARINA 0xD
#define MMSFX_NUM_CHANNELS 16

typedef struct {
    f32* posX;
    f32* posY;
    f32* posZ;
    f32* freqScale;
    f32* volume;
    s8* reverbAdd;
    f32 dist;
    u32 priority; // lower is more prioritized
    u16 sfxParams;
    u16 sfxId;
    u8 sfxFlags;
    u8 sfxImportance;
    u8 state;
    u8 freshness;
    u8 prev;
    u8 next;
    u8 channelIndex;
    u8 randFreq;
    u8 token;
} MmSfxBankEntry;

typedef struct {
    u32 priority;
    u8 entryIndex;
} MmActiveSfx;

typedef struct {
    u16 sfxId;
    u8 token;
    s8* reverbAdd;
    Vec3f* pos;
    f32* freqScale;
    f32* volume;
} MmSfxRequest;

typedef struct {
    f32 value;
    f32 target;
    f32 step;
    u16 remainingFrames;
} MmSfxBankLerp;

// MM SfxChannelState: the sfx sequence reads its bytes by offset (channel opcodes 0xA0/0xA1), so the layout is MM's.
typedef struct {
    /* 0x0 */ f32 volume;
    /* 0x4 */ f32 freqScale;
    /* 0x8 */ s8 reverb;
    /* 0x9 */ s8 panSigned;
    /* 0xA */ s8 stereoBits;
    /* 0xB */ u8 filter;
    /* 0xC */ u8 combFilterGain;
    /* 0xD */ u8 zVolume;
} MmSfxChannelState; // size = 0x10

typedef enum {
    MMSFX_RM_REQ_BY_BANK,
    MMSFX_RM_REQ_BY_POS_AND_BANK,
    MMSFX_RM_REQ_BY_POS,
    MMSFX_RM_REQ_BY_POS_AND_ID,
    MMSFX_RM_REQ_BY_TOKEN_AND_ID,
    MMSFX_RM_REQ_BY_ID
} MmSfxRemoveRequest;

// ---- MM state (mm sfx.c / code_8019AF00.c) ------------------------------------------------------------------------

static MmSfxBankEntry sMmSfxPlayerBank[9];
static MmSfxBankEntry sMmSfxItemBank[12];
static MmSfxBankEntry sMmSfxEnvironmentBank[32];
static MmSfxBankEntry sMmSfxEnemyBank[20];
static MmSfxBankEntry sMmSfxSystemBank[8];
static MmSfxBankEntry sMmSfxOcarinaBank[3];
static MmSfxBankEntry sMmSfxVoiceBank[5];
static MmSfxRequest sMmSfxRequests[0x100];
static u8 sMmSfxBankListEnd[MMBANK_MAX];
static u8 sMmSfxBankFreeListStart[MMBANK_MAX];
static MmActiveSfx sMmActiveSfx[MMBANK_MAX][3];
static u8 sMmCurSfxPlayerChannelIndex;
static u8 sMmSfxBankMuted[MMBANK_MAX];
static MmSfxBankLerp sMmSfxBankLerp[MMBANK_MAX];
static u8 sMmSfxRequestWriteIndex = 0;
static u8 sMmSfxRequestReadIndex = 0;
static u16 sMmSfxChannelLowVolumeFlag = 0;
static MmSfxChannelState sMmSfxChannelState[MMSFX_NUM_CHANNELS];

static MmSfxBankEntry* const sMmSfxBanks[MMBANK_MAX] = {
    sMmSfxPlayerBank, sMmSfxItemBank,    sMmSfxEnvironmentBank, sMmSfxEnemyBank,
    sMmSfxSystemBank, sMmSfxOcarinaBank, sMmSfxVoiceBank,
};

static const u8 sMmSfxBankSizes[MMBANK_MAX] = {
    ARRAY_COUNT(sMmSfxPlayerBank), ARRAY_COUNT(sMmSfxItemBank),   ARRAY_COUNT(sMmSfxEnvironmentBank),
    ARRAY_COUNT(sMmSfxEnemyBank),  ARRAY_COUNT(sMmSfxSystemBank), ARRAY_COUNT(sMmSfxOcarinaBank),
    ARRAY_COUNT(sMmSfxVoiceBank),
};

// MM gIsLargeSfxBank / D_801D6608 / gChannelsPerBank / gUsedChannelsPerBank (channel layout 0 = MM's default).
static const u8 sMmIsLargeSfxBank[MMBANK_MAX] = { true, false, true, true, false, false, true };
static const u8 sMmSetsIoPort5[MMBANK_MAX] = { true, true, true, true, true, false, true };
static const u8 sMmChannelsPerBank[MMBANK_MAX] = { 3, 2, 3, 3, 2, 1, 2 };
static const u8 sMmUsedChannelsPerBank[MMBANK_MAX] = { 3, 2, 3, 2, 2, 1, 1 };

static Vec3f sMmSfxDefaultPos = { 0.0f, 0.0f, 0.0f };
static f32 sMmSfxDefaultFreqAndVolScale = 1.0f;
static s8 sMmSfxDefaultReverb = 0;

// OOT's environment (code_800EC960.c): scene reverb and filters feed MM's computations as MM's own globals would.
extern s8 sSpecReverb;
extern s8 sAudioEnvReverb;
extern s8 sAudioCodeReverb;
extern u8 sAudioBaseFilter;
extern u8 sAudioExtraFilter;
extern u8 sAudioBaseFilter2;
extern u8 sAudioExtraFilter2;
extern u16 D_801333D0; // OOT's "sfx channel lowered the bgm" mask (code_800F7260.c)

// Debug harness (COMBO_DEBUG_AUDIO=1): log every MM sfx a channel starts.
static s32 AudioMmSfx_Debug(void) {
#ifdef COMBO_BUILD
    static s32 sDebug = -1;
    if (sDebug < 0) {
        sDebug = Combo_DebugEnv("COMBO_DEBUG_AUDIO");
    }
    return sDebug;
#else
    return false;
#endif
}

// AUDIOCMD_CHANNEL_SET_IO for the native MM sfx player.
static void AudioMmSfx_SetChannelIo(u8 channelIndex, u8 ioPort, s8 value) {
    Audio_QueueCmdS8(0x06000000 | _SHIFTL(SEQ_PLAYER_COMBO_MM_SFX, 16, 8) | _SHIFTL(channelIndex, 8, 8) |
                         _SHIFTL(ioPort, 0, 8),
                     value);
}

// The ocarina channel of MM's Sequence_0 (SFX_CHANNEL_OCARINA, 0xD): a form's instrument in Hyrule
// (code_800EC960.c Combo_OcarinaIo).
void AudioMmSfx_OcarinaIo(u8 ioPort, s8 value) {
    AudioMmSfx_SetChannelIo(0xD, ioPort, value);
}

static u8 AudioMmSfx_SoundMode(void) {
    return gAudioContext.soundMode;
}

static const MmSfxParams* AudioMmSfx_GetParams(u16 sfxId) {
    u8 bankId = MMSFX_BANK_SHIFT(sfxId);
    u16 index = MMSFX_INDEX(sfxId);

    if ((bankId >= MMBANK_MAX) || (index >= gMmSfxParamsCount[bankId])) {
        return NULL;
    }
    return &gMmSfxParams[bankId][index];
}

// ---- mm sfx.c -----------------------------------------------------------------------------------------------------

static void AudioMmSfx_LowerBgmVolume(u8 channelIndex) {
    sMmSfxChannelLowVolumeFlag |= (1 << channelIndex);
    Audio_SetVolScale(SEQ_PLAYER_BGM_MAIN, 2, 0x40, 0xF);
    Audio_SetVolScale(SEQ_PLAYER_BGM_SUB, 2, 0x40, 0xF);
}

static void AudioMmSfx_RestoreBgmVolume(u8 channelIndex) {
    sMmSfxChannelLowVolumeFlag &= ((1 << channelIndex) ^ 0xFFFF);
    // OOT's own sfx share the same bgm volume scale: only restore when neither engine still wants it lowered.
    if ((sMmSfxChannelLowVolumeFlag == 0) && (D_801333D0 == 0)) {
        Audio_SetVolScale(SEQ_PLAYER_BGM_MAIN, 2, 0x7F, 0xF);
        Audio_SetVolScale(SEQ_PLAYER_BGM_SUB, 2, 0x7F, 0xF);
    }
}

u16 AudioMmSfx_LowVolumeFlags(void) {
    return sMmSfxChannelLowVolumeFlag;
}

void AudioMmSfx_PlaySfx(u16 sfxId, Vec3f* pos, u8 token, f32* freqScale, f32* volume, s8* reverbAdd) {
    u8 i;
    MmSfxRequest* reqWrite;
    MmSfxRequest* reqRead;

    if (AudioMmSfx_GetParams(sfxId) == NULL) {
        return; // not an MM sfx id (MM would read past its tables)
    }

    if (!sMmSfxBankMuted[MMSFX_BANK_SHIFT(sfxId)]) {
        reqWrite = &sMmSfxRequests[sMmSfxRequestWriteIndex];

        for (i = sMmSfxRequestReadIndex; sMmSfxRequestWriteIndex != i; i++) {
            reqRead = &sMmSfxRequests[i];
            if ((reqRead->pos == pos) && (reqRead->sfxId == sfxId)) {
                return;
            }
        }

        reqWrite->sfxId = sfxId;
        reqWrite->pos = pos;
        reqWrite->token = token;
        reqWrite->freqScale = freqScale;
        reqWrite->volume = volume;
        reqWrite->reverbAdd = reverbAdd;
        sMmSfxRequestWriteIndex++;
    }
}

static void AudioMmSfx_RemoveMatchingRequests(u8 aspect, MmSfxBankEntry* entry) {
    MmSfxRequest* req;
    s32 remove;
    u8 i = sMmSfxRequestReadIndex;

    for (; i != sMmSfxRequestWriteIndex; i++) {
        remove = false;
        req = &sMmSfxRequests[i];

        switch (aspect) {
            case MMSFX_RM_REQ_BY_BANK:
                if (MMSFX_BANK_MASK(req->sfxId) == MMSFX_BANK_MASK(entry->sfxId)) {
                    remove = true;
                }
                break;

            case MMSFX_RM_REQ_BY_POS_AND_BANK:
                if ((MMSFX_BANK_MASK(req->sfxId) == MMSFX_BANK_MASK(entry->sfxId)) && (&req->pos->x == entry->posX)) {
                    remove = true;
                }
                break;

            case MMSFX_RM_REQ_BY_POS:
                if (&req->pos->x == entry->posX) {
                    remove = true;
                }
                break;

            case MMSFX_RM_REQ_BY_POS_AND_ID:
                if ((&req->pos->x == entry->posX) && (req->sfxId == entry->sfxId)) {
                    remove = true;
                }
                break;

            case MMSFX_RM_REQ_BY_TOKEN_AND_ID:
                if ((req->token == entry->token) && (req->sfxId == entry->sfxId)) {
                    remove = true;
                }
                break;

            case MMSFX_RM_REQ_BY_ID:
                if (req->sfxId == entry->sfxId) {
                    remove = true;
                }
                break;

            default:
                break;
        }

        if (remove) {
            req->sfxId = MMSFX_NONE;
        }
    }
}

static void AudioMmSfx_ProcessRequest(void) {
    u16 sfxId;
    u8 channelCount;
    u8 index;
    MmSfxRequest* req = &sMmSfxRequests[sMmSfxRequestReadIndex];
    MmSfxBankEntry* entry;
    const MmSfxParams* sfxParams;
    const MmSfxParams* reqParams;
    s32 bankId;
    u8 evictImportance = 0;
    u8 evictIndex = 0x80;

    if (req->sfxId == MMSFX_NONE) {
        return;
    }
    reqParams = AudioMmSfx_GetParams(req->sfxId);
    if (reqParams == NULL) {
        return;
    }

    bankId = MMSFX_BANK(req->sfxId);
    channelCount = 0;
    index = sMmSfxBanks[bankId][0].next;

    // Compare the sfx request to all sfx bank entries
    while ((index != 0xFF) && (index != 0)) {
        // An existing sfx from the same source: renew it, take a free channel, interrupt it, or drop the request.
        if (sMmSfxBanks[bankId][index].posX == &req->pos->x) {
            if ((reqParams->params & MMSFX_FLAG_BLOCK_EQUAL_IMPORTANCE) &&
                (reqParams->importance == sMmSfxBanks[bankId][index].sfxImportance)) {
                // Drop the new request
                return;
            }

            if (sMmSfxBanks[bankId][index].sfxId == req->sfxId) {
                // The new sfx is the same as the existing sfx: reprocess it
                channelCount = sMmUsedChannelsPerBank[bankId];
            } else {
                // Determine which sfx to evict based on importance if there are no more channels available
                if (channelCount == 0) {
                    evictIndex = index;
                    sfxId = sMmSfxBanks[bankId][index].sfxId & 0xFFFF;
                    evictImportance = AudioMmSfx_GetParams(sfxId)->importance;
                } else if (sMmSfxBanks[bankId][index].sfxImportance < evictImportance) {
                    evictIndex = index;
                    sfxId = sMmSfxBanks[bankId][index].sfxId & 0xFFFF;
                    evictImportance = AudioMmSfx_GetParams(sfxId)->importance;
                }

                channelCount++;

                // There are no channels available, evict the lowest importance
                if (channelCount == sMmUsedChannelsPerBank[bankId]) {
                    if (reqParams->importance >= evictImportance) {
                        index = evictIndex;
                    } else {
                        index = 0;
                    }
                }
            }

            // Process this sfx by refreshing or replacing an existing sfx
            if (channelCount == sMmUsedChannelsPerBank[bankId]) {
                sfxParams = reqParams;

                // Interrupt existing sfx and play the new sfx instead.
                if ((req->sfxId & MMSFX_FLAG_MASK) || (sfxParams->flags & MMSFX_FLAG2_FORCE_RESET) ||
                    (index == evictIndex)) {
                    // Restore bgm if the sfx about to be replaced has the right flag
                    if ((sMmSfxBanks[bankId][index].sfxParams & MMSFX_FLAG_LOWER_VOLUME_BGM) &&
                        (sMmSfxBanks[bankId][index].state != MMSFX_STATE_QUEUED)) {
                        AudioMmSfx_RestoreBgmVolume(sMmSfxBanks[bankId][index].channelIndex);
                    }

                    sMmSfxBanks[bankId][index].token = req->token;
                    sMmSfxBanks[bankId][index].sfxId = req->sfxId;
                    sMmSfxBanks[bankId][index].state = MMSFX_STATE_QUEUED;
                    sMmSfxBanks[bankId][index].freshness = 2;
                    sMmSfxBanks[bankId][index].freqScale = req->freqScale;
                    sMmSfxBanks[bankId][index].volume = req->volume;
                    sMmSfxBanks[bankId][index].reverbAdd = req->reverbAdd;
                    sMmSfxBanks[bankId][index].sfxParams = sfxParams->params;
                    sMmSfxBanks[bankId][index].sfxFlags = sfxParams->flags;
                    sMmSfxBanks[bankId][index].sfxImportance = sfxParams->importance;
                } else if (sMmSfxBanks[bankId][index].state == MMSFX_STATE_PLAYING_ONE_FRAME) {
                    // Keep a "one-frame" sfx going (avoids deletion, gets "SFX_STATE_REFRESH")
                    sMmSfxBanks[bankId][index].state = MMSFX_STATE_PLAYING;
                }
                // Terminate loop
                index = 0;
            }
        }

        if (index != 0) {
            index = sMmSfxBanks[bankId][index].next;
        }
    }

    if ((sMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].next != 0xFF) && (index != 0)) {
        // Allocate from free list
        index = sMmSfxBankFreeListStart[bankId];

        entry = &sMmSfxBanks[bankId][index];
        entry->posX = &req->pos->x;
        entry->posY = &req->pos->y;
        entry->posZ = &req->pos->z;
        entry->token = req->token;
        entry->freqScale = req->freqScale;
        entry->volume = req->volume;
        entry->reverbAdd = req->reverbAdd;

        sfxParams = reqParams;

        entry->sfxParams = sfxParams->params;
        entry->sfxFlags = sfxParams->flags;
        entry->sfxImportance = sfxParams->importance;
        entry->sfxId = req->sfxId;
        entry->state = MMSFX_STATE_QUEUED;
        entry->freshness = 2;
        entry->prev = sMmSfxBankListEnd[bankId];

        sMmSfxBanks[bankId][sMmSfxBankListEnd[bankId]].next = sMmSfxBankFreeListStart[bankId];
        sMmSfxBankListEnd[bankId] = sMmSfxBankFreeListStart[bankId];
        sMmSfxBankFreeListStart[bankId] = sMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].next;
        sMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].prev = 0xFF;

        entry->next = 0xFF;
    }
}

static void AudioMmSfx_RemoveBankEntry(u8 bankId, u8 entryIndex) {
    MmSfxBankEntry* entry = &sMmSfxBanks[bankId][entryIndex];
    u8 i;

    if (entry->sfxParams & MMSFX_FLAG_LOWER_VOLUME_BGM) {
        AudioMmSfx_RestoreBgmVolume(entry->channelIndex);
    }

    if (entryIndex == sMmSfxBankListEnd[bankId]) {
        sMmSfxBankListEnd[bankId] = entry->prev;
    } else {
        sMmSfxBanks[bankId][entry->next].prev = entry->prev;
    }

    sMmSfxBanks[bankId][entry->prev].next = entry->next;
    entry->next = sMmSfxBankFreeListStart[bankId];
    entry->prev = 0xFF;
    sMmSfxBanks[bankId][sMmSfxBankFreeListStart[bankId]].prev = entryIndex;
    sMmSfxBankFreeListStart[bankId] = entryIndex;
    entry->state = MMSFX_STATE_EMPTY;

    for (i = 0; i < sMmChannelsPerBank[bankId]; i++) {
        if (sMmActiveSfx[bankId][i].entryIndex == entryIndex) {
            sMmActiveSfx[bankId][i].entryIndex = 0xFF;
            i = sMmChannelsPerBank[bankId];
        }
    }
}

static void AudioMmSfx_ChooseActiveSfx(u8 bankId) {
    u8 numChosenSfx = 0;
    u8 numChannels;
    u8 entryIndex;
    u8 i;
    u8 j;
    u8 k;
    u8 sfxImportance;
    u8 needNewSfx;
    u8 chosenEntryIndex;
    MmSfxBankEntry* entry;
    MmActiveSfx chosenSfx[MAX_CHANNELS_PER_BANK];
    MmActiveSfx* activeSfx;
    f32 entryPosY;
    f32 entryPosX;

    for (i = 0; i < MAX_CHANNELS_PER_BANK; i++) {
        chosenSfx[i].priority = 0x7FFFFFFF;
        chosenSfx[i].entryIndex = 0xFF;
    }

    entryIndex = sMmSfxBanks[bankId][0].next;
    k = 0;

    // Delete stale sfx and prioritize remaining sfx into the active arrays
    while (entryIndex != 0xFF) {
        // Update the freshness for an "all-frame" sfx if it is still queued
        if ((sMmSfxBanks[bankId][entryIndex].state == MMSFX_STATE_QUEUED) &&
            (sMmSfxBanks[bankId][entryIndex].sfxId & MMSFX_FLAG_MASK)) {
            sMmSfxBanks[bankId][entryIndex].freshness--;

            // If a "one-frame" sfx is still in "SFX_STATE_PLAYING_ONE_FRAME", then remove the sfx
        } else if (!(sMmSfxBanks[bankId][entryIndex].sfxId & MMSFX_FLAG_MASK) &&
                   (sMmSfxBanks[bankId][entryIndex].state == MMSFX_STATE_PLAYING_ONE_FRAME)) {
            // ioPort 0, force stop sfx in seq 0
            AudioMmSfx_SetChannelIo(sMmSfxBanks[bankId][entryIndex].channelIndex, 0, 0);
            AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
        }

        // If a "all-frame" sfx goes 2 frames in the "queued" state (because it is too low priority), then remove it
        if (sMmSfxBanks[bankId][entryIndex].freshness == 0) {
            AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
        } else if (sMmSfxBanks[bankId][entryIndex].state != MMSFX_STATE_EMPTY) {
            entry = &sMmSfxBanks[bankId][entryIndex];

            // Recompute distSq each frame since the sound's position may have changed
            if (&sMmSfxDefaultPos.x == entry[0].posX) {
                entry->dist = 0.0f;
            } else {
                entryPosY = *entry->posY * 1;
                entryPosX = *entry->posX * 0.5f;
                entry->dist = (SQ(entryPosX) + SQ(entryPosY) + SQ(*entry->posZ)) / 10.0f;
            }

            // Recompute priority (lower is more preserved)
            sfxImportance = entry->sfxImportance;

            if (entry->sfxParams & MMSFX_FLAG_PRIORITY_NO_DIST) {
                entry->priority = SQ(0xFF - sfxImportance) * SQ(76);
            } else {
                if (entry->dist > 0x7FFFFFD0) {
                    entry->dist = 0x70000008;
                }

                entry->priority = (u32)entry->dist + (SQ(0xFF - sfxImportance) * SQ(76));
                if (*entry->posZ < 0.0f) {
                    entry->priority += (s32)(-*entry->posZ * 6.0f);
                }
            }

            // Sfx is too far away, do not consider for chosen sfx
            if (entry->dist > SQ(1e5f)) {
                // If too far away and also playing, stop playing
                if (entry->state == MMSFX_STATE_PLAYING) {
                    AudioMmSfx_SetChannelIo(entry->channelIndex, 0, 0);
                    if (entry->sfxId & MMSFX_FLAG_MASK) {
                        AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
                        entryIndex = k;
                    }
                }
            } else {
                // Sort the bank's sfx entries by priority into the chosen list
                numChannels = sMmChannelsPerBank[bankId];

                for (i = 0; i < numChannels; i++) {
                    if (chosenSfx[i].priority >= entry->priority) {
                        if (numChosenSfx < sMmChannelsPerBank[bankId]) {
                            numChosenSfx++;
                        }

                        for (j = numChannels - 1; j > i; j--) {
                            chosenSfx[j].priority = chosenSfx[j - 1].priority;
                            chosenSfx[j].entryIndex = chosenSfx[j - 1].entryIndex;
                        }

                        chosenSfx[i].priority = entry->priority;
                        chosenSfx[i].entryIndex = entryIndex;
                        i = numChannels; // "break"
                    }
                }
            }

            k = entryIndex;
        }

        entryIndex = sMmSfxBanks[bankId][k].next;
    }

    // If a sfx is chosen, update its state
    for (i = 0; i < numChosenSfx; i++) {
        entry = &sMmSfxBanks[bankId][chosenSfx[i].entryIndex];

        if (entry->state == MMSFX_STATE_QUEUED) {
            entry->state = MMSFX_STATE_READY;
        } else if (entry->state == MMSFX_STATE_PLAYING) {
            entry->state = MMSFX_STATE_PLAYING_REFRESH;
        }
    }

    // Apply the chosen sfx to the active sfx in each channel
    numChannels = sMmChannelsPerBank[bankId];
    for (i = 0; i < numChannels; i++) {
        needNewSfx = false;
        activeSfx = &sMmActiveSfx[bankId][i];

        if (activeSfx->entryIndex == 0xFF) {
            needNewSfx = true;
        } else {
            entry = &sMmSfxBanks[bankId][activeSfx[0].entryIndex];

            if (entry->state == MMSFX_STATE_PLAYING) {
                if (entry->sfxId & MMSFX_FLAG_MASK) {
                    AudioMmSfx_RemoveBankEntry(bankId, activeSfx->entryIndex);
                } else {
                    entry->state = MMSFX_STATE_QUEUED;
                    entry->freshness = 0x80;
                }
                needNewSfx = true;
            } else if (entry->state == MMSFX_STATE_EMPTY) {
                activeSfx->entryIndex = 0xFF;
                needNewSfx = true;
            } else {
                // Sfx is already playing as it should, nothing to do.
                for (j = 0; j < numChannels; j++) {
                    if (activeSfx->entryIndex == chosenSfx[j].entryIndex) {
                        chosenSfx[j].entryIndex = 0xFF;
                        j = numChannels;
                    }
                }

                numChosenSfx--;
            }
        }

        // enter the new sfx into the active sfx
        if (needNewSfx == true) {
            for (j = 0; j < numChannels; j++) {
                chosenEntryIndex = chosenSfx[j].entryIndex;
                if ((chosenEntryIndex != 0xFF) &&
                    (sMmSfxBanks[bankId][chosenEntryIndex].state != MMSFX_STATE_PLAYING_REFRESH)) {
                    for (k = 0; k < numChannels; k++) {
                        if (chosenEntryIndex == sMmActiveSfx[bankId][k].entryIndex) {
                            needNewSfx = false;
                            k = numChannels;
                        }
                    }

                    if (needNewSfx == true) {
                        activeSfx->entryIndex = chosenEntryIndex;
                        chosenSfx[j].entryIndex = 0xFF;
                        j = numChannels + 1;
                        numChosenSfx--;
                    }
                }
            }
            if (j == numChannels) {
                // nothing found
                activeSfx->entryIndex = 0xFF;
            }
        }
    }
}

static void AudioMmSfx_SetProperties(u8 bankId, u8 entryIndex, u8 channelIndex);

static void AudioMmSfx_PlayActiveSfx(u8 bankId) {
    u8 entryIndex;
    SequenceChannel* channel;
    MmSfxBankEntry* entry;
    u8 i;
    u8 ioPort5Data;

    for (i = 0; i < sMmChannelsPerBank[bankId]; i++) {
        entryIndex = sMmActiveSfx[bankId][i].entryIndex;
        if (entryIndex != 0xFF) {
            entry = &sMmSfxBanks[bankId][entryIndex];
            channel = gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX].channels[sMmCurSfxPlayerChannelIndex];

            if (entry->state == MMSFX_STATE_READY) {
                // Initialize a sfx (new sfx request)
                entry->channelIndex = sMmCurSfxPlayerChannelIndex;
                if (entry->sfxParams & MMSFX_FLAG_LOWER_VOLUME_BGM) {
                    AudioMmSfx_LowerBgmVolume(sMmCurSfxPlayerChannelIndex);
                }

                // Add noise that will offset the frequency of the sfx
                if ((entry->sfxParams & MMSFX_PARAM_RAND_FREQ_RAISE_MASK) != 0) {
                    switch (entry->sfxParams & MMSFX_PARAM_RAND_FREQ_RAISE_MASK) {
                        case (1 << MMSFX_PARAM_RAND_FREQ_RAISE_SHIFT):
                            entry->randFreq = Audio_NextRandom() & 0xF;
                            break;

                        case (2 << MMSFX_PARAM_RAND_FREQ_RAISE_SHIFT):
                            entry->randFreq = Audio_NextRandom() & 0x1F;
                            break;

                        case (3 << MMSFX_PARAM_RAND_FREQ_RAISE_SHIFT):
                            entry->randFreq = Audio_NextRandom() & 0x3F;
                            break;

                        default:
                            entry->randFreq = 0;
                            break;
                    }
                }

                // Calculate all the properties of sfx
                AudioMmSfx_SetProperties(bankId, entryIndex, sMmCurSfxPlayerChannelIndex);

                // ioPort 0, enable the sfx to play in MM's sfx sequence
                AudioMmSfx_SetChannelIo(sMmCurSfxPlayerChannelIndex, 0, 1);

                // ioPort 4, the lower bits of the sfx index for the sequence to find the right code to execute
                AudioMmSfx_SetChannelIo(sMmCurSfxPlayerChannelIndex, 4, entry->sfxId & 0xFF);

                // Banks with more than 255 entries store the upper bits too
                if (sMmIsLargeSfxBank[bankId]) {
                    ioPort5Data = ((u8)((entry->sfxId & 0x300) >> 7) + (u8)((entry->sfxId & 0xFF) >> 7));
                } else {
                    ioPort5Data = 0;
                }

                if ((entry->sfxParams & MMSFX_FLAG_8) && (entry->freshness == 0x80)) {
                    ioPort5Data += 0x80;
                }

                if (sMmSetsIoPort5[bankId]) {
                    // ioPort 5, the upper bits of the sfx index and a flag
                    AudioMmSfx_SetChannelIo(sMmCurSfxPlayerChannelIndex, 5, ioPort5Data);
                }

#ifdef COMBO_BUILD
                if (AudioMmSfx_Debug()) {
                    Combo_DebugLog("[ComboShip] MM sfx 0x%04X starts on MM player channel %d (io4 0x%02X io5 0x%02X)\n",
                                   entry->sfxId, sMmCurSfxPlayerChannelIndex, entry->sfxId & 0xFF, ioPort5Data);
                }
#endif

                if (entry->sfxId & MMSFX_FLAG_MASK) {
                    entry->state = MMSFX_STATE_PLAYING; // "all-frames" sfx
                } else {
                    entry->state = MMSFX_STATE_PLAYING_ONE_FRAME; // "one-frame" sfx
                }
            } else if ((u8)channel->soundScriptIO[1] == (u8)-1) {
                // Signal from the sequence that the sfx is finished playing. Remove entry
                AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
            } else if (entry->state == MMSFX_STATE_PLAYING_REFRESH) {
                // Sfx is playing but a refresh is requested
                AudioMmSfx_SetProperties(bankId, entryIndex, sMmCurSfxPlayerChannelIndex);

                if (entry->sfxId & MMSFX_FLAG_MASK) {
                    entry->state = MMSFX_STATE_PLAYING;
                } else {
                    entry->state = MMSFX_STATE_PLAYING_ONE_FRAME;
                }
            }
        }

        sMmCurSfxPlayerChannelIndex++;
    }
}

static void AudioMmSfx_StopByPosAndBankImpl(u8 bankId, Vec3f* pos) {
    MmSfxBankEntry* entry;
    u8 entryIndex = sMmSfxBanks[bankId][0].next;
    u8 prevEntryIndex = 0;

    while (entryIndex != 0xFF) {
        entry = &sMmSfxBanks[bankId][entryIndex];
        if (entry->posX == &pos->x) {
            if (entry->state >= MMSFX_STATE_PLAYING_REFRESH) {
                AudioMmSfx_SetChannelIo(entry->channelIndex, 0, 0);
            }

            if (entry->state != MMSFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
            }
        } else {
            prevEntryIndex = entryIndex;
        }

        entryIndex = sMmSfxBanks[bankId][prevEntryIndex].next;
    }
}

void AudioMmSfx_StopByPos(Vec3f* pos) {
    u8 bankId;
    MmSfxBankEntry entryToRemove;

    for (bankId = 0; bankId < MMBANK_MAX; bankId++) {
        AudioMmSfx_StopByPosAndBankImpl(bankId, pos);
    }

    entryToRemove.posX = &pos->x;
    AudioMmSfx_RemoveMatchingRequests(MMSFX_RM_REQ_BY_POS, &entryToRemove);
}

void AudioMmSfx_StopByPosAndId(Vec3f* pos, u16 sfxId) {
    MmSfxBankEntry* entry;
    u8 bankId = MMSFX_BANK(sfxId);
    u8 entryIndex;
    u8 prevEntryIndex = 0;
    MmSfxBankEntry entryToRemove;

    if (bankId >= MMBANK_MAX) {
        return;
    }
    entryIndex = sMmSfxBanks[bankId][0].next;
    while (entryIndex != 0xFF) {
        entry = &sMmSfxBanks[bankId][entryIndex];
        if ((entry->posX == &pos->x) && (entry->sfxId == sfxId)) {
            if (entry->state >= MMSFX_STATE_PLAYING_REFRESH) {
                AudioMmSfx_SetChannelIo(entry->channelIndex, 0, 0);
            }

            if (entry->state != MMSFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
            }
            entryIndex = 0xFF;
        } else {
            prevEntryIndex = entryIndex;
        }

        if (entryIndex != 0xFF) {
            entryIndex = sMmSfxBanks[bankId][prevEntryIndex].next;
        }
    }

    entryToRemove.posX = &pos->x;
    entryToRemove.sfxId = sfxId;
    AudioMmSfx_RemoveMatchingRequests(MMSFX_RM_REQ_BY_POS_AND_ID, &entryToRemove);
}

void AudioMmSfx_StopById(u32 sfxId) {
    MmSfxBankEntry* entry;
    u8 bankId = MMSFX_BANK(sfxId);
    u8 entryIndex;
    u8 prevEntryIndex = 0;
    MmSfxBankEntry entryToRemove;

    if (bankId >= MMBANK_MAX) {
        return;
    }
    entryIndex = sMmSfxBanks[bankId][0].next;
    while (entryIndex != 0xFF) {
        entry = &sMmSfxBanks[bankId][entryIndex];
        if (entry->sfxId == sfxId) {
            if (entry->state >= MMSFX_STATE_PLAYING_REFRESH) {
                AudioMmSfx_SetChannelIo(entry->channelIndex, 0, 0);
            }
            if (entry->state != MMSFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
            }
        } else {
            prevEntryIndex = entryIndex;
        }

        entryIndex = sMmSfxBanks[bankId][prevEntryIndex].next;
    }

    entryToRemove.sfxId = sfxId;
    AudioMmSfx_RemoveMatchingRequests(MMSFX_RM_REQ_BY_ID, &entryToRemove);
}

void AudioMmSfx_StopAll(void) {
    u8 bankId;
    MmSfxBankEntry* entry;
    MmSfxBankEntry entryToRemove;

    for (bankId = 0; bankId < MMBANK_MAX; bankId++) {
        u8 entryIndex = sMmSfxBanks[bankId][0].next;

        while (entryIndex != 0xFF) {
            entry = &sMmSfxBanks[bankId][entryIndex];
            if (entry->state >= MMSFX_STATE_PLAYING_REFRESH) {
                AudioMmSfx_SetChannelIo(entry->channelIndex, 0, 0);
            }
            if (entry->state != MMSFX_STATE_EMPTY) {
                AudioMmSfx_RemoveBankEntry(bankId, entryIndex);
            }
            entryIndex = sMmSfxBanks[bankId][0].next;
        }

        entryToRemove.sfxId = bankId << 12;
        AudioMmSfx_RemoveMatchingRequests(MMSFX_RM_REQ_BY_BANK, &entryToRemove);
    }
}

static void AudioMmSfx_ProcessRequests(void) {
    if (gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX].enabled) {
        while (sMmSfxRequestWriteIndex != sMmSfxRequestReadIndex) {
            AudioMmSfx_ProcessRequest();
            sMmSfxRequestReadIndex++;
        }
    }
}

static void AudioMmSfx_StepBankLerp(u8 bankId) {
    if (sMmSfxBankLerp[bankId].remainingFrames != 0) {
        sMmSfxBankLerp[bankId].remainingFrames--;

        if (sMmSfxBankLerp[bankId].remainingFrames != 0) {
            sMmSfxBankLerp[bankId].value -= sMmSfxBankLerp[bankId].step;
        } else {
            sMmSfxBankLerp[bankId].value = sMmSfxBankLerp[bankId].target;
        }
    }
}

static void AudioMmSfx_ProcessActiveSfx(void) {
    u8 bankId;

    if (gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX].enabled) {
        sMmCurSfxPlayerChannelIndex = 0;

        for (bankId = 0; bankId < MMBANK_MAX; bankId++) {
            AudioMmSfx_ChooseActiveSfx(bankId);
            AudioMmSfx_PlayActiveSfx(bankId);
            AudioMmSfx_StepBankLerp(bankId);
        }
    }
}

u8 AudioMmSfx_IsPlaying(u32 sfxId) {
    MmSfxBankEntry* entry;
    u8 bankId = MMSFX_BANK(sfxId);
    u8 entryIndex;

    if (bankId >= MMBANK_MAX) {
        return false;
    }
    entryIndex = sMmSfxBanks[bankId][0].next;
    while (entryIndex != 0xFF) {
        entry = &sMmSfxBanks[bankId][entryIndex];
        if (entry->sfxId == sfxId) {
            return true;
        }
        entryIndex = entry->next;
    }

    return false;
}

void AudioMmSfx_Reset(void) {
    u8 bankId;
    u8 i;

    sMmSfxRequestWriteIndex = 0;
    sMmSfxRequestReadIndex = 0;
    sMmSfxChannelLowVolumeFlag = 0;

    for (bankId = 0; bankId < MMBANK_MAX; bankId++) {
        sMmSfxBankListEnd[bankId] = 0;
        sMmSfxBankFreeListStart[bankId] = 1;
        sMmSfxBankMuted[bankId] = false;
        sMmSfxBankLerp[bankId].value = 1.0f;
        sMmSfxBankLerp[bankId].remainingFrames = 0;
    }

    for (bankId = 0; bankId < MMBANK_MAX; bankId++) {
        for (i = 0; i < MAX_CHANNELS_PER_BANK; i++) {
            sMmActiveSfx[bankId][i].entryIndex = 0xFF;
        }
    }

    for (bankId = 0; bankId < MMBANK_MAX; bankId++) {
        sMmSfxBanks[bankId][0].prev = 0xFF;
        sMmSfxBanks[bankId][0].next = 0xFF;

        for (i = 1; i < sMmSfxBankSizes[bankId] - 1; i++) {
            sMmSfxBanks[bankId][i].prev = i - 1;
            sMmSfxBanks[bankId][i].next = i + 1;
        }

        sMmSfxBanks[bankId][i].prev = i - 1;
        sMmSfxBanks[bankId][i].next = 0xFF;
    }
}

// ---- mm code_8019AF00.c: sfx properties ----------------------------------------------------------------------------

static f32 AudioMmSfx_ComputeVolume(u8 bankId, u8 entryIndex) {
    MmSfxBankEntry* bankEntry = &sMmSfxBanks[bankId][entryIndex];
    f32 minDist;
    f32 phi_f14;
    f32 baseDist = 500.0f;
    f32 phi_f12;
    f32 volume; // Also acts as intermediate volumeSquared

    if (bankEntry->sfxParams & MMSFX_FLAG_VOLUME_NO_DIST) {
        return 1.0f;
    }

    if (bankEntry->dist > 10000.0f) {
        volume = 0.0f;
    } else {
        if (bankEntry->sfxParams & MMSFX_PARAM_DIST_RANGE_MASK_UPPER) {
            phi_f12 = 0.35f;
            phi_f14 = 0.65f;
        } else {
            phi_f12 = 0.81f;
            phi_f14 = 0.19f;
        }

        switch (bankEntry->sfxParams & MMSFX_PARAM_DIST_RANGE_MASK) {
            case 0:
                baseDist = 500.0f;
                break;
            case 1:
                baseDist = 666.6667f;
                break;
            case 2:
                baseDist = 952.381f;
                break;
            case 3:
                baseDist = 3846.154f;
                break;
            case 4:
                baseDist = 1000.0f;
                break;
            case 5:
                baseDist = 666.6667f;
                break;
            case 6:
                baseDist = 500.0f;
                break;
            case 7:
                baseDist = 400.0f;
                break;
        }

        minDist = baseDist / 5.0f;

        if (bankEntry->dist < minDist) {
            volume = 1.0f;
        } else if (bankEntry->dist < baseDist) {
            volume =
                ((((baseDist - minDist) - (bankEntry->dist - minDist)) / (baseDist - minDist)) * phi_f14) + phi_f12;
        } else {
            volume = (1.0f - ((bankEntry->dist - baseDist) / (10000.0f - baseDist))) * phi_f12;
        }

        volume = SQ(volume);
    }

    return volume;
}

static s8 AudioMmSfx_ComputeReverb(u8 bankId, u8 entryIndex, u8 channelIndex) {
    s8 distAdd = 0;
    s32 scriptAdd = 0;
    MmSfxBankEntry* entry = &sMmSfxBanks[bankId][entryIndex];
    SequenceChannel* channel = gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX].channels[channelIndex];
    s32 reverb;

    if (!(entry->sfxParams & MMSFX_FLAG_REVERB_NO_DIST)) {
        if (entry->dist < 2500.0f) {
            distAdd = *entry->posZ > 0.0f ? (entry->dist / 2500.0f) * 70.0f : (entry->dist / 2500.0f) * 91.0f;
        } else {
            distAdd = 70;
        }
    }

    if (IS_SEQUENCE_CHANNEL_VALID(channel)) {
        scriptAdd = channel->soundScriptIO[1];
        if (channel->soundScriptIO[1] <= -1) {
            scriptAdd = 0;
        }
    }

    reverb = (*entry->reverbAdd & 0x7F) + distAdd;

    if (entry->state != MMSFX_STATE_READY) {
        reverb += scriptAdd;
    }

    if ((bankId != MMBANK_OCARINA) || (MMSFX_INDEX(entry->sfxId) >= 2)) {
        reverb += sAudioEnvReverb + (sAudioCodeReverb & 0x3F) + sSpecReverb;
    }

    if (reverb > 0x7F) {
        reverb = 0x7F;
    }

    return reverb;
}

static s8 AudioMmSfx_ComputePanSigned(f32 x, f32 z, u8 token) {
    f32 absX = ABS(x);
    f32 absZ = ABS(z);
    f32 pan;

    if (absX > 8000.0f) {
        absX = 8000.0f;
    }

    if (absZ > 8000.0f) {
        absZ = 8000.0f;
    }

    if ((x == 0.0f) && (z == 0.0f)) {
        pan = 0.5f;
    } else if (absZ <= absX) {
        pan = (16000.0f - absX) / (4.5f * (16000.0f - absZ));
        if (x >= 0.0f) {
            pan = 1.0f - pan;
        }
    } else {
        pan = (x / (3.6f * absZ)) + 0.5f;
    }

    if ((absZ < 50.0f) && (absX < 50.0f)) {
        pan = ((pan - 0.5f) * SQ(absX / 50.0f)) + 0.5f;
    }

    return (pan * 127.0f) + 0.5f;
}

static f32 AudioMmSfx_ComputeFreqScale(u8 bankId, u8 entryIndex) {
    s32 applyRandScaling = false;
    MmSfxBankEntry* entry = &sMmSfxBanks[bankId][entryIndex];
    f32 dist;
    f32 freq = 1.0f;

    if (entry->sfxParams & MMSFX_PARAM_RAND_FREQ_LOWER) {
        freq = 1.0f - ((gAudioContext.audioRandom & 0xF) / 192.0f);
    }

    switch (bankId) {
        case MMBANK_VOICE:
            if (((entry->sfxId & 0xFF) < 0x40) && (sAudioBaseFilter2 != 0)) {
                applyRandScaling = true;
            } else if (((entry->sfxId & 0xFF) >= 0x40) && (sAudioExtraFilter2 != 0)) {
                applyRandScaling = true;
            }
            break;

        case MMBANK_PLAYER:
        case MMBANK_ITEM:
            if (sAudioBaseFilter2 != 0) {
                applyRandScaling = true;
            }
            break;

        case MMBANK_ENV:
        case MMBANK_ENEMY:
            if (((*entry->reverbAdd & 0x80) != 0) | (sAudioExtraFilter2 != 0)) {
                applyRandScaling = true;
            }
            break;

        default:
            break;
    }

    if ((applyRandScaling == true) && !(entry->sfxParams & MMSFX_PARAM_RAND_FREQ_SCALE)) {
        freq *= 1.0293 - ((gAudioContext.audioRandom & 0xF) / 144.0f);
    }

    dist = entry->dist;
    if (!(entry->sfxParams & MMSFX_FLAG_VOLUME_NO_DIST) && !(entry->sfxParams & MMSFX_FLAG_FREQ_NO_DIST)) {
        if (dist >= 10000.0f) {
            freq += 0.2f;
        } else {
            freq += (0.2f * (dist / 10000.0f));
        }
    }

    if ((entry->sfxParams & MMSFX_PARAM_RAND_FREQ_RAISE_MASK) != 0) {
        freq += (entry->randFreq / 192.0f);
    }

    return freq;
}

static u8 AudioMmSfx_ComputeSurroundSoundFilter(MmSfxBankEntry* entry) {
    u8 lowPassCutoff = 0;
    u8 highPassCutoff;
    f32 distParam = 15.0f;
    f32 dist;

    if (entry->sfxParams & MMSFX_FLAG_SURROUND_LOWPASS_FILTER) {
        lowPassCutoff = 0xF;
    }

    switch (entry->sfxParams & MMSFX_PARAM_DIST_RANGE_MASK) {
        case 0:
            distParam = 15.0f;
            break;
        case 1:
            distParam = 12.0f;
            break;
        case 2:
            distParam = 9.0f;
            break;
        case 3:
            distParam = 6.0f;
            break;
        case 4:
            distParam = 18.0f;
            break;
        case 5:
            distParam = 21.0f;
            break;
        case 6:
            distParam = 24.0f;
            break;
        case 7:
            distParam = 27.0f;
            break;
    }

    if (!(entry->sfxFlags & MMSFX_FLAG2_SURROUND_NO_HIGHPASS_FILTER)) {
        if (entry->dist > 1923.077f) {
            dist = 1923.077f;
        } else {
            dist = entry->dist;
        }

        highPassCutoff = (distParam * dist) / (2 * 1923.077f);
    } else {
        highPassCutoff = 0;
    }

    return (lowPassCutoff << 4) + highPassCutoff;
}

static s8 AudioMmSfx_ComputeCombFilter(f32 posY) {
    s8 combFilterGain;

    if (posY < 0.0f) {
        if (posY < -625.0f) {
            combFilterGain = -32;
        } else {
            combFilterGain = (s8)(((625.0f + posY) / 625.0f) * 31.0f) + 0xE0;
        }
    } else if (posY > 1250.0f) {
        combFilterGain = 127;
    } else {
        combFilterGain = (posY / 1250.0f) * 126.0f;
    }

    return combFilterGain | 1;
}

static void AudioMmSfx_SetProperties(u8 bankId, u8 entryIndex, u8 channelIndex) {
    static s8 sZVolumes[] = {
        0,  2,  4,   6,   8,   10,  12,  14,  16,  18,  20,  22,  24,  25,  26,  27,  28,  29,  30, 31, 32, 33,
        34, 35, 36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52, 53, 54, 55,
        56, 56, 57,  57,  58,  58,  59,  59,  60,  60,  60,  61,  61,  61,  62,  62,  62,  63,  63, 63, 64, 64,
        64, 65, 65,  65,  66,  66,  66,  67,  67,  67,  68,  68,  69,  69,  70,  70,  71,  71,  72, 73, 74, 75,
        76, 77, 78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94, 95, 96, 97,
        98, 99, 100, 101, 102, 103, 105, 107, 109, 111, 113, 115, 117, 119, 121, 123, 125, 127,
    };
    f32 volume = 1.0f;
    s8 volumeS8;
    s8 reverb = 0;
    f32 freqScale = 1.0f;
    s8 panSigned = 0x40;
    u8 filter = 0;
    s8 combFilterGain = 0;
    s8 zVolume = -1;
    u8 baseFilter = 0;
    MmSfxBankEntry* entry = &sMmSfxBanks[bankId][entryIndex];

    switch (bankId) {
        case MMBANK_PLAYER:
        case MMBANK_ITEM:
        case MMBANK_ENV:
        case MMBANK_ENEMY:
        case MMBANK_VOICE:
            if (AudioMmSfx_SoundMode() == MMSOUNDMODE_SURROUND_EXTERNAL) {
                combFilterGain = AudioMmSfx_ComputeCombFilter(*entry->posY);
            }
            // fallthrough
        case MMBANK_OCARINA:
            entry->dist = sqrtf(entry->dist * 10.0f);
            volume = AudioMmSfx_ComputeVolume(bankId, entryIndex) * *entry->volume;
            reverb = AudioMmSfx_ComputeReverb(bankId, entryIndex, channelIndex);
            panSigned = AudioMmSfx_ComputePanSigned(*entry->posX, *entry->posZ, entry->token);
            freqScale = AudioMmSfx_ComputeFreqScale(bankId, entryIndex) * *entry->freqScale;

            if (AudioMmSfx_SoundMode() == MMSOUNDMODE_SURROUND_EXTERNAL) {
                if (*entry->posZ >= 0.0f) {
                    if (*entry->posZ < 200.0f) {
                        zVolume = 32.0f - ((*entry->posZ / 300.0f) * 32.0f);
                    } else {
                        zVolume = 0;
                    }
                } else if (*entry->posZ < 0.0f) {
                    if (*entry->posZ > -300.0f) {
                        zVolume = 32.0f - ((*entry->posZ / 300.0f) * 95.0f);
                    } else {
                        zVolume = 127;
                    }
                }
                zVolume = sZVolumes[(u8)zVolume];
            }

            if (sAudioBaseFilter != 0) {
                if ((bankId == MMBANK_PLAYER) || (bankId == MMBANK_ITEM) ||
                    ((bankId == MMBANK_VOICE) && (((entry->sfxId & 0xFF) < 0x40) || ((entry->sfxId & 0xFF) >= 0x80)))) {
                    baseFilter = sAudioBaseFilter;
                    if (sAudioExtraFilter) {
                        reverb >>= 2;
                    }
                }
            }

            if (((*entry->reverbAdd & 0x80) | baseFilter | sAudioExtraFilter |
                 (entry->sfxFlags & MMSFX_FLAG2_APPLY_LOWPASS_FILTER)) != 0) {
                filter = ((((*entry->reverbAdd & 0x80) >> 2) | baseFilter | sAudioExtraFilter |
                           ((entry->sfxFlags & MMSFX_FLAG2_APPLY_LOWPASS_FILTER) >> 2)) *
                          2);
                filter &= 0xFF;
            } else if ((AudioMmSfx_SoundMode() == MMSOUNDMODE_SURROUND_EXTERNAL) &&
                       !(entry->sfxParams & MMSFX_FLAG_VOLUME_NO_DIST)) {
                filter = AudioMmSfx_ComputeSurroundSoundFilter(entry);
            }
            break;

        case MMBANK_SYSTEM:
            break;
    }

    if (sMmSfxChannelState[channelIndex].volume != volume) {
        volumeS8 = (u8)(volume * 127.0f);
        sMmSfxChannelState[channelIndex].volume = volume;
    } else {
        volumeS8 = -1;
    }

    // ioPort 2, sets volume
    AudioMmSfx_SetChannelIo(channelIndex, 2, volumeS8);

    if (sMmSfxChannelState[channelIndex].reverb != reverb) {
        sMmSfxChannelState[channelIndex].reverb = reverb;
    }
    if (sMmSfxChannelState[channelIndex].freqScale != freqScale) {
        sMmSfxChannelState[channelIndex].freqScale = freqScale;
    }
    if (sMmSfxChannelState[channelIndex].stereoBits != 0) {
        sMmSfxChannelState[channelIndex].stereoBits = 0;
    }
    if (sMmSfxChannelState[channelIndex].filter != filter) {
        sMmSfxChannelState[channelIndex].filter = filter;
    }
    if (sMmSfxChannelState[channelIndex].combFilterGain != (u8)combFilterGain) {
        sMmSfxChannelState[channelIndex].combFilterGain = combFilterGain;
    }
    if (sMmSfxChannelState[channelIndex].zVolume != (u8)zVolume) {
        sMmSfxChannelState[channelIndex].zVolume = zVolume;
    }
    if (sMmSfxChannelState[channelIndex].panSigned != panSigned) {
        sMmSfxChannelState[channelIndex].panSigned = panSigned;
    }
}

// MM AUDIO_CUSTOM_FUNCTION_SEQ_0 (AudioSfx_SetFreqAndStereoBits), called by the MM sfx sequence (channel opcode
// 0xBE 0x00) on OOT's audio thread with the sfx channel index.
u32 AudioMmSfx_SetFreqAndStereoBits(u8 seqScriptValIn, SequenceChannel* channel) {
    if (seqScriptValIn < MMSFX_NUM_CHANNELS) {
        channel->stereo.asByte = sMmSfxChannelState[seqScriptValIn].stereoBits;
        channel->freqScale = sMmSfxChannelState[seqScriptValIn].freqScale;
        channel->changes.s.freqScale = true;
    }

    return seqScriptValIn;
}

// The SfxChannelState each MM sfx channel reads (MM sets these with AUDIOCMD_CHANNEL_SET_SFX_STATE after starting
// the sequence; audio_seqplayer_mm.c attaches them when the player runs).
u8* AudioMmSfx_GetChannelState(s32 channelIndex) {
    return (u8*)&sMmSfxChannelState[channelIndex];
}

void AudioMmSfx_ResetSfxChannelState(void) {
    MmSfxChannelState* state;
    u8 i;

    for (i = 0; i < ARRAY_COUNT(sMmSfxChannelState); i++) {
        state = &sMmSfxChannelState[i];
        state->volume = 1.0f;
        state->freqScale = 1.0f;
        state->reverb = 0;
        state->panSigned = 0x40;
        state->stereoBits = 0;
        state->filter = 0xFF;
        state->combFilterGain = 0xFF;
        state->zVolume = 0xFF;
    }

    sMmSfxChannelState[MMSFX_CHANNEL_OCARINA].combFilterGain = 0;
}

// ---- integration with OOT's Audio_Update and the C API -------------------------------------------------------------

#ifdef COMBO_BUILD
int Combo_MmAudio_SfxSeqId(void); // ComboMmAudio.cpp; -1 when MM's audio is unavailable
#else
static int Combo_MmAudio_SfxSeqId(void) {
    return -1;
}
#endif

// Owned source positions. Slot 0 follows Link (refreshed from the live player every update); the others are keyed by
// a caller identity that is never dereferenced, holding either a snapshot of a projected position (refreshed whenever
// the caller plays again) or a world position re-projected through the current camera every update (MM's
// SoundSource_PlaySfxAtFixedWorldPos).
#define MMSFX_NUM_SOURCES 24
typedef struct {
    const void* key; // caller identity (NULL = free)
    Vec3f pos;       // the projected position the MM sfx front end reads
    Vec3f worldPos;
    s32 worldAnchored;
    s32 idleFrames; // updates since the caller last played on it
} MmSfxSource;

static MmSfxSource sMmSfxSources[MMSFX_NUM_SOURCES];
static s32 sMmSfxStarted = false;
static f32 sMmSfxLastGameVolume = -1.0f;

#ifdef COMBO_BUILD
void Combo_MmSfxDebugTick(void); // ComboGuestAudio.cpp test harness (COMBO_DEBUG_SFX_RENDER)
#endif

void AudioMmSfx_ResetAll(void);

// MM AudioSfx_Init: start MM's sfx sequence on the native MM player.
void AudioMmSfx_Init(u16 fadeTimer) {
    s32 seqId = Combo_MmAudio_SfxSeqId();

    if (seqId < 0) {
        return;
    }
    if (!sMmSfxStarted) {
        AudioMmSfx_ResetAll(); // first start: build the bank lists
    }
    // AUDIOCMD_GLOBAL_INIT_SEQPLAYER (OOT op 0x82; the 16-bit seq id in the low bits, as Audio_StartSequence packs it)
    Audio_QueueCmdS32(0x82000000 | _SHIFTL(SEQ_PLAYER_COMBO_MM_SFX, 16, 8) | (seqId & 0xFFFF), fadeTimer);
    // MM's AUDIOCMD_SEQPLAYER_SET_IO(SEQ_PLAYER_SFX, 0, gSfxChannelLayout): channel layout 0
    Audio_QueueCmdS8(0x46000000 | _SHIFTL(SEQ_PLAYER_COMBO_MM_SFX, 16, 8) | 0, 0);
    sMmSfxStarted = true;
    sMmSfxLastGameVolume = -1.0f;
}

// Audio_InitSound / audio heap reset (MM: AudioSfx_ResetSfxChannelState + AudioSfx_Reset).
void AudioMmSfx_ResetAll(void) {
    s32 i;

    AudioMmSfx_ResetSfxChannelState();
    AudioMmSfx_Reset();
    for (i = 0; i < MMSFX_NUM_SOURCES; i++) {
        sMmSfxSources[i].key = NULL;
    }
}

// OOT game thread, from Audio_Update before the audio commands are scheduled (MM's order: requests, then active sfx).
void AudioMmSfx_Update(void) {
    s32 i;
    f32 sfxVolume;

    if (!sMmSfxStarted) {
        return;
    }

#ifdef COMBO_BUILD
    // Link's source follows the live player (looked up fresh every update, never a kept pointer).
    if ((gPlayState != NULL) && (GET_PLAYER(gPlayState) != NULL)) {
        sMmSfxSources[0].pos = GET_PLAYER(gPlayState)->actor.projectedPos;
    }
    for (i = 1; i < MMSFX_NUM_SOURCES; i++) {
        if ((sMmSfxSources[i].key != NULL) && sMmSfxSources[i].worldAnchored && (gPlayState != NULL)) {
            SkinMatrix_Vec3fMtxFMultXYZ(&gPlayState->viewProjectionMtxF, &sMmSfxSources[i].worldPos,
                                        &sMmSfxSources[i].pos);
        }
    }
    Combo_MmSfxDebugTick();
#endif

    // The MM player follows OOT's Sound Effects slider, like OOT's own sfx player (audioMgr.c).
    sfxVolume = CVarGetInteger("gSettings.Volume.SFX", 100) / 100.0f;
    if (sfxVolume != sMmSfxLastGameVolume) {
        Audio_SetGameVolume(SEQ_PLAYER_COMBO_MM_SFX, sfxVolume);
        sMmSfxLastGameVolume = sfxVolume;
    }

    AudioMmSfx_ProcessRequests();
    AudioMmSfx_ProcessActiveSfx();

    // Retire snapshot sources nothing plays at any more.
    for (i = 1; i < MMSFX_NUM_SOURCES; i++) {
        if ((sMmSfxSources[i].key != NULL) && (++sMmSfxSources[i].idleFrames > 60)) {
            u8 bankId;
            s32 inUse = false;

            for (bankId = 0; (bankId < MMBANK_MAX) && !inUse; bankId++) {
                u8 entryIndex = sMmSfxBanks[bankId][0].next;
                while (entryIndex != 0xFF) {
                    if (sMmSfxBanks[bankId][entryIndex].posX == &sMmSfxSources[i].pos.x) {
                        inUse = true;
                        break;
                    }
                    entryIndex = sMmSfxBanks[bankId][entryIndex].next;
                }
            }
            if (!inUse) {
                sMmSfxSources[i].key = NULL;
            }
        }
    }
}

static Vec3f* AudioMmSfx_SourceFor(const void* key, const Vec3f* pos, s32 worldAnchored) {
    s32 i;
    s32 free = -1;
    s32 oldest = 1;

    if (key == NULL) {
        return &sMmSfxSources[0].pos; // Link
    }
    for (i = 1; i < MMSFX_NUM_SOURCES; i++) {
        if (sMmSfxSources[i].key == key) {
            break;
        }
        if ((free < 0) && (sMmSfxSources[i].key == NULL)) {
            free = i;
        }
        if (sMmSfxSources[i].idleFrames > sMmSfxSources[oldest].idleFrames) {
            oldest = i;
        }
    }
    if (i == MMSFX_NUM_SOURCES) {
        if (free < 0) {
            // All in use: take over the stalest source (its sounds stop first).
            free = oldest;
            AudioMmSfx_StopByPos(&sMmSfxSources[free].pos);
        }
        i = free;
        sMmSfxSources[i].key = key;
    }
    sMmSfxSources[i].worldAnchored = worldAnchored;
    if (worldAnchored) {
        sMmSfxSources[i].worldPos = *pos;
#ifdef COMBO_BUILD
        if (gPlayState != NULL) {
            SkinMatrix_Vec3fMtxFMultXYZ(&gPlayState->viewProjectionMtxF, &sMmSfxSources[i].worldPos,
                                        &sMmSfxSources[i].pos);
        }
#endif
    } else {
        sMmSfxSources[i].pos = *pos;
    }
    sMmSfxSources[i].idleFrames = 0;
    return &sMmSfxSources[i].pos;
}

static MmSfxSource* AudioMmSfx_FindSource(const void* key) {
    s32 i;

    if (key == NULL) {
        return &sMmSfxSources[0];
    }
    for (i = 1; i < MMSFX_NUM_SOURCES; i++) {
        if (sMmSfxSources[i].key == key) {
            return &sMmSfxSources[i];
        }
    }
    return NULL;
}

// Plays MM sfx `mmSfxId` (an id from MM's include/sfx.h) natively at a source identified by `key`. `pos` is copied:
// a projected position (as MM actors pass their projectedPos), or with `worldAnchored` a world position kept in place
// as the camera moves. key NULL = at Link (pos ignored). Continuous ("all-frame") sfx are replayed every frame by the
// caller, as in MM. Returns 1 if MM's sfx are available.
s32 Combo_MmSfx_PlayKeyed(u16 mmSfxId, const void* key, const Vec3f* pos, s32 worldAnchored) {
    if (!sMmSfxStarted || !gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX].enabled) {
        return 0;
    }
    if ((key != NULL) && (pos == NULL)) {
        return 0;
    }
    AudioMmSfx_PlaySfx(mmSfxId, AudioMmSfx_SourceFor(key, pos, worldAnchored), 4, &sMmSfxDefaultFreqAndVolScale,
                       &sMmSfxDefaultFreqAndVolScale, &sMmSfxDefaultReverb);
    return 1;
}

// Plays MM sfx `mmSfxId` at the projected position `pos`, which also identifies the source (NULL = at Link). The
// value is copied: callers may pass a pointer that dies right after.
s32 Combo_MmSfx_Play(u16 mmSfxId, const Vec3f* pos) {
    return Combo_MmSfx_PlayKeyed(mmSfxId, pos, pos, false);
}

// Stops every MM sfx playing from the source `key` identifies (NULL = Link) and forgets that source.
void Combo_MmSfx_StopByKey(const void* key) {
    MmSfxSource* src = AudioMmSfx_FindSource(key);

    if (!sMmSfxStarted || (src == NULL)) {
        return;
    }
    AudioMmSfx_StopByPos(&src->pos);
    if (key != NULL) {
        src->key = NULL;
    }
}

void Combo_MmSfx_StopByPos(const Vec3f* pos) {
    Combo_MmSfx_StopByKey(pos);
}

// Stops MM sfx `mmSfxId` from the source `key` identifies (NULL = Link).
void Combo_MmSfx_StopByPosAndId(const void* key, u16 mmSfxId) {
    MmSfxSource* src = AudioMmSfx_FindSource(key);

    if (sMmSfxStarted && (src != NULL)) {
        AudioMmSfx_StopByPosAndId(&src->pos, mmSfxId);
    }
}

// Stops MM sfx `mmSfxId` wherever it plays.
void Combo_MmSfx_StopById(u16 mmSfxId) {
    if (sMmSfxStarted) {
        AudioMmSfx_StopById(mmSfxId);
    }
}

// (Every entry point is inert until AudioMmSfx_ResetAll + AudioMmSfx_Init ran: before that the bank lists are not
// built, and OOT may hand the foreground to MM before its audio was ever initialized.)
void Combo_MmSfx_StopAll(void) {
    if (sMmSfxStarted) {
        AudioMmSfx_StopAll();
    }
}

s32 Combo_MmSfx_Available(void) {
    return sMmSfxStarted && gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX].enabled;
}

s32 Combo_MmSfx_IsPlaying(u16 mmSfxId) {
    return sMmSfxStarted && AudioMmSfx_IsPlaying(mmSfxId);
}
