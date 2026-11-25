// combo/gui/ComboSettingsLink.cpp
//
// ComboShip: OOT and MM each own a private CVar for the same preference — camera speed, invert,
// first-person aim, the ocarina stick, D-pad equips, quality-of-life toggles, cheats. The names differ
// by design (OOT reads gSettings.FreeLook.*, MM reads gEnhancements.Camera.*), so a player had to find
// and set two controls for one thing. This links each pair (table in ComboLinkRows.h): move either
// control and the other follows. ComboBothGames.cpp draws each pair once and hides the per-game copies.
//
// Lives in comboui because the CVar store is one instance across the exe and both game DLLs, so
// the link is plain reads/writes — the same arrangement ComboCosmeticsSync.cpp uses. Each write is
// handed to the owning game's MenuApplyCVarChange so it takes effect without a restart.
//
// NOT linked on purpose: anything only one game has (OOT's D-pad on pause / in text, MM's camera
// pitch clamps), anything already shared (gSettings.Controllers.* bindings, volumes via
// ComboAudioBridge, HUD colours via ComboCosmeticsSync), and everything the randomizer reads.
#include <libultraship/libultraship.h> // CVar API
#include "ComboExport.h"
#include "ComboLinkRows.h"
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
constexpr int kLinkDefault = 1;
// Set once the pairs have been merged from both games' existing values. A sibling leaf of
// gCombo.LinkSettings, never a child of it: a key can't be a value and a parent at once in the nested config.
constexpr const char* kSeededCVar = "gCombo.LinkSettingsPairsSeeded";

using ComboRando::kLinkRowCount;
using ComboRando::kLinkRows;
using ComboRando::LinkRow;
using ComboRando::LinkType;

// Last value this module saw on each side, so a poll can tell which one the user moved. float holds
// every int we link exactly (all are small), so one cache type covers both. NaN = "unset".
struct RowCache {
    float oot = NAN;
    float mm = NAN;
};
RowCache sCache[kLinkRowCount];
bool sPrimed = false;

// NaN when the key has never been set. An unset key means "this game's own default", and the two games'
// defaults differ (OOT camera speed 1.0, invert-Y on, transition 25...), so an unset key is never copied:
// reading it as 0 would have written camera speed 0 / invert off into the other game.
float ReadKey(const char* key, LinkType type) {
    if (type == LinkType::Int) {
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
    if (type == LinkType::Int) {
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
    const LinkRow& row = kLinkRows[i];
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
    const LinkRow& row = kLinkRows[i];
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

// A key counts as "moved" only when it is set to something other than its default. Defaults are equal on
// both sides, and an unset key is never copied.
bool MovedOffDefault(float stored, const LinkRow& row) {
    return !std::isnan(stored) && stored != row.defaultValue;
}

// One-time merge of two existing configs. The side that moved off its default is copied to the other; when both
// moved, OOT wins (it is the side the Shared tab draws). An on/off switch that defaults to off therefore ends up
// on in both games when it was on in either.
void SeedRow(int i, const Applies& fns, bool* wrote) {
    const LinkRow& row = kLinkRows[i];
    const float oot = ReadKey(row.oot, row.type);
    const float mm = ReadKey(row.mm[0], row.type);
    const bool fromOot = MovedOffDefault(oot, row);
    if (!fromOot && !MovedOffDefault(mm, row)) {
        return;
    }
    bool rowWrote = false;
    if (fromOot) {
        PushOotToMm(i, fns, &rowWrote);
    } else {
        PushMmToOot(i, fns, &rowWrote);
    }
    if (rowWrote) {
        SPDLOG_INFO("[ComboShip] settings link seeded {} from {} ({})", row.oot, fromOot ? "OOT" : "MM",
                    fromOot ? oot : mm);
    }
    *wrote = *wrote || rowWrote;
}

void PrimeCaches() {
    for (int i = 0; i < kLinkRowCount; ++i) {
        sCache[i].oot = ReadKey(kLinkRows[i].oot, kLinkRows[i].type);
        sCache[i].mm = ReadKey(kLinkRows[i].mm[0], kLinkRows[i].type);
    }
    sPrimed = true;
}

void SeedPairs(const Applies& fns) {
    bool wrote = false;
    for (int i = 0; i < kLinkRowCount; ++i) {
        SeedRow(i, fns, &wrote);
    }
    PrimeCaches();
    CVarSetInteger(kSeededCVar, 1);
    RequestCVarSave(); // persists the marker even when no value changed
    SPDLOG_INFO("[ComboShip] settings link: first-time merge of {} linked settings done ({})", kLinkRowCount,
                wrote ? "values copied" : "nothing to copy");
}

bool BothGamesLoaded() {
    auto& model = ComboRando::ComboMenuModel::Get();
    model.EnsureLoaded();
    return model.Oot().loaded && model.Mm().loaded;
}

} // namespace

extern "C" COMBO_EXPORT int ComboUI_SettingsLinkEnabled(void) {
    return CVarGetInteger(kLinkCVar, kLinkDefault) != 0 ? 1 : 0;
}

extern "C" COMBO_EXPORT int ComboUI_SettingsLinkCount(void) {
    return kLinkRowCount;
}

extern "C" COMBO_EXPORT void ComboUI_SettingsLinkPushOotToMm(void) {
    const Applies fns = ResolveApplies();
    bool wrote = false;
    int copied = 0;
    for (int i = 0; i < kLinkRowCount; ++i) {
        bool rowWrote = false;
        PushOotToMm(i, fns, &rowWrote);
        copied += rowWrote ? 1 : 0;
        wrote = wrote || rowWrote;
    }
    sPrimed = true;
    CVarSetInteger(kSeededCVar, 1); // an explicit adopt supersedes the first-time merge
    if (wrote) {
        RequestCVarSave();
    }
    SPDLOG_INFO("[ComboShip] settings link on: {} of {} linked settings copied OOT -> MM (unset ones left alone)",
                copied, kLinkRowCount);
}

extern "C" COMBO_EXPORT void ComboUI_SettingsLinkPoll(void) {
    if (!ComboUI_SettingsLinkEnabled()) {
        sPrimed = false; // adopt OOT's values when the link is switched back on
        return;
    }
    if (!sPrimed) {
        if (!BothGamesLoaded()) {
            return; // seeding needs both games' live-apply hooks; try again next frame
        }
        // Link switched back on after the first merge: OOT is the side the Shared tab shows, so it wins.
        if (CVarGetInteger(kSeededCVar, 0)) {
            ComboUI_SettingsLinkPushOotToMm();
        } else {
            SeedPairs(ResolveApplies());
        }
        return;
    }

    const Applies fns = ResolveApplies();
    bool wrote = false;
    for (int i = 0; i < kLinkRowCount; ++i) {
        const LinkRow& row = kLinkRows[i];
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
