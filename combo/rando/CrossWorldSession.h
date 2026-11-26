// combo/rando/CrossWorldSession.h
// ComboShip: the model and the session behind a cross-world generation. The types (items, checks, placements, the
// oracle table) come first. A CrossWorldSession then holds everything decided BEFORE the assumed fill places an item:
// the parsed pools, the confined and forced placements, Shared Items trimming and the pool balance. It also holds the
// one reach fixpoint both games' oracles are asked through. The fill (CrossWorldRando.h) and the Archipelago oracle
// (ComboOracleServer.h) both use it, so "which checks do these items reach" is answered in exactly one place.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "CrossForeign.h" // for ComboRando::GameId
#include "SharedItems.h"
#include "CrossEntrance.h"
#include "CrossWarpLogic.h"
#include "ComboGames.h"

namespace ComboRando {

// ---------- Deterministic 64-bit LCG (Knuth / Newlib constants) ----------

struct CwRng {
    uint64_t s;
    explicit CwRng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {
    }
    uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    }
    uint32_t below(uint32_t n) {
        return n ? next() % n : 0;
    }
};

// MM's junk placeholder as the dump names it. Cross-placed junk is baked into a real item below, so
// this only reaches a consumer from an older seed or a hand-written plando row.
inline constexpr const char* kMmJunkName = "Junk";

// FNV-1a over a name, so a per-entry RNG stream can be seeded without touching the fill's own.
inline uint32_t CwHashName(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h = (h ^ c) * 16777619u;
    }
    return h;
}

template <class T> inline void cwShuffle(std::vector<T>& v, CwRng& rng) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = rng.below(static_cast<uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// ---------- Oracle function-pointer types (set by ComboShip.cpp) ----------

struct OracleFns {
    void (*Reset)(void);
    void (*SetOwnedItems)(const char*);
    const char* (*GetReachableChecks)(void);
    void (*PlaceItem)(const char*, const char*);
    // OOT only (null for MM): is the OOT->MM portal region (the Market's Mask Shop, or the Lost Woods hub in a seed
    // with the Lost Woods link, ComboLostWoodsLink.h) reachable? Valid only immediately after a GetReachableChecks
    // call, whose owned-set it describes (the DLL piggybacks on that search).
    uint8_t (*GetPortalOpen)(void) = nullptr;
    // Teleport-song bits for the last GetReachableChecks. Null = out of logic.
    uint32_t (*GetCrossOut)(void) = nullptr;
};

inline uint32_t OracleCrossOut(const OracleFns& o) {
    return o.GetCrossOut ? o.GetCrossOut() : 0u;
}

// ---------- Data types ----------

// Per-game OOT accessibility, mapped from OOT's RSK_NO_LOGIC + RSK_ALL_CHECKS_REACHABLE.
// MM is always ALL_REACHABLE. See CrossWorldCombinedFill for the per-mode fill/validation behavior.
//   ALL_REACHABLE  every OOT advancement item lands reachable (default; unchanged behavior)
//   BEATABLE_ONLY  OOT progression may strand off-path, but the seed stays beatable (ALR-off)
//   NO_LOGIC       OOT items land anywhere; only MM stays fully reachable
enum class OotAccess { ALL_REACHABLE, BEATABLE_ONLY, NO_LOGIC };

// Maps the OOT dump's "accessibility" block to a mode: No Logic wins; else ALR-off => BEATABLE_ONLY;
// else ALL_REACHABLE. Missing/old dumps default to ALL_REACHABLE (unchanged behavior).
inline OotAccess OotAccessFromDump(const std::string& sohDumpJson) {
    try {
        auto a = nlohmann::json::parse(sohDumpJson).value("accessibility", nlohmann::json::object());
        if (a.value("noLogic", false))
            return OotAccess::NO_LOGIC;
        if (!a.value("allLocationsReachable", true))
            return OotAccess::BEATABLE_ONLY;
    } catch (...) {}
    return OotAccess::ALL_REACHABLE;
}

// Combined win condition (#136). hunt=false is the default both-bosses goal; hunt=true wins on
// `required` Triforce Pieces from EITHER game's pool. These are the friendly names both dumps emit.
inline constexpr const char* kOotTriforcePiece = "Triforce Piece";
inline constexpr const char* kMmTriforcePiece = "Piece of the Triforce";

struct CwGoal {
    bool hunt = false;
    int required = 0;
    int total = -1; // combined pieces placed; -1 = unset (seed predates the combo-owned total)
};

// Even split of the combined total across the two pools; OOT takes the odd piece. -1 passes through
// so each game keeps its own slider (old seeds). A solo seed (ComboGames.h) puts every piece in its game.
inline int CwOotPieces(int total, ComboGames games = COMBO_GAMES_BOTH) {
    return ComboGamesOotPieces(games, total);
}
inline int CwMmPieces(int total, ComboGames games = COMBO_GAMES_BOTH) {
    return ComboGamesMmPieces(games, total);
}
// Ceiling on the combined total. OOT's pool can't absorb its half much past this (100/100 trips
// item_pool.cpp's itemPool <= locCount assert); 50/50 is verified to generate.
inline constexpr int kMaxComboTriforcePieces = 100;

inline bool CwIsTriforcePiece(GameId itemGame, const std::string& itemName) {
    return itemName == (itemGame == GAME_OOT ? kOotTriforcePiece : kMmTriforcePiece);
}

// How many pieces the two settings-scoped pools actually hold. Logged loudly: the names above are
// coupled to each game's item table, so a rename would otherwise silently drop that game's pieces.
inline int CountPoolTriforcePieces(const std::string& sohDumpJson, const std::string& mmDumpJson,
                                   ComboGames games = COMBO_GAMES_BOTH) {
    auto countIn = [](const std::string& dump, GameId g) {
        int n = 0;
        try {
            auto d = nlohmann::json::parse(dump);
            for (const auto& it : d.value("pool", nlohmann::json::array()))
                n += CwIsTriforcePiece(g, it.value("name", std::string())) ? 1 : 0;
        } catch (...) {}
        return n;
    };
    const int oot = countIn(sohDumpJson, GAME_OOT);
    const int mm = countIn(mmDumpJson, GAME_MM);
    std::cout << "[ComboShip] Triforce Hunt: combined pool holds " << oot << " OOT + " << mm << " MM piece(s)\n";
    if ((oot == 0 || mm == 0) && !ComboGamesSolo(games)) // a solo seed has every piece in its game
        std::cerr << "[ComboShip] Triforce Hunt: WARNING — one game contributes no pieces (slider at 0, or its "
                     "piece item was renamed)\n";
    return oot + mm;
}

// Reuses GameId from CrossForeign.h (GAME_OOT = 0, GAME_MM = 1)
using Game = GameId;

// Native item category from each game's dump. Governs what may be DISCARDED, independently of
// `advancement` (which governs which fill phase places it). Only JUNK is ever discardable.
enum class CwCat { JUNK, LESSER, HEALTH, BOSS_KEY, SMALL_KEY, TOKEN, MAJOR, MASK, STRAY_FAIRY, UNKNOWN };

inline CwCat CwCatFromName(const std::string& s) {
    if (s == "junk")
        return CwCat::JUNK;
    if (s == "lesser")
        return CwCat::LESSER;
    if (s == "health")
        return CwCat::HEALTH;
    if (s == "bossKey")
        return CwCat::BOSS_KEY;
    if (s == "smallKey")
        return CwCat::SMALL_KEY;
    if (s == "token")
        return CwCat::TOKEN;
    if (s == "major")
        return CwCat::MAJOR;
    if (s == "mask")
        return CwCat::MASK;
    if (s == "strayFairy")
        return CwCat::STRAY_FAIRY;
    return CwCat::UNKNOWN; // older DLL or a new category — never discardable
}

inline const char* CwCatName(CwCat c) {
    switch (c) {
        case CwCat::JUNK:
            return "junk";
        case CwCat::LESSER:
            return "lesser";
        case CwCat::HEALTH:
            return "health";
        case CwCat::BOSS_KEY:
            return "bossKey";
        case CwCat::SMALL_KEY:
            return "smallKey";
        case CwCat::TOKEN:
            return "token";
        case CwCat::MAJOR:
            return "major";
        case CwCat::MASK:
            return "mask";
        case CwCat::STRAY_FAIRY:
            return "strayFairy";
        default:
            return "unknown";
    }
}

struct CwItem {
    Game game;
    std::string name;
    bool advancement;
    CwCat cat = CwCat::UNKNOWN;
    // Only native-JUNK may absorb a surplus, and never if it gates logic: OOT files bombchus and
    // Buy Blue Fire/Arrows as ITEM_CATEGORY_JUNK with advancement=true (item_list.cpp:278-325).
    bool discardable() const {
        return cat == CwCat::JUNK && !advancement;
    }
};

struct CwCheck {
    Game game;
    std::string name;
};

struct CwPlacement {
    CwCheck check;
    CwItem item;
};

// ---------- Session ----------

// Per-oracle query stats (count + total ms), logged on completion — the searches dominate fill time, so this is
// the first thing to read when generation feels slow.
struct CwQueryStats {
    uint32_t count = 0;
    int64_t ms = 0;
};

// OOT-only, portal SHUT: nothing assumed from the free pool, MM never queried.
struct CwOotClosedResult {
    std::unordered_set<std::string> reach;
    bool portalOpen = false;
};

// ootReachable/mmReachable = reachable checks at the fixpoint; ootOwned = OOT items collected along the way (drives
// the BEATABLE_ONLY boss-key goal check without a second traversal).
struct CwFixResult {
    std::unordered_set<std::string> ootReachable, mmReachable;
    std::vector<std::string> ootOwned;
    CrossWarpLatch cross; // which cross-game teleport-song abilities logic ended up using
};

struct CrossWorldSession {
    const OracleFns& ootOracle;
    const OracleFns& mmOracle;
    const uint32_t masterSeed;
    const OotAccess ootAccess;
    const CwGoal goal;
    const GameId startingGame;
    const uint32_t sharedMask;
    const ComboGames games;
    const bool solo;
    // Portal gate: NO_LOGIC deliberately skips it (an impossible seed is that mode's point). Any other mode without
    // the export would silently fill MM as sphere-0 reachable — the bug this gate fixes.
    const bool portalGated;
    const bool mmStart;

    // Filled by Prepare().
    std::vector<CwItem> advItems, junkItems;
    std::vector<std::string> mmJunkNames; // MM's own pickup-rotation names, for the cross-placed junk bake
    std::vector<CwCheck> allChecks;
    std::vector<CwPlacement> lockedPlacements; // confined pre-placements (own-dungeon keys, etc.)
    std::vector<CwPlacement> forcedPlacements;
    std::vector<std::string> ootForcedOwned, mmForcedOwned;
    uint32_t effectiveSharedMask = 0;
    // Shared items OOT starts with (MM names); MM logic starts with them too.
    std::vector<std::string> sharedStartingMm;
    size_t trimmedTotal = 0, paddedTotal = 0;
    CwQueryStats ootStats, mmStats;

    CrossWorldSession(const OracleFns& ootOracleIn, const OracleFns& mmOracleIn, uint32_t masterSeedIn,
                      OotAccess ootAccessIn, CwGoal goalIn, GameId startingGameIn, uint32_t sharedMaskIn,
                      ComboGames gamesIn)
        : ootOracle(ootOracleIn), mmOracle(mmOracleIn), masterSeed(masterSeedIn), ootAccess(ootAccessIn), goal(goalIn),
          startingGame(startingGameIn), sharedMask(sharedMaskIn), games(gamesIn), solo(ComboGamesSolo(gamesIn)),
          portalGated(ootAccessIn != OotAccess::NO_LOGIC), mmStart(startingGameIn == GAME_MM) {
    }

    // Parses both dumps into pools, reserves the forced placements, trims Shared Items and balances each game's
    // pool against its checks. Returns "" on success, else why the session cannot be built. RNG-free apart from the
    // forced-category pick, which has its own stream.
    // forcedOotJson: OOT checks the dump can't carry (e.g. Link's Pocket). Each forced item is reserved out of the
    // cross pool and owned from the start for logic.
    std::string Prepare(const std::string& sohDumpJson, const std::string& mmDumpJson,
                        const std::string& forcedOotJson) {
        if (portalGated && ootOracle.GetPortalOpen == nullptr) {
            return "OOT oracle is missing Combo_SOH_Rando_GetPortalOpen — rebuild soh.dll (the OOT->MM "
                   "portal cannot be gated without it)";
        }

        // --- Parse pools (single pass per dump) ---
        // Checks from "checks", items from the real "pool" (split by advancement), confined
        // pre-placements from "fixed". Falls back to per-check vanillaItem if an old DLL omits "pool".
        // Set when a dump carries pool[] but no category — a stale DLL, which would otherwise surface as an
        // unbalanceable pool ("no discardable junk") instead of the rebuild it actually needs.
        std::string missingCategory;

        auto parsePool = [&](Game game, const std::string& dumpJson) {
            auto d = nlohmann::json::parse(dumpJson);

            // Fillable checks (name only; the pool below feeds the items).
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                std::string name = c.value("name", "");
                if (!name.empty())
                    allChecks.push_back({ game, name });
            }

            // Item pool: the game's real generated pool, split into advancement/junk.
            if (d.contains("pool")) {
                size_t parsed = 0, withCategory = 0;
                for (auto& it : d["pool"]) {
                    std::string name = it.value("name", "");
                    if (name.empty())
                        continue;
                    bool adv = it.value("advancement", true);
                    CwCat cat = CwCatFromName(it.value("category", std::string{}));
                    ++parsed;
                    withCategory += (cat != CwCat::UNKNOWN) ? 1 : 0;
                    (adv ? advItems : junkItems).push_back({ game, name, adv, cat });
                }
                if (parsed > 0 && withCategory == 0)
                    missingCategory = (game == GAME_OOT ? "soh.dll" : "2ship.dll");
            } else {
                // Older DLL: reconstruct from per-check vanilla items — exactly one per check, so the
                // balancer no-ops. Class non-advancement as junk so a forced reservation can still pad.
                for (auto& c : d.value("checks", nlohmann::json::array())) {
                    std::string vi = c.value("vanillaItem", "");
                    if (vi.empty())
                        continue;
                    bool adv = c.value("advancement", true);
                    (adv ? advItems : junkItems).push_back({ game, vi, adv, adv ? CwCat::UNKNOWN : CwCat::JUNK });
                }
            }

            // MM's junk rotation set, for the bake below. Absent on an older 2ship.dll -> no bake.
            if (game == GAME_MM) {
                for (auto& j : d.value("junkPool", nlohmann::json::array())) {
                    if (j.is_string() && !j.get<std::string>().empty())
                        mmJunkNames.push_back(j.get<std::string>());
                }
            }

            // Confined pre-placements: locked to their check, credited to logic when reached. No category —
            // locked items never enter junkItems, so they are never trim candidates.
            for (auto& f : d.value("fixed", nlohmann::json::array())) {
                std::string check = f.value("check", "");
                std::string item = f.value("item", "");
                if (check.empty() || item.empty())
                    continue;
                lockedPlacements.push_back({ { game, check }, { game, item, f.value("advancement", true) } });
            }
        };

        try {
            parsePool(GAME_OOT, sohDumpJson);
            parsePool(GAME_MM, mmDumpJson);
        } catch (const std::exception& e) { return std::string("Pool parse error: ") + e.what(); }
        if (!missingCategory.empty()) {
            const std::string error = missingCategory +
                                      " is stale: its pool[] has no per-item `category`, so the fill cannot tell junk "
                                      "from progression and refuses to balance the pool — rebuild soh.dll / 2ship.dll";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << error << "\n";
            return error;
        }
        // #135: under an MM start the Mask Shop Key is forced start-with, so a confined placement of it in
        // fixed[] means the force never reached OOT's settings. Warn only — A0 keeps the portal re-openable.
        if (mmStart) {
            bool lockedDoors = false;
            try {
                lockedDoors = nlohmann::json::parse(sohDumpJson)
                                  .value("accessibility", nlohmann::json::object())
                                  .value("lockOverworldDoors", false);
            } catch (...) {}
            const bool keyPlaced =
                lockedDoors && std::any_of(lockedPlacements.begin(), lockedPlacements.end(), [](const CwPlacement& p) {
                    return p.item.game == GAME_OOT && p.item.name == "Mask Shop Key";
                });
            if (keyPlaced)
                std::cerr
                    << "[ComboShip] CrossWorldCombinedFill: WARNING — MM start with Lock Overworld Doors, but the "
                       "Mask Shop Key was confined-placed (Exclude Mask Shop Key was not forced on)\n";
        }

        // Forced OOT placements (e.g. Link's Pocket): reserve each item out of the cross pool and treat
        // it as owned-from-start (auto-granted at save creation). Appended to placements after the fill.
        {
            auto removeOneItem = [](std::vector<CwItem>& v, Game g, const std::string& name) -> bool {
                for (size_t i = 0; i < v.size(); ++i) {
                    if (v[i].game == g && v[i].name == name) {
                        v[i] = v.back();
                        v.pop_back();
                        return true;
                    }
                }
                return false;
            };
            CwRng frng(masterSeed ^ 0xF0F0F0F0u);
            try {
                if (!forcedOotJson.empty()) {
                    auto fj = nlohmann::json::parse(forcedOotJson);
                    for (auto& [checkName, spec] : fj.items()) {
                        std::string itemName;
                        bool adv = true;
                        if (spec.contains("item")) {
                            itemName = spec.value("item", std::string{});
                            if (removeOneItem(advItems, GAME_OOT, itemName)) {
                                adv = true;
                            } else if (removeOneItem(junkItems, GAME_OOT, itemName)) {
                                adv = false;
                            } // else: not in pool — place anyway (it's owned at start regardless)
                        } else {
                            // "category": pick a random OOT item from the (settings-scoped) cross pool.
                            std::string cat = spec.value("category", std::string("any"));
                            std::vector<size_t> idx;
                            std::vector<CwItem>* src = nullptr;
                            for (size_t i = 0; i < advItems.size(); ++i)
                                if (advItems[i].game == GAME_OOT)
                                    idx.push_back(i);
                            if (!idx.empty()) {
                                src = &advItems;
                                adv = true;
                            } else if (cat == "any") {
                                for (size_t i = 0; i < junkItems.size(); ++i)
                                    if (junkItems[i].game == GAME_OOT)
                                        idx.push_back(i);
                                if (!idx.empty()) {
                                    src = &junkItems;
                                    adv = false;
                                }
                            }
                            if (src) {
                                size_t pick = idx[frng.below(static_cast<uint32_t>(idx.size()))];
                                itemName = (*src)[pick].name;
                                (*src)[pick] = src->back();
                                src->pop_back();
                            }
                        }
                        if (itemName.empty())
                            continue;
                        forcedPlacements.push_back({ { GAME_OOT, checkName }, { GAME_OOT, itemName, adv } });
                        ootForcedOwned.push_back(itemName);
                        // No filler: Link's Pocket isn't in checks[], so reserving its item is already
                        // balanced. The old filler left a permanent +1 surplus Phase B silently discarded.
                    }
                }
            } catch (...) {}
        }

        // Shared Items (OoTMM-style): trim MM's copies of each effective family; the fixpoint below
        // mirrors the OOT-owned count back onto MM's oracle. See deviations/rando.md.
        if (sharedMask != 0) {
            bool maskQuestShuffle = false;
            std::vector<std::string> ootStarting;
            try {
                auto dump = nlohmann::json::parse(sohDumpJson);
                maskQuestShuffle =
                    dump.value("accessibility", nlohmann::json::object()).value("maskQuestShuffle", false);
                for (const auto& n : dump.value("startingItems", nlohmann::json::array()))
                    if (n.is_string())
                        ootStarting.push_back(n.get<std::string>());
            } catch (...) {}
            for (int i = 0; i < SF_COUNT; ++i) {
                if (!(sharedMask & (1u << i)))
                    continue;
                const auto& def = SharedFamilyByIndex(i);
                if (def.pooled) {
                    // Pooled (bottles): every copy in both pools stays; the fixpoint mirrors them both ways.
                    effectiveSharedMask |= (1u << i);
                    std::cout << "[ComboShip] Shared " << def.key << ": pooled, every copy kept in both games\n";
                    continue;
                }
                if (def.isMask && !maskQuestShuffle) {
                    std::cout << "[ComboShip] Shared " << def.key << ": skipped (OOT Shuffle Masks is off)\n";
                    continue;
                }
                size_t ootCopies = 0;
                for (const auto& it : advItems)
                    if (it.game == GAME_OOT && it.name == def.ootName)
                        ++ootCopies;
                for (const auto& p : lockedPlacements)
                    if (p.item.game == GAME_OOT && p.item.name == def.ootName)
                        ++ootCopies;
                const size_t ootStartingCopies =
                    static_cast<size_t>(std::count(ootStarting.begin(), ootStarting.end(), std::string(def.ootName)));
                if (ootCopies == 0 && ootStartingCopies == 0) {
                    std::cout << "[ComboShip] Shared " << def.key << ": OOT pool has none, left MM copies alone\n";
                    continue;
                }
                if (ootCopies == 0)
                    std::cout << "[ComboShip] Shared " << def.key << ": OOT starts with it (" << ootStartingCopies
                              << "), MM gets it at save creation\n";
                if (def.mmHasItem)
                    for (size_t n = 0; n < std::min<size_t>(ootStartingCopies, static_cast<size_t>(def.mmTierCap)); ++n)
                        sharedStartingMm.push_back(def.mmName);
                if (!def.mmHasItem) {
                    // MM has no pool copy of this item — nothing to trim, just mark the family effective.
                    effectiveSharedMask |= (1u << i);
                    std::cout << "[ComboShip] Shared " << def.key << ": MM has no item, not trimmed\n";
                    continue;
                }
                std::vector<CwItem> kept;
                kept.reserve(advItems.size());
                size_t removed = 0;
                for (auto& it : advItems) {
                    if (it.game == GAME_MM && it.name == def.mmName) {
                        ++removed;
                        continue;
                    }
                    kept.push_back(std::move(it));
                }
                advItems = std::move(kept);
                if (removed == 0) {
                    // Name drift between the two games' pools (see deviations/rando.md) — not fatal, but
                    // loud: this is exactly the bug class that shipped twice during implementation.
                    std::cout << "[ComboShip] Shared " << def.key << ": MM pool has none of '" << def.mmName
                              << "', not shared\n";
                    continue;
                }
                effectiveSharedMask |= (1u << i);
                std::cout << "[ComboShip] Shared " << def.key << ": trimmed " << removed << " MM '" << def.mmName
                          << "' (balancer will pad with junk)\n";
            }
            // Keep only the families that ended up effective (a bail-out above may follow the push).
            std::erase_if(sharedStartingMm, [&](const std::string& mmName) {
                for (int i = 0; i < SF_COUNT; ++i)
                    if ((effectiveSharedMask & (1u << i)) && mmName == SharedFamilyByIndex(i).mmName)
                        return false;
                return true;
            });
            // MM logic owns these from the start (the OOT oracle already applies OOT's own starting kit).
            mmForcedOwned.insert(mmForcedOwned.end(), sharedStartingMm.begin(), sharedStartingMm.end());
        }

        // --- Balance each game's pool against its checks: P_g == C_g ---
        // Both generators over-supply on purpose and the over-supply is PROGRESSION, so only junk may be
        // sacrificed. RNG-free and ahead of `rng`, so it can't shift the stream. See deviations/rando.md.
        {
            auto countFor = [](const std::vector<CwItem>& v, Game g) {
                return static_cast<size_t>(
                    std::count_if(v.begin(), v.end(), [g](const CwItem& i) { return i.game == g; }));
            };
            std::string balanceLine;
            std::vector<std::string> trimNotes;
            for (Game g : { GAME_OOT, GAME_MM }) {
                const char* tag = (g == GAME_OOT ? "oot" : "mm");
                const size_t checkCount = static_cast<size_t>(
                    std::count_if(allChecks.begin(), allChecks.end(), [g](const CwCheck& c) { return c.game == g; }));
                const size_t itemCount = countFor(advItems, g) + countFor(junkItems, g);
                size_t trimmed = 0, padded = 0;

                if (itemCount > checkCount) {
                    // Most-duplicated discardable name first, (count desc, name asc) — a total order, so
                    // hash iteration order can't leak in. Sheds bulk filler, keeps the last of rarer junk.
                    const size_t surplus = itemCount - checkCount;
                    std::unordered_map<std::string, int> avail;
                    for (const auto& i : junkItems)
                        if (i.game == g && i.discardable())
                            ++avail[i.name];
                    std::unordered_map<std::string, int> remove;
                    for (size_t k = 0; k < surplus; ++k) {
                        const std::string* best = nullptr;
                        int bestCount = 0;
                        for (const auto& [n, c] : avail) {
                            if (c <= 0)
                                continue;
                            if (!best || c > bestCount || (c == bestCount && n < *best)) {
                                best = &n;
                                bestCount = c;
                            }
                        }
                        if (!best) {
                            const std::string error = std::string("cannot balance the ") + tag +
                                                      " pool: " + std::to_string(surplus - k) +
                                                      " surplus item(s) left and no discardable junk remains; only "
                                                      "junk may be discarded, so refusing to drop anything else";
                            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << error << "\n";
                            return error;
                        }
                        --avail[*best];
                        ++remove[*best];
                    }
                    for (const auto& [n, c] : remove)
                        trimNotes.push_back(std::string(tag) + " junk: " + n + " x" + std::to_string(c));
                    // Single pass: drop exactly `remove[name]` instances of each chosen junk name.
                    std::vector<CwItem> kept;
                    kept.reserve(junkItems.size());
                    for (auto& i : junkItems) {
                        auto it = (i.game == g && i.discardable()) ? remove.find(i.name) : remove.end();
                        if (it != remove.end() && it->second > 0) {
                            --it->second;
                            ++trimmed;
                            continue;
                        }
                        kept.push_back(std::move(i));
                    }
                    junkItems = std::move(kept);
                } else if (itemCount < checkCount) {
                    // Clone one of THIS game's junk items so no check goes itemless. Never clone the other
                    // game's: the name wouldn't resolve in this game's apply path (a silent empty check).
                    const size_t deficit = checkCount - itemCount;
                    const CwItem* src = nullptr;
                    for (const auto& i : junkItems)
                        if (i.game == g && i.discardable()) {
                            src = &i;
                            break;
                        }
                    if (!src) {
                        const std::string error = std::string("cannot balance the ") + tag +
                                                  " pool: " + std::to_string(deficit) + " check(s) short and no " +
                                                  tag + " junk to clone";
                        std::cerr << "[ComboShip] CrossWorldCombinedFill: " << error << "\n";
                        return error;
                    }
                    CwItem filler = *src; // copy before push_back (may reallocate)
                    for (size_t k = 0; k < deficit; ++k)
                        junkItems.push_back(filler);
                    padded = deficit;
                    std::cout << "[ComboShip] CrossWorldCombinedFill: " << tag << " pool was " << deficit
                              << " item(s) short of its checks; padded with junk ('" << filler.name
                              << "') — benign if a forced placement reserved an item, else the dump under-filled\n";
                }

                trimmedTotal += trimmed;
                paddedTotal += padded;
                balanceLine += std::string(balanceLine.empty() ? "" : ", ") + tag +
                               " items=" + std::to_string(countFor(advItems, g) + countFor(junkItems, g)) +
                               "/checks=" + std::to_string(checkCount) + " (trimmed " + std::to_string(trimmed) +
                               ", padded " + std::to_string(padded) + ")";
            }
            std::cout << "[ComboShip] CrossWorldCombinedFill: pool balance — " << balanceLine << " (forced "
                      << forcedPlacements.size() << ")\n";
            for (const auto& n : trimNotes)
                std::cout << "[ComboShip] CrossWorldCombinedFill:     trimmed " << n << "\n";
        }

        return "";
    }

    // One oracle search over `ownedItems`, timed into that oracle's stats.
    std::unordered_set<std::string> Query(const OracleFns& oracle, const std::vector<std::string>& ownedItems) {
        CwQueryStats& stats = (&oracle == &ootOracle) ? ootStats : mmStats;
        auto t0 = std::chrono::steady_clock::now();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& n : ownedItems)
            arr.push_back(n);
        oracle.Reset();
        oracle.SetOwnedItems(arr.dump().c_str());
        std::string raw = oracle.GetReachableChecks();
        std::unordered_set<std::string> out;
        try {
            auto parsed = nlohmann::json::parse(raw);
            for (const auto& name : parsed)
                out.insert(name.get<std::string>());
        } catch (...) {}
        stats.count++;
        stats.ms +=
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        return out;
    }

    // Credits a placement only once its check is reachable — ReachableFixpoint's rule, so this can't certify what
    // it rejects.
    CwOotClosedResult OotClosedFixpoint(const std::vector<CwPlacement>& pl,
                                        const std::vector<std::string>& extraOwned) {
        using OotClosedResult = CwOotClosedResult;
        auto queryReachable = [&](const OracleFns& oracle, const std::vector<std::string>& owned) {
            return Query(oracle, owned);
        };
        std::vector<std::string> owned = ootForcedOwned;
        owned.insert(owned.end(), extraOwned.begin(), extraOwned.end());
        std::vector<bool> credited(pl.size(), false);
        OotClosedResult r;
        for (;;) {
            r.reach = queryReachable(ootOracle, owned);
            r.portalOpen = r.portalOpen || ootOracle.GetPortalOpen == nullptr || ootOracle.GetPortalOpen() != 0;
            bool changed = false;
            for (size_t i = 0; i < pl.size(); ++i) {
                if (credited[i] || pl[i].check.game != GAME_OOT || pl[i].item.game != GAME_OOT)
                    continue;
                if (r.reach.count(pl[i].check.name)) {
                    owned.push_back(pl[i].item.name);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                return r;
        }
    }

    // Cross-game sphere-collect fixpoint: starting from the per-game base sets (unplaced
    // advancement items), repeatedly query both oracles and credit any prior placement whose
    // CHECK became reachable to the ITEM's game's owned set, until nothing changes. This spans
    // both games because an MM item at an OOT check (or vice versa) can open progress anywhere,
    // including the portal itself — so portal openness is re-evaluated every iteration.
    // `placements` are the placements made so far: the fill passes its own, the oracle only the confined ones.
    CwFixResult ReachableFixpoint(const std::vector<CwPlacement>& placements, const std::vector<std::string>& ootBase,
                                  const std::vector<std::string>& mmBase) {
        auto queryReachable = [&](const OracleFns& oracle, const std::vector<std::string>& owned) {
            return Query(oracle, owned);
        };
        std::vector<std::string> ootOwned = ootBase, mmOwned = mmBase;
        // Forced placements (Link's Pocket etc.) are granted at save creation → owned from the start.
        ootOwned.insert(ootOwned.end(), ootForcedOwned.begin(), ootForcedOwned.end());
        mmOwned.insert(mmOwned.end(), mmForcedOwned.begin(), mmForcedOwned.end());
        std::vector<bool> credited(placements.size(), false);
        std::unordered_set<std::string> ootReachable, mmReachable;
        // Latched: once OOT can reach the portal it stays open. An MM start roots MM immediately (#135), and so
        // does a solo seed: each game is filled from its own start, as nothing crosses between them.
        bool portalOpen = !portalGated || mmStart || solo;
        // Shared Items mirror: how many copies of each effective family have already been pushed onto
        // mmOwned this call, so re-querying doesn't duplicate them as ootOwned grows.
        size_t sharedGiven[SF_COUNT] = { 0 };
        PooledMirrorState pooledGiven; // pooled families (bottles): both directions
        // Cross-game teleport songs: what each side can do for the other, latched.
        CrossWarpLatch crossLatch;
        CrossEntranceLatch doorLatch; // cross-game entrances (age-tagged arrivals)
        for (;;) {
            ootReachable = queryReachable(ootOracle, ootOwned);
            const uint32_t ootCrossOut = OracleCrossOut(ootOracle);
            const std::vector<std::string> ootDoors = ReadOotCrossExits(); // read right after the OOT search
            // Read the portal off THIS OOT query, before any MM check is credited below — that ordering
            // is what stops the fill proving the portal with an item that lives behind it.
            if (!portalOpen)
                portalOpen = ootOracle.GetPortalOpen() != 0;
            if (effectiveSharedMask != 0) {
                for (int i = 0; i < SF_COUNT; ++i) {
                    if (!(effectiveSharedMask & (1u << i)))
                        continue;
                    const auto& def = SharedFamilyByIndex(i);
                    if (!def.mmHasItem || def.pooled)
                        continue; // nothing to push / pooled families mirror below
                    size_t k =
                        static_cast<size_t>(std::count(ootOwned.begin(), ootOwned.end(), std::string(def.ootName)));
                    size_t target = std::min<size_t>(k, static_cast<size_t>(def.mmTierCap));
                    for (size_t n = sharedGiven[i]; n < target; ++n)
                        mmOwned.push_back(def.mmName);
                    sharedGiven[i] = target;
                }
                ApplyPooledMirror(effectiveSharedMask, ootOwned, mmOwned, pooledGiven);
            }
            // Cross-game entrances: Termina can be entered through a crossed door before the portal opens.
            const bool mmQueried = portalOpen || HasMmCrossArrival(mmOwned);
            mmReachable = !mmQueried   ? std::unordered_set<std::string>{}
                          : portalOpen ? queryReachable(mmOracle, mmOwned)
                                       : queryReachable(mmOracle, MmOwnedNoRoot(mmOwned));
            const std::vector<std::string> mmDoors = mmQueried ? ReadMmCrossExits() : std::vector<std::string>{};
            // Push each side's new warp abilities onto the other's owned list and repeat.
            // A solo seed never crosses between the games: no teleport songs into the other one.
            bool changed = !solo && ApplyCrossWarps(ootCrossOut, portalOpen ? OracleCrossOut(mmOracle) : 0u, portalOpen,
                                                    mmStart, ootOwned, mmOwned, crossLatch);
            if (!solo)
                changed |= ApplyCrossEntrances(ootDoors, mmDoors, ootOwned, mmOwned, doorLatch);
            for (size_t i = 0; i < placements.size(); ++i) {
                if (credited[i])
                    continue;
                const auto& reach = placements[i].check.game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(placements[i].check.name)) {
                    auto& owned = placements[i].item.game == GAME_OOT ? ootOwned : mmOwned;
                    owned.push_back(placements[i].item.name);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                return { std::move(ootReachable), std::move(mmReachable), std::move(ootOwned), crossLatch };
        }
    }
};

} // namespace ComboRando
