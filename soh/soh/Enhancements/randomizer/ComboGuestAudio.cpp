// soh/soh/Enhancements/randomizer/ComboGuestAudio.cpp — ComboShip: MM transformations in Hyrule play MM's own sounds.
//
// The forms' voices and move sounds are Majora's Mask's real sound effects, played NATIVELY by OOT's audio engine: MM's
// sfx sequence and soundfonts (read from MM's resident ResourceManager, ComboMmAudio.cpp) run on an extra OOT sequence
// player with MM's script semantics (audio_seqplayer_mm.c), driven by a port of MM's sfx front end (audio_mm_sfx.c).
// One engine, one mixer, no added latency. (The previous design ran MM's whole engine beside OOT's and mixed the two
// outputs; it crackled and added latency. That guest engine, mm/2s2h/Rando/MiscBehavior/ComboGuestAudio.cpp, now only
// serves the fidelity harness below as the MM-native reference.)
//
// Voices: OOT and MM lay out Link's voice bank identically (0x6800 + index, 0x20 per voice set). OOT's adult/child
// offsets (0 / 0x20) are swapped for MM's per-form offsets (mm z_player.c sAgeProperties voiceSfxIdOffset:
// Fierce Deity x0, Goron x6, Zora x5, Deku x4), so every voice line OOT's player code plays (jump, damage, fall,
// climb, sword shouts...) comes out in the form's own voice.
//
// The Combo_Guest* names are kept so the game-code seams (z_player.c, z_actor.c, z_en_arrow_combo_bubble.c) need no
// change; they route to Combo_MmSfx_* now. CVar gCombo.Audio.MmSfx (default 1) turns the MM sounds off (OOT's fall
// back where a seam has one).

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <libultraship/libultraship.h> // C++ headers first, as the other combo files do

#include "ComboForms.h"
#include "ComboResolve.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;

// audio_mm_sfx.c
s32 Combo_MmSfx_Play(u16 mmSfxId, const Vec3f* pos);
s32 Combo_MmSfx_PlayKeyed(u16 mmSfxId, const void* key, const Vec3f* pos, s32 worldAnchored);
void Combo_MmSfx_StopByKey(const void* key);
void Combo_MmSfx_StopByPosAndId(const void* key, u16 mmSfxId);
void Combo_MmSfx_StopById(u16 mmSfxId);
void Combo_MmSfx_StopAll(void);
s32 Combo_MmSfx_Available(void);

// code_800EC960.c: MM music as a fanfare on OOT's fanfare player (MM Audio_PlayFanfare semantics)
void Audio_ComboMmPlayFanfare(u16 ootSeqId);
void Audio_ComboMmStopFanfare(void);
s32 Audio_ComboMmFanfareIsPlaying(void);
// ComboMmAudio.cpp
int Combo_MmAudio_MusicSeqId(int mmSeqId);
}

namespace {

const bool sDbg = Combo_DebugEnv("COMBO_DEBUG_AUDIO");

constexpr u16 kVoiceBase = 0x6800; // NA_SE_VO_LI_SWORD_N in both games
constexpr u16 kVoiceBankSize = 0x20;
constexpr u16 kVoiceLast = 0x683F; // NA_SE_VO_LI_* end of OOT's child set
constexpr int kSlots = 16;         // actor sound slots 1..15 (0 unused)

u16 MmVoiceOffset(int form) {
    switch (form) {
        case COMBO_FORM_FIERCE_DEITY:
            return kVoiceBankSize * 0;
        case COMBO_FORM_GORON:
            return kVoiceBankSize * 6;
        case COMBO_FORM_ZORA:
            return kVoiceBankSize * 5;
        case COMBO_FORM_DEKU:
            return kVoiceBankSize * 4;
        default:
            return 0;
    }
}

bool Enabled() {
    return CVarGetInteger("gCombo.Audio.MmSfx", 1) && Combo_MmSfx_Available();
}

// Identity keys for actor slots (never dereferenced: positions are copied by the MM sfx front end).
char sSlotKeys[kSlots];

} // namespace

extern "C" {

// OOT -> MM handoff: stop every MM sound OOT's engine plays before MM takes the foreground.
void Combo_GuestAudioEnd(void) {
    Combo_MmSfx_StopAll();
    if (Audio_ComboMmFanfareIsPlaying()) {
        Audio_ComboMmStopFanfare();
    }
}

// MM music on OOT's fanfare player, as MM's Player plays the Bremen Mask march (NA_BGM_BREMEN_MARCH 0x53) and Kamaro's
// dance (NA_BGM_KAMARO_DANCE 0x71) with Audio_PlayFanfare. MM's own sequence, soundfonts and samples, run natively
// with MM script semantics. 1 = started; 0 = unavailable (MM sounds off, or the sequence is not registered —
// ComboMmAudio.cpp kMmMusicSeqs) so the caller can fall back.
int Combo_MmPlayFanfare(unsigned short mmSeqId) {
    if (!CVarGetInteger("gCombo.Audio.MmSfx", 1)) {
        return 0;
    }
    const int ootSeqId = Combo_MmAudio_MusicSeqId(mmSeqId);
    if (ootSeqId < 0) {
        return 0;
    }
    if (sDbg) {
        Combo_DebugLog("[ComboShip] MM fanfare: MM seq 0x%02X (OOT seq %d)\n", mmSeqId, ootSeqId);
    }
    Audio_ComboMmPlayFanfare((u16)ootSeqId);
    return 1;
}

// MM Player func_80831454 when the march / dance ends: SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0). Like MM's, it
// stops whatever the fanfare player plays (the BGM is restored by the setup commands the start queued).
void Combo_MmStopFanfare(void) {
    Audio_ComboMmStopFanfare();
}

// The MM fanfare started by Combo_MmPlayFanfare is pending or still playing.
int Combo_MmFanfareIsPlaying(void) {
    return Audio_ComboMmFanfareIsPlaying() ? 1 : 0;
}

// Player_PlaySfx: a voice line while a form is out plays in the form's MM voice. 1 = handled (skip OOT's).
int Combo_GuestVoice(u16 ootSfxId) {
    const int form = Combo_OotForm();
    // A continuous line is requested without SFX_FLAG (0x800), e.g. Actor_PlaySfx_Flagged2(NA_SE_VO_LI_TAKEN_AWAY -
    // SFX_FLAG), the electric shock's cry: same voice line, and MM requests its own continuous lines the same way.
    const bool continuous = (ootSfxId & 0x800) == 0;
    const u16 id = (u16)(ootSfxId | 0x800);
    if ((form == COMBO_FORM_HUMAN) || (id < kVoiceBase) || (id > kVoiceLast) || !Enabled()) {
        return 0;
    }
    const u16 line = (u16)((id - kVoiceBase) % kVoiceBankSize);
    const u16 mmId = (u16)((kVoiceBase + MmVoiceOffset(form) + line) & (continuous ? ~0x800 : 0xFFFF));
    if (sDbg) {
        Combo_DebugLog("[ComboShip] MM voice: OOT 0x%04X -> MM 0x%04X (form %d)\n", ootSfxId, mmId, form);
    }
    return Combo_MmSfx_Play(mmId, nullptr); // at Link, as MM's Player_PlaySfx plays at the player's projectedPos
}

// OOT stops a voice line (func_808326F0: the fall / climb cries when the action changes); the form's MM line playing
// in its place stops with it, as MM's func_8082E00C stops its own.
void Combo_GuestVoiceStop(u16 ootSfxId) {
    const int form = Combo_OotForm();
    const u16 id = (u16)(ootSfxId | 0x800);
    if ((form == COMBO_FORM_HUMAN) || (id < kVoiceBase) || (id > kVoiceLast) || !Enabled()) {
        return;
    }
    Combo_MmSfx_StopByPosAndId(nullptr, (u16)(kVoiceBase + MmVoiceOffset(form) + ((id - kVoiceBase) % kVoiceBankSize)));
}

// A specific MM sound at Link (form moves: punch, roll, pound...). 1 = played.
int Combo_GuestPlaySfx(u16 mmSfxId) {
    if (!Enabled()) {
        return 0;
    }
    if (sDbg) {
        Combo_DebugLog("[ComboShip] MM sfx: 0x%04X at Link\n", mmSfxId);
    }
    return Combo_MmSfx_Play(mmSfxId, nullptr);
}

// Stop an MM sound playing at Link (held/looping sounds such as the Goron roll).
void Combo_GuestStopSfx(u16 mmSfxId) {
    Combo_MmSfx_StopByPosAndId(nullptr, mmSfxId);
}

// Actor-positioned MM sounds: an actor takes a slot (1..15) for its life, plays at its projected position (continuous
// sounds re-played each frame, as MM's actor audio does), and frees it on destroy.
int Combo_GuestAllocSlot(void) {
    static int sNext = 0;
    sNext = (sNext % (kSlots - 1)) + 1;
    return sNext;
}

void Combo_GuestPlaySfxAt(u16 mmSfxId, int slot, const float* projectedPos) {
    if ((projectedPos == nullptr) || (slot <= 0) || (slot >= kSlots) || !Enabled()) {
        return;
    }
    if (sDbg) {
        Combo_DebugLog("[ComboShip] MM sfx: 0x%04X at slot %d\n", mmSfxId, slot);
    }
    Combo_MmSfx_PlayKeyed(mmSfxId, &sSlotKeys[slot], reinterpret_cast<const Vec3f*>(projectedPos), false);
}

// A one-shot MM sound at a fixed world position (MM SoundSource_PlaySfxAtFixedWorldPos), e.g. a bubble popping. The
// source stays anchored in the world: it is re-projected through the current camera every frame while it plays.
void Combo_GuestPlaySfxAtWorld(PlayState* play, u16 mmSfxId, const float* worldPos) {
    if ((play == nullptr) || (worldPos == nullptr) || !Enabled()) {
        return;
    }
    static int sWorldKey = 0;
    static char sWorldKeys[8];
    sWorldKey = (sWorldKey + 1) % 8;
    Combo_MmSfx_StopByKey(&sWorldKeys[sWorldKey]);
    Combo_MmSfx_PlayKeyed(mmSfxId, &sWorldKeys[sWorldKey], reinterpret_cast<const Vec3f*>(worldPos), true);
}

void Combo_GuestStopSlot(int slot) {
    if ((slot > 0) && (slot < kSlots)) {
        Combo_MmSfx_StopByKey(&sSlotKeys[slot]);
    }
}

} // extern "C"

// ============================================================================================================== //
// Test harness (headless runs only).
//
// COMBO_DEBUG_AUDIO=1: once a second the audio thread logs the native MM player's live notes (font, sample address
// and size, codec) and the loudest output sample, proving MM sfx start notes in OOT's engine with MM's samples.
//
// COMBO_DEBUG_SFX_RENDER=68C0*6,1857,0990,105A:30 (hex MM sfx ids; ":N" = replay every frame for N frames, for
// continuous sfx; "*K" = K renders each, files suffixed _1.._K-1, as MM sfx pick random variants): renders each id through BOTH OOT's native path and MM's own engine (the guest engine, as the
// MM-native reference) into WAV files <dir>/mmsfx_<id>_{oot,mm}.wav (COMBO_DEBUG_SFX_RENDER_DIR, default "."). Both
// play at the same fixed projected position (the screen center), with OOT's other players silenced during capture.
// ============================================================================================================== //
namespace {

typedef void (*FnVoid)(void);
typedef void (*FnTick)(const float*, float, float);
typedef void (*FnSfxAt)(u16, int, const float*);
typedef void (*FnSlot)(int);
typedef u32 (*FnRead)(s16*, u32, u32);

struct Job {
    u16 id;
    int hold; // frames to replay the request (0 = one request)
    int rep;  // repetition (MM sfx pick random variants: compare sets of renders)
};

enum class Phase { Wait, NativeSilence, NativeCapture, GuestBegin, GuestCapture, Next, Done };
enum class Source : int { None, Native, Guest };

struct Harness {
    bool parsed = false;
    std::vector<Job> jobs;
    std::string dir = ".";
    size_t job = 0;
    Phase phase = Phase::Wait;
    int frames = 0;
    int phaseFrames = 0;
    float savedVolumes[4] = {};
    bool muted = false;
    bool guestOpen = false;
    FnVoid guestBegin = nullptr, guestEnd = nullptr;
    FnTick guestTick = nullptr;
    FnSfxAt guestPlayAt = nullptr;
    FnSlot guestStopSlot = nullptr;
    FnRead guestRead = nullptr;
} sH;

constexpr u32 kCaptureFrames = 32000 * 3; // 3 s of 32 kHz stereo frames
std::atomic<int> sCaptureSource{ (int)Source::None };
std::atomic<bool> sCaptureDone{ false };
std::vector<s16> sCapture; // written by the audio thread while armed, read by the game thread once done
Vec3f sRenderPos = { 0.0f, 0.0f, 0.0f };

void ParseJobs() {
    sH.parsed = true;
    const char* env = std::getenv("COMBO_DEBUG_SFX_RENDER");
    if (env == nullptr) {
        sH.phase = Phase::Done;
        return;
    }
    if (const char* dir = std::getenv("COMBO_DEBUG_SFX_RENDER_DIR")) {
        sH.dir = dir;
    }
    std::string s(env);
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(',', start);
        std::string tok = s.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        Job j{};
        size_t star = tok.find('*');
        const int reps = (star != std::string::npos) ? std::max(1, std::atoi(tok.c_str() + star + 1)) : 1;
        tok = tok.substr(0, star);
        size_t colon = tok.find(':');
        j.id = (u16)std::strtoul(tok.substr(0, colon).c_str(), nullptr, 16);
        j.hold = (colon != std::string::npos) ? std::atoi(tok.c_str() + colon + 1) : 0;
        for (int r = 0; r < reps; r++) {
            j.rep = r;
            sH.jobs.push_back(j);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    sH.guestBegin = (FnVoid)Combo_ResolveSym("2ship", "MM_ComboGuestAudioBegin");
    sH.guestEnd = (FnVoid)Combo_ResolveSym("2ship", "MM_ComboGuestAudioEnd");
    sH.guestTick = (FnTick)Combo_ResolveSym("2ship", "MM_ComboGuestAudioTick");
    sH.guestPlayAt = (FnSfxAt)Combo_ResolveSym("2ship", "MM_ComboGuestPlaySfxAt");
    sH.guestStopSlot = (FnSlot)Combo_ResolveSym("2ship", "MM_ComboGuestStopSlot");
    sH.guestRead = (FnRead)Combo_ResolveSym("2ship", "MM_ComboGuestAudioRead");
    Combo_DebugLog("[ComboShip] sfx render harness: %zu job(s), MM reference engine %s\n", sH.jobs.size(),
                   (sH.guestBegin && sH.guestRead) ? "available" : "MISSING");
}

void WriteWav(const std::string& path, const std::vector<s16>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        Combo_DebugLog("[ComboShip] sfx render: cannot write %s\n", path.c_str());
        return;
    }
    const uint32_t bytes = (uint32_t)(data.size() * sizeof(s16));
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    w32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16(2);
    w32(32000);
    w32(32000 * 4);
    w16(4);
    w16(16);
    std::fwrite("data", 1, 4, f);
    w32(bytes);
    std::fwrite(data.data(), 1, bytes, f);
    std::fclose(f);
    int peak = 0;
    for (s16 v : data) {
        peak = std::max(peak, std::abs((int)v));
    }
    Combo_DebugLog("[ComboShip] sfx render: wrote %s (%zu frames, peak %d)\n", path.c_str(), data.size() / 2, peak);
}

void Arm(Source src) {
    sCapture.clear();
    sCapture.reserve(kCaptureFrames * 2);
    sCaptureDone = false;
    sCaptureSource = (int)src;
}

void MuteOot(bool mute) {
    if (mute == sH.muted) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (mute) {
            sH.savedVolumes[i] = Audio_GetGameVolume(i);
            Audio_SetGameVolume(i, 0.0f);
        } else {
            Audio_SetGameVolume(i, sH.savedVolumes[i]);
        }
    }
    sH.muted = mute;
}

char sName[64];
const char* JobName(const Job& j, const char* engine) {
    std::snprintf(sName, sizeof(sName), "/mmsfx_%04X_%s%s%d.wav", j.id, engine, (j.rep > 0) ? "_" : "", j.rep);
    if (j.rep == 0) {
        std::snprintf(sName, sizeof(sName), "/mmsfx_%04X_%s.wav", j.id, engine);
    }
    return sName;
}

} // namespace

extern "C" {

// OOT game thread, from AudioMmSfx_Update (every Audio_Update).
void Combo_MmSfxDebugTick(void) {
    if (!sH.parsed) {
        ParseJobs();
    }
    if (sH.phase == Phase::Done) {
        return;
    }
    sH.frames++;
    sH.phaseFrames++;
    const float master = CVarGetInteger("gSettings.Volume.Master", 40) / 100.0f;
    const float sfx = CVarGetInteger("gSettings.Volume.SFX", 100) / 100.0f;
    if (sH.guestOpen && sH.guestTick) {
        sH.guestTick(&sRenderPos.x, master, sfx); // MM's game-side update for the reference engine
    }
    auto go = [](Phase p) {
        sH.phase = p;
        sH.phaseFrames = 0;
    };
    const Job* job = (sH.job < sH.jobs.size()) ? &sH.jobs[sH.job] : nullptr;

    switch (sH.phase) {
        case Phase::Wait:
            if ((sH.frames > 200) && Combo_MmSfx_Available()) { // let the scene settle
                MuteOot(true);
                go(job ? Phase::NativeSilence : Phase::Done);
            }
            break;
        case Phase::NativeSilence: // let OOT's own notes die away
            if (sH.phaseFrames >= 30) {
                Arm(Source::Native);
                Combo_MmSfx_PlayKeyed(job->id, &sRenderPos, &sRenderPos, false);
                Combo_DebugLog("[ComboShip] sfx render: MM 0x%04X through OOT's engine\n", job->id);
                go(Phase::NativeCapture);
            }
            break;
        case Phase::NativeCapture:
            if (sH.phaseFrames < job->hold) {
                Combo_MmSfx_PlayKeyed(job->id, &sRenderPos, &sRenderPos, false);
            }
            if (sCaptureDone) {
                Combo_MmSfx_StopByKey(&sRenderPos);
                WriteWav(sH.dir + JobName(*job, "oot"), sCapture);
                go(Phase::GuestBegin);
            }
            break;
        case Phase::GuestBegin:
            if (!sH.guestBegin || !sH.guestRead || !sH.guestPlayAt) {
                go(Phase::Next);
                break;
            }
            if (!sH.guestOpen) {
                sH.guestBegin();
                sH.guestOpen = true;
            }
            if (sH.phaseFrames >= 10) {
                Arm(Source::Guest);
                sH.guestPlayAt(job->id, 1, &sRenderPos.x);
                Combo_DebugLog("[ComboShip] sfx render: MM 0x%04X through MM's engine\n", job->id);
                go(Phase::GuestCapture);
            }
            break;
        case Phase::GuestCapture:
            if (sH.phaseFrames < job->hold) {
                sH.guestPlayAt(job->id, 1, &sRenderPos.x);
            }
            if (sCaptureDone) {
                sH.guestStopSlot(1);
                WriteWav(sH.dir + JobName(*job, "mm"), sCapture);
                go(Phase::Next);
            }
            break;
        case Phase::Next:
            sH.job++;
            go((sH.job < sH.jobs.size()) ? Phase::NativeSilence : Phase::Done);
            if (sH.phase == Phase::Done) {
                if (sH.guestOpen && sH.guestEnd) {
                    sH.guestEnd();
                    sH.guestOpen = false;
                }
                MuteOot(false);
                Combo_DebugLog("[ComboShip] sfx render: all jobs done\n");
            }
            break;
        case Phase::Done:
            break;
    }
}

// OOT audio thread, per OTRAudio_Thread iteration. Only with COMBO_DEBUG_SFX_RENDER: alternate 528/560 samples per
// update (the MM reference engine's pacing, 544 average) so both renders advance their scripts at the same rate per
// sample; a headless SDL dummy device otherwise always asks for 560.
unsigned int Combo_MmAudioDebugSampleCount(unsigned int numSamples) {
    static const bool sRender = std::getenv("COMBO_DEBUG_SFX_RENDER") != nullptr;
    static bool sAlt = false;
    if (!sRender) {
        return numSamples;
    }
    sAlt = !sAlt;
    return sAlt ? 528 : 560;
}

// OOT audio thread, after each rendered sub-buffer (OTRAudio_Thread). Debug only.
void Combo_MmAudioDebugAfterRender(s16* samples, u32 numSamples) {
    static const bool sRender = std::getenv("COMBO_DEBUG_SFX_RENDER") != nullptr;
    if (sDbg) {
        static int sPeak = 0;
        static u32 sFrames = 0;
        static int sMmNotesSeen = 0;
        for (u32 i = 0; i < numSamples * 2; i++) {
            sPeak = std::max(sPeak, std::abs((int)samples[i]));
        }
        for (int i = 0; i < gAudioContext.numNotes; i++) {
            Note* note = &gAudioContext.notes[i];
            SequenceLayer* layer = note->playbackState.parentLayer;
            if ((layer != NO_LAYER) && (layer->channel != nullptr) && (layer->channel->seqPlayer != nullptr) &&
                SEQ_PLAYER_IS_MM(layer->channel->seqPlayer)) {
                sMmNotesSeen++;
            }
        }
        sFrames += numSamples;
        if (sFrames >= 32000) {
            int live = 0;
            for (int i = 0; i < gAudioContext.numNotes; i++) {
                Note* note = &gAudioContext.notes[i];
                SequenceLayer* layer = note->playbackState.parentLayer;
                if ((layer == NO_LAYER) || (layer->channel == nullptr) || (layer->channel->seqPlayer == nullptr) ||
                    !SEQ_PLAYER_IS_MM(layer->channel->seqPlayer) || (note->noteSubEu.sound.soundFontSound == nullptr)) {
                    continue;
                }
                const SoundFontSample* smp = note->noteSubEu.sound.soundFontSound->sample;
                live++;
                Combo_DebugLog("[ComboShip] MM native note %d: MM player ch %d font %u sample %p size %u codec %u "
                               "vel %.3f freq %.3f gain 0x%02X reverb %u\n",
                               i, layer->channel->channelIndex, note->playbackState.fontId, (void*)smp->sampleAddr,
                               smp->size, smp->codec, layer->noteVelocity, layer->noteFreqScale,
                               layer->channel->unk_0C, layer->channel->reverb);
            }
            Combo_DebugLog("[ComboShip] OOT audio: peak output sample %d, MM-player note-updates %d, live MM notes %d\n",
                           sPeak, sMmNotesSeen, live);
            sPeak = 0;
            sFrames = 0;
            sMmNotesSeen = 0;
        }
    }
    if (!sRender) {
        return;
    }
    // COMBO_DEBUG_SFX_PIN_RANDOM: the scripts' random value is pinned (as in the MM reference engine) so random sfx
    // variants match; set after each buffer, which is when the engine rerolls it.
    static const bool sPinRandom = std::getenv("COMBO_DEBUG_SFX_PIN_RANDOM") != nullptr;
    if (sPinRandom) {
        gAudioContext.audioRandom = 0x2468ACE1;
    }
    const Source src = (Source)sCaptureSource.load();
    if ((src == Source::None) || sCaptureDone) {
        return;
    }
    if (src == Source::Native) {
        sCapture.insert(sCapture.end(), samples, samples + numSamples * 2);
    } else if (sH.guestRead) {
        static s16 sBuf[2048 * 2];
        const u32 n = std::min<u32>(numSamples, 2048);
        sH.guestRead(sBuf, n, 50);
        sCapture.insert(sCapture.end(), sBuf, sBuf + n * 2);
    }
    if (sCapture.size() >= kCaptureFrames * 2) {
        sCaptureSource = (int)Source::None;
        sCaptureDone = true;
    }
}

} // extern "C"
