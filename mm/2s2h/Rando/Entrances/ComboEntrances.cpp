// mm/2s2h/Rando/Entrances/ComboEntrances.cpp — ComboShip MM entrance randomizer, MM side
// (WomboComboShip's docs/plans/full-coop-sync.md §B1). Combo-owned file; picked up by 2ship's GLOB_RECURSE.
//
// The shuffler itself lives in the launcher (combo/rando/CrossEntrance.h), so OOT and MM entrances can be
// shuffled together (§B2). This file gives it MM's graph and applies its result:
//
//   MM_DumpEntranceGraph()        -> every logic region with its scene, connections and exits
//   MM_SetEntranceOverrides(json) -> {"<entrance>": <entrance>, ...}; "taking an exit that leads to <k>
//                                    lands at <v>". Used by logic (FindReachableRegions) and at runtime.
//   Combo_MM_RedirectExit(e)      -> runtime seam at MM's walk-through exit sites (player loading zones and
//                                    scene doors, grotto exits, load planes). Void-outs, respawns, warps and
//                                    scripted transitions never pass through it, so they are never redirected.
//
// The override table also arrives with the slot's consolidated seed (`entrances.mm`, MM_LoadComboRando), so
// a reload reproduces it exactly instead of re-deriving it.

#include <set>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "2s2h/BenPort.h"
#include "2s2h/ShipUtils.h"
#include "ComboExport.h"
#include "Rando/Logic/Logic.h"
#include "Rando/Rando.h"

namespace Rando::Entrances {

static std::unordered_map<s32, s32> sOverrides;

s32 LogicTarget(s32 entrance) {
    auto it = sOverrides.find(entrance);
    return it == sOverrides.end() ? entrance : it->second;
}

bool HasOverrides() {
    return !sOverrides.empty();
}

static void SetFromJson(const nlohmann::json& j) {
    sOverrides.clear();
    if (!j.is_object())
        return;
    for (auto& [k, v] : j.items()) {
        try {
            s32 from = static_cast<s32>(std::stol(k));
            if (v.is_number_integer())
                sOverrides[from] = v.get<s32>();
        } catch (...) {}
    }
}

// ── Cross-game entrances (§B2; age is KEPT across games) ──
// exits: MM destinations (post-override values) that now lead into Hyrule. Logic skips those edges and records,
// per age tag, which ones a search reaches. arrivals: MM destinations Hyrule leads into; their landing regions are
// extra roots for the tag the arrival pseudo-item names ("@combo:arrive:mm:V:c" / ":a").
static std::set<s32> sCrossExits;
static std::set<s32> sCrossArrivals;
static std::set<std::string> sArrivalsOwned;
static bool sNoRoot = false; // "@combo:mmNoRoot": Termina not entered by the portal yet (only via crossed doors)
static char sTag = 0;        // age tag of the traversal in progress ('c'/'a'; 0 = untagged main search)
static std::set<std::string> sReached;
static std::unordered_map<s32, s32> sCrossRuntime; // seed entrances.cross.mm: {V: ootEntrance}

bool CrossIsExit(s32 dest) {
    return sCrossExits.count(dest) != 0;
}
void CrossReached(s32 dest) {
    if (sTag)
        sReached.insert(std::to_string(dest) + ":" + sTag);
}
bool CrossParseOwned(const std::string& name) {
    static const std::string kPrefix = "@combo:arrive:mm:";
    if (name == "@combo:mmNoRoot") {
        sNoRoot = true;
        return true;
    }
    if (name.rfind(kPrefix, 0) != 0)
        return false;
    sArrivalsOwned.insert(name.substr(kPrefix.size()));
    return true;
}
void CrossResetOwned() {
    sArrivalsOwned.clear();
    sNoRoot = false;
}
bool CrossNoRoot() {
    return sNoRoot;
}
// Roots of one age tag: the save-warp root (child; it is how the portal and an MM start arrive) plus owned arrivals.
std::set<RandoRegionId> CrossRoots(char tag) {
    std::set<RandoRegionId> roots;
    if (tag == 'c' && !sNoRoot)
        roots.insert(RR_MAX);
    for (s32 v : sCrossArrivals)
        if (sArrivalsOwned.count(std::to_string(v) + ":" + tag))
            roots.insert(Rando::Logic::GetRegionIdFromEntrance(v));
    return roots;
}
void CrossSetTag(char tag) {
    sTag = tag;
}
void CrossClearReached() {
    sReached.clear();
}
const std::set<std::string>& CrossReachedSet() {
    return sReached;
}
bool CrossActive() {
    return !sCrossExits.empty() || !sCrossArrivals.empty();
}

void LoadFromSeed(const nlohmann::json& seed) {
    try {
        auto ent = seed.value("entrances", nlohmann::json::object());
        SetFromJson(ent.value("mm", nlohmann::json::object()));
        sCrossRuntime.clear();
        const nlohmann::json crossMm =
            ent.value("cross", nlohmann::json::object()).value("mm", nlohmann::json::object());
        for (auto& [k, v] : crossMm.items()) // named copy: items() of a temporary dangles
            sCrossRuntime[static_cast<s32>(std::stol(k))] = v.get<s32>();
    } catch (...) {
        sOverrides.clear();
        sCrossRuntime.clear();
    }
}

} // namespace Rando::Entrances

// Runtime seam (C linkage, called from fenced lines in z_player.c / z_door_ana.c / z_en_holl.c).
extern "C" s32 Combo_MM_RedirectExit(s32 entrance) {
    if (!IS_RANDO || Rando::Entrances::sOverrides.empty())
        return entrance;
    // Scene-layer bits ride on the low nibble of an exit value only for cutscene layers; exits taken by
    // walking always use layer 0, which is exactly how the table's keys are spelled.
    s32 to = Rando::Entrances::LogicTarget(entrance);
    if (to != entrance)
        SPDLOG_INFO("[ComboEntrances] exit 0x{:04X} -> 0x{:04X}", (u16)entrance, (u16)to);
    // Cross-game: this door now leads into Hyrule. The OOT entrance is final (raw, bit 0x10000: OOT must not
    // re-override it) and Link keeps his age (OOT reopens its save as he left it).
    auto cross = Rando::Entrances::sCrossRuntime.find(to);
    if (cross != Rando::Entrances::sCrossRuntime.end()) {
        SPDLOG_INFO("[ComboEntrances] MM door 0x{:04X} -> OOT entrance 0x{:X}", (u16)to, cross->second);
        Combo_RequestCrossSwitch(cross->second | 0x10000);
    }
    return to;
}

extern "C" COMBO_EXPORT void MM_SetCrossEntrances(const char* json) {
    Rando::Entrances::sCrossExits.clear();
    Rando::Entrances::sCrossArrivals.clear();
    try {
        auto j = json ? nlohmann::json::parse(json) : nlohmann::json::object();
        for (auto& v : j.value("exits", nlohmann::json::array()))
            Rando::Entrances::sCrossExits.insert(v.get<s32>());
        for (auto& v : j.value("arrivals", nlohmann::json::array()))
            Rando::Entrances::sCrossArrivals.insert(v.get<s32>());
    } catch (...) {}
}

extern "C" COMBO_EXPORT const char* Combo_MM_Rando_GetCrossExits(void) {
    static std::string cache;
    try {
        cache = nlohmann::json(Rando::Entrances::CrossReachedSet()).dump();
    } catch (...) { cache = "[]"; }
    return cache.c_str();
}

extern "C" COMBO_EXPORT void MM_SetEntranceOverrides(const char* json) {
    try {
        Rando::Entrances::SetFromJson(json ? nlohmann::json::parse(json) : nlohmann::json::object());
    } catch (...) { Rando::Entrances::sOverrides.clear(); }
}

extern "C" COMBO_EXPORT const char* MM_DumpEntranceGraph(void) {
    static std::string cached;
    try {
        nlohmann::json regions = nlohmann::json::array();
        for (auto& [id, region] : Rando::Logic::Regions) {
            nlohmann::json r;
            r["id"] = static_cast<int>(id);
            r["name"] = region.name ? region.name : "";
            r["scene"] = static_cast<int>(region.sceneId);
            r["sceneName"] = (region.sceneId >= 0 && region.sceneId < SCENE_MAX)
                                 ? Ship_GetSceneName(static_cast<s16>(region.sceneId))
                                 : std::string();
            nlohmann::json conns = nlohmann::json::array();
            for (auto& [to, cond] : region.connections)
                conns.push_back({ { "to", static_cast<int>(to) }, { "cond", cond.second } });
            r["connections"] = conns;
            nlohmann::json exits = nlohmann::json::array();
            for (auto& [to, ex] : region.exits)
                exits.push_back({ { "to", to }, { "from", ex.returnEntrance }, { "cond", ex.conditionString } });
            r["exits"] = exits;
            nlohmann::json oneWay = nlohmann::json::array();
            for (s32 e : region.oneWayEntrances)
                oneWay.push_back(e);
            r["oneWayEntrances"] = oneWay;
            regions.push_back(std::move(r));
        }
        cached = nlohmann::json{ { "game", "mm" }, { "regions", regions } }.dump();
    } catch (...) { cached = "{}"; }
    return cached.c_str();
}
