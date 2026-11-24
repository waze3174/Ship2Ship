// mm/2s2h/Rando/MiscBehavior/ComboGuestAudio.cpp — ComboShip: MM's sound engine as a guest while OOT is the
// foreground game, so an MM transformation in Hyrule sounds like itself (Goron/Zora/Deku/Fierce Deity voices, punch,
// roll, fins, bubbles) instead of borrowing adult Link's voice.
//
// MM boots once at startup (MM_BootForCombo), so its audio heap, sound banks and SFX sequence stay resident while OOT
// runs; MM_PrepareForTransition only stopped MM's audio THREAD. A guest session runs MM's engine on a thread of its
// own, and OOT never touches MM's audio state directly:
//   - OOT's game thread only ENQUEUES commands (tick with Link's position + OOT's volume sliders, play/stop an MM sfx,
//     an actor's sound slot moved/freed) into a lock-free single-producer queue.
//   - The guest thread is the ONLY thread that runs MM audio code: it applies queued commands, runs MM's game-side
//     Audio_Update once per OOT tick, and renders MM's output (AudioMgr_CreateNextAudioBuffer) into a lock-free ring a
//     couple of buffers ahead of need.
//   - OOT's audio thread only PULLS from that ring (MM_ComboGuestAudioMix) and never waits: if MM is behind, it mixes
//     what is there. The sum goes through a soft limiter, never a hard clip.
//   - With nothing MM playing and nothing queued, the guest thread renders nothing (idle).
// This replaces B-M7's design (MM rendered on OOT's audio thread under a mutex the game thread also held), whose
// stalls and hard clipping crackled.
// Every guest sfx plays at a position owned here (Link, or a slot per OOT actor), never at a pointer into OOT memory.
// End (OOT->MM handoff) joins the thread and stops all guest sfx before MM's own audio thread restarts.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <libultraship/libultraship.h> // C++ headers first, as the other combo files do

#include "ComboExport.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "seqcmd.h"
void AudioMgr_CreateNextAudioBuffer(s16* samples, u32 num_samples);
}

namespace {

// ---- lock-free single-producer/single-consumer ring -------------------------------------------------------------
template <typename T, size_t N> struct SpscRing {
    static_assert((N & (N - 1)) == 0, "power of two");
    T buf[N];
    std::atomic<size_t> head{ 0 }; // written by the producer
    std::atomic<size_t> tail{ 0 }; // written by the consumer
    size_t Size() const {
        return head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire);
    }
    bool Push(const T& v) {
        const size_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) >= N) {
            return false;
        }
        buf[h & (N - 1)] = v;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool Pop(T& v) {
        const size_t t = tail.load(std::memory_order_relaxed);
        if (head.load(std::memory_order_acquire) == t) {
            return false;
        }
        v = buf[t & (N - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
    void Clear() {
        tail.store(head.load(std::memory_order_acquire), std::memory_order_release);
    }
};

enum class CmdType : u8 { Tick, Play, PlayAt, StopId, StopSlot };
struct Cmd {
    CmdType type;
    u16 sfxId;
    s16 slot;
    float pos[3];
    float master;
    float sfxVol;
    bool hasPos;
};

struct Frame {
    s16 l, r;
};

constexpr int kSlots = 16;                 // 0 = Link, 1..15 = OOT actors
constexpr u32 kRenderLow = 528;            // as both ports' audio threads (32 kHz)
constexpr u32 kRenderHigh = 560;
constexpr size_t kAheadFrames = 1200;      // keep ~37 ms rendered ahead of OOT's audio thread
constexpr size_t kIdleAfterFrames = 32000; // ~1 s with no MM notes and no requests -> stop rendering

SpscRing<Cmd, 1024> sCmds;  // OOT game thread -> guest thread
SpscRing<Frame, 8192> sOut; // guest thread -> OOT audio thread

std::atomic<bool> sActive{ false };
std::atomic<bool> sRunning{ false };
std::thread sThread;
Vec3f sPos[kSlots] = {}; // written and read by the guest thread only (MM keeps pointers to these)

const bool sDbg = std::getenv("COMBO_DEBUG_AUDIO") != nullptr;
std::atomic<int> sDbgUnderruns{ 0 };
std::atomic<int> sDbgLimited{ 0 };
std::atomic<int> sDbgDropped{ 0 };
std::atomic<int> sDbgNotes{ 0 };

int ActiveMmNotes() {
    int active = 0;
    for (int i = 0; i < gAudioCtx.numNotes; i++) {
        if (gAudioCtx.notes[i].playbackState.parentLayer != NO_LAYER) {
            active++;
        }
    }
    return active;
}

void StopMusicPlayers() {
    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0);
    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_SUB, 0);
    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_AMBIENCE, 0);
}

void StopAllGuestSfx() {
    for (int i = 0; i < kSlots; i++) {
        AudioSfx_StopByPos(&sPos[i]);
    }
}

// Guest thread: apply one queued command. Returns true for a tick (then MM's game-side update runs).
bool Apply(const Cmd& c) {
    static float sLastMaster = -1.0f;
    static float sLastSfx = -1.0f;
    switch (c.type) {
        case CmdType::Tick:
            if (c.hasPos) {
                sPos[0] = { c.pos[0], c.pos[1], c.pos[2] };
            }
            if (c.master != sLastMaster) {
                CVarSetFloat("gSettings.Audio.MasterVolume", c.master); // read live by MM at each note init
                sLastMaster = c.master;
            }
            if (c.sfxVol != sLastSfx) {
                CVarSetFloat("gSettings.Audio.SoundEffectsVolume", c.sfxVol);
                AudioSeq_SetPortVolumeScale(SEQ_PLAYER_SFX, c.sfxVol);
                sLastSfx = c.sfxVol;
            }
            return true;
        case CmdType::Play:
            AudioSfx_PlaySfx(c.sfxId, &sPos[0], 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultReverb);
            return false;
        case CmdType::PlayAt:
            if ((c.slot > 0) && (c.slot < kSlots)) {
                sPos[c.slot] = { c.pos[0], c.pos[1], c.pos[2] };
                AudioSfx_PlaySfx(c.sfxId, &sPos[c.slot], 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultReverb);
            }
            return false;
        case CmdType::StopId:
            AudioSfx_StopByPosAndId(&sPos[0], c.sfxId);
            return false;
        case CmdType::StopSlot:
            if ((c.slot > 0) && (c.slot < kSlots)) {
                AudioSfx_StopByPos(&sPos[c.slot]);
            }
            return false;
    }
    return false;
}

void GuestThread() {
    static s16 sRender[kRenderHigh * 2];
    size_t quietFrames = kIdleAfterFrames; // idle until something is requested
    bool alternate = false;

    while (sRunning.load(std::memory_order_acquire)) {
        bool requested = false;
        Cmd c;
        while (sCmds.Pop(c)) {
            if (Apply(c)) {
                Audio_Update(); // MM's game-side update, once per OOT tick (sfx requests, active sfx, command flush)
            } else {
                requested = true;
            }
        }
        if (requested) {
            quietFrames = 0;
        }

        if ((quietFrames >= kIdleAfterFrames) || (sOut.Size() >= kAheadFrames)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const u32 n = alternate ? kRenderHigh : kRenderLow; // averages ~544 frames, MM's own pacing
        alternate = !alternate;
        std::memset(sRender, 0, sizeof(sRender));
        // Test harness (COMBO_DEBUG_SFX_PIN_RANDOM): the same script random value as the native render gets.
        static const bool sPinRandom = std::getenv("COMBO_DEBUG_SFX_PIN_RANDOM") != nullptr;
        if (sPinRandom) {
            gAudioCtx.audioRandom = 0x2468ACE1;
        }
        AudioMgr_CreateNextAudioBuffer(sRender, n);
        if (sPinRandom) {
            gAudioCtx.audioRandom = 0x2468ACE1;
        }
        for (u32 i = 0; i < n; i++) {
            if (!sOut.Push({ sRender[i * 2], sRender[i * 2 + 1] })) {
                sDbgDropped.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        const int notes = ActiveMmNotes();
        sDbgNotes.store(notes, std::memory_order_relaxed);
        quietFrames = (notes > 0) ? 0 : (quietFrames + n);
    }
}

// Soft limiter: unity below the knee, a tanh curve to full scale above it (no hard edge to crackle on).
inline s16 SoftLimit(s32 x) {
    constexpr float kKnee = 24000.0f;
    constexpr float kRange = 32767.0f - kKnee;
    const float a = (float)((x < 0) ? -x : x);
    if (a <= kKnee) {
        return (s16)x;
    }
    sDbgLimited.fetch_add(1, std::memory_order_relaxed);
    const float y = kKnee + kRange * std::tanh((a - kKnee) / kRange);
    return (s16)((x < 0) ? -y : y);
}

void Enqueue(const Cmd& c) {
    if (!sCmds.Push(c)) {
        sDbgDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

extern "C" COMBO_EXPORT void MM_ComboGuestAudioBegin(void) {
    if (sActive) {
        return;
    }
    // MM's own audio thread is stopped; nothing else touches MM audio until the guest thread starts.
    StopMusicPlayers();
    StopAllGuestSfx();
    sCmds.Clear();
    sOut.Clear();
    sRunning = true;
    sThread = std::thread(GuestThread);
    sActive = true;
}

extern "C" COMBO_EXPORT void MM_ComboGuestAudioEnd(void) {
    if (!sActive) {
        return;
    }
    sActive = false;
    sRunning = false;
    if (sThread.joinable()) {
        sThread.join();
    }
    // Guest thread gone: this thread may touch MM audio now. Stop every guest sound and flush that into MM's command
    // queue before MM's own audio thread restarts.
    StopAllGuestSfx();
    Audio_Update();
    sCmds.Clear();
    sOut.Clear();
}

extern "C" COMBO_EXPORT int MM_ComboGuestAudioActive(void) {
    return sActive ? 1 : 0;
}

// OOT game thread, once per OOT Audio_Update: Link's projected position (may be NULL) and OOT's Master and Sound
// Effects sliders (0-1), so the guest sounds follow Link and sit at OOT's levels.
extern "C" COMBO_EXPORT void MM_ComboGuestAudioTick(const float* projectedPos, float masterVol, float sfxVol) {
    if (!sActive) {
        return;
    }
    Cmd c{};
    c.type = CmdType::Tick;
    c.hasPos = (projectedPos != NULL);
    if (c.hasPos) {
        std::memcpy(c.pos, projectedPos, sizeof(c.pos));
    }
    c.master = masterVol;
    c.sfxVol = sfxVol;
    Enqueue(c);

    if (sDbg) {
        static int sTicks = 0;
        if (++sTicks >= 60) {
            sTicks = 0;
            fprintf(stderr,
                    "[ComboShip] MM guest audio: ring %zu frames, underruns %d, limited %d, dropped %d, notes %d\n",
                    sOut.Size(), sDbgUnderruns.exchange(0), sDbgLimited.exchange(0), sDbgDropped.exchange(0),
                    sDbgNotes.load());
        }
    }
}

// OOT game thread: play an MM sfx id at Link.
extern "C" COMBO_EXPORT void MM_ComboGuestPlaySfx(u16 sfxId) {
    if (!sActive) {
        return;
    }
    Cmd c{};
    c.type = CmdType::Play;
    c.sfxId = sfxId;
    Enqueue(c);
}

// OOT game thread: play an MM sfx at an OOT actor's projected position, held in slot 1..15 (the caller keeps the slot
// for the actor's life and re-plays continuous sounds every frame, as MM's actor audio does).
extern "C" COMBO_EXPORT void MM_ComboGuestPlaySfxAt(u16 sfxId, int slot, const float* projectedPos) {
    if (!sActive || (slot <= 0) || (slot >= kSlots) || (projectedPos == NULL)) {
        return;
    }
    Cmd c{};
    c.type = CmdType::PlayAt;
    c.sfxId = sfxId;
    c.slot = (s16)slot;
    std::memcpy(c.pos, projectedPos, sizeof(c.pos));
    Enqueue(c);
}

// OOT game thread: the actor owning a slot is gone — stop whatever still plays there.
extern "C" COMBO_EXPORT void MM_ComboGuestStopSlot(int slot) {
    if (!sActive || (slot <= 0) || (slot >= kSlots)) {
        return;
    }
    Cmd c{};
    c.type = CmdType::StopSlot;
    c.slot = (s16)slot;
    Enqueue(c);
}

// OOT game thread: stop one MM sfx id playing at Link (held/looping sounds such as the Goron roll).
extern "C" COMBO_EXPORT void MM_ComboGuestStopSfx(u16 sfxId) {
    if (!sActive) {
        return;
    }
    Cmd c{};
    c.type = CmdType::StopId;
    c.sfxId = sfxId;
    Enqueue(c);
}

// OOT audio thread: add up to numSamples stereo frames of MM audio into `samples` (interleaved L/R). Never blocks.
extern "C" COMBO_EXPORT void MM_ComboGuestAudioMix(s16* samples, u32 numSamples) {
    if (!sActive || (samples == NULL)) {
        return;
    }
    const size_t available = sOut.Size();
    if (available == 0) {
        return; // MM idle (nothing playing)
    }
    if (available < numSamples) {
        // Never mix a partial buffer: that leaves a hole in MM's stream (a click). Wait one buffer instead; MM's
        // stream starts a few ms later but stays continuous.
        sDbgUnderruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Frame f;
    for (u32 i = 0; (i < numSamples) && sOut.Pop(f); i++) {
        samples[i * 2] = SoftLimit((s32)samples[i * 2] + f.l);
        samples[i * 2 + 1] = SoftLimit((s32)samples[i * 2 + 1] + f.r);
    }
}

// Test harness only (soh ComboGuestAudio.cpp, COMBO_DEBUG_SFX_RENDER): MM's guest engine is no longer mixed into OOT's
// output — MM's sounds play natively in OOT's engine — but it still renders MM-native reference audio to compare the
// native port against. Unlike MM_ComboGuestAudioMix this waits (up to timeoutMs) for a whole buffer, so a capture has
// no underrun holes, and it writes (not adds) MM's frames. Returns the frames written.
extern "C" COMBO_EXPORT u32 MM_ComboGuestAudioRead(s16* samples, u32 numSamples, u32 timeoutMs) {
    if (!sActive || (samples == NULL)) {
        return 0;
    }
    for (u32 waited = 0; (sOut.Size() < numSamples) && (waited < timeoutMs); waited++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Frame f;
    u32 i = 0;
    for (; (i < numSamples) && sOut.Pop(f); i++) {
        samples[i * 2] = f.l;
        samples[i * 2 + 1] = f.r;
    }
    for (u32 j = i; j < numSamples; j++) {
        samples[j * 2] = 0;
        samples[j * 2 + 1] = 0;
    }
    return i;
}
