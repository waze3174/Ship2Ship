// soh/soh/Enhancements/randomizer/ComboCrossEntrances.cpp — ComboShip cross-game entrances, OOT side
// (WomboComboShip's docs/plans/full-coop-sync.md §B2; owner: "a full slurry, you never know which game you'll end up in"; age is
// KEPT across games). Combo-owned file (soh GLOB_RECURSE).
//
// A door's DESTINATION is the entrance index it lands on after SoH's own shuffle (replacement->GetIndex(), the
// value Entrance_OverrideNextIndex returns). The launcher pairs OOT and MM connections and pushes:
//   SOH_SetCrossEntrances({"exits":[D...], "arrivals":[D...]})
//     exits    — destinations that now lead INTO Termina: those doors leave OOT's region graph and, after each
//                search, are reported per age (Combo_SOH_Rando_GetCrossExits -> ["D:c", "D:a", ...]).
//     arrivals — destinations Termina leads INTO: their landing regions become extra Root exits, open at an age
//                when the oracle owns "@combo:arrive:oot:D:c" / ":a" (the fill derives those from MM's exits).
// Runtime (seed `entrances.cross.oot` = {D: mmEntrance}): Combo_OOT_CrossRedirect switches to MM.

#include <array>
#include <set>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ComboExport.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/entrance.h"
#include "soh/Enhancements/randomizer/location_access.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "z64.h"
#include "variables.h"
}

namespace Rando {
void SetAllEntrancesData(); // entrance.cpp: index/type/primary/reverse for every shuffleable door
}

using namespace Rando;

namespace {
constexpr const char* kArrivePrefix = "@combo:arrive:oot:";

// A crossed door, by identity rather than pointer: SoH can rebuild its region table (RegionTable_Init in the
// override-apply path), which would leave a stored Entrance* dangling.
struct CrossExit {
    RandomizerRegion parent = RR_NONE;
    RandomizerRegion originalTarget = RR_NONE; // GetOriginalConnectedRegionKey(): stable across Disconnect
    RandomizerRegion connectedBefore = RR_NONE;
    int16_t dest = 0;
};
std::vector<CrossExit> sExits;
std::vector<int16_t> sArrivalDests;   // root exits we appended, by destination
std::set<std::string> sArrivalsOwned; // "D:c" / "D:a"
std::vector<std::string> sReached;    // last search
std::unordered_map<int16_t, int32_t> sRuntime;
std::string sCache;

// SoH's ConditionFn is a plain function pointer (no captures), so each arrival root gets a slot: a table of
// capture-free functions, one per slot, each reading its slot's "D:c" / "D:a" keys.
constexpr size_t kMaxArrivals = 256;
std::array<std::string, kMaxArrivals> sSlotChild, sSlotAdult;
template <size_t I> bool ArrivalCondition() {
    return (logic->IsChild && sArrivalsOwned.count(sSlotChild[I])) ||
           (logic->IsAdult && sArrivalsOwned.count(sSlotAdult[I]));
}
template <size_t... Is> std::array<ConditionFn, sizeof...(Is)> MakeArrivalConditions(std::index_sequence<Is...>) {
    return { &ArrivalCondition<Is>... };
}
const std::array<ConditionFn, kMaxArrivals> kArrivalConditions =
    MakeArrivalConditions(std::make_index_sequence<kMaxArrivals>{});

int16_t DestOf(const Entrance& e) {
    return (e.IsShuffled() && e.GetReplacement() != nullptr) ? e.GetReplacement()->GetIndex() : e.GetIndex();
}

// Every door that has an entrance index, by its current destination.
std::unordered_map<int16_t, Entrance*> DoorsByDest() {
    std::unordered_map<int16_t, Entrance*> out;
    for (int r = 0; r < RR_MAX; ++r) {
        Region* region = RegionTable(static_cast<RandomizerRegion>(r));
        if (region == nullptr)
            continue;
        for (Entrance& e : region->exits)
            if (e.GetIndex() >= 0)
                out[DestOf(e)] = &e;
    }
    return out;
}

const char* TypeName(EntranceType t) {
    switch (t) {
        case EntranceType::Overworld:
            return "overworld";
        case EntranceType::Interior:
        case EntranceType::SpecialInterior:
        case EntranceType::InteriorReverse:
            return "interior";
        case EntranceType::Dungeon:
        case EntranceType::DungeonReverse:
            return "dungeon";
        case EntranceType::GrottoGrave:
        case EntranceType::GrottoGraveReverse:
            return "grotto";
        default:
            return "other";
    }
}

Entrance* Find(const CrossExit& x) {
    Region* parent = RegionTable(x.parent);
    if (parent == nullptr)
        return nullptr;
    for (Entrance& e : parent->exits)
        if (e.GetOriginalConnectedRegionKey() == x.originalTarget)
            return &e;
    return nullptr;
}

// SoH only attaches indices/types to its doors in its own shuffle setup, which never runs with OOT entrance rando off.
// The index map holds raw pointers into the region table, and every static-data dump re-runs RegionTable_Init, so the
// map can point at destroyed doors. A rebuilt door may even reuse the old address, so "still a live exit" is not
// enough: trust the map only if its Mido's House pointer is a live exit (pointer compare, no deref of freed memory)
// AND that exit still carries the index (a freshly built door has none).
void EnsureEntranceData() {
    const Entrance* mido = EntranceShuffler::GetEntranceByIndex(ENTR_MIDOS_HOUSE_0);
    const Entrance* live = nullptr;
    if (mido != nullptr)
        if (Region* kf = RegionTable(RR_KOKIRI_FOREST))
            for (const Entrance& e : kf->exits)
                if (&e == mido)
                    live = &e;
    if (live == nullptr || live->GetIndex() != ENTR_MIDOS_HOUSE_0)
        SetAllEntrancesData();
}

void Undo() {
    for (auto& x : sExits) {
        Entrance* e = Find(x);
        if (e && e->GetConnectedRegionKey() == RR_NONE) // a rebuilt table is already whole: nothing to restore
            e->Connect(x.connectedBefore);
    }
    sExits.clear();
    if (Region* root = RegionTable(RR_ROOT_EXITS)) {
        root->exits.remove_if([](const Entrance& e) { return e.GetName().rfind("ComboArrival:", 0) == 0; });
    }
    sArrivalDests.clear();
}
} // namespace

bool ComboCross_ParseOwned(const std::string& name) {
    if (name.rfind(kArrivePrefix, 0) != 0)
        return false;
    sArrivalsOwned.insert(name.substr(std::char_traits<char>::length(kArrivePrefix)));
    return true;
}

void ComboCross_ResetOwned() {
    sArrivalsOwned.clear();
}

// After a reachability search: which crossed doors each age reaches (condition checked at that age/time).
void ComboCross_CollectReached() {
    sReached.clear();
    for (auto& x : sExits) {
        Entrance* e = Find(x);
        Region* parent = RegionTable(x.parent);
        if (!e || !parent)
            continue;
        if (e->GetConnectedRegionKey() != RR_NONE) {
            // Tripwire: a crossed door must stay disconnected; reconnected = the region table was rebuilt under us.
            static bool sWarned = false;
            if (!sWarned) {
                sWarned = true;
                SPDLOG_ERROR("[ComboCross] crossed door {} is connected again: the region table was rebuilt after "
                             "SOH_SetCrossEntrances, logic is validating vanilla OOT doors",
                             x.dest);
                fprintf(stderr, "[ComboCross] TRIPWIRE: crossed door %d reconnected (region table rebuilt)\n",
                        (int)x.dest);
            }
        }
        const bool child = (parent->childDay && e->CheckConditionAtAgeTime(logic->IsChild, logic->AtDay, true)) ||
                           (parent->childNight && e->CheckConditionAtAgeTime(logic->IsChild, logic->AtNight, true));
        const bool adult = (parent->adultDay && e->CheckConditionAtAgeTime(logic->IsAdult, logic->AtDay, true)) ||
                           (parent->adultNight && e->CheckConditionAtAgeTime(logic->IsAdult, logic->AtNight, true));
        if (child)
            sReached.push_back(std::to_string(x.dest) + ":c");
        if (adult)
            sReached.push_back(std::to_string(x.dest) + ":a");
    }
    if (std::getenv("COMBO_PT_VERBOSE") && !sArrivalsOwned.empty()) {
        size_t rootN = 0;
        if (Region* root = RegionTable(RR_ROOT_EXITS))
            for (auto& e : root->exits)
                rootN += e.GetName().rfind("ComboArrival:", 0) == 0;
        fprintf(stderr, "[cross/oot] search: owned %zu, root arrivals %zu, crossed exits %zu, reached %zu\n",
                sArrivalsOwned.size(), rootN, sExits.size(), sReached.size());
    }
}

extern "C" COMBO_EXPORT const char* Combo_SOH_Rando_GetCrossExits(void) {
    try {
        sCache = nlohmann::json(sReached).dump();
    } catch (...) { sCache = "[]"; }
    return sCache.c_str();
}

void ComboCross_EnsureOracleInit(); // OTRGlobals.cpp

// Two-way connections of OOT's current (post-shuffle) layout, for the launcher's cross pairing.
extern "C" COMBO_EXPORT const char* SOH_DumpEntranceGraph(void) {
    static std::string cache;
    try {
        ComboCross_EnsureOracleInit(); // a region table must exist (a fresh process has none yet)
        EnsureEntranceData();
        nlohmann::json conns = nlohmann::json::array();
        for (int r = 0; r < RR_MAX; ++r) {
            Region* region = RegionTable(static_cast<RandomizerRegion>(r));
            if (region == nullptr)
                continue;
            for (Entrance& e : region->exits) {
                if (e.GetIndex() < 0 || !e.IsPrimary() || e.GetReverse() == nullptr)
                    continue;
                if (e.GetConnectedRegionKey() == RR_NONE)
                    continue; // already crossed this attempt
                // The way back from where this door NOW lands is the reverse of its current destination's own
                // entrance (after a shuffle, e.GetReverse() belongs to the ORIGINAL target).
                const int16_t toB = DestOf(e);
                const Entrance* landed = EntranceShuffler::GetEntranceByIndex(toB);
                if (landed == nullptr || landed->GetReverse() == nullptr)
                    continue;
                conns.push_back({ { "type", TypeName(e.GetType()) },
                                  { "toB", toB },
                                  { "toA", DestOf(*landed->GetReverse()) },
                                  { "nameA", RegionTable(e.GetParentRegionKey())->regionName },
                                  { "nameB", e.GetConnectedRegion() ? e.GetConnectedRegion()->regionName : "" } });
            }
        }
        cache = nlohmann::json{ { "game", "oot" }, { "connections", conns } }.dump();
    } catch (...) { cache = "{}"; }
    return cache.c_str();
}

extern "C" COMBO_EXPORT void SOH_SetCrossEntrances(const char* json) {
    // The oracle's one-time lazy init rebuilds the region table (RegionTable_Init). If it ran AFTER this, every
    // crossed door would be silently reconnected and logic would validate a world the save doesn't play. With
    // OOT's own entrance shuffle off nothing else burns it first, so do it here.
    ComboCross_EnsureOracleInit();
    Undo();
    nlohmann::json j;
    try {
        j = json ? nlohmann::json::parse(json) : nlohmann::json::object();
    } catch (...) { return; }
    if (!j.contains("exits") && !j.contains("arrivals"))
        return; // undo only
    EnsureEntranceData();
    auto doors = DoorsByDest();
    // Arrival regions first: a crossed connection's landing spot is also one of its exits' destination.
    std::vector<std::pair<int16_t, RandomizerRegion>> arrivals;
    for (auto& d : j.value("arrivals", nlohmann::json::array())) {
        auto it = doors.find(d.get<int16_t>());
        if (it != doors.end() && it->second->GetConnectedRegionKey() != RR_NONE)
            arrivals.push_back({ it->first, it->second->GetConnectedRegionKey() });
    }
    for (auto& d : j.value("exits", nlohmann::json::array())) {
        auto it = doors.find(d.get<int16_t>());
        if (it == doors.end())
            continue;
        Entrance* e = it->second;
        CrossExit x{ e->GetParentRegionKey(), e->GetOriginalConnectedRegionKey(), e->GetConnectedRegionKey(),
                     it->first };
        e->Disconnect();
        sExits.push_back(x);
    }
    if (Region* root = RegionTable(RR_ROOT_EXITS)) {
        for (auto& [dest, region] : arrivals) {
            const size_t slot = sArrivalDests.size();
            if (slot >= kMaxArrivals) {
                SPDLOG_ERROR("[ComboCross] more than {} arrival roots; the rest are left out of OOT logic",
                             kMaxArrivals);
                break;
            }
            sSlotChild[slot] = std::to_string(dest) + ":c";
            sSlotAdult[slot] = std::to_string(dest) + ":a";
            Entrance e(region, kArrivalConditions[slot], "combo cross arrival");
            e.SetName("ComboArrival:" + std::to_string(dest));
            e.SetParentRegion(RR_ROOT_EXITS);
            root->exits.push_back(e);
            sArrivalDests.push_back(dest);
        }
    }
    if (std::getenv("COMBO_PT_VERBOSE"))
        fprintf(stderr, "[cross/oot] set: exits %zu/%zu, arrivals %zu/%zu, doors-by-dest %zu\n", sExits.size(),
                j.value("exits", nlohmann::json::array()).size(), sArrivalDests.size(),
                j.value("arrivals", nlohmann::json::array()).size(), doors.size());
}

// Runtime: the slot's seed `entrances.cross.oot` ({D: mmEntrance}). Called from SOH_LoadComboRando.
void ComboCross_LoadRuntime(const nlohmann::json& seed) {
    sRuntime.clear();
    try {
        auto t = seed.value("entrances", nlohmann::json::object())
                     .value("cross", nlohmann::json::object())
                     .value("oot", nlohmann::json::object());
        for (auto& [k, v] : t.items())
            sRuntime[static_cast<int16_t>(std::stoi(k))] = v.get<int32_t>();
    } catch (...) { sRuntime.clear(); }
    SPDLOG_INFO("[ComboCross] runtime loaded: {} crossed OOT doors{}", sRuntime.size(),
                sRuntime.count(0x1F9) ? " (incl. adult savewarp spawn 0x1F9)" : "");
}

extern "C" void Combo_RequestCrossSwitch(int mmEntrance); // OTRGlobals.cpp

// Called with the destination Entrance_OverrideNextIndex settled on. A crossed door switches to MM and lands
// on its MM entrance; the OOT transition already started never finishes (the switch ends OOT's loop next frame).
// Suppression depth: while > 0, entrance overrides are being COMPUTED (savewarp), not walked — never cross.
static int sCrossSuppress = 0;
extern "C" void Combo_OOT_CrossSuppress(int on) {
    sCrossSuppress += on ? 1 : -1;
    if (sCrossSuppress < 0)
        sCrossSuppress = 0;
}

extern "C" void Combo_OOT_CrossRedirect(s16 dest) {
    if (sRuntime.empty() || gSaveContext.fileNum > 2)
        return;
    if (sCrossSuppress > 0) {
        if (sRuntime.find(dest) != sRuntime.end())
            SPDLOG_INFO("[ComboCross] OOT door 0x{:X} reached by a computed savewarp: not crossing", (u16)dest);
        return;
    }
    auto it = sRuntime.find(dest);
    if (it == sRuntime.end())
        return;
    SPDLOG_INFO("[ComboCross] OOT door 0x{:X} -> MM entrance 0x{:X}", (u16)dest, it->second);
    Combo_RequestCrossSwitch(it->second);
}
