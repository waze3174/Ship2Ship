// soh/soh/Enhancements/randomizer/ComboMmAudio.cpp — ComboShip: Majora's Mask audio data inside OOT's audio engine.
//
// MM transformation forms in Hyrule play MM's own sound effects natively: OOT's audio engine runs MM's sfx sequence
// (Sequence_0) on an extra sequence player (SEQ_PLAYER_COMBO_MM_SFX, audio_seqplayer_mm.c) with MM's soundfonts and
// samples, all read from MM's ResourceManager — MM is booted at startup and its RM stays resident. Nothing is copied
// out of MM's archive into OOT's; the only runtime-built data is an OOT-side copy of Sequence_0's header with its font
// list translated to OOT font ids (the script bytes are copied too: OOT copies a sequence into its audio heap anyway,
// and MM's scripts write into their own sequence data).
//
// "@mm:" paths: OOT's audio accessors (ResourceMgr_LoadSeqByName / LoadSeqPtrByName / LoadAudioSoundFontByName /
// LoadAudioSample, ResourceManagerHelpers.cpp) route a path starting "@mm:" here, mirroring the "@mm:" animation
// routing. Resources are pinned (shared_ptr) for the life of the process; raw pointers are cached because the font
// lookup runs per note on the audio thread.
//
// Registration (from AudioLoad_Init, after OOT built its maps): Sequence_0 is added to OOT's sequenceMap, and every
// font Sequence_0 lists is appended to OOT's fontMap / fontLoadStatus (fontMapSize grows).
//
// MM music: a few of MM's music sequences (kMmMusicSeqs: the Bremen Mask march, Kamaro's dance) are registered the
// same way, each appended as one more sequenceMap slot with its fonts translated (fonts Sequence_0 did not already
// register are appended too). They play on OOT's fanfare player, which runs MM script semantics while one of them is
// its sequence (audio_load.c sets the player's mmMode per started sequence from Combo_MmAudio_IsMmSeq), driven by the
// game-thread front end in code_800EC960.c (Audio_ComboMmPlayFanfare) behind the C API at the bottom of this file.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <libultraship/libultraship.h> // C++ headers first, as the other combo files do
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManagerScope.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"

extern char** sequenceMap;
extern size_t sequenceMapSize;
extern size_t fontMapSize;
extern char** fontMap;
}

// strdup is POSIX; MSVC deprecates it (an error under /WX). Same behaviour everywhere.
static char* ComboStrDup(const char* src) {
    const size_t n = std::strlen(src) + 1;
    char* dst = static_cast<char*>(std::malloc(n));
    if (dst != nullptr) {
        std::memcpy(dst, src, n);
    }
    return dst;
}

namespace {

constexpr const char* kPrefix = "@mm:";
constexpr size_t kPrefixLen = 4;
constexpr const char* kMmSfxSeqPath = "@mm:audio/sequences/Sequence_0";

// MM music sequences registered with OOT (MM sequence ids, mm/include/sequence.h). Add an id here to make it playable.
constexpr u16 kMmMusicSeqs[] = {
    0x53, // NA_BGM_BREMEN_MARCH (MM Player: Bremen Mask march, Audio_PlayFanfare)
    0x71, // NA_BGM_KAMARO_DANCE (MM Player: Kamaro's Mask dance, Audio_PlayFanfare)
};
constexpr size_t kNumMmMusicSeqs = sizeof(kMmMusicSeqs) / sizeof(kMmMusicSeqs[0]);
// The OOT sequence id + 1 each kMmMusicSeqs entry was registered as (0 = unavailable, also before registration: static
// atomics start zeroed). Written at registration, before the audio thread or the game can start one; read lock-free by
// both afterwards.
std::atomic<int> sMmMusicOotIdPlus1[kNumMmMusicSeqs];

int MusicOotId(size_t index) {
    return sMmMusicOotIdPlus1[index].load(std::memory_order_relaxed) - 1;
}

enum RoutedKind { kSeq, kFont, kSample };

struct RoutedEntry {
    std::shared_ptr<Ship::IResource> pin; // keeps MM's resource alive
    void* raw = nullptr;                  // what OOT's accessor returns
    std::unique_ptr<SequenceData> seqCopy;
    std::vector<char> seqBytes;
};

std::mutex sMutex;
std::unordered_map<std::string, RoutedEntry> sEntries;
std::unordered_map<u16, u16> sMmFontToOot; // MM font index -> OOT font id
std::atomic<bool> sTornDown{ false };
std::atomic<int> sMmSfxSeqId{ -1 };

// Lock-free fast path for the per-note font lookups: fontMap strings are strdup'd once and stay put, so the pointer
// itself identifies the entry. Filled at registration (before the audio thread can see the new font ids), read-only
// afterwards.
struct FastEntry {
    const char* key;
    void* raw;
};
FastEntry sFast[64];
std::atomic<int> sFastCount{ 0 };

SoundFont sEmptyFont = {}; // returned after teardown so a late lookup finds no instruments/drums/sfx

void ClearPins() {
    sTornDown = true;
    std::lock_guard<std::mutex> lock(sMutex);
    for (auto& [path, entry] : sEntries) {
        entry.pin.reset(); // drop MM's resources while lib2ship is still mapped
    }
}

void HookTeardown() {
    static bool sHooked = (Ship::CrossRMRegistry::RegisterTeardownListener(&ClearPins), true);
    (void)sHooked;
}

std::shared_ptr<Ship::IResource> LoadFromMm(const std::string& mmPath) {
    auto rm = Ship::CrossRMRegistry::Get("mm");
    if (rm == nullptr) {
        return nullptr;
    }
    try {
        Ship::ResourceManagerScope scope(rm); // nested loads (a font's samples) go through MM's RM
        return rm->LoadResource(mmPath);
    } catch (...) { return nullptr; }
}

// Builds the OOT-side SequenceData for an MM sequence: the same bytes, its font list in OOT font ids.
void BuildSeqCopy(RoutedEntry& entry, const SequenceData* mmSeq) {
    entry.seqBytes.assign(mmSeq->seqData, mmSeq->seqData + mmSeq->seqDataSize);
    entry.seqCopy = std::make_unique<SequenceData>(*mmSeq);
    entry.seqCopy->seqData = entry.seqBytes.data();
    for (int32_t i = 0; (i < mmSeq->numFonts) && (i < 16); i++) {
        auto it = sMmFontToOot.find(mmSeq->fonts[i]);
        entry.seqCopy->fonts[i] = (it != sMmFontToOot.end()) ? it->second : FONT_ID_NONE;
    }
    entry.raw = entry.seqCopy.get();
}

void* Route(const char* path, RoutedKind kind) {
    if (sTornDown) {
        return (kind == kFont) ? &sEmptyFont : nullptr;
    }
    const int fastCount = sFastCount.load(std::memory_order_acquire);
    for (int i = 0; i < fastCount; i++) {
        if (sFast[i].key == path) {
            return sFast[i].raw;
        }
    }

    std::lock_guard<std::mutex> lock(sMutex);
    const std::string key(path);
    auto it = sEntries.find(key);
    if (it != sEntries.end()) {
        return it->second.raw;
    }
    RoutedEntry entry;
    entry.pin = LoadFromMm(key.substr(kPrefixLen));
    if (entry.pin != nullptr) {
        void* raw = entry.pin->GetRawPointer();
        if (kind == kSeq) {
            BuildSeqCopy(entry, static_cast<const SequenceData*>(raw));
        } else {
            entry.raw = raw;
        }
    } else {
        SPDLOG_ERROR("[ComboShip] MM audio resource '{}' not found", key);
    }
    HookTeardown();
    void* raw = entry.raw;
    sEntries.emplace(key, std::move(entry));
    return (raw == nullptr && kind == kFont) ? &sEmptyFont : raw;
}

void AddFast(const char* key, void* raw) {
    const int n = sFastCount.load(std::memory_order_relaxed);
    if ((raw != nullptr) && (n < (int)(sizeof(sFast) / sizeof(sFast[0])))) {
        sFast[n] = { key, raw };
        sFastCount.store(n + 1, std::memory_order_release);
    }
}

const char* CodecName(u32 codec) {
    static const char* kNames[] = { "ADPCM", "S8", "S16_INMEMORY", "SMALL_ADPCM", "REVERB", "S16" };
    return (codec < 6) ? kNames[codec] : "MM-only";
}

// MM's codecs 6 (UNK6), 7 (UNK7) and 8 (OPUS, custom audio only) are not OOT's (OOT 6 = OPUS). Extracted MM data
// uses ADPCM; count anything else so a mismatch cannot pass silently.
int CountForeignCodecs(const SoundFont* sf) {
    int foreign = 0;
    auto check = [&](const SoundFontSound* s) {
        if ((s != nullptr) && (s->sample != nullptr) && (s->sample->codec > CODEC_S16)) {
            foreign++;
        }
    };
    for (u16 i = 0; i < sf->numSfx; i++) {
        check(&sf->soundEffects[i]);
    }
    for (u8 i = 0; i < sf->numDrums; i++) {
        if (sf->drums[i] != nullptr) {
            check(&sf->drums[i]->sound);
        }
    }
    for (u8 i = 0; i < sf->numInstruments; i++) {
        const Instrument* inst = sf->instruments[i];
        if (inst != nullptr) {
            check(&inst->lowNotesSound);
            check(&inst->normalNotesSound);
            check(&inst->highNotesSound);
        }
    }
    return foreign;
}

// Appends one slot to OOT's sequenceMap for an MM sequence (the map keeps its 0xF spare slots past sequenceMapSize,
// so the new id is the old slot count), with its routed copy carrying OOT font ids. Returns the new OOT seq id.
int AppendSeq(const std::string& routedPath, const std::shared_ptr<Ship::IResource>& pin) {
    const size_t seqId = sequenceMapSize + 0xF; // == new sequenceMapSize + 0xE
    sequenceMapSize += 1;
    sequenceMap = static_cast<char**>(realloc(sequenceMap, (sequenceMapSize + 0xF) * sizeof(char*)));
    gAudioContext.seqLoadStatus = static_cast<u8*>(realloc(gAudioContext.seqLoadStatus, sequenceMapSize + 0xF));
    sequenceMap[seqId] = ComboStrDup(routedPath.c_str());
    gAudioContext.seqLoadStatus[seqId] = 5; // as AudioLoad_Init leaves every sequence
    {
        // Build the routed copy now that the font translation is known.
        std::lock_guard<std::mutex> lock(sMutex);
        RoutedEntry entry;
        entry.pin = pin;
        BuildSeqCopy(entry, static_cast<const SequenceData*>(pin->GetRawPointer()));
        entry.seqCopy->seqNumber = (u16)seqId;
        sEntries[routedPath] = std::move(entry);
        HookTeardown();
    }
    AddFast(sequenceMap[seqId], Route(sequenceMap[seqId], kSeq));
    return (int)seqId;
}

} // namespace

extern "C" {

u16 Combo_MmAudio_OotFontFor(s32 mmFont);

// ResourceManagerHelpers.cpp: OOT's audio accessors hand "@mm:" paths here.
SequenceData* Combo_RoutedAudioSeq(const char* path) {
    return static_cast<SequenceData*>(Route(path, kSeq));
}
SoundFont* Combo_RoutedAudioFont(const char* path) {
    return static_cast<SoundFont*>(Route(path, kFont));
}
SoundFontSample* Combo_RoutedAudioSample(const char* path) {
    return static_cast<SoundFontSample*>(Route(path, kSample));
}
int Combo_IsRoutedAudioPath(const char* path) {
    return (path != nullptr) && (strncmp(path, kPrefix, kPrefixLen) == 0);
}

// The OOT font id an MM font index was registered as (FONT_ID_NONE if MM's audio is unavailable).
u16 Combo_MmAudio_OotFontFor(s32 mmFont) {
    std::lock_guard<std::mutex> lock(sMutex);
    auto it = sMmFontToOot.find((u16)mmFont);
    return (it != sMmFontToOot.end()) ? it->second : FONT_ID_NONE;
}

// The OOT sequence id of MM's sfx sequence, or -1 when MM's audio is unavailable (no MM RM).
int Combo_MmAudio_SfxSeqId(void) {
    return sMmSfxSeqId.load();
}

// From AudioLoad_Init, after OOT's sequenceMap / fontMap are built (audio thread not yet processing).
void Combo_MmAudio_RegisterWithOot(void) {
    sMmSfxSeqId = -1;
    sFastCount = 0;
    for (auto& id : sMmMusicOotIdPlus1) {
        id = 0;
    }
    auto rm = Ship::CrossRMRegistry::Get("mm");
    if (rm == nullptr) {
        fprintf(stderr, "[ComboShip] MM audio: MM's ResourceManager is not resident; MM sfx unavailable\n");
        return;
    }

    auto seqRes = LoadFromMm("audio/sequences/Sequence_0");
    if (seqRes == nullptr) {
        fprintf(stderr, "[ComboShip] MM audio: audio/sequences/Sequence_0 not found in MM's archive\n");
        return;
    }
    const SequenceData* mmSeq = static_cast<const SequenceData*>(seqRes->GetRawPointer());

    // MM font index -> MM font path (each font resource knows its own index).
    std::unordered_map<s32, std::string> mmFontPaths;
    {
        auto list = rm->GetArchiveManager()->ListFiles("audio/fonts*");
        for (const auto& p : *list) {
            auto res = LoadFromMm(p);
            if (res != nullptr) {
                mmFontPaths[static_cast<const SoundFont*>(res->GetRawPointer())->fntIndex] = p;
            }
        }
    }

    // MM music: MM sequence id -> its resource (each sequence resource knows its own number, as MM's AudioLoad_Init
    // maps them; MM's own audio init already has them cached in MM's RM).
    struct MusicSeq {
        size_t index; // into kMmMusicSeqs
        std::string path;
        std::shared_ptr<Ship::IResource> pin;
    };
    std::vector<MusicSeq> music;
    {
        auto list = rm->GetArchiveManager()->ListFiles("audio/sequences*");
        for (const auto& p : *list) {
            auto res = LoadFromMm(p);
            if (res == nullptr) {
                continue;
            }
            const u16 seqNumber = static_cast<const SequenceData*>(res->GetRawPointer())->seqNumber;
            for (size_t i = 0; i < kNumMmMusicSeqs; i++) {
                if (kMmMusicSeqs[i] == seqNumber) {
                    music.push_back({ i, p, res });
                }
            }
        }
    }

    // Append Sequence_0's fonts, then the music sequences' (each MM font once), to OOT's fontMap.
    std::vector<std::pair<std::string, u16>> added;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sMmFontToOot.clear();
    }
    auto addFonts = [&](const SequenceData* seq, const char* what) {
        bool complete = true;
        for (int32_t i = 0; (i < seq->numFonts) && (i < 16); i++) {
            const u16 mmFont = seq->fonts[i];
            if (sMmFontToOot.count(mmFont)) {
                continue;
            }
            auto pathIt = mmFontPaths.find(mmFont);
            if (pathIt == mmFontPaths.end()) {
                fprintf(stderr, "[ComboShip] MM audio: %s font %u has no MM soundfont\n", what, mmFont);
                complete = false;
                continue;
            }
            const u16 ootFont = (u16)(fontMapSize + added.size());
            sMmFontToOot[mmFont] = ootFont;
            added.emplace_back(std::string(kPrefix) + pathIt->second, ootFont);
        }
        return complete;
    };
    addFonts(mmSeq, "Sequence_0");
    if (added.empty()) {
        fprintf(stderr, "[ComboShip] MM audio: no soundfonts registered; MM sfx unavailable\n");
        return;
    }
    // A music sequence is registered only when every one of its fonts is: OOT loads each listed font id when the
    // sequence starts, and a FONT_ID_NONE there would index past fontMap.
    std::vector<bool> musicOk(music.size(), false);
    for (size_t m = 0; m < music.size(); m++) {
        char what[32];
        snprintf(what, sizeof(what), "MM sequence 0x%02X", kMmMusicSeqs[music[m].index]);
        const SequenceData* seq = static_cast<const SequenceData*>(music[m].pin->GetRawPointer());
        musicOk[m] = (seq->numFonts > 0) && addFonts(seq, what);
    }

    const size_t newFontMapSize = fontMapSize + added.size();
    fontMap = static_cast<char**>(realloc(fontMap, newFontMapSize * sizeof(char*)));
    gAudioContext.fontLoadStatus = static_cast<u8*>(realloc(gAudioContext.fontLoadStatus, newFontMapSize));
    for (const auto& [path, ootFont] : added) {
        fontMap[ootFont] = ComboStrDup(path.c_str());
        gAudioContext.fontLoadStatus[ootFont] = 0;
        SoundFont* sf = Combo_RoutedAudioFont(fontMap[ootFont]);
        AddFast(fontMap[ootFont], sf);
        const int foreign = CountForeignCodecs(sf);
        fprintf(stderr,
                "[ComboShip] MM audio: %s -> OOT font %u (sfx %u, drums %u, instruments %u, foreign codecs %d)\n",
                path.c_str(), ootFont, sf->numSfx, sf->numDrums, sf->numInstruments, foreign);
    }
    fontMapSize = newFontMapSize;

    // Append Sequence_0 to OOT's sequenceMap.
    const int seqId = AppendSeq(kMmSfxSeqPath, seqRes);
    sMmSfxSeqId = seqId;

    // The audio heap was initialized before this registration: give the MM player's freshly allocated channels MM
    // font 0's OOT id now (later heap resets do it at allocation, audio_seqplayer.c).
    {
        SequencePlayer* mmPlayer = &gAudioContext.seqPlayers[SEQ_PLAYER_COMBO_MM_SFX];
        const u16 font0 = Combo_MmAudio_OotFontFor(0);
        for (int i = 0; i < 16; i++) {
            if (IS_SEQUENCE_CHANNEL_VALID(mmPlayer->channels[i])) {
                mmPlayer->channels[i]->fontId = font0;
            }
        }
    }

    const SequenceData* copy = Combo_RoutedAudioSeq(sequenceMap[seqId]);
    fprintf(stderr, "[ComboShip] MM audio: Sequence_0 (%d bytes, %d fonts: %u %u) registered as OOT sequence %d\n",
            copy->seqDataSize, copy->numFonts, copy->fonts[0], copy->fonts[1], seqId);

    // Append the MM music sequences (after Sequence_0, whose id the MM sfx front end already relies on).
    for (size_t m = 0; m < music.size(); m++) {
        const size_t idx = music[m].index;
        if (!musicOk[m]) {
            fprintf(stderr, "[ComboShip] MM audio: MM sequence 0x%02X not registered (missing soundfont)\n",
                    kMmMusicSeqs[idx]);
            continue;
        }
        if (MusicOotId(idx) >= 0) {
            continue; // two archive entries with the same number: the first wins
        }
        const int ootId = AppendSeq(std::string(kPrefix) + music[m].path, music[m].pin);
        const SequenceData* mcopy = Combo_RoutedAudioSeq(sequenceMap[ootId]);
        if (mcopy == nullptr) {
            continue;
        }
        sMmMusicOotIdPlus1[idx] = ootId + 1;
        fprintf(stderr,
                "[ComboShip] MM audio: MM music 0x%02X (%s, %d bytes, cache policy %u, %d fonts, default OOT font %u) "
                "registered as OOT sequence %d\n",
                kMmMusicSeqs[idx], music[m].path.c_str(), mcopy->seqDataSize, mcopy->cachePolicy, mcopy->numFonts,
                mcopy->fonts[mcopy->numFonts - 1], ootId);
    }
    for (size_t i = 0; i < kNumMmMusicSeqs; i++) {
        if (MusicOotId(i) < 0) {
            fprintf(stderr, "[ComboShip] MM audio: MM music 0x%02X unavailable\n", kMmMusicSeqs[i]);
        }
    }
    (void)&CodecName;
}

// The OOT sequence id an MM music sequence (MM seq id) was registered as, or -1 when it is not available.
int Combo_MmAudio_MusicSeqId(int mmSeqId) {
    for (size_t i = 0; i < kNumMmMusicSeqs; i++) {
        if (kMmMusicSeqs[i] == mmSeqId) {
            return MusicOotId(i);
        }
    }
    return -1;
}

// Audio thread (audio_load.c, every sequence start): whether an OOT sequence id is one of MM's, to be run with MM's
// script semantics. Lock-free.
int Combo_MmAudio_IsMmSeq(int ootSeqId) {
    if (ootSeqId < 0) {
        return 0;
    }
    if (ootSeqId == sMmSfxSeqId.load(std::memory_order_relaxed)) {
        return 1;
    }
    for (size_t i = 0; i < kNumMmMusicSeqs; i++) {
        if (MusicOotId(i) == ootSeqId) {
            return 1;
        }
    }
    return 0;
}

} // extern "C"
