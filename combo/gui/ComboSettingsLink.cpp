// combo/gui/ComboSettingsLink.cpp
//
// ComboShip: OOT and MM each own a private CVar for the same preference — camera speed, invert,
// first-person aim, the ocarina stick, D-pad equips. The names differ by design (OOT reads
// gSettings.FreeLook.*, MM reads gEnhancements.Camera.*), so a player had to find and set two
// sliders for one thing. This links each pair: move either control and the other follows.
//
// Lives in comboui because the CVar store is one instance across the exe and both game DLLs, so
// the link is plain reads/writes — the same arrangement ComboCosmeticsSync.cpp uses. Each write is
// handed to the owning game's MenuApplyCVarChange so it takes effect without a restart.
//
// NOT linked on purpose: anything only one game has (OOT's D-pad on pause / in text, MM's camera
// pitch clamps), and anything already shared (gSettings.Controllers.* bindings, volumes via
// ComboAudioBridge, HUD colours via ComboCosmeticsSync).
#include <libultraship/libultraship.h> // CVar API
#include "ComboExport.h"
#include "ComboSettingsLink.h"
#include <ship/Context.h> // SaveConsoleVariablesNextFrame
#include <spdlog/spdlog.h>
#include "ComboMenuModel.h"
#include <climits>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr const char* kLinkCVar = "gCombo.LinkSettings";
// Set once the first push has happened, so switching the link on seeds MM from OOT exactly once.
constexpr const char* kSeededCVar = "gCombo.LinkSettingsSeeded";

enum LinkType { LINK_INT, LINK_FLOAT };

// One preference, one row. mm[] is nullptr-terminated: OOT's single first-person sensitivity feeds
// BOTH of MM's (left-stick/gyro and right-stick), which is why there is room for more than one.
// When MM is the side that changed, mm[0] is the authority.
struct LinkRow {
    const char* oot;
    LinkType type;
    const char* mm[3];
};

const LinkRow kRows[] = {
    // Third-person / free look
    { "gSettings.FreeLook.Enabled", LINK_INT, { "gEnhancements.Camera.FreeLook.Enable" } },
    { "gSettings.FreeLook.CameraSensitivity.X",
      LINK_FLOAT,
      { "gEnhancements.Camera.RightStick.CameraSensitivity.X" } },
    { "gSettings.FreeLook.CameraSensitivity.Y",
      LINK_FLOAT,
      { "gEnhancements.Camera.RightStick.CameraSensitivity.Y" } },
    { "gSettings.FreeLook.InvertXAxis", LINK_INT, { "gEnhancements.Camera.RightStick.InvertXAxis" } },
    { "gSettings.FreeLook.InvertYAxis", LINK_INT, { "gEnhancements.Camera.RightStick.InvertYAxis" } },
    { "gSettings.FreeLook.MaxCameraDistance", LINK_INT, { "gEnhancements.Camera.FreeLook.MaxCameraDistance" } },
    { "gSettings.FreeLook.TransitionSpeed", LINK_INT, { "gEnhancements.Camera.FreeLook.TransitionSpeed" } },
    // First person
    { "gSettings.Controls.RightStickAim", LINK_INT, { "gEnhancements.Camera.FirstPerson.RightStickEnabled" } },
    { "gSettings.MoveInFirstPerson", LINK_INT, { "gEnhancements.Camera.FirstPerson.MoveInFirstPerson" } },
    { "gSettings.DisableFirstPersonAutoCenterView",
      LINK_INT,
      { "gEnhancements.Camera.FirstPerson.DisableFirstPersonAutoCenterView" } },
    // MM splits aiming sensitivity in two (left stick / gyro, and right stick); OOT has one slider,
    // so it drives both. Coming back the other way, MM's right-stick value is the one OOT takes.
    { "gSettings.FirstPersonCameraSensitivity.X",
      LINK_FLOAT,
      { "gEnhancements.Camera.FirstPerson.RightStickSensitivityX", "gEnhancements.Camera.FirstPerson.SensitivityX" } },
    { "gSettings.FirstPersonCameraSensitivity.Y",
      LINK_FLOAT,
      { "gEnhancements.Camera.FirstPerson.RightStickSensitivityY", "gEnhancements.Camera.FirstPerson.SensitivityY" } },
    // Ocarina input
    { "gSettings.CustomOcarina.RightStick", LINK_INT, { "gEnhancements.Playback.RightStickOcarina" } },
    { "gSettings.CustomOcarina.Dpad", LINK_INT, { "gEnhancements.Playback.DpadOcarina" } },
    // D-pad as extra item buttons. Both leaves live under the shared gEnhancements prefix but are
    // different keys, so this is a link like any other.
    { "gEnhancements.DpadEquips", LINK_INT, { "gEnhancements.Dpad.DpadEquips" } },
};

constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

// Last value this module saw on each side, so a poll can tell which one the user moved. float holds
// every int we link exactly (all are small), so one cache type covers both. NaN = "unset".
struct RowCache {
    float oot = NAN;
    float mm = NAN;
};
RowCache sCache[kRowCount];
bool sPrimed = false;

// NaN when the key has never been set. An unset key means "this game's own default", and the two games'
// defaults differ (OOT camera speed 1.0, invert-Y on, transition 25...), so an unset key is never copied:
// reading it as 0 would have written camera speed 0 / invert off into the other game.
float ReadKey(const char* key, LinkType type) {
    if (type == LINK_INT) {
        const int v = CVarGetInteger(key, INT_MIN);
        return (v == INT_MIN) ? NAN : static_cast<float>(v);
    }
    return CVarGetFloat(key, NAN);
}

// Equal including "both unset" (NaN != NaN otherwise).
bool Same(float a, float b) {
    return (std::isnan(a) && std::isnan(b)) || (a == b);
}

void WriteKey(const char* key, LinkType type, float value, Fn_MenuApplyCVar apply) {
    if (type == LINK_INT) {
        CVarSetInteger(key, static_cast<int>(value));
    } else {
        CVarSetFloat(key, value);
    }
    if (apply) {
        apply(key);
    }
}

void RequestCVarSave() {
    if (auto* ctx = Ship::Context::GetRawInstance()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

struct Applies {
    Fn_MenuApplyCVar oot = nullptr;
    Fn_MenuApplyCVar mm = nullptr;
};

Applies ResolveApplies() {
    auto& model = ComboRando::ComboMenuModel::Get();
    model.EnsureLoaded();
    return { model.Oot().applyCVarChange, model.Mm().applyCVarChange };
}

// OOT clears its aiming-sensitivity values whenever the master checkbox is off, so a value arriving
// from MM has to switch that master on or it would be wiped on the next menu draw.
void EnableOotFirstPersonSensitivityIfNeeded(const LinkRow& row, Fn_MenuApplyCVar apply) {
    if (std::strncmp(row.oot, "gSettings.FirstPersonCameraSensitivity.", 39) != 0) {
        return;
    }
    const char* master = "gSettings.FirstPersonCameraSensitivity.Enabled";
    if (CVarGetInteger(master, 0) != 1) {
        CVarSetInteger(master, 1);
        if (apply) {
            apply(master);
        }
    }
}

void PushOotToMm(int i, const Applies& fns, bool* wrote) {
    const LinkRow& row = kRows[i];
    const float value = ReadKey(row.oot, row.type);
    if (std::isnan(value)) {
        // Unset on OOT's side. If it WAS set (the player just reset it to default), reset MM too; if it never
        // was, leave MM alone on its own default.
        if (!std::isnan(sCache[i].oot)) {
            for (const char* mmKey : row.mm) {
                if (!mmKey) {
                    break;
                }
                CVarClear(mmKey);
                if (fns.mm) {
                    fns.mm(mmKey);
                }
                *wrote = true;
            }
        }
        sCache[i].oot = value;
        sCache[i].mm = ReadKey(row.mm[0], row.type);
        return;
    }
    for (const char* mmKey : row.mm) {
        if (!mmKey) {
            break;
        }
        if (!Same(ReadKey(mmKey, row.type), value)) {
            WriteKey(mmKey, row.type, value, fns.mm);
            *wrote = true;
        }
    }
    sCache[i].oot = value;
    sCache[i].mm = value;
}

void PushMmToOot(int i, const Applies& fns, bool* wrote) {
    const LinkRow& row = kRows[i];
    const float value = ReadKey(row.mm[0], row.type);
    if (std::isnan(value)) {
        // Unset on MM's side: mirror a reset-to-default, never invent a value.
        if (!std::isnan(sCache[i].mm)) {
            CVarClear(row.oot);
            if (fns.oot) {
                fns.oot(row.oot);
            }
            for (int k = 1; k < 3 && row.mm[k]; ++k) {
                CVarClear(row.mm[k]);
                if (fns.mm) {
                    fns.mm(row.mm[k]);
                }
            }
            *wrote = true;
        }
        sCache[i].oot = ReadKey(row.oot, row.type);
        sCache[i].mm = value;
        return;
    }
    if (!Same(ReadKey(row.oot, row.type), value)) {
        WriteKey(row.oot, row.type, value, fns.oot);
        EnableOotFirstPersonSensitivityIfNeeded(row, fns.oot);
        *wrote = true;
    }
    // MM's second key (its left-stick/gyro twin) follows its right-stick partner.
    for (int k = 1; k < 3 && row.mm[k]; ++k) {
        if (!Same(ReadKey(row.mm[k], row.type), value)) {
            WriteKey(row.mm[k], row.type, value, fns.mm);
            *wrote = true;
        }
    }
    sCache[i].oot = value;
    sCache[i].mm = value;
}

} // namespace

extern "C" COMBO_EXPORT int ComboUI_SettingsLinkEnabled(void) {
    return CVarGetInteger(kLinkCVar, 1) != 0 ? 1 : 0;
}

extern "C" COMBO_EXPORT int ComboUI_SettingsLinkCount(void) {
    return kRowCount;
}

extern "C" COMBO_EXPORT void ComboUI_SettingsLinkPushOotToMm(void) {
    const Applies fns = ResolveApplies();
    bool wrote = false;
    int copied = 0;
    for (int i = 0; i < kRowCount; ++i) {
        bool rowWrote = false;
        PushOotToMm(i, fns, &rowWrote);
        copied += rowWrote ? 1 : 0;
        wrote = wrote || rowWrote;
    }
    sPrimed = true;
    CVarSetInteger(kSeededCVar, 1);
    if (wrote) {
        RequestCVarSave();
    }
    SPDLOG_INFO("[ComboShip] settings link on: {} of {} linked settings copied OOT -> MM (unset ones left alone)",
                copied, kRowCount);
}

extern "C" COMBO_EXPORT void ComboUI_SettingsLinkPoll(void) {
    if (!ComboUI_SettingsLinkEnabled()) {
        sPrimed = false; // re-seed from OOT when the link is switched back on
        return;
    }
    // First poll after the link is switched on: OOT is the side the Shared tab shows, so it wins once.
    if (!sPrimed) {
        ComboUI_SettingsLinkPushOotToMm();
        return;
    }

    const Applies fns = ResolveApplies();
    bool wrote = false;
    for (int i = 0; i < kRowCount; ++i) {
        const LinkRow& row = kRows[i];
        const float ootNow = ReadKey(row.oot, row.type);
        const float mmNow = ReadKey(row.mm[0], row.type);
        if (Same(ootNow, sCache[i].oot) && Same(mmNow, sCache[i].mm)) {
            continue; // neither side touched
        }
        // OOT wins a simultaneous change: it is the side the Shared tab puts in front of the player.
        if (!Same(ootNow, sCache[i].oot)) {
            PushOotToMm(i, fns, &wrote);
        } else {
            PushMmToOot(i, fns, &wrote);
        }
    }
    if (wrote) {
        RequestCVarSave();
    }
}
