#pragma once
// ComboShip entrance randomizer, launcher side (WomboComboShip's docs/plans/full-coop-sync.md §B1, groundwork for §B2).
//
// One shuffler for every game: each game's DLL dumps its logic graph (MM: MM_DumpEntranceGraph), this file
// turns two-way exits into typed Connections, shuffles them per pool from the seed, checks the result is
// fully connected with every item (a structural precondition; the combined fill then proves the seed
// beatable with real logic), and emits {"<entrance>": <entrance>} override tables the games apply both in
// logic and at runtime. The table is stored in the seed (`entrances.mm`) and applied from there on reload,
// so a layout never depends on this code staying deterministic across builds (§B2).
//
// Terms: a Connection joins side A and side B. Leaving A through its door targets entrance `toB` (you land in
// B); leaving B through its door targets `toA`. A shuffle rewrites those targets: "taking a door whose target
// is toB_i lands you at toB_j instead".

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ComboRando {

enum class EntType { Overworld, Interior, Dungeon, Grotto, Boss, Fixed };

inline const char* EntTypeName(EntType t) {
    switch (t) {
        case EntType::Overworld:
            return "overworld";
        case EntType::Interior:
            return "interior";
        case EntType::Dungeon:
            return "dungeon";
        case EntType::Grotto:
            return "grotto";
        case EntType::Boss:
            return "boss";
        case EntType::Fixed:
            return "fixed";
    }
    return "?";
}

struct EntConnection {
    int game = 1;    // 0 = OOT, 1 = MM
    int32_t toB = 0; // target when leaving A (lands in B)
    int32_t toA = 0; // target when leaving B (lands in A)
    int regionA = 0, regionB = 0;
    int sceneA = 0, sceneB = 0;
    std::string nameA, nameB;
    EntType type = EntType::Fixed;
};

struct EntranceSettings {
    bool overworld = false;
    bool interior = false;
    bool dungeon = false;
    bool decoupled = false;
    bool mixed = false; // overworld + interior + dungeon share one pool

    bool Any() const {
        return overworld || interior || dungeon;
    }
    bool Enabled(EntType t) const {
        return (t == EntType::Overworld && overworld) || (t == EntType::Interior && interior) ||
               (t == EntType::Dungeon && dungeon);
    }
    nlohmann::json ToJson() const {
        return { { "overworld", overworld },
                 { "interior", interior },
                 { "dungeon", dungeon },
                 { "decoupled", decoupled },
                 { "mixed", mixed } };
    }
    static EntranceSettings FromJson(const nlohmann::json& j) {
        EntranceSettings s;
        if (!j.is_object())
            return s;
        s.overworld = j.value("overworld", false);
        s.interior = j.value("interior", false);
        s.dungeon = j.value("dungeon", false);
        s.decoupled = j.value("decoupled", false);
        s.mixed = j.value("mixed", false);
        return s;
    }
};

// ── MM classification (scene ids from MM's SceneId enum, as dumped) ──
namespace MmEnt {
// Outdoor areas. Their links to each other form the overworld pool.
inline const std::set<int> kOverworld = { 45, 108, 109, 110, 111, 112, 64, 69, 100, 43, 70, 28, 80, 93,
                                          91, 92,  34,  53,  55,  56,  37, 83, 67,  19, 88, 74, 65, 51 };
// The dungeon pool: the four temples and the dungeon-like side areas, entered from the overworld.
inline const std::set<int> kDungeon = { 27, 33, 73, 22, 75, 29, 39, 40 };
inline const std::set<int> kBoss = { 31, 68, 95, 54, 86, 11 };
inline const std::set<int> kGrotto = { 7 };
// Never shuffled (story, minigame, multi-scene complexes): moon and its trials, Clock Tower, Pirates'
// Fortress complex, Gorman/Goron race tracks, Inverted Stone Tower, Zora Hall rooms.
inline const std::set<int> kFixedScenes = { 103, 42, 63, 102, 71, 99, 25, 20, 35, 59, 106, 107, 89, 24, 76 };
// Individual links kept vanilla (either direction's target listed): the Woodfall Temple princess exit
// (post-clear cutscene route) and the dungeon-internal links of Ikana Castle / Beneath the Well.
inline const std::set<int32_t> kFixedTargets = { 0x8630, 0x3020, 0x3400, 0x9010, 0x3420, 0x3430 };
} // namespace MmEnt

inline EntType ClassifyMm(int sceneA, int sceneB, int32_t toA, int32_t toB) {
    using namespace MmEnt;
    if (kFixedTargets.count(toA) || kFixedTargets.count(toB) || kFixedScenes.count(sceneA) ||
        kFixedScenes.count(sceneB) || sceneA == sceneB)
        return EntType::Fixed;
    if (kGrotto.count(sceneA) || kGrotto.count(sceneB))
        return EntType::Grotto;
    if (kBoss.count(sceneA) || kBoss.count(sceneB))
        return EntType::Boss;
    const bool owA = kOverworld.count(sceneA) != 0, owB = kOverworld.count(sceneB) != 0;
    const bool dgA = kDungeon.count(sceneA) != 0, dgB = kDungeon.count(sceneB) != 0;
    if ((dgA && owB) || (dgB && owA))
        return EntType::Dungeon;
    if (dgA || dgB)
        return EntType::Fixed; // dungeon-to-dungeon or dungeon-to-interior: internal plumbing
    if (owA && owB)
        return EntType::Overworld;
    if (owA || owB)
        return EntType::Interior;
    return EntType::Fixed; // interior-to-interior (Zora Hall rooms etc.)
}

// The parsed graph of one game, plus the derived connection list.
struct EntGraph {
    int game = 1;
    struct Region {
        int id = 0;
        int scene = 0;
        std::string name;
        std::vector<int> connections;                   // condition-free view (any items)
        std::vector<std::pair<int32_t, int32_t>> exits; // (target entrance, return entrance or -1)
    };
    std::map<int, Region> regions;
    std::map<int32_t, int> entranceRegion; // entrance -> region you land in
    std::vector<EntConnection> connections;
    int startRegion = 0;

    bool Parse(const nlohmann::json& j, int startRegionId) {
        regions.clear();
        entranceRegion.clear();
        connections.clear();
        startRegion = startRegionId;
        if (!j.is_object() || !j.contains("regions"))
            return false;
        for (auto& r : j["regions"]) {
            Region reg;
            reg.id = r.value("id", 0);
            reg.scene = r.value("scene", -1);
            reg.name = r.value("sceneName", std::string()) + ": " + r.value("name", std::string());
            for (auto& c : r.value("connections", nlohmann::json::array()))
                reg.connections.push_back(c.value("to", 0));
            for (auto& e : r.value("exits", nlohmann::json::array())) {
                int32_t to = e.value("to", 0), from = e.value("from", -1);
                reg.exits.push_back({ to, from });
                if (from != -1)
                    entranceRegion[from] = reg.id;
            }
            for (auto& e : r.value("oneWayEntrances", nlohmann::json::array()))
                entranceRegion[e.get<int32_t>()] = reg.id;
            regions[reg.id] = std::move(reg);
        }
        // One Connection per two-way pair, sides ordered by region id for a stable orientation.
        std::set<std::pair<int32_t, int32_t>> seen;
        for (auto& [id, reg] : regions) {
            for (auto [to, from] : reg.exits) {
                if (from == -1 || !entranceRegion.count(to))
                    continue;
                auto key = std::minmax(to, from);
                if (!seen.insert(key).second)
                    continue;
                EntConnection c;
                c.game = game;
                int other = entranceRegion[to];
                // Orient A = the overworld side when exactly one side is overworld, else lower region id.
                bool selfOw = MmEnt::kOverworld.count(reg.scene) != 0;
                bool otherOw = MmEnt::kOverworld.count(regions[other].scene) != 0;
                bool selfIsA = (selfOw != otherOw) ? selfOw : (id < other);
                if (selfIsA) {
                    c.regionA = id, c.regionB = other, c.toB = to, c.toA = from;
                } else {
                    c.regionA = other, c.regionB = id, c.toB = from, c.toA = to;
                }
                c.sceneA = regions[c.regionA].scene;
                c.sceneB = regions[c.regionB].scene;
                c.nameA = regions[c.regionA].name;
                c.nameB = regions[c.regionB].name;
                c.type = ClassifyMm(c.sceneA, c.sceneB, c.toA, c.toB);
                connections.push_back(std::move(c));
            }
        }
        return !regions.empty();
    }

    // Regions reachable from the start with EVERY item (conditions ignored), through the shuffled exits.
    std::set<int> ReachableAnyItems(const std::map<int32_t, int32_t>& overrides) const {
        std::set<int> seen = { startRegion };
        std::vector<int> stack = { startRegion };
        auto target = [&](int32_t e) {
            auto it = overrides.find(e);
            return it == overrides.end() ? e : it->second;
        };
        while (!stack.empty()) {
            int id = stack.back();
            stack.pop_back();
            auto it = regions.find(id);
            if (it == regions.end())
                continue;
            auto visit = [&](int next) {
                if (seen.insert(next).second)
                    stack.push_back(next);
            };
            for (int c : it->second.connections)
                visit(c);
            for (auto [to, from] : it->second.exits) {
                auto er = entranceRegion.find(target(to));
                if (er != entranceRegion.end())
                    visit(er->second);
            }
        }
        return seen;
    }
};

struct EntShuffleResult {
    bool ok = false;
    std::map<int32_t, int32_t> overrides; // target -> new target (only changed entries)
    nlohmann::json spoiler = nlohmann::json::array();
    int tries = 0;
    std::string error;
};

// Shuffles `graph` per `settings` with an RNG derived from `seed`. Retries internally until every region
// that is reachable in the vanilla layout (with all items) stays reachable.
inline EntShuffleResult ShuffleEntrances(const EntGraph& graph, const EntranceSettings& settings, uint32_t seed,
                                         int maxTries = 200) {
    EntShuffleResult res;
    if (!settings.Any()) {
        res.ok = true;
        return res;
    }
    const std::set<int> mustReach = graph.ReachableAnyItems({});

    // Pools: per type, or one mixed pool.
    std::map<int, std::vector<const EntConnection*>> pools;
    for (auto& c : graph.connections) {
        if (!settings.Enabled(c.type))
            continue;
        int key = settings.mixed ? 0 : static_cast<int>(c.type);
        pools[key].push_back(&c);
    }
    std::mt19937 rng(seed ^ 0xE47A9C3Bu);
    for (res.tries = 1; res.tries <= maxTries; ++res.tries) {
        std::map<int32_t, int32_t> ov;
        nlohmann::json spoiler = nlohmann::json::array();
        for (auto& [key, pool] : pools) {
            const size_t n = pool.size();
            if (n < 2)
                continue;
            std::vector<size_t> p(n), q(n);
            for (size_t i = 0; i < n; ++i)
                p[i] = q[i] = i;
            std::shuffle(p.begin(), p.end(), rng);
            if (settings.decoupled)
                std::shuffle(q.begin(), q.end(), rng);
            for (size_t i = 0; i < n; ++i) {
                const EntConnection& src = *pool[i];
                const EntConnection& dst = *pool[p[i]];
                // A_i's door now lands in B_{p(i)}.
                if (dst.toB != src.toB)
                    ov[src.toB] = dst.toB;
                if (settings.decoupled) {
                    // B_i's door lands in an independently chosen A side.
                    const EntConnection& back = *pool[q[i]];
                    if (back.toA != src.toA)
                        ov[src.toA] = back.toA;
                } else if (src.toA != dst.toA) {
                    // Coupled: B_{p(i)}'s door leads back to A_i.
                    ov[dst.toA] = src.toA;
                }
                spoiler.push_back({ { "from", src.nameA },
                                    { "to", dst.nameB },
                                    { "type", EntTypeName(src.type) },
                                    { "entrance", src.toB },
                                    { "lands", dst.toB } });
            }
        }
        // Structural check: nothing that was reachable becomes unreachable (no island, no one-way trap).
        std::set<int> reach = graph.ReachableAnyItems(ov);
        bool ok = std::includes(reach.begin(), reach.end(), mustReach.begin(), mustReach.end());
        if (ok) {
            res.ok = true;
            res.overrides = std::move(ov);
            res.spoiler = std::move(spoiler);
            return res;
        }
    }
    res.error = "no connected entrance layout found in " + std::to_string(maxTries) + " tries";
    return res;
}

// MM's logic starts in the pseudo-region with sceneId SCENE_MAX (the save-warp root, RR_MAX).
constexpr int kMmSceneMax = 113;

struct MmEntranceAttempt {
    bool ok = true;
    nlohmann::json overrides = nlohmann::json::object();
    nlohmann::json spoiler = nlohmann::json::array();
    std::string error;
};

inline nlohmann::json OverridesToJson(const std::map<int32_t, int32_t>& ov);

// One fill attempt's MM entrance shuffle: dump the graph, shuffle per settings, push the result into MM's
// logic (so the fill that follows validates the shuffled world). Settings off => pushes an empty table.
// Shared by the in-game generator, gentest, playthrough and headless comborando.
inline MmEntranceAttempt ShuffleMmEntrancesForAttempt(const char* (*dumpGraph)(void), void (*setOverrides)(const char*),
                                                      const EntranceSettings& settings, uint32_t masterSeed) {
    MmEntranceAttempt out;
    if (!settings.Any() || !dumpGraph || !setOverrides) {
        if (setOverrides)
            setOverrides("{}");
        if (settings.Any()) {
            out.ok = false;
            out.error = "stale 2ship: MM entrance-randomizer exports missing";
        }
        return out;
    }
    EntGraph g;
    nlohmann::json dump;
    try {
        dump = nlohmann::json::parse(dumpGraph());
    } catch (...) {}
    int start = 0;
    for (auto& r : dump.value("regions", nlohmann::json::array()))
        if (r.value("scene", -1) == kMmSceneMax)
            start = r.value("id", 0);
    if (!g.Parse(dump, start)) {
        out.ok = false;
        out.error = "MM entrance graph dump was empty";
        setOverrides("{}");
        return out;
    }
    EntShuffleResult r = ShuffleEntrances(g, settings, masterSeed);
    if (!r.ok) {
        out.ok = false;
        out.error = "MM entrance shuffle: " + r.error;
        setOverrides("{}");
        return out;
    }
    out.overrides = OverridesToJson(r.overrides);
    out.spoiler = std::move(r.spoiler);
    setOverrides(out.overrides.dump().c_str());
    return out;
}

// ── Cross-game entrances (§B2: "a full slurry"; age is KEPT across games) ─────────────────────────────────
// The tables for the generation attempt in progress: OOT destination -> MM entrance and MM destination -> OOT
// entrance. ootExits/mmExits read, right after that game's own GetReachableChecks, which crossed doors the search
// reached and at which age ("D:c" / "D:a"). Fill and playthrough loops turn those into age-tagged arrival
// pseudo-items for the other oracle ("@combo:arrive:<game>:<dest>:<c|a>").
struct CrossEntranceTables {
    std::map<int32_t, int32_t> oot, mm;
    const char* (*ootExits)(void) = nullptr;
    const char* (*mmExits)(void) = nullptr;
    bool Active() const {
        return !oot.empty() || !mm.empty();
    }
};
inline CrossEntranceTables& ActiveCrossEntrances() {
    static CrossEntranceTables t;
    return t;
}

inline std::vector<std::string> ReadCrossExits(const char* (*fn)(void)) {
    std::vector<std::string> out;
    if (!fn)
        return out;
    try {
        for (auto& v : nlohmann::json::parse(fn()))
            if (v.is_string())
                out.push_back(v.get<std::string>());
    } catch (...) {}
    return out;
}
inline std::vector<std::string> ReadOotCrossExits() {
    return ActiveCrossEntrances().Active() ? ReadCrossExits(ActiveCrossEntrances().ootExits)
                                           : std::vector<std::string>{};
}
inline std::vector<std::string> ReadMmCrossExits() {
    return ActiveCrossEntrances().Active() ? ReadCrossExits(ActiveCrossEntrances().mmExits)
                                           : std::vector<std::string>{};
}

// Which crossed doors the oracle's LAST search reached. The OOT oracle is the one with a portal probe.
template <typename Oracle> inline std::vector<std::string> ReadCrossExitsFor(const Oracle& o) {
    return o.GetPortalOpen ? ReadOotCrossExits() : ReadMmCrossExits();
}

// Termina may be entered through a crossed door before the portal opens.
inline bool HasMmCrossArrival(const std::vector<std::string>& mmOwned) {
    for (const auto& o : mmOwned)
        if (o.rfind("@combo:arrive:mm:", 0) == 0)
            return true;
    return false;
}
// MM queried while the portal is still closed: its save-warp root (South Clock Town) is not reachable yet.
inline std::vector<std::string> MmOwnedNoRoot(const std::vector<std::string>& mmOwned) {
    std::vector<std::string> o = mmOwned;
    o.push_back("@combo:mmNoRoot");
    return o;
}

struct CrossEntranceLatch {
    std::set<std::string> sent;
};

// Reached crossed doors -> arrivals in the other game at the same age. True if anything new was owed.
inline bool ApplyCrossEntrances(const std::vector<std::string>& ootReached, const std::vector<std::string>& mmReached,
                                std::vector<std::string>& ootOwned, std::vector<std::string>& mmOwned,
                                CrossEntranceLatch& latch) {
    const auto& t = ActiveCrossEntrances();
    if (!t.Active())
        return false;
    bool changed = false;
    auto apply = [&](const std::vector<std::string>& reached, const std::map<int32_t, int32_t>& table,
                     const char* otherGame, std::vector<std::string>& otherOwned) {
        for (const auto& r : reached) {
            const size_t colon = r.rfind(':');
            if (colon == std::string::npos)
                continue;
            int32_t dest = 0;
            try {
                dest = std::stoi(r.substr(0, colon));
            } catch (...) { continue; }
            auto it = table.find(dest);
            if (it == table.end())
                continue;
            std::string name = std::string("@combo:arrive:") + otherGame + ":" + std::to_string(it->second) +
                               r.substr(colon); // keeps ":c" / ":a"
            if (latch.sent.insert(name).second) {
                otherOwned.push_back(name);
                changed = true;
                if (std::getenv("COMBO_PT_VERBOSE"))
                    std::cerr << "[cross] " << r << " -> " << name << "\n";
            }
        }
    };
    apply(ootReached, t.oot, "mm", mmOwned);
    apply(mmReached, t.mm, "oot", ootOwned);
    return changed;
}

struct CrossSettings {
    bool overworld = false, interior = false, dungeon = false;
    int percent = 50; // share of pairable connections that cross games (100 = every one that can)
    bool Any() const {
        return overworld || interior || dungeon;
    }
    bool Enabled(const std::string& t) const {
        return (t == "overworld" && overworld) || (t == "interior" && interior) || (t == "dungeon" && dungeon);
    }
    nlohmann::json ToJson() const {
        return { { "overworld", overworld }, { "interior", interior }, { "dungeon", dungeon }, { "percent", percent } };
    }
    static CrossSettings FromJson(const nlohmann::json& j) {
        CrossSettings s;
        if (!j.is_object())
            return s;
        s.overworld = j.value("overworld", false);
        s.interior = j.value("interior", false);
        s.dungeon = j.value("dungeon", false);
        s.percent = std::clamp(j.value("percent", 50), 0, 100);
        return s;
    }
};

struct CrossAttempt {
    bool ok = true;
    nlohmann::json runtime = { { "oot", nlohmann::json::object() }, { "mm", nlohmann::json::object() } };
    nlohmann::json spoiler = nlohmann::json::array();
    std::string error;
};

// One attempt's cross-game pairing, run AFTER both games' own shuffles. Pairs OOT and MM connections of the same
// type (current layout, two-way: door to `dB`, the way back to `dA`) and swaps them: OOT A1 -> MM B2 -> back to OOT
// A1, MM A2 -> OOT B1 -> back to MM A2. Pushes each game's exits/arrivals, installs the fill's tables, and returns
// the runtime tables the seed stores (entrances.cross).
inline CrossAttempt ShuffleCrossEntrancesForAttempt(const char* (*sohDumpGraph)(void), void (*sohSetCross)(const char*),
                                                    const char* (*mmDumpGraph)(void), void (*mmSetCross)(const char*),
                                                    const char* (*ootExits)(void), const char* (*mmExits)(void),
                                                    const nlohmann::json& mmOverrides, const CrossSettings& settings,
                                                    uint32_t masterSeed) {
    CrossAttempt out;
    auto& tables = ActiveCrossEntrances();
    tables = {};
    if (!settings.Any()) {
        if (sohSetCross)
            sohSetCross("{}");
        if (mmSetCross)
            mmSetCross("{}");
        return out;
    }
    if (!sohDumpGraph || !sohSetCross || !mmDumpGraph || !mmSetCross || !ootExits || !mmExits) {
        out.ok = false;
        out.error = "stale game modules: cross-game entrance exports missing";
        return out;
    }
    struct Conn {
        std::string type, nameA, nameB;
        int32_t dB = 0, dA = 0;
    };
    std::map<std::string, std::vector<Conn>> oot, mm;
    try {
        for (auto& c : nlohmann::json::parse(sohDumpGraph()).value("connections", nlohmann::json::array())) {
            Conn x{ c.value("type", std::string()), c.value("nameA", std::string()), c.value("nameB", std::string()),
                    c.value("toB", 0), c.value("toA", 0) };
            if (settings.Enabled(x.type))
                oot[x.type].push_back(x);
        }
    } catch (...) {}
    {
        EntGraph g;
        nlohmann::json dump;
        try {
            dump = nlohmann::json::parse(mmDumpGraph());
        } catch (...) {}
        int start = 0;
        for (auto& r : dump.value("regions", nlohmann::json::array()))
            if (r.value("scene", -1) == kMmSceneMax)
                start = r.value("id", 0);
        g.Parse(dump, start);
        std::map<int32_t, int32_t> ov;
        for (auto& [k, v] : mmOverrides.items())
            ov[std::stoi(k)] = v.get<int32_t>();
        auto cur = [&](int32_t e) {
            auto it = ov.find(e);
            return it == ov.end() ? e : it->second;
        };
        std::map<int32_t, const EntConnection*> byToB;
        for (auto& c : g.connections)
            byToB[c.toB] = &c;
        for (auto& c : g.connections) {
            const std::string t = EntTypeName(c.type);
            if (!settings.Enabled(t))
                continue;
            const int32_t dB = cur(c.toB);
            auto landed = byToB.find(dB); // the connection whose B side we now land in
            if (landed == byToB.end())
                continue;
            mm[t].push_back({ t, c.nameA, landed->second->nameB, dB, cur(landed->second->toA) });
        }
    }
    // One entry per two-way connection: a shuffled layout can list the same connection from both sides, and pairing
    // it twice would overwrite one crossing with another (a door's table entry can only say one thing).
    auto dedupe = [](std::map<std::string, std::vector<Conn>>& byType) {
        std::set<int32_t> used;
        for (auto& [type, list] : byType) {
            std::vector<Conn> kept;
            for (auto& c : list) {
                if (c.dB == c.dA || used.count(c.dB) || used.count(c.dA))
                    continue;
                used.insert(c.dB);
                used.insert(c.dA);
                kept.push_back(c);
            }
            list = std::move(kept);
        }
    };
    dedupe(oot);
    dedupe(mm);
    std::mt19937 rng(masterSeed ^ 0xC2055A11u);
    nlohmann::json ootPush = { { "exits", nlohmann::json::array() }, { "arrivals", nlohmann::json::array() } };
    nlohmann::json mmPush = ootPush;
    if (std::getenv("COMBO_PT_VERBOSE"))
        for (auto& [type, l] : oot)
            std::cerr << "[cross] pairable " << type << ": oot=" << l.size() << " mm=" << mm[type].size() << "\n";
    for (auto& [type, ootList] : oot) {
        auto& mmList = mm[type];
        std::shuffle(ootList.begin(), ootList.end(), rng);
        std::shuffle(mmList.begin(), mmList.end(), rng);
        const size_t pairable = std::min(ootList.size(), mmList.size());
        const size_t n = (pairable * static_cast<size_t>(settings.percent) + 50) / 100;
        for (size_t k = 0; k < n; ++k) {
            const Conn& o = ootList[k];
            const Conn& m = mmList[k];
            tables.oot[o.dB] = m.dB;
            tables.oot[o.dA] = m.dA;
            tables.mm[m.dB] = o.dB;
            tables.mm[m.dA] = o.dA;
            for (int32_t d : { o.dB, o.dA }) {
                ootPush["exits"].push_back(d);
                ootPush["arrivals"].push_back(d);
            }
            for (int32_t d : { m.dB, m.dA }) {
                mmPush["exits"].push_back(d);
                mmPush["arrivals"].push_back(d);
            }
            out.spoiler.push_back({ { "type", type }, { "from", "OOT " + o.nameA }, { "to", "MM " + m.nameB } });
            out.spoiler.push_back({ { "type", type }, { "from", "MM " + m.nameA }, { "to", "OOT " + o.nameB } });
        }
    }
    if (out.spoiler.empty()) {
        // Crossing is on but nothing paired: never ship a "cross-game" seed that crosses nothing (that hid a stale
        // OOT door map once). Fail the attempt loudly instead.
        tables = {};
        sohSetCross("{}");
        mmSetCross("{}");
        out.ok = false;
        out.error = "cross-game entrances: no door could be paired (OOT or MM door graph came back empty)";
        return out;
    }
    for (auto& [k, v] : tables.oot)
        out.runtime["oot"][std::to_string(k)] = v;
    for (auto& [k, v] : tables.mm)
        out.runtime["mm"][std::to_string(k)] = v;
    tables.ootExits = ootExits;
    tables.mmExits = mmExits;
    sohSetCross(ootPush.dump().c_str());
    mmSetCross(mmPush.dump().c_str());
    return out;
}

// Rebuild a seed's crossing from its stored runtime tables (entrances.cross), for replays: OOT's crossed doors are
// the `oot` table's keys and its arrivals the `mm` table's values; MM's mirror that.
inline void ApplyStoredCross(const nlohmann::json& runtime, void (*sohSetCross)(const char*),
                             void (*mmSetCross)(const char*), const char* (*ootExits)(void),
                             const char* (*mmExits)(void)) {
    auto& t = ActiveCrossEntrances();
    t = {};
    nlohmann::json ootPush = { { "exits", nlohmann::json::array() }, { "arrivals", nlohmann::json::array() } };
    nlohmann::json mmPush = ootPush;
    try {
        // Named copies: range-for over .items() of a temporary dangles (and silently read nothing here).
        const nlohmann::json ootTable = runtime.value("oot", nlohmann::json::object());
        const nlohmann::json mmTable = runtime.value("mm", nlohmann::json::object());
        for (auto& [k, v] : ootTable.items()) {
            t.oot[std::stoi(k)] = v.get<int32_t>();
            ootPush["exits"].push_back(std::stoi(k));
            mmPush["arrivals"].push_back(v.get<int32_t>());
        }
        for (auto& [k, v] : mmTable.items()) {
            t.mm[std::stoi(k)] = v.get<int32_t>();
            mmPush["exits"].push_back(std::stoi(k));
            ootPush["arrivals"].push_back(v.get<int32_t>());
        }
    } catch (...) {}
    t.ootExits = ootExits;
    t.mmExits = mmExits;
    if (std::getenv("COMBO_PT_VERBOSE"))
        std::cerr << "[cross] stored: oot=" << t.oot.size() << " mm=" << t.mm.size() << " fns=" << (ootExits != nullptr)
                  << (mmExits != nullptr) << "\n";
    if (sohSetCross)
        sohSetCross(t.Active() ? ootPush.dump().c_str() : "{}");
    if (mmSetCross)
        mmSetCross(t.Active() ? mmPush.dump().c_str() : "{}");
}

inline nlohmann::json OverridesToJson(const std::map<int32_t, int32_t>& ov) {
    nlohmann::json j = nlohmann::json::object();
    for (auto& [k, v] : ov)
        j[std::to_string(k)] = v;
    return j;
}

} // namespace ComboRando
