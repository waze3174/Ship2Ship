// combo/gui/ComboWorldCvars.cpp — ComboShip co-op world-CVar overlay (full-coop-sync plan task 1.3).
//
// comboui owns this because it links libultraship (the process-global CVar store) and can resolve both
// games' live-apply exports; the launcher only moves JSON. Main thread only (called from the launcher's
// per-frame pump and from the seed-apply path, both on the game thread).
//
//   ComboUI_DumpWorldCvars()            -> {"oot":{key:value|null}, "mm":{...}} of the LIVE values
//   ComboUI_ApplyWorldCvars(json, ov)   -> writes registry keys only; ov=1 = session overlay (shadowed,
//                                          user value remembered), ov=0 = new persisted baseline
//   ComboUI_ClearWorldCvarOverlay()     -> puts every overlaid key back to the user's value, unshadows
//   ComboUI_ShadowSessionKeys(jsonArr)  -> shadow extra keys for the session (RoomShare rando settings)
//
// After a value changes, the owning game's ShipInit funcs for that key re-run (SOH_/MM_MenuApplyCVarChange,
// the same path the combo menu uses), so enemy randomizer, drop tweaks etc. take effect live.

#include <map>
#include <optional>
#include <set>
#include <string>

#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>

#include "ComboExport.h"
#include "ComboResolve.h"
#include "coop/WorldCvars.h"

namespace {

typedef void (*FnApplyCVarChange)(const char*);
FnApplyCVarChange sSohApply = nullptr;
FnApplyCVarChange sMmApply = nullptr;

void ResolveSyms() {
    if (!sSohApply)
        sSohApply = (FnApplyCVarChange)Combo_ResolveSym("soh", "SOH_MenuApplyCVarChange");
    if (!sMmApply)
        sMmApply = (FnApplyCVarChange)Combo_ResolveSym("2ship", "MM_MenuApplyCVarChange");
}

// The user's live value before the overlay touched a key (nullopt = the CVar did not exist).
std::map<std::string, std::optional<nlohmann::json>> sUserValues;
std::set<std::string> sSessionShadowed; // every key this module shadowed (overlay + session keys)
// Session keys (RoomShare rando settings): the live state when first shadowed, restored verbatim at the end
// so a key the user never set doesn't come back explicitly set (and then persisted) after unshadowing.
std::map<std::string, std::optional<nlohmann::json>> sSessionUserValues;

const Coop::WorldCvar* Find(const std::string& key, int& gameOut) {
    for (const auto& w : Coop::kWorldCvarsOot)
        if (key == w.key) {
            gameOut = 0;
            return &w;
        }
    for (const auto& w : Coop::kWorldCvarsMm)
        if (key == w.key) {
            gameOut = 1;
            return &w;
        }
    return nullptr;
}

std::optional<nlohmann::json> ReadLive(const char* key) {
    auto cv = CVarGet(key);
    if (!cv)
        return std::nullopt;
    switch (cv->Type) {
        case Ship::ConsoleVariableType::Integer:
            return nlohmann::json(cv->Integer);
        case Ship::ConsoleVariableType::Float:
            return nlohmann::json(cv->Float);
        case Ship::ConsoleVariableType::String:
            return nlohmann::json(cv->String ? std::string(cv->String) : std::string());
        default:
            return std::nullopt; // colors are never WORLD
    }
}

// Returns true when the live value changed.
bool WriteLive(const Coop::WorldCvar& w, const nlohmann::json& v) {
    std::optional<nlohmann::json> before = ReadLive(w.key);
    if (v.is_null()) {
        if (!before)
            return false;
        CVarClear(w.key);
        return true;
    }
    try {
        if (w.type == 's' && v.is_string())
            CVarSetString(w.key, v.get<std::string>().c_str());
        else if (w.type == 'f' && v.is_number())
            CVarSetFloat(w.key, v.get<float>());
        else if (w.type == 'i' && v.is_number())
            CVarSetInteger(w.key, v.get<int32_t>());
        else
            return false; // wrong shape for this key: ignore rather than coerce
    } catch (...) { return false; }
    return ReadLive(w.key) != before;
}

void ApplyLive(int game, const char* key) {
    ResolveSyms();
    FnApplyCVarChange fn = game == 1 ? sMmApply : sSohApply;
    if (fn)
        fn(key);
}

void RestoreRaw(const std::string& key, const std::optional<nlohmann::json>& v) {
    if (!v) {
        CVarClear(key.c_str());
    } else if (v->is_string()) {
        CVarSetString(key.c_str(), v->get<std::string>().c_str());
    } else if (v->is_number_float()) {
        CVarSetFloat(key.c_str(), v->get<float>());
    } else if (v->is_number()) {
        CVarSetInteger(key.c_str(), v->get<int32_t>());
    }
}

std::string sDumpCache;

} // namespace

extern "C" COMBO_EXPORT const char* ComboUI_DumpWorldCvars(void) {
    try {
        nlohmann::json out;
        nlohmann::json oot = nlohmann::json::object(), mm = nlohmann::json::object();
        for (const auto& w : Coop::kWorldCvarsOot) {
            auto v = ReadLive(w.key);
            oot[w.key] = v ? *v : nlohmann::json();
        }
        for (const auto& w : Coop::kWorldCvarsMm) {
            auto v = ReadLive(w.key);
            mm[w.key] = v ? *v : nlohmann::json();
        }
        out["oot"] = std::move(oot);
        out["mm"] = std::move(mm);
        sDumpCache = out.dump();
    } catch (...) { sDumpCache = "{}"; }
    return sDumpCache.c_str();
}

// Returns the number of keys whose live value changed (-1 on a malformed payload).
extern "C" COMBO_EXPORT int ComboUI_ApplyWorldCvars(const char* json, int overlay) {
    if (!json)
        return -1;
    try {
        auto j = nlohmann::json::parse(json);
        int changed = 0;
        for (const char* g : { "oot", "mm" }) {
            auto block = j.value(g, nlohmann::json::object());
            if (!block.is_object())
                continue;
            for (auto it = block.begin(); it != block.end(); ++it) {
                int game = 0;
                const Coop::WorldCvar* w = Find(it.key(), game);
                if (!w)
                    continue; // never write a key outside the registry
                if (overlay) {
                    if (!sUserValues.count(w->key))
                        sUserValues[w->key] = ReadLive(w->key);
                    CVarShadowPersist(w->key);
                    sSessionShadowed.insert(w->key);
                }
                if (WriteLive(*w, it.value())) {
                    ++changed;
                    ApplyLive(game, w->key);
                }
            }
        }
        if (!overlay && changed > 0)
            CVarSave(); // ExplicitDrop: the seed's world settings become the user's baseline
        return changed;
    } catch (...) { return -1; }
}

extern "C" COMBO_EXPORT void ComboUI_ClearWorldCvarOverlay(void) {
    try {
        for (auto& [key, value] : sUserValues) {
            int game = 0;
            const Coop::WorldCvar* w = Find(key, game);
            if (!w)
                continue;
            if (WriteLive(*w, value ? *value : nlohmann::json()))
                ApplyLive(game, w->key);
        }
        sUserValues.clear();
        for (auto& [key, value] : sSessionUserValues)
            RestoreRaw(key, value);
        sSessionUserValues.clear();
        for (const auto& key : sSessionShadowed)
            CVarUnshadowPersist(key.c_str());
        sSessionShadowed.clear();
    } catch (...) {}
}

// RoomShare: the room seed's rando settings are written to CVars during the apply (and restored to the
// user's afterwards); shadowing them for the session guarantees none of that reaches comboship.json.
extern "C" COMBO_EXPORT void ComboUI_ShadowSessionKeys(const char* jsonArray) {
    if (!jsonArray)
        return;
    try {
        auto j = nlohmann::json::parse(jsonArray);
        if (!j.is_array())
            return;
        for (auto& k : j) {
            if (!k.is_string())
                continue;
            std::string key = k.get<std::string>();
            if (!sSessionShadowed.count(key))
                sSessionUserValues[key] = ReadLive(key.c_str());
            CVarShadowPersist(key.c_str());
            sSessionShadowed.insert(key);
        }
    } catch (...) {}
}

extern "C" COMBO_EXPORT int ComboUI_WorldCvarOverlayActive(void) {
    return sUserValues.empty() ? 0 : 1;
}
