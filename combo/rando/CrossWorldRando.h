// combo/rando/CrossWorldRando.h
// ComboShip: combined cross-world randomizer generator.
// Header-only, deterministic. No game source touched. The pools, the oracle table and the reach fixpoint live in
// CrossWorldSession.h.
//
// Phase 1 (no-logic, native-only): permutation of each game's vanilla items.
// Phase 2 (combined-logic): assumed fill over the union of both games' pools,
//   joined by the portal, driving per-game reachability oracles.

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
#include <functional>
#include <nlohmann/json.hpp>
#include "gui/ComboGenProgress.h"
#include "CrossWorldSession.h"

namespace ComboRando {

// ---------- Combined assumed fill ----------

struct CombinedFillResult {
    std::string spoilerJson;
    bool success;
    std::string error;
};

// Retry budgets. Pass counts, never wall-clock: a time limit would make success machine-dependent and
// break seed sharing. kFillAttempts is shared with ComboShip.cpp/ComboRandoHeadless.cpp — headless seeds
// only reproduce in-game ones while both loops agree.
// Measured: 25 seeds over 3 adult/Overworld-Keys configs all succeeded on pass 1 with 0 retries, so 6
// total passes is ~6x headroom while capping the worst-case wait (15 passes was ~3 min).
constexpr int kFillAttempts = 2; // whole-fill retries (re-derives the seed, re-rolls the dumps)
// full-coop-sync §B1: with entrances shuffled, an attempt can fail on logic the any-items connectivity check
// can't see (a door that needs Deku to use, a dungeon behind its own item). Each attempt reshuffles, so allow
// more of them. Only used when entrance randomization is on; plain seeds keep kFillAttempts.
constexpr int kEntranceFillAttempts = 12;
constexpr int kMaxPasses = 3;      // passes within one fill
constexpr int kMaxPrereqTries = 4; // Tier-1 repicks of just the portal prerequisites

// MM resolves its junk placeholder at pickup from finalSeed + the MM check id, and a check in OOT has none, so a
// cross-placed one is baked here into a real item from MM's own rotation set; every surface then agrees (see the
// deviations doc). Own RNG stream, seeded per check: never draws from the fill's, never depends on order.
inline void BakeCrossPlacedMmJunk(CrossWorldSession& session, std::vector<CwPlacement>& placements) {
    std::vector<std::string>& mmJunkNames = session.mmJunkNames;
    const uint32_t masterSeed = session.masterSeed;
    std::sort(mmJunkNames.begin(), mmJunkNames.end());
    mmJunkNames.erase(std::unique(mmJunkNames.begin(), mmJunkNames.end()), mmJunkNames.end());
    size_t unbaked = 0;
    for (auto& p : placements) {
        if (p.check.game != GAME_OOT || p.item.game != GAME_MM || p.item.name != kMmJunkName)
            continue;
        if (mmJunkNames.empty()) {
            ++unbaked;
            continue;
        }
        CwRng jr((static_cast<uint64_t>(masterSeed) << 32) ^ CwHashName(p.check.name) ^ 0x4A554E4BULL);
        p.item.name = mmJunkNames[jr.below(static_cast<uint32_t>(mmJunkNames.size()))];
    }
    if (unbaked > 0)
        std::cout << "[ComboShip] junk bake skipped for " << unbaked
                  << " cross placements: 2ship.dll reported no junkPool" << std::endl;
}

// The fill's spoiler, same shape as the no-logic generator: every placement by game, the cross-game markers and what
// the pools looked like. `mode` names who made the placements.
inline nlohmann::json BuildFillSpoiler(const CrossWorldSession& session, const std::vector<CwPlacement>& placements,
                                       int passesUsed, const char* mode = "combined-logic assumed-fill") {
    const uint32_t masterSeed = session.masterSeed;
    const bool mmStart = session.mmStart;
    const auto& advItems = session.advItems;
    const auto& junkItems = session.junkItems;
    const auto& allChecks = session.allChecks;
    const auto& forcedPlacements = session.forcedPlacements;
    const uint32_t effectiveSharedMask = session.effectiveSharedMask;
    const auto& sharedStartingMm = session.sharedStartingMm;
    const size_t trimmedTotal = session.trimmedTotal, paddedTotal = session.paddedTotal;
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = mode;
    spoiler["startingGame"] = mmStart ? "MM" : "OOT"; // #135
    // poolTrimmed/poolPadded let a shipped spoiler self-report that it came from a balanced fill.
    spoiler["fillStats"] = { { "advancementItems", static_cast<uint32_t>(advItems.size()) },
                             { "junkItems", static_cast<uint32_t>(junkItems.size()) },
                             { "poolTrimmed", static_cast<uint32_t>(trimmedTotal) },
                             { "poolPadded", static_cast<uint32_t>(paddedTotal) },
                             { "checks", static_cast<uint32_t>(allChecks.size()) },
                             { "passes", passesUsed } };
    spoiler["sharedItems"] = SharedKeysFromMask(effectiveSharedMask);
    if (!sharedStartingMm.empty())
        spoiler["sharedStartingMm"] = sharedStartingMm;

    nlohmann::json ootPlacements = nlohmann::json::object();
    nlohmann::json mmPlacements = nlohmann::json::object();
    nlohmann::json foreignMarkers = nlohmann::json::array();

    for (const auto& p : placements) {
        if (p.check.game == GAME_OOT) {
            ootPlacements[p.check.name] = p.item.name;
        } else {
            mmPlacements[p.check.name] = p.item.name;
        }
        if (p.check.game != p.item.game) {
            nlohmann::json marker = { { "checkGame", p.check.game == GAME_OOT ? "oot" : "mm" },
                                      { "checkName", p.check.name },
                                      { "itemGame", p.item.game == GAME_OOT ? "oot" : "mm" },
                                      { "itemName", p.item.name },
                                      // Propagate advancement so foreign checks holding an important item
                                      // still play the held-up pickup animation (else defaulted to junk).
                                      { "advancement", p.item.advancement } };
            // Native item category, so CMC/CSMC can dress the container as the real item instead of
            // the junk sentinel. UNKNOWN is omitted so consumers use the advancement fallback.
            if (p.item.cat != CwCat::UNKNOWN)
                marker["category"] = CwCatName(p.item.cat);
            foreignMarkers.push_back(std::move(marker));
        }
    }

    spoiler["ootCount"] = static_cast<uint32_t>(ootPlacements.size());
    spoiler["oot"] = ootPlacements;
    spoiler["mmCount"] = static_cast<uint32_t>(mmPlacements.size());
    spoiler["mm"] = mmPlacements;
    spoiler["foreign"] = foreignMarkers;
    // Forced placements (e.g. Link's Pocket) are owned at start, so hints must never target them —
    // the dump's fixed[] skips forced checks, so the spoiler names them for CrossHints instead.
    nlohmann::json startKnown = nlohmann::json::array();
    for (const auto& fp : forcedPlacements)
        startKnown.push_back(
            { { "checkGame", fp.check.game == GAME_OOT ? "oot" : "mm" }, { "checkName", fp.check.name } });
    spoiler["startKnown"] = std::move(startKnown);
    return spoiler;
}

// MM access is gated on the OOT->MM portal region (Happy Mask Shop, or the Lost Woods hub in a seed with the Lost
// Woods link) via ootOracle.GetPortalOpen.
// progress: optional thread-safe progress struct polled by the UI. May be nullptr.
// forcedOotJson: OOT checks the dump can't carry (e.g. Link's Pocket). Each forced item is reserved
// out of the cross pool, owned-from-start for logic, and appended to the OOT placements.
// startingGame (#135): GAME_MM roots MM from the start instead of behind the portal; the portal
// prerequisites are still derived, as the re-entry guarantee for a player who strays into OOT.
// games (ComboGames.h): a solo seed keeps every item in its own game (both phases pick only checks of the item's
// game), uses no cross-game teleports in logic, and skips the portal prerequisites / re-entry guarantee, since
// nothing crosses between the games at runtime.
inline CombinedFillResult
CrossWorldCombinedFill(const std::string& sohDumpJson, const std::string& mmDumpJson, uint32_t masterSeed,
                       const OracleFns& ootOracle, const OracleFns& mmOracle,
                       ComboRando::ComboGenProgress* progress = nullptr, const std::string& forcedOotJson = "",
                       OotAccess ootAccess = OotAccess::ALL_REACHABLE, CwGoal goal = {}, GameId startingGame = GAME_OOT,
                       uint32_t sharedMask = 0, ComboGames games = COMBO_GAMES_BOTH) {
    CombinedFillResult result;
    result.success = false;
    CrossWorldSession session(ootOracle, mmOracle, masterSeed, ootAccess, goal, startingGame, sharedMask, games);
    const bool solo = session.solo;
    const bool portalGated = session.portalGated;
    const bool mmStart = session.mmStart;

    if (progress)
        progress->phase.store(1); // Preparing pools

    result.error = session.Prepare(sohDumpJson, mmDumpJson, forcedOotJson);
    if (!result.error.empty())
        return result;
    auto& advItems = session.advItems;
    auto& junkItems = session.junkItems;
    auto& lockedPlacements = session.lockedPlacements;
    auto& forcedPlacements = session.forcedPlacements;
    auto& trimmedTotal = session.trimmedTotal;
    auto& paddedTotal = session.paddedTotal;
    auto& ootStats = session.ootStats;
    auto& mmStats = session.mmStats;

    // The fill places into a shuffled copy; the session keeps the dump's order.
    std::vector<CwCheck> allChecks = session.allChecks;
    CwRng rng(masterSeed);
    cwShuffle(allChecks, rng);

    // OOT and MM check names are distinct namespaces; key fill bookkeeping by game+name.
    auto checkKey = [](const CwCheck& c) { return std::string(c.game == GAME_OOT ? "oot:" : "mm:") + c.name; };

    // Locked (fixed) checks are each game's OWN vanilla/confined placements, already guaranteed
    // reachable by that game's own fill; the cross-fill neither shuffles nor re-validates them (the
    // oracles under-model some — e.g. vanilla GS spots — so they'd falsely fail validation).
    std::unordered_set<std::string> lockedCheckKeys;
    for (const auto& lp : lockedPlacements)
        lockedCheckKeys.insert(checkKey(lp.check));

    auto ootClosedFixpoint = [&](const std::vector<CwPlacement>& pl, const std::vector<std::string>& extraOwned) {
        return session.OotClosedFixpoint(pl, extraOwned);
    };

    // Portal prerequisites: a SUFFICIENT witness set for the OOT->MM portal, built FORWARD from empty.
    // RNG-free (canonical order) so it can't shift the seeded stream. See docs/deviations/rando.md.
    std::vector<std::string> portalPrereqs;
    if (portalGated && !solo) {
        // Each probe is a fixpoint (several queries), so this bounds queries, not probes.
        const int kMaxDeriveQueries = 1500;
        const uint32_t queries0 = ootStats.count;
        int used = 0;
        auto portalOpenWith = [&](const std::vector<std::string>& owned) {
            bool open = ootClosedFixpoint(lockedPlacements, owned).portalOpen;
            used = static_cast<int>(ootStats.count - queries0);
            return open;
        };
        const std::vector<std::string> base; // free pool only; locked items earn their way in by reach
        std::vector<std::string> rest;
        for (const auto& it : advItems)
            if (it.game == GAME_OOT)
                rest.push_back(it.name);
        std::sort(rest.begin(), rest.end());

        std::vector<std::string> witness;
        auto ownedWith = [&](size_t k) {
            std::vector<std::string> o = base;
            o.insert(o.end(), witness.begin(), witness.end());
            o.insert(o.end(), rest.begin(), rest.begin() + k);
            return o;
        };
        if (portalOpenWith(base)) {
            std::cout << "[ComboShip] CrossWorldCombinedFill: portal open from the start — no prerequisites\n";
        } else if (!portalOpenWith(ownedWith(rest.size()))) {
            // Tier 3: warn loudly, try anyway — a derivation bug must not block a config that generates.
            std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — the OOT->MM portal looks unreachable even "
                         "with every OOT item owned; generating anyway, MM may end up unreachable\n";
        } else {
            // Bisect `rest` for the item that flips the portal, add it, drop everything after it, repeat.
            while (used < kMaxDeriveQueries && !rest.empty()) {
                size_t lo = 0, hi = rest.size(); // portal closed at lo, open at hi
                while (hi - lo > 1 && used < kMaxDeriveQueries) {
                    size_t mid = lo + (hi - lo) / 2;
                    if (portalOpenWith(ownedWith(mid)))
                        hi = mid;
                    else
                        lo = mid;
                }
                witness.push_back(rest[hi - 1]);
                rest.resize(hi - 1);
                if (portalOpenWith(ownedWith(0)))
                    break;
            }
            if (used >= kMaxDeriveQueries) {
                std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — portal prerequisite derivation exceeded "
                             "its "
                          << kMaxDeriveQueries << "-query budget; falling back to an unconstrained fill\n";
            } else {
                portalPrereqs = witness;
                std::cout << "[ComboShip] CrossWorldCombinedFill: portal prerequisites (" << portalPrereqs.size()
                          << ", " << used << " queries):";
                for (const auto& n : portalPrereqs)
                    std::cout << " " << n;
                std::cout << "\n";
            }
        }
    }

    std::vector<CwPlacement> placements;
    auto reachableFixpoint = [&](const std::vector<std::string>& ootBase, const std::vector<std::string>& mmBase) {
        return session.ReachableFixpoint(placements, ootBase, mmBase);
    };

    if (progress) {
        progress->phase.store(2);                                 // Placing items
        progress->total.store(static_cast<int>(advItems.size())); // tracked phase = advancement fill
        progress->placed.store(0);
    }

    // --- Assumed fill of advancement items, then junk fast-fill; retry whole passes on dead
    // ends or failed validation (deterministic: one rng stream continues across passes) ---
    std::unordered_set<std::string> filledChecks; // checkKey()-keyed
    bool fillOk = false;
    int passesUsed = 0;
    std::string lastPassError; // names the terminal cause instead of a generic "fill failed"

    // Expected post-balance multiset, for the per-pass backstop below: every pool item must reach
    // `placements`. This is the assertion that catches item loss from ANY cause, not just truncation.
    std::unordered_map<std::string, int> expectedItems;
    struct ExpectedInfo {
        CwCat cat;
        bool discardable;
    };
    std::unordered_map<std::string, ExpectedInfo> expectedInfo;
    for (const auto* src : { &advItems, &junkItems })
        for (const auto& i : *src) {
            const std::string key = std::string(i.game == GAME_OOT ? "oot:" : "mm:") + i.name;
            ++expectedItems[key];
            expectedInfo[key] = { i.cat, i.discardable() };
        }

    for (int pass = 1; pass <= kMaxPasses && !fillOk; ++pass) {
        if (GenCancelled(progress)) {
            result.error = "cancelled";
            return result;
        }
        passesUsed = pass;
        auto passStart = std::chrono::steady_clock::now();
        auto passMs = [&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - passStart)
                .count();
        };
        placements.clear();
        filledChecks.clear();

        // Seed confined pre-placements: locked (never re-filled) and credited to logic when their
        // check is reached, via reachableFixpoint — the collected-in-place semantics for dungeon keys.
        placements = lockedPlacements;
        for (const auto& lp : lockedPlacements)
            filledChecks.insert(checkKey(lp.check));

        // Phase A0: portal prerequisites FIRST, into portal-closed-reachable checks, each chosen RANDOMLY
        // across the whole valid set (variety comes from the choice, not the ordering).
        std::vector<CwItem> advRest = advItems;
        bool prereqOk = portalPrereqs.empty();
        for (int t = 1; t <= kMaxPrereqTries && !prereqOk; ++t) {
            if (GenCancelled(progress)) {
                result.error = "cancelled";
                return result;
            }
            placements = lockedPlacements;
            filledChecks.clear();
            for (const auto& lp : lockedPlacements)
                filledChecks.insert(checkKey(lp.check));
            advRest = advItems;
            std::vector<CwItem> prereq; // one pool instance per witness name
            for (const auto& name : portalPrereqs) {
                for (size_t i = 0; i < advRest.size(); ++i) {
                    if (advRest[i].game == GAME_OOT && advRest[i].name == name) {
                        prereq.push_back(advRest[i]);
                        advRest.erase(advRest.begin() + i);
                        break;
                    }
                }
            }
            cwShuffle(prereq, rng);
            bool placedAll = true;
            for (const auto& item : prereq) {
                auto fr = ootClosedFixpoint(placements, {});
                std::vector<size_t> cands;
                for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                    if (allChecks[ci].game != GAME_OOT || filledChecks.count(checkKey(allChecks[ci])))
                        continue;
                    if (fr.reach.count(allChecks[ci].name))
                        cands.push_back(ci);
                }
                if (cands.empty()) {
                    placedAll = false;
                    break;
                }
                size_t pick = cands[rng.below(static_cast<uint32_t>(cands.size()))];
                placements.push_back({ allChecks[pick], item });
                filledChecks.insert(checkKey(allChecks[pick]));
            }
            prereqOk = placedAll && ootClosedFixpoint(placements, {}).portalOpen;
            if (!prereqOk)
                std::cerr << "[ComboShip] CrossWorldCombinedFill: portal still closed after prerequisite placement "
                             "(pass "
                          << pass << ", try " << t << "/" << kMaxPrereqTries << ") — repicking\n";
        }
        if (!prereqOk) {
            lastPassError = "the OOT->MM portal's prerequisites could not be placed within reach of the start";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << lastPassError << " (pass " << pass << ", "
                      << passMs() << " ms) — retrying\n";
            continue;
        }

        // Phase A: place advancement items with logic, assuming only UNPLACED ones are owned. NO_LOGIC
        // assumed-fills only MM adv (OOT adv rides Phase B's fast fill); other modes keep advItems as-is.
        std::vector<CwItem> toPlace;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advRest)
                if (it.game == GAME_MM)
                    toPlace.push_back(it);
        } else {
            toPlace = advRest;
        }
        std::vector<CwItem> relaxedToJunk; // OOT adv stranded off-path under BEATABLE_ONLY dead-ends
        cwShuffle(toPlace, rng);
        std::vector<std::string> ootRemaining, mmRemaining;
        for (const auto& it : toPlace)
            (it.game == GAME_OOT ? ootRemaining : mmRemaining).push_back(it.name);

        // Batch placement: pull K items and place all K into distinct reachable checks off ONE
        // fixpoint — still a strictly-conservative assumed fill (each lands on a check reachable
        // without itself and the rest of its batch), but divides the fixpoint cost by K. The cap
        // halves on a failed batch (never grows back within a pass); only a failed K=1 ends the pass.
        size_t batchCap = 16;
        auto removeOne = [](std::vector<std::string>& v, const std::string& name) {
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == name) {
                    v[i] = v.back();
                    v.pop_back();
                    return;
                }
            }
        };

        bool deadEnd = false;
        while (!toPlace.empty()) {
            if (GenCancelled(progress)) {
                result.error = "cancelled";
                return result;
            }
            size_t k = std::min({ batchCap, std::max<size_t>(1, toPlace.size() / 4), toPlace.size() });

            std::vector<CwItem> batch;
            batch.reserve(k);
            for (size_t i = 0; i < k; ++i) {
                batch.push_back(toPlace.back());
                toPlace.pop_back();
                removeOne(batch.back().game == GAME_OOT ? ootRemaining : mmRemaining, batch.back().name);
            }

            auto fr = reachableFixpoint(ootRemaining, mmRemaining);
            auto& ootReachable = fr.ootReachable;
            auto& mmReachable = fr.mmReachable;

            std::vector<size_t> candidates;
            for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                if (filledChecks.count(checkKey(allChecks[ci])))
                    continue;
                const auto& reach = allChecks[ci].game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(allChecks[ci].name))
                    candidates.push_back(ci);
            }
            // Solo: each item only takes a check of its own game, so each game needs room for its part of the batch.
            bool soloShort = false;
            if (solo) {
                size_t need[2] = { 0, 0 }, have[2] = { 0, 0 };
                for (const auto& item : batch)
                    ++need[item.game == GAME_OOT ? 0 : 1];
                for (size_t ci : candidates)
                    ++have[allChecks[ci].game == GAME_OOT ? 0 : 1];
                soloShort = have[0] < need[0] || have[1] < need[1];
            }

            if (candidates.size() < batch.size() || soloShort) {
                // BEATABLE_ONLY: a single OOT advancement item with no reachable check may strand
                // off-path — move it to the junk fast-fill and continue (already removed from
                // toPlace/ootRemaining above). MM items and ALL_REACHABLE always retry instead.
                if (batch.size() == 1 && ootAccess == OotAccess::BEATABLE_ONLY && batch.front().game == GAME_OOT) {
                    relaxedToJunk.push_back(batch.front());
                    continue;
                }
                // Put the batch back (reverse order restores the pop sequence — deterministic).
                for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                    (it->game == GAME_OOT ? ootRemaining : mmRemaining).push_back(it->name);
                    toPlace.push_back(*it);
                }
                if (batch.size() > 1) {
                    batchCap = batch.size() / 2; // too conservative at this depth — shrink
                    continue;
                }
                lastPassError = "dead end placing '" + batch.front().name + "' (no reachable check left for it)";
                std::cerr << "[ComboShip] CrossWorldCombinedFill: dead end placing '" << batch.front().name
                          << "' (pass " << pass << ", " << placements.size() << " placed, " << passMs()
                          << " ms) — retrying\n";
                deadEnd = true;
                break;
            }

            for (const auto& item : batch) {
                if (solo) {
                    // Same draw over the item's own game's candidates (checked above to be non-empty).
                    std::vector<size_t> own;
                    for (size_t s = 0; s < candidates.size(); ++s)
                        if (allChecks[candidates[s]].game == item.game)
                            own.push_back(s);
                    size_t slot = own[rng.below(static_cast<uint32_t>(own.size()))];
                    size_t pick = candidates[slot];
                    candidates[slot] = candidates.back();
                    candidates.pop_back();
                    placements.push_back({ allChecks[pick], item });
                    filledChecks.insert(checkKey(allChecks[pick]));
                    continue;
                }
                size_t slot = rng.below(static_cast<uint32_t>(candidates.size()));
                size_t pick = candidates[slot];
                candidates[slot] = candidates.back();
                candidates.pop_back();
                placements.push_back({ allChecks[pick], item });
                filledChecks.insert(checkKey(allChecks[pick]));
            }
            if (progress)
                // Count advancement placed (placements started at lockedPlacements), so placed/total is
                // an honest 0..100% over the advancement fill — not locked+adv+junk over adv alone.
                progress->placed.store(static_cast<int>(placements.size() - lockedPlacements.size()));
        }
        if (deadEnd)
            continue;

        // Phase B: fast-fill the remaining checks — zero oracle calls. Holds junk AND relaxed OOT
        // advancement, placed without logic and tolerated-if-unreachable by the validation below.
        std::vector<CwItem> fastFillItems = junkItems;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advRest)
                if (it.game == GAME_OOT)
                    fastFillItems.push_back(it);
        }
        fastFillItems.insert(fastFillItems.end(), relaxedToJunk.begin(), relaxedToJunk.end());
        cwShuffle(fastFillItems, rng);
        // Advancement to the front so the truncatable tail is junk by construction. stable_partition is
        // RNG-free, so the shuffle above still decides placement; only adv/junk precedence is forced.
        auto dropFrom = std::stable_partition(fastFillItems.begin(), fastFillItems.end(),
                                              [](const CwItem& i) { return i.advancement; });
        const size_t mustPlace = static_cast<size_t>(dropFrom - fastFillItems.begin());
        size_t ji = 0;
        if (solo) {
            // Each game's checks take that game's items, in the same order. The balancer made items == checks per
            // game, so this places everything; whatever is left is moved to the tail for the arithmetic check below.
            std::vector<CwItem> perGame[2];
            for (const auto& it : fastFillItems)
                perGame[it.game == GAME_OOT ? 0 : 1].push_back(it);
            size_t next[2] = { 0, 0 };
            std::vector<CwItem> placedOrder;
            for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                if (filledChecks.count(checkKey(allChecks[ci])))
                    continue;
                const int g = allChecks[ci].game == GAME_OOT ? 0 : 1;
                if (next[g] >= perGame[g].size())
                    continue;
                placements.push_back({ allChecks[ci], perGame[g][next[g]] });
                placedOrder.push_back(perGame[g][next[g]++]);
                filledChecks.insert(checkKey(allChecks[ci]));
            }
            ji = placedOrder.size();
            for (int g = 0; g < 2; ++g)
                placedOrder.insert(placedOrder.end(), perGame[g].begin() + next[g], perGame[g].end());
            fastFillItems = std::move(placedOrder);
        } else {
            for (size_t ci = 0; ci < allChecks.size() && ji < fastFillItems.size(); ++ci) {
                if (filledChecks.count(checkKey(allChecks[ci])))
                    continue;
                placements.push_back({ allChecks[ci], fastFillItems[ji++] });
                filledChecks.insert(checkKey(allChecks[ci]));
            }
        }
        // The balancer made items == checks per game, so ANY leftover or unfilled check is an
        // arithmetic bug — nothing is discarded here. Non-junk leftovers are named first: they are the
        // rule-1 violations, and mislabelling one as junk is how the original item loss stayed hidden.
        size_t unfilled = 0;
        for (const auto& c : allChecks)
            if (!filledChecks.count(checkKey(c)))
                ++unfilled;
        size_t leftoverBad = 0;
        std::string leftoverNames;
        for (size_t k = ji; k < fastFillItems.size(); ++k) {
            if (fastFillItems[k].discardable())
                continue;
            ++leftoverBad;
            if (leftoverNames.size() < 300)
                leftoverNames += (leftoverNames.empty() ? "" : ", ") + fastFillItems[k].name + " [" +
                                 (fastFillItems[k].game == GAME_OOT ? "oot/" : "mm/") +
                                 CwCatName(fastFillItems[k].cat) + "]";
        }
        if (ji < fastFillItems.size() || unfilled > 0) {
            // Layout-independent arithmetic, identical on every pass — bail to the caller's seed reroll
            // instead of burning kMaxPasses reproducing the same wrong answer.
            result.error = "pool/check imbalance — " + std::to_string(fastFillItems.size() - ji) +
                           " item(s) had no check left (" + std::to_string(leftoverBad) + " non-junk" +
                           (leftoverNames.empty() ? "" : ": " + leftoverNames) + "), " + std::to_string(unfilled) +
                           " check(s) unfilled, placed " + std::to_string(ji) + " of which " +
                           std::to_string(mustPlace) + " were must-place";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
            return result;
        }

        // Backstop: every pool item must have landed somewhere. O(n), no oracle calls, and ahead of the
        // validation fixpoint so a validation retry can't mask it. Non-JUNK residue is the headline.
        {
            auto residual = expectedItems;
            size_t unexpected = 0;
            for (const auto& p : placements) {
                if (lockedCheckKeys.count(checkKey(p.check)))
                    continue;
                auto it = residual.find(std::string(p.item.game == GAME_OOT ? "oot:" : "mm:") + p.item.name);
                if (it == residual.end())
                    ++unexpected; // placed an item that was never in the pool
                else
                    --it->second;
            }
            size_t missingBad = 0, missingJunk = 0, overPlaced = 0;
            std::string missingNames;
            for (const auto& [key, n] : residual) {
                if (n < 0) {
                    overPlaced += static_cast<size_t>(-n);
                    continue;
                }
                if (n == 0)
                    continue;
                // Classify by the same predicate the trim uses, so an advancement item that happens to
                // be category JUNK (OOT bombchus, Buy Blue Fire) is reported as a real loss, not filler.
                const auto info =
                    expectedInfo.count(key) ? expectedInfo.at(key) : ExpectedInfo{ CwCat::UNKNOWN, false };
                (info.discardable ? missingJunk : missingBad) += static_cast<size_t>(n);
                if (!info.discardable && missingNames.size() < 300)
                    missingNames += (missingNames.empty() ? "" : ", ") + key + " x" + std::to_string(n) + " [" +
                                    CwCatName(info.cat) + "]";
            }
            if (missingBad > 0 || missingJunk > 0 || overPlaced > 0 || unexpected > 0) {
                result.error = missingBad > 0
                                   ? std::to_string(missingBad) + " non-junk pool item(s) never placed: " + missingNames
                                   : std::to_string(missingJunk) + " junk never placed, " + std::to_string(overPlaced) +
                                         " over-placed, " + std::to_string(unexpected) +
                                         " not from the pool (balancer arithmetic bug)";
                std::cerr << "[ComboShip] CrossWorldCombinedFill: INVARIANT VIOLATED — " << result.error << "\n";
                return result;
            }
        }

        if (GenCancelled(progress)) {
            result.error = "cancelled";
            return result;
        }
        // Validation: with nothing assumed, sphere-collecting placed items must reach every
        // ADVANCEMENT check (the assumed-fill guarantee). Junk-holding checks may legitimately be
        // oracle-unreachable (oracles under-model, e.g. MM with zeroed save options) — count and log
        // those, don't fail.
        auto vf = reachableFixpoint({}, {});
        if ((vf.cross.ootIn & CW_OOT_IN_WARP_MASK) || vf.cross.mmIn) {
            std::cout << "[ComboShip] CrossWorldCombinedFill: cross-game teleport songs in logic — MM warp songs "
                      << "reaching OOT pads: mask 0x" << std::hex << (vf.cross.ootIn & CW_OOT_IN_WARP_MASK) << std::dec
                      << ", OOT Song of Soaring reaching MM owls: " << (vf.cross.mmIn ? "yes" : "no") << "\n";
        }
        auto& ootFinal = vf.ootReachable;
        auto& mmFinal = vf.mmReachable;
        // Classify unreachable advancement by ITEM game: MM adv unreachable is always fatal (MM must
        // stay all-reachable); OOT adv unreachable is fatal only under ALL_REACHABLE, tolerated under
        // the relaxed modes (it was placed off-logic on purpose).
        size_t mmAdvUnreachable = 0, ootAdvUnreachable = 0, junkUnreachable = 0, junkUnreachableOot = 0;
        for (const auto& p : placements) {
            // Locked placements are the game's own guaranteed-reachable vanilla/confined items — not
            // the cross-fill's responsibility, and the oracles under-model some. Don't validate them.
            if (lockedCheckKeys.count(checkKey(p.check)))
                continue;
            const auto& reach = p.check.game == GAME_OOT ? ootFinal : mmFinal;
            if (reach.count(p.check.name))
                continue;
            if (p.item.advancement) {
                (p.item.game == GAME_MM ? mmAdvUnreachable : ootAdvUnreachable)++;
            } else {
                ++junkUnreachable;
                if (p.check.game == GAME_OOT)
                    ++junkUnreachableOot;
            }
        }
        // BEATABLE_ONLY additionally requires the win still holds (mirrors DefaultGanonMajoraGoal;
        // duplicated here to keep this header free of ComboPlaythrough.h). NO_LOGIC accepts an
        // OOT-unbeatable seed; MM stays reachable+beatable regardless.
        // RC_GANON reachable = OOT beatable: its region logic gates on bridge + boss key (placed OR
        // force-granted), correct for every GBK/bridge mode — unlike the old tower-chest proxy (held a
        // random item, could be unreachable). Names are the friendly forms the OOT/MM oracles return.
        const bool ootWin = ootFinal.count("Ganon") > 0;
        const bool mmWin = mmFinal.count("Moon Majora Pot 01") > 0;
        // Triforce Hunt (#136): the win is `required` pieces from either pool, so count the pieces
        // sitting on reachable checks instead of the two boss markers.
        int reachablePieces = 0;
        if (goal.hunt) {
            for (const auto& p : placements) {
                if (CwIsTriforcePiece(p.item.game, p.item.name) &&
                    (p.check.game == GAME_OOT ? ootFinal : mmFinal).count(p.check.name)) {
                    ++reachablePieces;
                }
            }
        }
        bool goalOk = (ootAccess != OotAccess::BEATABLE_ONLY) ||
                      (goal.hunt ? reachablePieces >= goal.required : (ootWin && mmWin));
        bool needRetry = mmAdvUnreachable > 0 || (ootAccess == OotAccess::ALL_REACHABLE && ootAdvUnreachable > 0) ||
                         (ootAccess == OotAccess::BEATABLE_ONLY && !goalOk);
        if (needRetry) {
            // On UNBEATABLE, name which side blocked (ganon/majora) so a future win-marker rename or
            // logic regression is self-diagnosing instead of a silent all-passes-fail.
            std::string goalStr = ootAccess != OotAccess::BEATABLE_ONLY ? ""
                                  : goalOk                              ? " goal=ok"
                                  : goal.hunt ? " goal=UNBEATABLE(pieces=" + std::to_string(reachablePieces) + "/" +
                                                    std::to_string(goal.required) + ")"
                                              : " goal=UNBEATABLE(ganon=" + std::to_string(ootWin) +
                                                    " majora=" + std::to_string(mmWin) + ")";
            lastPassError = "validation failed — mmAdvUnreachable=" + std::to_string(mmAdvUnreachable) +
                            " ootAdvUnreachable=" + std::to_string(ootAdvUnreachable) + goalStr +
                            (mmAdvUnreachable > 0 ? " (MM items stranded: the portal closed mid-fill)" : "");
            std::cerr << "[ComboShip] CrossWorldCombinedFill: validation failed — mmAdvUnreachable=" << mmAdvUnreachable
                      << " ootAdvUnreachable=" << ootAdvUnreachable << goalStr << " (pass " << pass << ", " << passMs()
                      << " ms) — retrying\n";
            continue;
        }
        // ALL_REACHABLE covers every check, not just advancement ones: a junk-holding check the oracle
        // can't reach still violates it. Entrance shuffle is what makes this reachable in practice, and
        // it's layout-driven (the same seed strands the same checks under a completely different item
        // distribution), so re-placing items can't fix it — bail to the caller's seed reroll rather
        // than burn the remaining passes. MM is excluded: its oracle under-models 3 checks on every
        // seed, so enforcing there would fail everything.
        if (ootAccess == OotAccess::ALL_REACHABLE && junkUnreachableOot > 0) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << junkUnreachableOot
                      << " OOT check(s) unreachable under All Locations Reachable (pass " << pass
                      << ") — rerolling seed\n";
            result.error = "OOT All Locations Reachable violated (" + std::to_string(junkUnreachableOot) +
                           " unreachable checks) — reroll for a new entrance layout";
            return result;
        }
        // #135: MM start frees the OOT root, so mmAdvUnreachable can't catch a portal that A0's fallback
        // paths left unopenable. Check directly: a strayed itemless player must always be able to walk back in.
        if (mmStart && portalGated && !solo && !ootClosedFixpoint(placements, {}).portalOpen) {
            lastPassError = "MM start: the OOT->MM portal is not re-openable from an empty OOT start";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << lastPassError << " (pass " << pass << ", "
                      << passMs() << " ms) — retrying\n";
            continue;
        }
        if (ootAdvUnreachable > 0)
            std::cout << "[ComboShip] CrossWorldCombinedFill: " << ootAdvUnreachable
                      << " OOT advancement item(s) on unreachable checks — tolerated (relaxed OOT mode)\n";

        std::cout << "[ComboShip] CrossWorldCombinedFill: all " << advItems.size()
                  << " advancement items reachable from scratch (pass " << pass << ", " << passMs() << " ms; "
                  << junkItems.size() << " junk, " << junkUnreachable
                  << " junk on oracle-unreachable checks [oot=" << junkUnreachableOot
                  << " mm=" << (junkUnreachable - junkUnreachableOot) << "]; pool balanced: " << trimmedTotal
                  << " trimmed, " << paddedTotal << " padded)\n";
        std::cout << "[ComboShip] CrossWorldCombinedFill: oracle queries — oot " << ootStats.count << "x/"
                  << ootStats.ms << " ms, mm " << mmStats.count << "x/" << mmStats.ms << " ms\n";
        fillOk = true;
    }

    if (!fillOk) {
        result.error = "assumed fill failed after " + std::to_string(kMaxPasses) +
                       " passes; last cause: " + (lastPassError.empty() ? "unknown (see log)" : lastPassError);
        return result;
    }

    // Append the reserved forced placements (Link's Pocket etc.) so they flow into the spoiler and
    // get committed to the oracle alongside the assumed-fill results.
    if (!forcedPlacements.empty()) {
        placements.insert(placements.end(), forcedPlacements.begin(), forcedPlacements.end());
        std::cout << "[ComboShip] CrossWorldCombinedFill: " << forcedPlacements.size()
                  << " forced placement(s) applied (e.g. Link's Pocket)\n";
    }

    if (progress) {
        progress->phase.store(3); // Finalizing
        // Zero the bar: post-fill work (the pare-down) re-stores its own placed/total.
        progress->placed.store(0);
        progress->total.store(0);
    }

    BakeCrossPlacedMmJunk(session, placements);
    const nlohmann::json spoiler = BuildFillSpoiler(session, placements, passesUsed);

    // --- Commit placements to oracles (for save consumption) ---
    for (const auto& p : placements) {
        // ComboShip: a foreign item's name lives in the OTHER game's namespace. Baked junk names now
        // collide with real OOT items ("Blue Rupee"), so committing one would place the wrong native
        // item at the check instead of harmlessly missing the lookup.
        if (p.check.game != p.item.game)
            continue;
        if (p.check.game == GAME_OOT) {
            ootOracle.PlaceItem(p.check.name.c_str(), p.item.name.c_str());
        } else {
            mmOracle.PlaceItem(p.check.name.c_str(), p.item.name.c_str());
        }
    }

    result.spoilerJson = spoiler.dump(2);
    result.success = true;
    return result;
}

// ---------- Legacy no-logic generator (kept for fallback) ----------

inline std::string CrossWorldGenerateSpoiler(const std::string& sohDumpJson, const std::string& mmDumpJson,
                                             uint32_t masterSeed) {
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "no-logic native-only (phase1)";

    auto doGame = [&](const char* key, const std::string& dumpJson, uint32_t seed) {
        nlohmann::json out = nlohmann::json::object();
        try {
            auto d = nlohmann::json::parse(dumpJson);
            std::vector<std::string> checkNames, poolItems;
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                std::string n = c.value("name", std::string{});
                if (!n.empty())
                    checkNames.push_back(n);
            }
            if (d.contains("pool")) {
                for (auto& it : d["pool"]) {
                    std::string n = it.value("name", std::string{});
                    if (!n.empty())
                        poolItems.push_back(n);
                }
            } else {
                for (auto& c : d.value("checks", nlohmann::json::array())) {
                    std::string v = c.value("vanillaItem", std::string{});
                    if (!v.empty())
                        poolItems.push_back(v);
                }
            }
            // Confined pre-placements pass through unshuffled.
            for (auto& f : d.value("fixed", nlohmann::json::array())) {
                std::string chk = f.value("check", std::string{});
                std::string item = f.value("item", std::string{});
                if (!chk.empty() && !item.empty())
                    out[chk] = item;
            }
            std::vector<std::string> shuffled = poolItems;
            CwRng rng(seed);
            cwShuffle(shuffled, rng);
            // Same silent-truncation shape as the real fill had; this path only runs with no oracles.
            if (shuffled.size() != checkNames.size())
                std::cerr << "[ComboShip] CrossWorldGenerateSpoiler: " << key << " pool/check mismatch — "
                          << shuffled.size() << " items vs " << checkNames.size() << " checks\n";
            for (size_t i = 0; i < checkNames.size() && i < shuffled.size(); ++i) {
                out[checkNames[i]] = shuffled[i];
            }
            spoiler[std::string(key) + "Count"] = static_cast<uint32_t>(checkNames.size());
        } catch (...) { spoiler[std::string(key) + "Error"] = true; }
        spoiler[key] = out;
    };

    doGame("oot", sohDumpJson, masterSeed ^ 0x4F4F5400u);
    doGame("mm", mmDumpJson, masterSeed ^ 0x4D4D0000u);

    return spoiler.dump(2);
}

} // namespace ComboRando
