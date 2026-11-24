// combo/gui/ComboPlayPreset.cpp
//
// ComboShip: writes a batch of leaf CVars for presets and the ComboShip settings import, to BOTH
// games in one pass.
//
// Why this is combo-owned and not the preset system of either game:
//   * OOT's preset apply (soh/soh/Enhancements/Presets/Presets.cpp) replaces a whole CVar block with
//     Config::SetBlock. gSettings and gEnhancements are SHARED by both games here, so an OOT preset
//     carrying a "settings" block would delete gSettings.Controllers and every gEnhancements key MM
//     owns. MM's PresetManager has the mirror-image problem: its ClearCVars list CVarClearBlock's the
//     same shared prefixes.
//   * ShipInit registries are per-DLL, so one game's apply never re-runs the other's init funcs.
// So this writes LEAF CVars only, and hands each changed key to both games' live-apply exports. A key
// that only one game knows has no init func in the other, which makes the extra call a no-op.
#include <libultraship/libultraship.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ComboExport.h"
#include "ComboForeground.h"
#include "ComboPlayPreset.h"
#include "ComboPresetScope.h"
#include "ComboResolve.h"

#include <cstdint>

namespace ComboPresets {

namespace {

constexpr size_t kRgbChannels = 3;
constexpr size_t kRgbaChannels = 4;
constexpr int kMaxChannelValue = 255;
constexpr uint8_t kControllerPortCount = 4;

constexpr const char* kHeldBackNote = " randomizer options skipped while a save is loaded (use the file select).";
constexpr const char* kRejectedNote = " entries had an unsupported value and were skipped.";
constexpr const char* kRestartNote = " Restart ComboShip to load the changed mod list.";

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

void ApplyLive(const std::string& key) {
    if (sSohApply) {
        sSohApply(key.c_str());
    }
    if (sMmApply) {
        sMmApply(key.c_str());
    }
}

bool IsColorArray(const nlohmann::json& value) {
    if (!value.is_array() || (value.size() != kRgbChannels && value.size() != kRgbaChannels)) {
        return false;
    }
    for (const auto& channel : value) {
        if (!channel.is_number_integer() || channel.get<int>() < 0 || channel.get<int>() > kMaxChannelValue) {
            return false;
        }
    }
    return true;
}

void WriteColor(const std::string& key, const nlohmann::json& value) {
    const uint8_t r = value[0].get<uint8_t>();
    const uint8_t g = value[1].get<uint8_t>();
    const uint8_t b = value[2].get<uint8_t>();
    if (value.size() == kRgbaChannels) {
        CVarSetColor(key.c_str(), Color_RGBA8{ r, g, b, value[3].get<uint8_t>() });
    } else {
        CVarSetColor24(key.c_str(), Color_RGB8{ r, g, b });
    }
}

// Write one leaf. The JSON type picks the CVar type: a 3 or 4 number array is a colour, true/false
// and whole numbers are ints, anything else numeric is a float, strings are strings. False when the
// value is none of those.
bool WriteLeaf(const std::string& key, const nlohmann::json& value) {
    if (IsColorArray(value)) {
        WriteColor(key, value);
    } else if (value.is_boolean()) {
        CVarSetInteger(key.c_str(), value.get<bool>() ? 1 : 0);
    } else if (value.is_number_integer()) {
        CVarSetInteger(key.c_str(), value.get<int>());
    } else if (value.is_number_float()) {
        CVarSetFloat(key.c_str(), value.get<float>());
    } else if (value.is_string()) {
        CVarSetString(key.c_str(), value.get<std::string>().c_str());
    } else {
        SPDLOG_WARN("[ComboShip] settings write: skipping '{}' (unsupported value type)", key);
        return false;
    }
    return true;
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
    for (uint8_t port = 0; port < kControllerPortCount; ++port) {
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

Leaves ReadAllLeaves() {
    CVarSave();
    const nlohmann::json root = Ship::Context::GetRawInstance()->GetConfig()->GetNestedJson();
    auto cvars = root.find(ComboConfigTree::kCVarsBlock);
    return cvars == root.end() ? Leaves{} : ComboConfigTree::Flatten(*cvars);
}

ApplyResult WriteLeaves(const Leaves& leaves, const ApplyOptions& options) {
    ResolveSyms();
    const Leaves current = ReadAllLeaves();

    ApplyResult result;
    bool touchedControllers = false;
    for (const auto& [key, value] : leaves) {
        if (!options.allowSeedBound && ComboPresetScope::IsSeedBoundKey(key)) {
            ++result.heldBackSeedBound;
            continue;
        }
        auto existing = current.find(key);
        if (existing != current.end() && existing->second == value) {
            ++result.unchanged;
            continue;
        }
        if (!WriteLeaf(key, value)) {
            ++result.rejected;
            continue;
        }
        ++result.written;
        touchedControllers = touchedControllers || ComboPresetScope::IsControllerBindingKey(key);
        result.needsRestart = result.needsRestart || ComboPresetScope::IsModListKey(key);
        // Pad bindings live in libultraship and are re-read by ReloadControllers instead.
        if (!ComboPresetScope::IsControllerBindingKey(key)) {
            ApplyLive(key);
        }
    }
    if (touchedControllers) {
        ReloadControllers();
    }
    if (auto* ctx = Ship::Context::GetRawInstance()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    SPDLOG_INFO("[ComboShip] settings write: {} written, {} unchanged, {} held back (save loaded), {} rejected",
                result.written, result.unchanged, result.heldBackSeedBound, result.rejected);
    return result;
}

bool SeedBoundChangesAllowed() {
    typedef uint8_t (*FnIsOnFileSelect)(void);
    static FnIsOnFileSelect isOnFileSelect = nullptr;
    if (!isOnFileSelect) {
        isOnFileSelect = (FnIsOnFileSelect)Combo_ResolveSym("soh", "SOH_IsOnFileSelect");
    }
    return isOnFileSelect != nullptr && isOnFileSelect() != 0 && !ComboUI::IsMmActive();
}

std::string DescribeApplyResult(const ApplyResult& result) {
    std::string text = std::to_string(result.written) + " settings applied";
    if (result.unchanged > 0) {
        text += ", " + std::to_string(result.unchanged) + " already matched";
    }
    text += ".";
    if (result.heldBackSeedBound > 0) {
        text += " " + std::to_string(result.heldBackSeedBound) + kHeldBackNote;
    }
    if (result.rejected > 0) {
        text += " " + std::to_string(result.rejected) + kRejectedNote;
    }
    if (result.needsRestart) {
        text += kRestartNote;
    }
    return text;
}

} // namespace ComboPresets
