// combo/gui/ComboPlayPreset.cpp
//
// ComboShip: a shipped preset is one player's whole control setup — pad bindings, HUD button
// colours and the control preferences — applied to BOTH games in one click.
//
// Why this is combo-owned and not a preset in either game's own system:
//   * OOT's preset apply (soh/soh/Enhancements/Presets/Presets.cpp) replaces a whole CVar block with
//     Config::SetBlock. gSettings and gEnhancements are SHARED by both games here, so an OOT preset
//     carrying a "settings" block would delete gSettings.Controllers and every gEnhancements key MM
//     owns. MM's PresetManager has the mirror-image problem: its ClearCVars list CVarClearBlock's the
//     same shared prefixes.
//   * ShipInit registries are per-DLL, so one game's apply never re-runs the other's init funcs.
// So this writes LEAF CVars only, and hands each key to the owning game's live-apply export.
//
// The shipped presets are the compiled-in kPresets table below (none yet; the menu hides the preset
// section while the table is empty).
#include <libultraship/libultraship.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ComboExport.h"
#include "ComboPlayPreset.h"
#include "ComboResolve.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

typedef void (*FnApplyCVarChange)(const char*);
FnApplyCVarChange sSohApply = nullptr;
FnApplyCVarChange sMmApply = nullptr;

void ResolveSyms() {
    if (!sSohApply) {
        sSohApply = (FnApplyCVarChange)Combo_ResolveSym("soh", "SOH_MenuApplyCVarChange");
    }
    if (!sMmApply) {
        sMmApply = (FnApplyCVarChange)Combo_ResolveSym("2ship", "MM_MenuApplyCVarChange");
    }
}

struct PresetDef {
    const char* name;
    const char* description;
    const char* json; // {"bindings":{...},"colors":{...},"prefs":{...}} of leaf CVar -> value
};

const std::vector<PresetDef> kPresets;
const int kPresetCount = static_cast<int>(kPresets.size());

// Which game's ShipInit should re-run for a key. MM's cosmetic prefix is SINGULAR ("gCosmetic")
// on purpose — that is the thing keeping its keys from colliding with OOT's "gCosmetics".
bool IsMmKey(const std::string& key) {
    static const char* kMmPrefixes[] = {
        "gCosmetic.", "gEnhancements.Camera.", "gEnhancements.Dpad.", "gEnhancements.Playback.",
        "gEnhancements.Graphics.",
    };
    for (const char* p : kMmPrefixes) {
        if (key.rfind(p, 0) == 0) {
            return true;
        }
    }
    return false;
}

// Pad bindings live in libultraship, not in either game.
bool IsBindingKey(const std::string& key) {
    return key.rfind("gSettings.Controllers.", 0) == 0;
}

// Write one leaf. The JSON type picks the CVar type: a 4-number array is a colour, true/false and
// whole numbers are ints, anything else numeric is a float, strings are strings.
void WriteLeaf(const std::string& key, const nlohmann::json& value) {
    if (value.is_array() && value.size() == 4) {
        CVarSetColor(key.c_str(), Color_RGBA8{ value[0].get<uint8_t>(), value[1].get<uint8_t>(),
                                               value[2].get<uint8_t>(), value[3].get<uint8_t>() });
    } else if (value.is_boolean()) {
        CVarSetInteger(key.c_str(), value.get<bool>() ? 1 : 0);
    } else if (value.is_number_integer() || value.is_number_unsigned()) {
        CVarSetInteger(key.c_str(), value.get<int>());
    } else if (value.is_number_float()) {
        CVarSetFloat(key.c_str(), value.get<float>());
    } else if (value.is_string()) {
        CVarSetString(key.c_str(), value.get<std::string>().c_str());
    } else {
        SPDLOG_WARN("[ComboShip] preset: skipping '{}' (unsupported value type)", key);
        return;
    }
    if (!IsBindingKey(key)) {
        if (IsMmKey(key)) {
            if (sMmApply) {
                sMmApply(key.c_str());
            }
        } else if (sSohApply) {
            sSohApply(key.c_str());
        }
    }
}

int ApplySection(const nlohmann::json& root, const char* section) {
    auto it = root.find(section);
    if (it == root.end() || !it->is_object()) {
        return 0;
    }
    int written = 0;
    for (auto& [key, value] : it->items()) {
        WriteLeaf(key, value);
        ++written;
    }
    return written;
}

// Both games read the one ControlDeck, but each caches its mappings until told to re-read.
void ReloadControllers() {
    auto* ctx = Ship::Context::GetRawInstance();
    if (!ctx) {
        return;
    }
    auto controlDeck = ctx->GetControlDeck();
    if (!controlDeck) {
        return;
    }
    for (uint8_t port = 0; port < 4; ++port) {
        if (auto controller = controlDeck->GetControllerByPort(port)) {
            controller->ReloadAllMappingsFromConfig();
        }
    }
    // MM holds its own ControlDeck handle while dormant; nudge it too when it is loaded.
    typedef void (*FnReload)(void);
    static FnReload mmReload = (FnReload)Combo_ResolveSym("2ship", "MM_ReloadControls");
    if (mmReload) {
        mmReload();
    }
}

} // namespace

extern "C" COMBO_EXPORT int ComboUI_PlayPresetCount(void) {
    return kPresetCount;
}

extern "C" COMBO_EXPORT const char* ComboUI_PlayPresetName(int i) {
    return (i >= 0 && i < kPresetCount) ? kPresets[i].name : nullptr;
}

extern "C" COMBO_EXPORT const char* ComboUI_PlayPresetDescription(int i) {
    return (i >= 0 && i < kPresetCount) ? kPresets[i].description : nullptr;
}

extern "C" COMBO_EXPORT int ComboUI_PlayPresetApply(int i, int parts) {
    if (i < 0 || i >= kPresetCount) {
        return -1;
    }
    nlohmann::json root = nlohmann::json::parse(kPresets[i].json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        SPDLOG_ERROR("[ComboShip] preset '{}' has malformed data", kPresets[i].name);
        return -1;
    }
    ResolveSyms();

    int written = 0;
    bool touchedBindings = false;
    if (parts & COMBO_PRESET_PART_BINDINGS) {
        const int n = ApplySection(root, "bindings");
        touchedBindings = n > 0;
        written += n;
    }
    if (parts & COMBO_PRESET_PART_COLORS) {
        written += ApplySection(root, "colors");
    }
    if (parts & COMBO_PRESET_PART_PREFS) {
        written += ApplySection(root, "prefs");
    }
    if (touchedBindings) {
        ReloadControllers();
    }
    if (auto* ctx = Ship::Context::GetRawInstance()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    SPDLOG_INFO("[ComboShip] preset '{}' applied: {} settings written (parts {})", kPresets[i].name, written, parts);
    return written;
}
