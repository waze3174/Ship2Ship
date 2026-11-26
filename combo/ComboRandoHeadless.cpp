// combo/ComboRandoHeadless.cpp
// ComboShip: standalone HEADLESS cross-world seed generator/validator. Loads soh.dll + 2ship.dll and
// drives the rando-only headless init (SOH_InitRandoHeadless / MM_InitRandoHeadless) + the cross-world
// fill directly — no game window, ResourceManager, audio, or ComboShip.exe boot.
//
// Modes (run from the game dir, next to soh.dll/2ship.dll):
//   comborando.exe [--seed <s>] [--count <n>]      generate+validate seed(s); also writes a consolidated
//                                                  spoiler to saves/combo/comborando.spoiler.json (count 1)
//   comborando.exe --playthrough <spoiler.json>    forward-traverse a finished seed to judge beatability
//
// --starting-game <oot|mm|random> overrides the menu's Starting Game CVar for generate mode (#135).
// --games <ootmm|oot|mm> overrides the menu's Games CVar (combo/rando/ComboGames.h) for generate mode.
// --lost-woods-link / --no-lost-woods-link override the menu's Lost Woods link CVar (ComboLostWoodsLink.h) for generate
// mode. A --playthrough run reads the link from the spoiler.
//
// Exact-seed repro (generate mode): --settings <consolidated.json> restores that seed's OOT/MM settings
// and tricks, and --master-seed <n> uses n directly instead of hashing --seed. Together they reproduce a
// reported seed's generation bit-for-bit, which plain --seed cannot.
//
// --playthrough runs two passes, ignoring the seed's No-Logic/Glitchless flag (it always evaluates real
// gates; permissiveness comes only from tricks): Pass 1 uses the seed's own tricks ("can this player beat
// it?"); if that sticks, Pass 2 enables every trick to tell "needs more tricks than configured" apart from
// "factually impossible" (e.g. a required item behind an unbreakable possession lock).
//
// Exit codes: 0 = beatable as configured; 3 = beatable only with all tricks; 1 = not beatable / seed
// failed; 2 = setup error.
#ifdef _WIN32
#define NOMINMAX // windows.h min/max macros clash with std::min/std::max in CrossWorldRando.h
#include <io.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "rando/ComboApTables.h"
#include "rando/ComboAttempt.h"
#include "rando/ComboLostWoodsLink.h"
#include "rando/ComboOracleServer.h"
#include "rando/CrossWorldRando.h"
#include "rando/CrossEntrance.h"
#include "rando/MasterQuestSeed.h"
#include "rando/ComboSeedFileType.h"
#include "rando/ComboPlaythrough.h"
#include "rando/CrossForeign.h" // BuildForeignArray (foreign enrichment parity with RunComboFill)
#include "rando/CrossHints.h"

// Set by combo/CMakeLists.txt; the identity `hello` reports.
#ifndef COMBO_SHIP2SHIP_VERSION
#define COMBO_SHIP2SHIP_VERSION "unknown"
#endif
#ifndef COMBO_RELEASE_VERSION
#define COMBO_RELEASE_VERSION "unknown"
#endif
#ifndef COMBO_SOH_PIN
#define COMBO_SOH_PIN "unknown"
#endif
#ifndef COMBO_MM_PIN
#define COMBO_MM_PIN "unknown"
#endif

namespace {

typedef void (*FnVoidV)(void);
typedef const char* (*FnDump)(void);
typedef void (*FnSetSeed)(uint64_t);
typedef const char* (*FnGetForced)(uint32_t);
typedef void (*FnTakeStr)(const char*);
typedef void (*FnOracleVoid)(void);
typedef void (*FnOracleSetItems)(const char*);
typedef const char* (*FnOracleGetChecks)(void);
typedef void (*FnOraclePlaceItem)(const char*, const char*);
typedef uint8_t (*FnOracleGetPortalOpen)(void); // OOT->MM portal gate (see CrossWorldRando.h)

// Must match ComboShip.cpp's ComboHash (FNV-1a 32-bit) so headless seeds match in-game seeds.
uint32_t ComboHash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
        h = (h ^ c) * 16777619u;
    return h;
}

// #135: resolve the starting-game config (0 = OOT, 1 = MM, 2 = Random) for one attempt. Must stay
// byte-identical to ComboShip.cpp's ResolveStartingGameMM, derivation string included.
bool ResolveStartingGameMM(int cfg, uint32_t masterSeed) {
    if (cfg == 2)
        return (ComboHash("startingGame:" + std::to_string(masterSeed)) & 1u) != 0;
    return cfg == 1;
}

// Same module-loading seam as ComboShip.cpp's LoadDll/GetSym (see there for the RTLD notes).
#ifdef _WIN32
typedef HMODULE DllHandle;
DllHandle LoadGameModule(const char* name) {
    return LoadLibraryA(name);
}
template <typename T> T Sym(DllHandle m, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(m, name));
}
#else
typedef void* DllHandle;
DllHandle LoadGameModule(const char* name) {
    // Bare names search the system library paths; the tool runs from the game dir, so anchor there.
    DllHandle module = dlopen((std::string("./") + name).c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!module) {
        const char* error = dlerror();
        std::cerr << "[comborando] " << name << ": " << (error ? error : "unknown dlopen error") << "\n";
    }
    return module;
}
template <typename T> T Sym(DllHandle m, const char* name) {
    return reinterpret_cast<T>(dlsym(m, name));
}
#endif

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// FNV-1a 64-bit over the parts of a generated seed, joined by the unit separator. CI pins it per seed, so a change to
// the fill that moves any placement, entrance, Master Quest roll or price shows up as a different value.
uint64_t Fnv1a64(const std::vector<std::string>& parts) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    constexpr unsigned char kPartSeparator = 0x1f;
    uint64_t h = kOffsetBasis;
    for (const std::string& part : parts) {
        for (unsigned char c : part)
            h = (h ^ c) * kPrime;
        h = (h ^ kPartSeparator) * kPrime;
    }
    return h;
}

std::string HexU64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

nlohmann::json PricesOf(const std::string& dump) {
    return nlohmann::json::parse(dump).value("prices", nlohmann::json::object());
}

// The oracle protocol owns stdout. Anything else the process or the game modules print (logs, the fill's progress
// lines) would corrupt it, so fd 1 is pointed at stderr and the real stdout is kept for protocol lines only.
FILE* ClaimProtocolStream() {
    std::cout.flush();
    std::fflush(stdout);
#ifdef _WIN32
    const int protocolFd = _dup(_fileno(stdout));
    _dup2(_fileno(stderr), _fileno(stdout));
    return _fdopen(protocolFd, "wb");
#else
    const int protocolFd = dup(STDOUT_FILENO);
    dup2(STDERR_FILENO, STDOUT_FILENO);
    return fdopen(protocolFd, "w");
#endif
}

// "sharedItems" in an open request: the key list a consolidated seed stores, or "all" / "none".
uint32_t SharedMaskFromRequest(const nlohmann::json& v) {
    if (v.is_string())
        return v.get<std::string>() == "all" ? ComboRando::SharedMaskAll() : 0u;
    return ComboRando::SharedMaskFromKeys(v);
}

// "OOT" / "MM" / "random" (any case) as the starting-game config comborando uses: 0, 1 or 2.
int StartingGameConfigFromName(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    return name == "mm" ? 1 : name == "random" ? 2 : 0;
}

// Render the spoiler's "seed" (string or number) as a label.
std::string SeedLabel(const nlohmann::json& spoiler) {
    if (spoiler.contains("seed")) {
        const auto& s = spoiler["seed"];
        if (s.is_string())
            return s.get<std::string>();
        if (s.is_number())
            return std::to_string(s.get<int64_t>());
    }
    return "?";
}

} // namespace

static int Run(int argc, char** argv) {
    std::string seed = "1";
    int count = 1;
    std::string playthroughFile;
    std::string settingsFile; // consolidated spoiler to restore settings/tricks from (exact-seed repro)
    bool haveMasterSeed = false;
    uint32_t masterSeedArg = 0;
    // #136: combined goal. Absent => read the menu CVars (matches in-game); 0 => force bosses.
    int triforceRequiredArg = -1;
    // #136: combined pool total. Absent (-1) => the menu CVar, or `required` when that is overridden.
    int triforceTotalArg = -1;
    // #135: starting game. Absent (-1) => read the menu CVar, same as in-game.
    int startingGameArg = -1;
    // Shared Items: "all", "none", or a comma-separated key list (see SharedItems.h). Empty => menu CVars.
    std::string sharedItemsArg;
    // Games (ComboGames.h). Absent (-1) => the menu CVar, same as in-game.
    int gamesArg = -1;
    bool oracleMode = false;         // --oracle: the Archipelago oracle protocol on stdin/stdout
    std::string apTablesFile;        // --ap-tables <file>: every item and check, under the AP names
    std::string playthroughJsonFile; // --playthrough-json <file>: pass 1's spheres and counts, for scripts
    std::string dumpEntrancesFile;   // full-coop-sync §B1: write MM's entrance graph and exit
    std::string mmEntrancesArg;      // §B1: "off" | "all" | csv of overworld,interior,dungeon,decoupled,mixed
    int unifiedArg = -1;             // --unified / --no-unified (else the menu CVar)
    int lostWoodsLinkArg = -1;       // --lost-woods-link / --no-lost-woods-link (else the menu CVar)
    std::string crossArg;            // §B2: "off" | csv of overworld,interior,dungeon[,pct=N] (else the menu CVars)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--triforce-required" && i + 1 < argc)
            triforceRequiredArg = std::atoi(argv[++i]);
        else if (a == "--triforce-total" && i + 1 < argc)
            triforceTotalArg = std::atoi(argv[++i]);
        else if (a == "--starting-game" && i + 1 < argc) {
            std::string v = argv[++i];
            startingGameArg = v == "oot" ? 0 : v == "mm" ? 1 : v == "random" ? 2 : -2;
            if (startingGameArg == -2) {
                std::cerr << "[comborando] --starting-game must be oot, mm or random (got '" << v << "')\n";
                return 2;
            }
        } else if (a == "--shared-items" && i + 1 < argc) {
            sharedItemsArg = argv[++i];
        } else if (a == "--games" && i + 1 < argc) {
            std::string v = argv[++i];
            gamesArg = v == "ootmm" ? 0 : v == "oot" ? 1 : v == "mm" ? 2 : -2;
            if (gamesArg == -2) {
                std::cerr << "[comborando] --games must be ootmm, oot or mm (got '" << v << "')\n";
                return 2;
            }
        } else if (a == "--cross" && i + 1 < argc) {
            crossArg = argv[++i];
        } else if (a == "--unified") {
            unifiedArg = 1;
        } else if (a == "--no-unified") {
            unifiedArg = 0;
        } else if (a == "--lost-woods-link") {
            lostWoodsLinkArg = 1;
        } else if (a == "--no-lost-woods-link") {
            lostWoodsLinkArg = 0;
        } else if (a == "--mm-entrances" && i + 1 < argc) {
            mmEntrancesArg = argv[++i];
        } else if (a == "--dump-mm-entrances" && i + 1 < argc) {
            dumpEntrancesFile = argv[++i];
        } else if (a == "--playthrough-json" && i + 1 < argc) {
            playthroughJsonFile = argv[++i];
        } else if (a == "--oracle") {
            oracleMode = true;
        } else if (a == "--ap-tables" && i + 1 < argc) {
            apTablesFile = argv[++i];
        } else if (a == "--seed" && i + 1 < argc)
            seed = argv[++i];
        else if (a == "--count" && i + 1 < argc)
            count = std::max(1, std::atoi(argv[++i]));
        else if (a == "--playthrough" && i + 1 < argc)
            playthroughFile = argv[++i];
        else if (a == "--settings" && i + 1 < argc)
            settingsFile = argv[++i];
        else if (a == "--master-seed" && i + 1 < argc) {
            masterSeedArg = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            haveMasterSeed = true;
        }
    }

#ifdef _WIN32
    DllHandle soh = LoadGameModule("soh.dll");
    DllHandle mm = LoadGameModule("2ship.dll");
#elif defined(__APPLE__)
    DllHandle soh = LoadGameModule("libsoh.dylib");
    DllHandle mm = LoadGameModule("lib2ship.dylib");
#else
    DllHandle soh = LoadGameModule("libsoh.so");
    DllHandle mm = LoadGameModule("lib2ship.so");
#endif
    if (!soh || !mm) {
        std::cerr << "[comborando] failed to load the soh / 2ship game modules — run from the game directory\n";
        return 2;
    }

    auto SOH_InitRandoHeadless = Sym<FnVoidV>(soh, "SOH_InitRandoHeadless");
    auto MM_InitRandoHeadless = Sym<FnVoidV>(mm, "MM_InitRandoHeadless");
    auto SOH_Dump = Sym<FnDump>(soh, "SOH_DumpRandoStaticData");
    auto MM_Dump = Sym<FnDump>(mm, "MM_DumpRandoStaticData");
    auto SOH_DumpSettings = Sym<FnDump>(soh, "SOH_DumpRandoSettings");
    auto MM_DumpSettings = Sym<FnDump>(mm, "MM_DumpRandoSettings");
    auto SOH_RestoreSettings = Sym<FnTakeStr>(soh, "SOH_RestoreRandoSettings");
    auto SOH_PrepContext = Sym<FnVoidV>(soh, "SOH_PrepRandoContext");
    auto MM_RestoreSettings = Sym<FnTakeStr>(mm, "MM_RestoreRandoSettings");
    auto SOH_SetSeed = Sym<FnSetSeed>(soh, "SOH_SetComboRandoSeed");
    auto MM_SetSeed = Sym<FnSetSeed>(mm, "MM_SetComboRandoSeed");
    auto SOH_GetForced = Sym<FnGetForced>(soh, "SOH_GetForcedPlacements");
    // OOT entrance shuffle (#90) — without this the validator checks a vanilla entrance graph.
    auto SOH_ShuffleEntrances = Sym<int (*)(uint64_t)>(soh, "SOH_ShuffleEntrancesForCombo");
    auto SOH_DumpEntranceOverrides = Sym<FnDump>(soh, "SOH_DumpEntranceOverrides");
    auto SOH_ApplyEntranceOverrides = Sym<int (*)(const char*)>(soh, "SOH_ApplyEntranceOverridesForCombo");
    auto MM_Restore = Sym<FnOracleVoid>(mm, "Combo_MM_Rando_Restore");
    // full-coop-sync §B1: MM entrance randomizer.
    auto MM_DumpEntranceGraphFn = Sym<FnDump>(mm, "MM_DumpEntranceGraph");
    auto MM_SetEntranceOverridesFn = Sym<void (*)(const char*)>(mm, "MM_SetEntranceOverrides");
    auto SOH_ReadComboEntranceCVarsFn = Sym<FnDump>(soh, "SOH_ReadComboEntranceCVars");
    // §B2 cross-game entrances.
    auto SOH_DumpEntranceGraphFn = Sym<FnDump>(soh, "SOH_DumpEntranceGraph");
    auto SOH_SetCrossEntrancesFn = Sym<void (*)(const char*)>(soh, "SOH_SetCrossEntrances");
    auto SOH_GetCrossExitsFn = Sym<FnDump>(soh, "Combo_SOH_Rando_GetCrossExits");
    auto MM_SetCrossEntrancesFn = Sym<void (*)(const char*)>(mm, "MM_SetCrossEntrances");
    auto MM_GetCrossExitsFn = Sym<FnDump>(mm, "Combo_MM_Rando_GetCrossExits");
    auto undoCross = [&]() {
        if (SOH_SetCrossEntrancesFn)
            SOH_SetCrossEntrancesFn("{}");
        if (MM_SetCrossEntrancesFn)
            MM_SetCrossEntrancesFn("{}");
        ComboRando::ActiveCrossEntrances() = {};
    };
    auto SOH_DumpEnabledTricks = Sym<FnDump>(soh, "SOH_DumpEnabledTricks");
    auto SOH_DumpRandoHintData = Sym<FnDump>(soh, "SOH_DumpRandoHintData"); // cross-hint verification
    auto SOH_SetEnabledTricks = Sym<FnTakeStr>(soh, "SOH_SetEnabledTricks");
    auto SOH_SetAllTricks = Sym<FnVoidV>(soh, "SOH_SetAllTricks");
    auto SOH_SetCheckPrices = Sym<FnTakeStr>(soh, "SOH_SetCheckPrices");
    auto MM_SetCheckPrices = Sym<FnTakeStr>(mm, "MM_SetCheckPrices");
    // #136: combined goal — must be pushed before every dump (it shapes both games' pools).
    auto SOH_SetComboGoal = Sym<void (*)(int, int, int)>(soh, "SOH_SetComboGoal");
    auto MM_SetComboGoal = Sym<void (*)(int, int, int)>(mm, "MM_SetComboGoal");
    auto SOH_ReadComboGoalCVars = Sym<int (*)(int*, int*)>(soh, "SOH_ReadComboGoalCVars");
    // #135: starting game — also pushed before every dump (it forces OOT's age/forest/exclusions).
    auto SOH_SetComboStartingGame = Sym<void (*)(int)>(soh, "SOH_SetComboStartingGame");
    auto SOH_ReadComboStartingGameCVar = Sym<int (*)(void)>(soh, "SOH_ReadComboStartingGameCVar");
    auto SOH_ReadComboGamesCVar = Sym<int (*)(void)>(soh, "SOH_ReadComboGamesCVar");
    // Shared Items: also pushed before every dump (shapes OOT's wallet force + MM's trimmed pool).
    auto SOH_SetComboSharedItems = Sym<void (*)(uint32_t)>(soh, "SOH_SetComboSharedItems");
    // Owner ruling 2026-11-11: Unified Inventory (both Links use every item).
    auto SOH_SetComboUnifiedInventory = Sym<void (*)(int)>(soh, "SOH_SetComboUnifiedInventory");
    auto SOH_SetComboMirrorStart = Sym<void (*)(uint32_t)>(soh, "SOH_SetComboMirrorStart");
    // Seed-bound logic credit for MM's Zora / Goron forms (gCombo.Rando.FormLogicCredit); off unless the seed says so.
    auto SOH_SetComboFormLogicCredit = Sym<void (*)(int)>(soh, "SOH_SetComboFormLogicCredit");
    auto pushFormLogicCredit = [&](bool on) {
        if (SOH_SetComboFormLogicCredit)
            SOH_SetComboFormLogicCredit(on ? 1 : 0);
    };
    // Seed-bound Lost Woods link: the OOT->MM portal region and the MM start's shaping.
    auto SOH_SetComboLostWoodsLink = Sym<void (*)(int)>(soh, "SOH_SetComboLostWoodsLink");
    auto pushLostWoodsLink = [&](bool on) {
        if (SOH_SetComboLostWoodsLink)
            SOH_SetComboLostWoodsLink(on ? 1 : 0);
    };
    // Seed-bound Master Quest dungeons: pushed right after each SOH_SetSeed (which unpins), before the dump's prep.
    auto SOH_SetComboMasterQuestDungeons = Sym<FnTakeStr>(soh, "SOH_SetComboMasterQuestDungeons");
    auto SOH_DumpComboMasterQuestDungeons = Sym<FnDump>(soh, "SOH_DumpComboMasterQuestDungeons");
    auto pushMasterQuestDungeons = [&](const nlohmann::json& seed) {
        const std::string stored = ComboRando::StoredMasterQuestText(seed);
        if (SOH_SetComboMasterQuestDungeons && !stored.empty())
            SOH_SetComboMasterQuestDungeons(stored.c_str());
    };
    // What CI pins for a generated seed: the fill's placements plus everything decided before it.
    auto fillFingerprint = [&](const ComboRando::CombinedFillResult& fill, const ComboRando::MmEntranceAttempt& mmEnt,
                               const ComboRando::CrossAttempt& cross, const std::string& sohDump,
                               const std::string& mmDump) {
        return Fnv1a64({ fill.spoilerJson, SOH_DumpEntranceOverrides ? SOH_DumpEntranceOverrides() : "",
                         mmEnt.overrides.dump(), cross.runtime.dump(),
                         SOH_DumpComboMasterQuestDungeons ? SOH_DumpComboMasterQuestDungeons() : "",
                         PricesOf(sohDump).dump(), PricesOf(mmDump).dump() });
    };
    // Owner ruling 2026-11-12: under Unified Inventory OOT starts with what MM starts with (SharedItems.h).
    auto pushMirrorStart = [&](bool unified, const nlohmann::json& mmSettings) {
        if (SOH_SetComboMirrorStart)
            SOH_SetComboMirrorStart(unified ? ComboRando::MirrorStartMaskFromMmSettings(mmSettings) : 0u);
    };
    auto SOH_ReadComboSharedCVars = Sym<uint32_t (*)(void)>(soh, "SOH_ReadComboSharedCVars");

    // The setup in front of every fill attempt, and in front of every oracle session: the seed's settings into both
    // games, both pool dumps, the forced placements and the entrance layouts. The order matches RunComboFill.
    auto beginAttempt = [&](const ComboRando::AttemptConfig& cfg, uint32_t masterSeed, bool mmStart,
                            ComboRando::AttemptSetup& setup) {
        setup = ComboRando::AttemptSetup{};
        if (SOH_SetSeed)
            SOH_SetSeed(masterSeed);
        if (!cfg.storedMasterQuest.is_null())
            pushMasterQuestDungeons(cfg.storedMasterQuest);
        if (MM_SetSeed)
            MM_SetSeed(masterSeed);
        // Before the dumps: the goal shapes OOT's wincon placement and MM's hunt pool.
        if (SOH_SetComboGoal)
            SOH_SetComboGoal(cfg.goal.hunt ? 1 : 0, cfg.goal.required,
                             ComboRando::CwOotPieces(cfg.goal.total, cfg.games));
        if (MM_SetComboGoal)
            MM_SetComboGoal(cfg.goal.hunt ? 1 : 0, cfg.goal.required,
                            ComboRando::CwMmPieces(cfg.goal.total, cfg.games));
        // Same reason (#135): an MM start forces OOT's age/forest/exclusions.
        if (SOH_SetComboStartingGame)
            SOH_SetComboStartingGame(mmStart ? 1 : 0);
        // Shared Items: shapes OOT's wallet-force before the dump (settings.cpp reads gComboSharedMask).
        if (SOH_SetComboSharedItems)
            SOH_SetComboSharedItems(cfg.sharedMask);
        if (SOH_SetComboUnifiedInventory)
            SOH_SetComboUnifiedInventory(cfg.unified ? 1 : 0);
        pushFormLogicCredit(cfg.formLogicCredit);
        pushLostWoodsLink(cfg.lostWoodsLink);
        {
            nlohmann::json mmLive = nlohmann::json::object();
            try {
                mmLive = nlohmann::json::parse(MM_DumpSettings ? MM_DumpSettings() : "{}");
            } catch (...) {}
            pushMirrorStart(cfg.unified, mmLive);
        }
        setup.sohDump = SOH_Dump();
        setup.mmDump = MM_Dump();
        if (cfg.goal.hunt) {
            const int pieces = ComboRando::CountPoolTriforcePieces(setup.sohDump, setup.mmDump, cfg.games);
            if (pieces < cfg.goal.required) {
                std::cerr << "[comborando] Triforce Hunt needs " << cfg.goal.required << " pieces but only " << pieces
                          << " are in the combined pool — raise --triforce-total / the Combo pool total\n";
                return ComboRando::AttemptStatus::Fatal;
            }
        }
        setup.forced = SOH_GetForced ? SOH_GetForced(masterSeed) : "";
        // Same placement as RunComboFill: after the dump/forced read, before the fill, so logic
        // validates the shuffled world. No-op when the entrance options are off.
        undoCross(); // §B2: the previous attempt's crossed doors go back before OOT reshuffles
        if (SOH_ShuffleEntrances && !SOH_ShuffleEntrances(masterSeed)) {
            std::cerr << "[comborando] seed " << masterSeed << ": entrance shuffle found no valid layout\n";
            return ComboRando::AttemptStatus::OotShuffleFailed;
        }
        setup.entResult = ComboRando::ShuffleMmEntrancesForAttempt(MM_DumpEntranceGraphFn, MM_SetEntranceOverridesFn,
                                                                   cfg.entMm, masterSeed);
        if (setup.entResult.ok)
            setup.crossResult = ComboRando::ShuffleCrossEntrancesForAttempt(
                SOH_DumpEntranceGraphFn, SOH_SetCrossEntrancesFn, MM_DumpEntranceGraphFn, MM_SetCrossEntrancesFn,
                SOH_GetCrossExitsFn, MM_GetCrossExitsFn, setup.entResult.overrides, cfg.entCross, masterSeed);
        if (!setup.entResult.ok || !setup.crossResult.ok) {
            std::cerr << "[comborando] seed " << masterSeed << ": "
                      << (!setup.entResult.ok ? setup.entResult.error : setup.crossResult.error) << "\n";
            if (MM_Restore)
                MM_Restore();
            return ComboRando::AttemptStatus::EntranceShuffleFailed;
        }
        return ComboRando::AttemptStatus::Ready;
    };

    // The consolidated seed (settings, placements, entrances) as the in-game generator writes it, from a finished
    // fill spoiler. The caller adds the hints: they need the fill's requiredness result.
    auto buildConsolidated = [&](const std::string& seed, uint32_t masterSeed, const ComboRando::AttemptConfig& cfg,
                                 bool resolvedMmStart, const ComboRando::AttemptSetup& setup,
                                 const nlohmann::json& fillSpoiler) {
        const ComboRando::CwGoal& goal = cfg.goal;
        const ComboRando::ComboGames games = cfg.games;
        const bool unified = cfg.unified, formLogicCredit = cfg.formLogicCredit, lostWoodsLink = cfg.lostWoodsLink;
        const ComboRando::EntranceSettings& entMm = cfg.entMm;
        const ComboRando::CrossSettings& entCross = cfg.entCross;
        const std::string& sohDump = setup.sohDump;
        const std::string& mmDump = setup.mmDump;
        // OOT's curated ice-trap disguise set (parity with RunComboFill's writer).
        auto iceTrapModelsOf = [](const std::string& dump) {
            return nlohmann::json::parse(dump).value("iceTrapModels", nlohmann::json::array());
        };
        nlohmann::json consolidated;
        consolidated["fileType"] = ComboRando::kSeedFileType;
        consolidated["seed"] = seed;
        consolidated["masterSeed"] = masterSeed;
        consolidated["goal"] = { { "type", goal.hunt ? "triforceHunt" : "bosses" },
                                 { "requiredPieces", goal.required },
                                 { "totalPieces", goal.total } };
        consolidated["startingGame"] = resolvedMmStart ? "MM" : "OOT"; // #135
        consolidated["games"] = ComboRando::ComboGamesKey(games);
        // Shared Items: the EFFECTIVE mask (families actually trimmed this fill), not the
        // requested one — a family with no OOT copies was left alone (see CrossWorldRando.h).
        nlohmann::json sharedItemsJson = fillSpoiler.value("sharedItems", nlohmann::json::array());
        const uint32_t effectiveSharedMask = ComboRando::SharedMaskFromKeys(sharedItemsJson);
        consolidated["sharedItems"] = sharedItemsJson;
        if (unified)
            consolidated["unifiedInventory"] = true;
        if (formLogicCredit)
            consolidated["formLogicCredit"] = true;
        consolidated[ComboRando::LostWoodsLink::kSeedKey] = lostWoodsLink;
        if (SOH_DumpComboMasterQuestDungeons) {
            auto mq = ComboRando::MasterQuestFromDump(SOH_DumpComboMasterQuestDungeons());
            if (mq.is_array())
                consolidated[ComboRando::kMasterQuestKey] = mq;
        }
        if (fillSpoiler.contains("sharedStartingMm"))
            consolidated["sharedStartingMm"] = fillSpoiler["sharedStartingMm"];
        // ComboShip: suffix cross-game item-name collisions in the placements (parity with
        // RunComboFill's consolidated writer) so the headless spoiler shows "(OOT)"/"(MM)".
        nlohmann::json ootPl = fillSpoiler.value("oot", nlohmann::json::object());
        nlohmann::json mmPl = fillSpoiler.value("mm", nlohmann::json::object());
        ComboRando::SuffixCrossGameItems(ootPl, mmPl, fillSpoiler.value("foreign", nlohmann::json::array()), sohDump,
                                         mmDump, ComboRando::SharedUntaggedNames(effectiveSharedMask));
        consolidated["oot"] = { { "settings", nlohmann::json::parse(SOH_DumpSettings()) },
                                { "enabledTricks", SOH_DumpEnabledTricks
                                                       ? nlohmann::json::parse(SOH_DumpEnabledTricks())
                                                       : nlohmann::json::array() },
                                { "placements", ootPl },
                                { "prices", PricesOf(sohDump) },
                                { "iceTrapModels", iceTrapModelsOf(sohDump) } };
        consolidated["mm"] = { { "settings", nlohmann::json::parse(MM_DumpSettings()) },
                               { "placements", mmPl },
                               { "prices", PricesOf(mmDump) } };
        // ComboShip: enrich the foreign array exactly like RunComboFill (displayName game
        // suffix) so the headless consolidated file matches the in-game one.
        auto foreignArr = fillSpoiler.value("foreign", nlohmann::json::array());
        ComboRando::AssignTrapDisguises(foreignArr, fillSpoiler.value("oot", nlohmann::json::object()),
                                        fillSpoiler.value("mm", nlohmann::json::object()), sohDump, mmDump, masterSeed);
        consolidated["foreign"] = ComboRando::BuildForeignArray(foreignArr, effectiveSharedMask);
        // OOT entrance layout (parity with ComboShip.cpp's consolidated writer) — this is
        // what --playthrough installs into the region graph before walking.
        {
            nlohmann::json ootEnt = nlohmann::json::array();
            if (SOH_DumpEntranceOverrides) {
                try {
                    ootEnt = nlohmann::json::parse(SOH_DumpEntranceOverrides());
                } catch (...) {}
            }
            consolidated["entrances"] = { { "oot", std::move(ootEnt) } };
            if (entMm.Any()) { // §B1: stored, applied from the seed (never re-derived)
                consolidated["entrances"]["mm"] = setup.entResult.overrides;
                consolidated["entranceSettings"]["mm"] = entMm.ToJson();
                consolidated["entranceSpoiler"]["mm"] = setup.entResult.spoiler;
            }
            if (entCross.Any()) { // §B2
                consolidated["entrances"]["cross"] = setup.crossResult.runtime;
                consolidated["entranceSettings"]["cross"] = entCross.ToJson();
                consolidated["entranceSpoiler"]["cross"] = setup.crossResult.spoiler;
            }
        }
        return consolidated;
    };

    ComboRando::OracleFns oot{ Sym<FnOracleVoid>(soh, "Combo_SOH_Rando_Reset"),
                               Sym<FnOracleSetItems>(soh, "Combo_SOH_Rando_SetOwnedItems"),
                               Sym<FnOracleGetChecks>(soh, "Combo_SOH_Rando_GetReachableChecks"),
                               Sym<FnOraclePlaceItem>(soh, "Combo_SOH_Rando_PlaceItem"),
                               Sym<FnOracleGetPortalOpen>(soh, "Combo_SOH_Rando_GetPortalOpen"),
                               Sym<uint32_t (*)(void)>(soh, "Combo_SOH_Rando_GetCrossOut") };
    ComboRando::OracleFns mmO{ Sym<FnOracleVoid>(mm, "Combo_MM_Rando_Reset"),
                               Sym<FnOracleSetItems>(mm, "Combo_MM_Rando_SetOwnedItems"),
                               Sym<FnOracleGetChecks>(mm, "Combo_MM_Rando_GetReachableChecks"),
                               Sym<FnOraclePlaceItem>(mm, "Combo_MM_Rando_PlaceItem"),
                               nullptr,
                               Sym<uint32_t (*)(void)>(mm, "Combo_MM_Rando_GetCrossOut") };

    if (!SOH_InitRandoHeadless || !MM_InitRandoHeadless || !SOH_Dump || !MM_Dump || !oot.Reset || !oot.SetOwnedItems ||
        !oot.GetReachableChecks || !oot.PlaceItem || !oot.GetPortalOpen || !mmO.Reset || !mmO.SetOwnedItems ||
        !mmO.GetReachableChecks || !mmO.PlaceItem) {
        std::cerr << "[comborando] missing required DLL exports — rebuild soh.dll / 2ship.dll\n";
        return 2;
    }

    SOH_InitRandoHeadless();
    MM_InitRandoHeadless();

    if (!dumpEntrancesFile.empty()) {
        auto MM_DumpEntranceGraph = Sym<FnDump>(mm, "MM_DumpEntranceGraph");
        if (!MM_DumpEntranceGraph) {
            std::cerr << "[comborando] MM_DumpEntranceGraph missing — rebuild 2ship.dll\n";
            return 2;
        }
        std::ofstream(dumpEntrancesFile) << MM_DumpEntranceGraph();
        if (auto SOH_DumpGraph = Sym<FnDump>(soh, "SOH_DumpEntranceGraph")) // §B2: OOT side next to it
            std::ofstream(dumpEntrancesFile + ".oot.json") << SOH_DumpGraph();
        std::cout << "[comborando] MM entrance graph -> " << dumpEntrancesFile << "\n";
        return 0;
    }

    // ---- Archipelago modes (--ap-tables, --oracle): see ComboApTables.h and ComboOracleServer.h. ----
    // `open`: the seed's own choices (the consolidated seed's keys, never the menu CVars) into both games, then the
    // setup a fill attempt runs, then a session over the result.
    auto openWorld = [&](const nlohmann::json& req, ComboRando::OracleWorld& world) -> std::string {
        if (!SOH_RestoreSettings || !MM_RestoreSettings || !SOH_SetEnabledTricks)
            return "settings-restore exports unavailable: rebuild soh.dll / 2ship.dll";
        if (!req.contains("masterSeed") || !req["masterSeed"].is_number_unsigned())
            return "open needs \"masterSeed\", an unsigned number";
        const uint32_t masterSeed = req["masterSeed"].get<uint32_t>();
        if (MM_Restore)
            MM_Restore();
        const nlohmann::json settings = req.value("settings", nlohmann::json::object());
        const nlohmann::json ootBlock = settings.value("oot", nlohmann::json::object());
        const nlohmann::json mmBlock = settings.value("mm", nlohmann::json::object());
        SOH_RestoreSettings(ootBlock.value("settings", nlohmann::json::object()).dump().c_str());
        MM_RestoreSettings(mmBlock.value("settings", nlohmann::json::object()).dump().c_str());
        SOH_SetEnabledTricks(ootBlock.value("enabledTricks", nlohmann::json::array()).dump().c_str());

        ComboRando::AttemptConfig cfg;
        cfg.games = ComboRando::ComboGamesFromKey(req.value("games", std::string("ootmm")));
        const bool solo = ComboRando::ComboGamesSolo(cfg.games);
        const nlohmann::json goalJson = req.value("goal", nlohmann::json::object());
        cfg.goal.hunt = goalJson.value("type", std::string("bosses")) == "triforceHunt";
        cfg.goal.required =
            cfg.goal.hunt ? std::min(goalJson.value("requiredPieces", 0), ComboRando::kMaxComboTriforcePieces) : 0;
        cfg.goal.total =
            std::clamp(goalJson.value("totalPieces", -1), cfg.goal.required, ComboRando::kMaxComboTriforcePieces);
        // A solo seed shares nothing, so Unified Inventory (which shares every family) is inert there too.
        cfg.unified = !solo && req.value("unifiedInventory", false);
        cfg.sharedMask = solo          ? 0u
                         : cfg.unified ? ComboRando::SharedMaskAll()
                                       : SharedMaskFromRequest(req.value("sharedItems", nlohmann::json::array()));
        cfg.formLogicCredit = req.value("formLogicCredit", false);
        cfg.lostWoodsLink =
            !solo && req.value(ComboRando::LostWoodsLink::kSeedKey, ComboRando::LostWoodsLink::kDefault != 0);
        const nlohmann::json entranceSettings = req.value("entranceSettings", nlohmann::json::object());
        cfg.entMm = ComboRando::EntranceSettings::FromJson(entranceSettings.value("mm", nlohmann::json::object()));
        cfg.entCross = ComboRando::CrossSettings::FromJson(entranceSettings.value("cross", nlohmann::json::object()));
        if (req.contains(ComboRando::kMasterQuestKey))
            cfg.storedMasterQuest = { { ComboRando::kMasterQuestKey, req[ComboRando::kMasterQuestKey] } };

        const int startCfg = ComboRando::ComboGamesStartingGameCfg(
            cfg.games, StartingGameConfigFromName(req.value("startingGame", std::string("OOT"))));
        const bool mmStart = ResolveStartingGameMM(startCfg, masterSeed);

        const ComboRando::AttemptStatus status = beginAttempt(cfg, masterSeed, mmStart, world.setup);
        if (status == ComboRando::AttemptStatus::Fatal)
            return "the Triforce Hunt needs more pieces than the combined pool holds";
        if (status == ComboRando::AttemptStatus::OotShuffleFailed)
            return "OOT's entrance shuffle found no valid layout for this seed";
        if (status == ComboRando::AttemptStatus::EntranceShuffleFailed)
            return !world.setup.entResult.ok ? world.setup.entResult.error : world.setup.crossResult.error;

        world.config = cfg;
        world.masterSeed = masterSeed;
        world.mmStart = mmStart;
        world.ootAccess = ComboRando::OotAccessFromDump(world.setup.sohDump);
        if (world.ootAccess == ComboRando::OotAccess::NO_LOGIC)
            return "OoT No Logic is not supported by the oracle";
        world.seedLabel = req.contains("seed") && req["seed"].is_string() ? req["seed"].get<std::string>()
                                                                          : std::to_string(masterSeed);
        world.session = std::make_unique<ComboRando::CrossWorldSession>(
            oot, mmO, masterSeed, world.ootAccess, cfg.goal, mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT,
            cfg.sharedMask, cfg.games);
        return world.session->Prepare(world.setup.sohDump, world.setup.mmDump, world.setup.forced);
    };

    // `finalize`: the placements (confined and forced ones included) into our consolidated seed. Another player's item
    // is written as native junk at its check and listed under "archipelago" for the client to overlay.
    auto finalizeWorld = [&](ComboRando::OracleWorld& world, std::vector<ComboRando::CwPlacement>& placements,
                             const nlohmann::json& otherPlayersItems, nlohmann::json& seedOut) -> std::string {
        if (!SOH_DumpSettings || !MM_DumpSettings)
            return "settings dump exports unavailable: rebuild soh.dll / 2ship.dll";
        ComboRando::BakeCrossPlacedMmJunk(*world.session, placements);
        const nlohmann::json fillSpoiler =
            ComboRando::BuildFillSpoiler(*world.session, placements, 0, "archipelago oracle");
        seedOut =
            buildConsolidated(world.seedLabel, world.masterSeed, world.config, world.mmStart, world.setup, fillSpoiler);
        if (!otherPlayersItems.empty())
            seedOut["archipelago"] = { { "otherPlayersItems", otherPlayersItems } };
        return "";
    };

    auto serveOracle = [&]() {
        ComboRando::OracleBackend backend;
        backend.identity = { { "ship2ship", COMBO_SHIP2SHIP_VERSION },
                             { "comboRelease", COMBO_RELEASE_VERSION },
                             { "sohPin", COMBO_SOH_PIN },
                             { "mmPin", COMBO_MM_PIN } };
        backend.open = openWorld;
        backend.finalize = finalizeWorld;
        backend.close = [&]() {
            if (MM_Restore)
                MM_Restore();
            undoCross();
        };
        FILE* protocol = ClaimProtocolStream();
        if (!protocol) {
            std::cerr << "[comborando] --oracle could not take over stdout\n";
            return 2;
        }
        ComboRando::OracleServer server(std::move(backend));
        server.Serve(std::cin, [protocol](const std::string& line) {
            std::fputs(line.c_str(), protocol);
            std::fputc('\n', protocol);
            std::fflush(protocol);
        });
        return 0;
    };

    // Both games at their defaults, so the tables do not depend on this machine's menu choices.
    auto writeApTables = [&](const std::string& path) {
        if (!SOH_RestoreSettings || !MM_RestoreSettings || !SOH_DumpRandoHintData) {
            std::cerr << "[comborando] --ap-tables needs the settings-restore and hint-data exports: rebuild soh.dll "
                         "/ 2ship.dll\n";
            return 2;
        }
        SOH_RestoreSettings("{}");
        MM_RestoreSettings("{}");
        if (SOH_SetSeed)
            SOH_SetSeed(1);
        if (MM_SetSeed)
            MM_SetSeed(1);
        const std::string sohDump = SOH_Dump();
        const std::string mmDump = MM_Dump();
        const std::string hintDump = SOH_DumpRandoHintData();
        try {
            const nlohmann::json tables = ComboRando::BuildApTables(sohDump, hintDump, mmDump);
            std::ofstream(path, std::ios::trunc) << tables.dump(2);
            std::cout << "[comborando] AP tables: " << tables["items"].size() << " items, "
                      << tables["locations"].size() << " locations -> " << path << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[comborando] --ap-tables failed: " << e.what() << "\n";
            return 2;
        }
        return 0;
    };

    if (!apTablesFile.empty())
        return writeApTables(apTablesFile);
    if (oracleMode)
        return serveOracle();

    // ---- Playthrough mode: replay a finished seed and judge beatability. ----
    if (!playthroughFile.empty()) {
        if (!SOH_RestoreSettings || !SOH_PrepContext || !MM_RestoreSettings) {
            std::cerr << "[playthrough] settings-restore exports unavailable — rebuild soh.dll / 2ship.dll\n";
            return 2;
        }
        nlohmann::json spoiler;
        try {
            spoiler = nlohmann::json::parse(ReadFile(playthroughFile));
        } catch (const std::exception& e) {
            std::cerr << "[playthrough] could not read/parse '" << playthroughFile << "': " << e.what() << "\n";
            return 2;
        }
        auto ootSettings = spoiler.value("oot", nlohmann::json::object()).value("settings", nlohmann::json::object());
        auto mmSettings = spoiler.value("mm", nlohmann::json::object()).value("settings", nlohmann::json::object());
        // Flat placement map that RunPlaythrough consumes.
        nlohmann::json flat;
        flat["oot"] = spoiler.value("oot", nlohmann::json::object()).value("placements", nlohmann::json::object());
        flat["mm"] = spoiler.value("mm", nlohmann::json::object()).value("placements", nlohmann::json::object());
        flat["foreign"] = spoiler.value("foreign", nlohmann::json::array());
        // Shared Items: MM copies OOT starts with.
        if (spoiler.contains("sharedStartingMm"))
            flat["sharedStartingMm"] = spoiler["sharedStartingMm"];
        std::string label = SeedLabel(spoiler);
        uint32_t masterSeed = spoiler.value("masterSeed", 0u);
        // #136: the seed's own goal drives both the traversal win test and the pool shaping below.
        ComboRando::CwGoal goal;
        {
            auto g = spoiler.value("goal", nlohmann::json::object());
            goal.hunt = g.value("type", std::string("bosses")) == "triforceHunt";
            goal.required = goal.hunt ? g.value("requiredPieces", 0) : 0;
            goal.total = g.value("totalPieces", -1); // absent on seeds made before the combined total
        }
        // #135: the seed's starting game. Absent on old spoilers, which all started in OOT.
        const bool mmStart = spoiler.value("startingGame", std::string("OOT")) == "MM";
        // Shared Items: absent on old spoilers -> mask 0 -> feature off.
        const uint32_t sharedMask =
            ComboRando::SharedMaskFromKeys(spoiler.value("sharedItems", nlohmann::json::array()));
        // Games: absent on old spoilers -> combo.
        const ComboRando::ComboGames seedGames =
            ComboRando::ComboGamesFromKey(spoiler.value("games", std::string("ootmm")));

        auto ootEnabledTricks =
            spoiler.value("oot", nlohmann::json::object()).value("enabledTricks", nlohmann::json::array());
        auto ootEntrances = spoiler.value("entrances", nlohmann::json::object()).value("oot", nlohmann::json::array());

        // Prices are part of the seed: an unknown price can never be treated as buyable, so a spoiler
        // without them can't be validated honestly. Hard-fail rather than emit an optimistic verdict.
        auto ootPrices = spoiler.value("oot", nlohmann::json::object()).value("prices", nlohmann::json::object());
        auto mmPrices = spoiler.value("mm", nlohmann::json::object()).value("prices", nlohmann::json::object());
        if (ootPrices.empty() || mmPrices.empty() || !SOH_SetCheckPrices || !MM_SetCheckPrices) {
            std::cerr << "[playthrough] spoiler has no shop prices (predates price export) — regenerate the seed "
                         "to validate it\n";
            return 2;
        }

        bool entranceRestoreFailed = false; // set inside runPass; a wrong graph makes any verdict meaningless
        // One traversal pass. Forces real-logic evaluation (ignore the seed's No-Logic/Glitchless flag).
        // Tricks are set via SOH_SetEnabledTricks/SetAllTricks BEFORE SOH_Dump, whose SOH_PrepRandoContext
        // pushes them into the Context (Combo_ApplyEnabledTricks). SOH_Dump (+seed) sets the OOT/MM contexts
        // up exactly like the fill so reachability matches. MM has no tricks.
        auto runPass = [&](bool allTricks, nlohmann::json* ptOut) {
            // Restore the seed's ORIGINAL settings for the dump so the shuffled-shop-slot set matches
            // generation (a No-Logic seed shuffles 8 slots/shop; glitchless computes 7 — GAP-8).
            SOH_RestoreSettings(ootSettings.dump().c_str());
            nlohmann::json ms = mmSettings;
            for (auto it = ms.begin(); it != ms.end(); ++it)
                if (it.key().find("RO_LOGIC") != std::string::npos)
                    it.value() = 0; // MM glitchless — pool shape is logic-independent
            MM_RestoreSettings(ms.dump().c_str());
            if (allTricks) {
                if (SOH_SetAllTricks)
                    SOH_SetAllTricks();
            } else if (SOH_SetEnabledTricks) {
                SOH_SetEnabledTricks(ootEnabledTricks.dump().c_str());
            }
            if (SOH_SetSeed)
                SOH_SetSeed(masterSeed);
            pushMasterQuestDungeons(spoiler);
            if (MM_SetSeed)
                MM_SetSeed(masterSeed);
            if (SOH_SetComboGoal)
                SOH_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwOotPieces(goal.total, seedGames));
            if (MM_SetComboGoal)
                MM_SetComboGoal(goal.hunt ? 1 : 0, goal.required, ComboRando::CwMmPieces(goal.total, seedGames));
            if (SOH_SetComboStartingGame)
                SOH_SetComboStartingGame(mmStart ? 1 : 0);
            if (SOH_SetComboUnifiedInventory)
                SOH_SetComboUnifiedInventory(spoiler.value("unifiedInventory", false) ? 1 : 0);
            pushMirrorStart(spoiler.value("unifiedInventory", false), mmSettings);
            pushFormLogicCredit(spoiler.value("formLogicCredit", false) ||
                                (std::getenv("COMBO_FORM_CREDIT_TEST") != nullptr));
            pushLostWoodsLink(!ComboRando::ComboGamesSolo(seedGames) &&
                              spoiler.value(ComboRando::LostWoodsLink::kSeedKey,
                                            ComboRando::LostWoodsLink::kModelledWhenSeedLacksKey));
            // A real in-game save's placement map lists only SHUFFLED checks; non-shuffled items the
            // oracle still gates on (OOT vanilla shop items, MM boss remains when not shuffled) live in
            // the dump's fixed[]. Merge them in so those gates (e.g. MM RemainsCount for the Moon) resolve.
            std::string sohDump = SOH_Dump(), mmDump = MM_Dump();
            // full-coop-sync §B1: the seed's stored MM entrance layout (absent = vanilla).
            if (MM_SetEntranceOverridesFn)
                MM_SetEntranceOverridesFn(spoiler.value("entrances", nlohmann::json::object())
                                              .value("mm", nlohmann::json::object())
                                              .dump()
                                              .c_str());
            // NOW force glitchless for traversal (real gates); the dump above froze the seed's slot
            // set in the DLL-side cache, so this re-prep can't shrink it.
            nlohmann::json os = ootSettings;
            // "NoLogic" (bool) replaced "LogicRules" (0 = glitchless, 1 = no logic); accept both.
            for (auto it = os.begin(); it != os.end(); ++it)
                if (it.key().find("NoLogic") != std::string::npos || it.key().find("LogicRules") != std::string::npos)
                    it.value() = 0;
            SOH_RestoreSettings(os.dump().c_str());
            undoCross(); // §B2: no crossed door from a previous pass may survive the re-wiring below
            SOH_PrepContext();
            // The PrepContext above reset the OOT region graph to VANILLA wiring — not the graph the
            // seed was filled against. Install the seed's recorded entrance layout (empty array =
            // vanilla, still correct). Re-deriving via SOH_ShuffleEntrances is only a fallback for
            // old spoilers without the array: its logic-validated accepts depend on the trick set, so
            // the all-tricks pass could re-derive a DIFFERENT layout than generation.
            if (SOH_ApplyEntranceOverrides) {
                if (!SOH_ApplyEntranceOverrides(ootEntrances.dump().c_str())) {
                    std::cerr << "[playthrough] entrance apply failed — verdict unreliable\n";
                    entranceRestoreFailed = true;
                }
            } else if (!ootEntrances.empty()) {
                std::cerr << "[playthrough] spoiler has entrances but soh.dll lacks the apply export — "
                             "rebuild soh.dll; re-deriving (all-tricks pass may diverge)\n";
                if (!SOH_ShuffleEntrances || !SOH_ShuffleEntrances(masterSeed))
                    entranceRestoreFailed = true;
            }
            // §B2: the seed's stored cross-game doors, on top of both games' installed layouts.
            ComboRando::ApplyStoredCross(
                spoiler.value("entrances", nlohmann::json::object()).value("cross", nlohmann::json::object()),
                SOH_SetCrossEntrancesFn, MM_SetCrossEntrancesFn, SOH_GetCrossExitsFn, MM_GetCrossExitsFn);
            // Spoiler prices are the seed's truth — they override the dumps' re-rolls in every oracle
            // reset (SOH) / query (MM), so wallet gates evaluate against what the player actually pays.
            SOH_SetCheckPrices(ootPrices.dump().c_str());
            MM_SetCheckPrices(mmPrices.dump().c_str());
            nlohmann::json passFlat = flat;
            auto mergeFixed = [&](const std::string& dump, const char* game) {
                try {
                    for (auto& f : nlohmann::json::parse(dump).value("fixed", nlohmann::json::array())) {
                        std::string chk = f.value("check", ""), item = f.value("item", "");
                        if (!chk.empty() && !item.empty() && !passFlat[game].contains(chk))
                            passFlat[game][chk] = item;
                    }
                } catch (...) {}
            };
            mergeFixed(sohDump, "oot");
            mergeFixed(mmDump, "mm");
            if (std::getenv("COMBO_FORM_CREDIT_TEST")) {
                // Plan C1 check: with every OOT item this seed places EXCEPT a form's stand-ins, what the Zora / Goron
                // Mask adds to Hyrule's reachable checks (and that it adds nothing the real items wouldn't).
                std::vector<std::string> all;
                for (const char* g : { "oot", "mm" }) // cross-game seeds put Hyrule's items in Termina's checks too
                    for (auto& [chk, item] : passFlat[g].items())
                        if (item.is_string())
                            all.push_back(item.get<std::string>());
                auto without = [&](std::initializer_list<const char*> names) {
                    std::vector<std::string> v;
                    for (const auto& it : all) {
                        bool drop = false;
                        for (const char* n : names)
                            drop = drop || (it == n);
                        if (!drop)
                            v.push_back(it);
                    }
                    return v;
                };
                auto report = [&](const char* tag, std::vector<std::string> base, const char* pseudo) {
                    const auto a = ComboRando::QueryReachable(oot, base);
                    base.push_back(pseudo);
                    const auto b = ComboRando::QueryReachable(oot, base);
                    const auto real = ComboRando::QueryReachable(oot, all);
                    int gained = 0, beyondReal = 0;
                    for (const auto& c : b) {
                        if (!a.count(c)) {
                            gained++;
                            if (gained <= 12)
                                std::cout << "[formtest] " << tag << " adds: " << c << "\n";
                        }
                        if (!real.count(c))
                            beyondReal++;
                    }
                    std::cout << "[formtest] " << tag << ": without " << a.size() << ", with form " << b.size() << " (+"
                              << gained << "), with the real items " << real.size()
                              << ", form beyond real items: " << beyondReal << "\n";
                };
                report("zora",
                       without({ "Iron Boots", "Zora Tunic", "Bronze Scale", "Silver Scale", "Golden Scale",
                                 "Progressive Scale" }),
                       "@combo:form:zora");
                report("goron",
                       without({ "Goron Tunic", "Megaton Hammer", "Strength Upgrade", "Progressive Strength Upgrade" }),
                       "@combo:form:goron");
            }
            return ComboRando::RunPlaythrough(passFlat.dump(), oot, mmO, label, MM_Restore, ptOut, sohDump, mmDump,
                                              ComboRando::OotAccessFromDump(sohDump) != ComboRando::OotAccess::NO_LOGIC,
                                              /*progressionOnly*/ false, goal, mmStart, sharedMask);
        };

        // Affordability canary: re-check every priced purchase in the walk against the wallets held
        // at that sphere. A violation means the oracle's price wiring regressed to "shops are free".
        // With Child Wallet not shuffled, logic starts at the 99-capacity tier (logic.cpp Reset).
        const int ootBaseWalletTier = ootSettings.value("gRandoSettings.ShuffleChildWallet", 0) == 0 ? 1 : 0;
        auto affordabilityViolations = [&](const nlohmann::json& pt) {
            int ow = 0, mw = 0, bad = 0;
            for (const auto& sph : pt) {
                const int owS = ow, mwS = mw; // wallets from earlier spheres only
                for (const auto& st : sph.value("steps", nlohmann::json::array())) {
                    std::string game = st.value("game", ""), chk = st.value("check", ""), item = st.value("item", "");
                    // Junk is never required, so an unaffordable junk slot must not fail a beatable seed.
                    if (!st.value("advancement", true))
                        continue;
                    if (game == "oot" && ootPrices.contains(chk)) {
                        static const int caps[5] = { 0, 99, 200, 500, 999 };
                        int price = ootPrices[chk].get<int>();
                        if (price > caps[std::min(ootBaseWalletTier + owS, 4)]) {
                            ++bad;
                            std::cerr << "[playthrough] AFFORDABILITY: [OOT] " << chk << " price=" << price << " with "
                                      << owS << " wallet(s) at sphere " << sph.value("sphere", -1) << "\n";
                        }
                    } else if (game == "mm" && mmPrices.contains(chk) &&
                               // ComboShip: friendly (prettified) forms of the MM purchase check names.
                               (chk.find("Shop Item") != std::string::npos ||
                                chk.find("Tingle Map") != std::string::npos ||
                                chk.find("Gorman Milk") != std::string::npos ||
                                chk.find("Milk Bar") != std::string::npos ||
                                chk.find("Curiosity") != std::string::npos)) {
                        int p = mmPrices[chk].get<int>();
                        if (!(p < 100 || (p <= 200 && mwS >= 1) || mwS >= 2)) {
                            ++bad;
                            std::cerr << "[playthrough] AFFORDABILITY: [MM] " << chk << " price=" << p << " with "
                                      << mwS << " wallet(s) at sphere " << sph.value("sphere", -1) << "\n";
                        }
                    }
                    // Both games' wallet normalizes to "Progressive Wallet"; attribute to the home game
                    // unless shared, where a pickup raises both (see Combo_SharedReconcileNow).
                    if (item == "Progressive Wallet") {
                        if (sharedMask & (1u << ComboRando::SF_WALLET)) {
                            ++ow;
                            ++mw;
                        } else {
                            bool fgn = st.value("foreign", false);
                            std::string homeGame = fgn ? (game == "oot" ? "mm" : "oot") : game;
                            if (homeGame == "oot")
                                ++ow;
                            else
                                ++mw;
                        }
                    }
                }
            }
            return bad;
        };
        // Names the blocking win side from full-inventory reachability, so "stuck" is exact.
        auto blocked = [](const ComboRando::PlaythroughResult& r) -> const char* {
            if (r.hunt)
                return r.piecesReachable >= r.piecesRequired
                           ? "enough Triforce Pieces are placed but the forward walk dead-ends (item cycle)"
                           : "not enough Triforce Pieces are reachable";
            return (!r.ganonReachable && !r.majoraReachable) ? "neither Ganon nor Majora is reachable"
                   : !r.ganonReachable                       ? "Ganon (OOT) is unreachable"
                   : !r.majoraReachable
                       ? "Majora (MM) is unreachable"
                       : "both ends reachable with full inventory but the forward walk dead-ends (item cycle)";
        };

        // No Logic seeds may be OOT-unbeatable by design; the verdict below tolerates that as long as
        // MM stays fully reachable + Majora beatable.
        bool ootNoLogic = false;
        for (auto it = ootSettings.begin(); it != ootSettings.end(); ++it)
            if ((it.key().find("NoLogic") != std::string::npos || it.key().find("LogicRules") != std::string::npos) &&
                it.value().is_number() && it.value().get<int>() == 1)
                ootNoLogic = true;

        // Pass 1 — the seed's own settings + enabled tricks: "can this player beat it?"
        std::cout << "[playthrough] seed '" << label << "' — Pass 1 (" << ootEnabledTricks.size()
                  << " enabled trick(s), starts in " << (mmStart ? "MM" : "OOT") << ")\n";
        nlohmann::json pt1 = nlohmann::json::array();
        auto r1 = runPass(false, &pt1);
        if (!playthroughJsonFile.empty()) {
            const nlohmann::json out = { { "spheres", pt1 },
                                         { "beatable", r1.beatable },
                                         { "beatableSphere", r1.beatableSphere },
                                         { "reachableOot", r1.reachableOot },
                                         { "unreachableOot", r1.unreachableOot },
                                         { "reachableMm", r1.reachableMm },
                                         { "unreachableMm", r1.unreachableMm },
                                         { "ganonReachable", r1.ganonReachable },
                                         { "majoraReachable", r1.majoraReachable } };
            std::ofstream(playthroughJsonFile, std::ios::trunc) << out.dump();
        }
        if (entranceRestoreFailed) {
            std::cerr << "[playthrough] could not restore the seed's entrance layout — no verdict\n";
            return 2;
        }
        if (r1.beatable) {
            if (int bad = affordabilityViolations(pt1)) {
                std::cout << "[playthrough] RESULT: FAIL — beatable but " << bad
                          << " purchase(s) exceed the wallet held at their sphere (price wiring bug).\n";
                return 1;
            }
            std::cout << "[playthrough] RESULT: PASS — seed passed validation, should be beatable (sphere "
                      << r1.beatableSphere << ").\n";
            return 0;
        }
        // Pass 2 — all tricks: separates "needs more tricks than configured" from "impossible".
        std::cout << "[playthrough] Pass 1 stuck (" << blocked(r1) << ") — retrying with ALL tricks\n";
        nlohmann::json pt2 = nlohmann::json::array();
        auto r2 = runPass(true, &pt2);
        if (r2.beatable) {
            std::cout << "[playthrough] RESULT: beatable ONLY with tricks beyond the seed's settings (sphere "
                      << r2.beatableSphere << "). With the seed's tricks, Pass 1 got stuck: " << blocked(r1) << ".\n";
            return 3;
        }
        // No Logic: the forward-walk sphere ORDER is meaningless (in-game No Logic disables the very gates
        // the walk enforces), so beatability is judged on FULL-inventory reachability. Both win conditions
        // reachable with everything placed => genuinely beatable. Majora-only => tolerate OOT/Ganon (still a
        // valid No-Logic seed, MM win chain intact; MM-advancement reachability is guaranteed by the fill,
        // which retries on mmAdvUnreachable>0 — not gated on unreachableMm==0, ~1 MM junk check is always
        // under-modeled since the oracle evaluates MM with zeroed save options).
        if (ootNoLogic && r2.ganonReachable && r2.majoraReachable) {
            // Under a hunt both flags mirror the piece count, so name the pieces instead of the bosses.
            if (r2.hunt) {
                std::cout << "[playthrough] RESULT: PASS (No Logic) — beatable: " << r2.piecesReachable << "/"
                          << r2.piecesRequired
                          << " Triforce Pieces reachable with full inventory (forward-walk order N/A under No "
                             "Logic).\n";
            } else {
                std::cout << "[playthrough] RESULT: PASS (No Logic) — beatable: Ganon AND Majora both reachable "
                             "with full inventory (forward-walk order N/A under No Logic).\n";
            }
            return 0;
        }
        if (ootNoLogic && r2.majoraReachable) {
            std::cout << "[playthrough] RESULT: PASS (No Logic) — Majora beatable (MM reachable); Ganon (OOT) "
                         "unreachable even at full inventory, tolerated.\n";
            return 0;
        }
        std::cout << "[playthrough] RESULT: FAIL — not beatable even with all tricks; stuck: " << blocked(r2)
                  << " (OOT unreachable=" << r2.unreachableOot << ", MM unreachable=" << r2.unreachableMm
                  << "; see saves/combo/slot0.playthrough.txt).\n";
        return 1;
    }

    // ---- Generate + validate mode. ----
    // Restore a reported seed's settings so generation matches it; otherwise live CVar defaults apply.
    if (!settingsFile.empty()) {
        if (!SOH_RestoreSettings || !MM_RestoreSettings) {
            std::cerr << "[comborando] --settings needs the settings-restore exports — rebuild soh.dll / 2ship.dll\n";
            return 2;
        }
        try {
            auto sj = nlohmann::json::parse(ReadFile(settingsFile));
            const auto& oot = sj.contains("oot") ? sj["oot"] : sj;
            const auto& mm = sj.contains("mm") ? sj["mm"] : sj;
            if (oot.contains("settings"))
                SOH_RestoreSettings(oot["settings"].dump().c_str());
            if (mm.contains("settings"))
                MM_RestoreSettings(mm["settings"].dump().c_str());
            if (SOH_SetEnabledTricks && oot.contains("enabledTricks"))
                SOH_SetEnabledTricks(oot["enabledTricks"].dump().c_str());
            std::cout << "[comborando] restored settings from '" << settingsFile << "'\n";
        } catch (const std::exception& e) {
            std::cerr << "[comborando] could not read/parse '" << settingsFile << "': " << e.what() << "\n";
            return 2;
        }
    }
    // #136: goal resolution. --triforce-required wins; otherwise fall back to the menu CVars so a
    // headless seed reproduces the in-game one.
    ComboRando::CwGoal goal;
    if (triforceRequiredArg >= 0) {
        goal.hunt = triforceRequiredArg > 0;
        goal.required = std::min(triforceRequiredArg, ComboRando::kMaxComboTriforcePieces);
        // --triforce-total wins; else default the pool to exactly what the goal needs.
        goal.total = std::max(triforceTotalArg, goal.required);
    } else if (SOH_ReadComboGoalCVars) {
        int required = 0, total = -1;
        goal.hunt = SOH_ReadComboGoalCVars(&required, &total) != 0;
        goal.required = goal.hunt ? required : 0;
        goal.total = triforceTotalArg > 0 ? std::max(triforceTotalArg, goal.required) : total;
    }
    // Same cap the DLLs apply per game, so the recorded totalPieces can't disagree with the pools.
    goal.total = std::clamp(goal.total, goal.required, ComboRando::kMaxComboTriforcePieces);
    // Games: the flag wins, else the menu CVar. A solo seed starts in its game and has no Shared Items.
    const ComboRando::ComboGames games = ComboRando::ComboGamesFromInt(
        gamesArg >= 0 ? gamesArg : (SOH_ReadComboGamesCVar ? SOH_ReadComboGamesCVar() : 0));
    const bool solo = ComboRando::ComboGamesSolo(games);
    // #135: same fallback order — the flag wins, else the menu CVar.
    const int startCfg =
        ComboRando::ComboGamesStartingGameCfg(games, startingGameArg >= 0            ? startingGameArg
                                                     : SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar()
                                                                                     : 0);
    // Unified Inventory: CLI wins, else the menu CVar (via soh). It shares every family.
    bool unified = unifiedArg >= 0 ? unifiedArg == 1 : false;
    if (unifiedArg < 0 && SOH_ReadComboEntranceCVarsFn) {
        try {
            unified = nlohmann::json::parse(SOH_ReadComboEntranceCVarsFn()).value("unifiedInventory", false);
        } catch (...) {}
    }
    bool formLogicCredit = false;
    if (SOH_ReadComboEntranceCVarsFn) {
        try {
            formLogicCredit = nlohmann::json::parse(SOH_ReadComboEntranceCVarsFn()).value("formLogicCredit", false);
        } catch (...) {}
    }
    // Lost Woods link: --lost-woods-link / --no-lost-woods-link win, else the menu CVar (via soh); a solo seed has
    // none.
    bool lostWoodsLink = ComboRando::LostWoodsLink::kDefault != 0;
    if (lostWoodsLinkArg >= 0) {
        lostWoodsLink = lostWoodsLinkArg == 1;
    } else if (SOH_ReadComboEntranceCVarsFn) {
        try {
            lostWoodsLink = nlohmann::json::parse(SOH_ReadComboEntranceCVarsFn())
                                .value(ComboRando::LostWoodsLink::kSeedKey, lostWoodsLink);
        } catch (...) {}
    }
    lostWoodsLink = lostWoodsLink && !solo;
    // Shared Items: --shared-items wins ("all"/"none"/comma-separated keys); else the menu CVars.
    uint32_t sharedMask = 0;
    if (sharedItemsArg == "all") {
        sharedMask = ComboRando::SharedMaskAll();
    } else if (sharedItemsArg == "none" || sharedItemsArg.empty()) {
        sharedMask = (sharedItemsArg.empty() && SOH_ReadComboSharedCVars) ? SOH_ReadComboSharedCVars() : 0;
    } else {
        nlohmann::json keys = nlohmann::json::array();
        std::stringstream ss(sharedItemsArg);
        std::string tok;
        while (std::getline(ss, tok, ','))
            keys.push_back(tok);
        sharedMask = ComboRando::SharedMaskFromKeys(keys);
    }
    // A solo seed shares nothing, so Unified Inventory (which shares every family) is inert there too.
    if (solo) {
        sharedMask = 0;
        unified = false;
    } else if (unified) {
        sharedMask = ComboRando::SharedMaskAll();
    }
    std::cout << "[comborando] validating " << count << " seed(s) from "
              << (haveMasterSeed ? "masterSeed " + std::to_string(masterSeedArg) : "'" + seed + "'")
              << (goal.hunt ? " (Triforce Hunt, " + std::to_string(goal.required) + " of " +
                                  std::to_string(goal.total) + " pieces)"
                            : std::string(" (both bosses)"))
              << (startCfg == 1   ? ", starting game MM"
                  : startCfg == 2 ? ", starting game random"
                                  : "")
              << (solo ? std::string(", games ") + ComboRando::ComboGamesKey(games) : std::string())
              << (lostWoodsLink ? ", Lost Woods link" : "") << "\n";
    int failures = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) {
        const uint32_t base = (haveMasterSeed ? masterSeedArg : ComboHash(seed)) + static_cast<uint32_t>(i);
        uint32_t masterSeed = base;
        std::string sohDump, mmDump, sohHintDump;
        ComboRando::CombinedFillResult r{};
        ComboRando::RequirednessResult pareDownResult; // cross-hint verification (headless mirror of RunComboFill)
        // Whole-fill retries with re-derived seeds, identical to RunComboFill (GAP-4) so a headless
        // seed reproduces the in-game one even when early attempts fail (shared budget, same header).
        // full-coop-sync §B1: MM entrance settings (CLI override, else the combo menu CVars).
        ComboRando::EntranceSettings entMm;
        if (!mmEntrancesArg.empty()) {
            if (mmEntrancesArg == "all") {
                entMm.overworld = entMm.interior = entMm.dungeon = true;
            } else if (mmEntrancesArg != "off") {
                std::stringstream ss(mmEntrancesArg);
                for (std::string t; std::getline(ss, t, ',');) {
                    entMm.overworld |= t == "overworld";
                    entMm.interior |= t == "interior";
                    entMm.dungeon |= t == "dungeon";
                    entMm.decoupled |= t == "decoupled";
                    entMm.mixed |= t == "mixed";
                }
            }
        } else if (SOH_ReadComboEntranceCVarsFn) {
            try {
                entMm = ComboRando::EntranceSettings::FromJson(
                    nlohmann::json::parse(SOH_ReadComboEntranceCVarsFn()).value("mm", nlohmann::json::object()));
            } catch (...) {}
        }
        ComboRando::CrossSettings entCross;
        if (!crossArg.empty()) {
            if (crossArg != "off") {
                std::stringstream ss(crossArg);
                for (std::string t; std::getline(ss, t, ',');) {
                    entCross.overworld |= t == "overworld" || t == "all";
                    entCross.interior |= t == "interior" || t == "all";
                    entCross.dungeon |= t == "dungeon" || t == "all";
                    if (t.rfind("pct=", 0) == 0)
                        entCross.percent = std::clamp(std::atoi(t.c_str() + 4), 0, 100);
                }
            }
        } else if (SOH_ReadComboEntranceCVarsFn) {
            try {
                entCross = ComboRando::CrossSettings::FromJson(
                    nlohmann::json::parse(SOH_ReadComboEntranceCVarsFn()).value("cross", nlohmann::json::object()));
            } catch (...) {}
        }
        ComboRando::AttemptConfig attemptCfg;
        attemptCfg.goal = goal;
        attemptCfg.games = games;
        attemptCfg.sharedMask = sharedMask;
        attemptCfg.unified = unified;
        attemptCfg.formLogicCredit = formLogicCredit;
        attemptCfg.lostWoodsLink = lostWoodsLink;
        attemptCfg.entMm = entMm;
        attemptCfg.entCross = entCross;
        ComboRando::AttemptSetup setup;
        const int kFillAttempts =
            (entMm.Any() || entCross.Any()) ? ComboRando::kEntranceFillAttempts : ComboRando::kFillAttempts;
        bool pinStartOot = false, resolvedMmStart = false; // #135: Random falls back to OOT after a failure
        for (int attempt = 0; attempt < kFillAttempts; ++attempt) {
            masterSeed = base + attempt * 0x9E3779B9u;
            const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == kFillAttempts) &&
                                 ResolveStartingGameMM(startCfg, masterSeed);
            switch (beginAttempt(attemptCfg, masterSeed, mmStart, setup)) {
                case ComboRando::AttemptStatus::Fatal:
                    return 2;
                case ComboRando::AttemptStatus::OotShuffleFailed:
                    if (mmStart && startCfg == 2)
                        pinStartOot = true;
                    continue;
                case ComboRando::AttemptStatus::EntranceShuffleFailed:
                    continue;
                case ComboRando::AttemptStatus::Ready:
                    break;
            }
            sohDump = setup.sohDump;
            mmDump = setup.mmDump;
            const std::string& forced = setup.forced;
            ComboRando::OotAccess ootAccess = ComboRando::OotAccessFromDump(sohDump);
            r = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, oot, mmO, nullptr, forced, ootAccess,
                                                   goal, mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT,
                                                   sharedMask, games);
            if (r.success) {
                resolvedMmStart = mmStart;
                // Cross-hint data (Phase 2/3 mirror of RunComboFill, incl. the same area maps so the
                // WotH/Foolish rollup matches in-game): needs this attempt's still-live oracle session.
                sohHintDump = SOH_DumpRandoHintData ? SOH_DumpRandoHintData() : "";
                std::unordered_map<std::string, std::string> ootAreas, mmAreas;
                try {
                    auto hd = nlohmann::json::parse(sohHintDump.empty() ? "{}" : sohHintDump);
                    for (auto& c : hd.value("checks", nlohmann::json::array())) {
                        std::string name = c.value("name", ""), area = c.value("area", "");
                        if (!name.empty() && !area.empty())
                            ootAreas.emplace(std::move(name), std::move(area));
                    }
                    auto d = nlohmann::json::parse(mmDump);
                    const auto locHints = d.value("locationHints", nlohmann::json::object());
                    for (auto& [chk, region] : locHints.items())
                        mmAreas.emplace(chk, region.get<std::string>());
                } catch (...) {}
                const bool noLogic = ootAccess == ComboRando::OotAccess::NO_LOGIC;
                // A hunt under NO_LOGIC has no meaningful requiredness (see RunComboFill) — skip it.
                if (goal.hunt && noLogic)
                    std::cout << "[comborando]   pare-down skipped (No Logic + Triforce Hunt)\n";
                else if (noLogic && games == ComboRando::COMBO_GAMES_OOT)
                    std::cout << "[comborando]   pare-down skipped (No Logic + OoT only)\n";
                else if (ComboRando::NeedsRequirednessPareDown(sohHintDump, mmDump))
                    // NO_LOGIC: gate requiredness on MM only (OOT may be unbeatable by design). Solo: its boss.
                    pareDownResult = ComboRando::PareDownPlaythrough(
                        r.spoilerJson, oot, mmO, nullptr, sohDump, mmDump, ootAreas, mmAreas,
                        goal.hunt ? ComboRando::MakeTriforceHuntGoal(goal.required)
                        : (noLogic || games == ComboRando::COMBO_GAMES_MM) ? ComboRando::MmOnlyMajoraGoal
                        : games == ComboRando::COMBO_GAMES_OOT             ? ComboRando::OotOnlyGanonGoal
                                                                           : ComboRando::DefaultGanonMajoraGoal,
                        !noLogic, mmStart);
                else
                    std::cout << "[comborando]   pare-down skipped (no enabled hint surface needs requiredness)\n";
            }
            if (MM_Restore)
                MM_Restore();
            if (r.success)
                break;
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            std::cerr << "[comborando]   attempt " << (attempt + 1) << "/" << kFillAttempts << " failed: " << r.error
                      << "\n";
        }
        std::string tag = "'" + seed + "'" + (count > 1 ? ("+" + std::to_string(i)) : "");
        if (r.success) {
            size_t foreign = 0;
            try {
                foreign = nlohmann::json::parse(r.spoilerJson).value("foreign", nlohmann::json::array()).size();
            } catch (...) {}
            std::cout << "[comborando]   seed " << tag << " (" << masterSeed << ") PASS (" << foreign
                      << " cross-game placements) fingerprint="
                      << HexU64(fillFingerprint(r, setup.entResult, setup.crossResult, sohDump, mmDump)) << "\n";
            // For a single-seed run, emit a consolidated spoiler (settings + placements) so it can be fed
            // straight back to --playthrough. Mirrors ComboShip.cpp's consolidated shape.
            if (count == 1 && SOH_DumpSettings && MM_DumpSettings) {
                try {
                    auto fillSpoiler = nlohmann::json::parse(r.spoilerJson);
                    nlohmann::json consolidated =
                        buildConsolidated(seed, masterSeed, attemptCfg, resolvedMmStart, setup, fillSpoiler);
                    // Cross-hint generation (mirrors RunComboFill) — enables the headless determinism check.
                    consolidated["hints"] =
                        ComboRando::Generate(masterSeed, sohDump, sohHintDump, mmDump, consolidated["foreign"],
                                             r.spoilerJson, pareDownResult, games);
                    std::error_code ec;
                    std::filesystem::create_directories("saves/combo", ec);
                    std::ofstream f("saves/combo/comborando.spoiler.json", std::ios::trunc);
                    f << consolidated.dump(2);
                    std::cout << "[comborando]   consolidated spoiler -> saves/combo/comborando.spoiler.json\n";
                } catch (const std::exception& e) {
                    std::cerr << "[comborando]   spoiler write failed: " << e.what() << "\n";
                }
            }
        } else {
            std::cerr << "[comborando]   seed " << tag << " (" << masterSeed << ") FAIL: " << r.error << "\n";
            ++failures;
        }
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "[comborando] RESULT: " << (failures ? "FAIL" : "PASS") << " — " << (count - failures) << "/" << count
              << " completable, " << ms << " ms\n";
    return failures == 0 ? 0 : 1;
}

// Exit without running static destructors. The headless init leaves the shared Ship::Context to die
// in them, and their order across the dylibs is unspecified: on macOS spdlog's registry goes first,
// so ~Context logs through a freed logger and every run ends in SIGSEGV after printing its verdict,
// turning a PASS into exit 139. Every file is closed by now.
// ponytail: skips teardown rather than doing it. The upgrade is a SOH_DeinitRandoHeadless export that
// calls Context::DestroyInstance(), as the game's DeinitOTR does, invoked before returning.
int main(int argc, char** argv) {
    const int rc = Run(argc, argv);
    std::cout.flush();
    std::_Exit(rc);
}
