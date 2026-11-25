// combo/gui/ComboSettingsLink.cpp
//
// ComboShip: OOT and MM each own a private CVar for the same preference (camera speed, invert, first-person
// aim, quality-of-life toggles, cheats...). The names differ by design (OOT reads gSettings.FreeLook.*, MM reads
// gEnhancements.Camera.*). The Both games menu (ComboBothGames.cpp) shows one control per pair and, when it is
// changed, calls LinkCopyToOtherGame so both games get the value. The per-game controls stay as they are and
// only change their own game. Pairs are in ComboLinkRows.h.
//
// Lives in comboui because the CVar store is one instance across the exe and both game DLLs, so a copy is plain
// reads/writes, the same arrangement ComboCosmeticsSync.cpp uses. Each write is handed to the owning game's
// MenuApplyCVarChange so it takes effect without a restart.
//
// NOT linked on purpose: anything only one game has (OOT's D-pad on pause / in text, MM's camera pitch clamps),
// anything already shared (gSettings.Controllers.* bindings, volumes via ComboAudioBridge, HUD colours via
// ComboCosmeticsSync), D-pad equips (the combo's own setting owns them), and everything the randomizer reads.
#include <libultraship/libultraship.h> // CVar API
#include "ComboSettingsLink.h"
#include <ship/Context.h> // SaveConsoleVariablesNextFrame
#include <spdlog/spdlog.h>
#include "ComboMenuModel.h"
#include <climits>
#include <cmath>
#include <cstring>

namespace ComboRando {

namespace {

// One marker per seed wave, indexed by wave - 1. Siblings of the other gCombo.LinkSettings* names, never children of
// one: a key can't be a value and a parent at once in the nested config. "Seeded" keeps them out of presets.
constexpr const char* kSeedMarkers[kSeedWaveCount] = { "gCombo.LinkSettingsPairsSeeded",
                                                       "gCombo.LinkSettingsCutscenesSeeded" };

constexpr const char* kOotFirstPersonSensitivityPrefix = "gSettings.FirstPersonCameraSensitivity.";
constexpr const char* kOotFirstPersonSensitivityMaster = "gSettings.FirstPersonCameraSensitivity.Enabled";

// NaN when the key has never been set. An unset key means "this game's own default", so it is never copied:
// reading it as 0 would write camera speed 0 / invert off into the other game.
float ReadKey(const char* key, LinkType type) {
    if (type == LinkType::Float) {
        return CVarGetFloat(key, NAN);
    }
    const int v = CVarGetInteger(key, INT_MIN);
    return (v == INT_MIN) ? NAN : static_cast<float>(v);
}

float EffectiveOrDefault(float stored, float defaultValue) {
    return std::isnan(stored) ? defaultValue : stored;
}

const char* SideKey(const LinkRow& row, GameSide side) {
    return side == GameSide::Oot ? row.oot : row.mm[0];
}

float SideDefault(const LinkRow& row, GameSide side) {
    return side == GameSide::Oot ? row.ootDefault : row.mmDefault;
}

void WriteKey(const char* key, LinkType type, float value, Fn_MenuApplyCVar apply) {
    if (type == LinkType::Float) {
        CVarSetFloat(key, value);
    } else {
        CVarSetInteger(key, static_cast<int>(value));
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
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();
    return { model.Oot().applyCVarChange, model.Mm().applyCVarChange };
}

// OOT clears its aiming-sensitivity values whenever the master checkbox is off, so a value arriving
// from MM has to switch that master on or it would be wiped on the next menu draw.
void EnableOotFirstPersonSensitivityIfNeeded(const LinkRow& row, Fn_MenuApplyCVar apply) {
    if (std::strncmp(row.oot, kOotFirstPersonSensitivityPrefix, std::strlen(kOotFirstPersonSensitivityPrefix)) != 0) {
        return;
    }
    if (CVarGetInteger(kOotFirstPersonSensitivityMaster, 0) != 1) {
        CVarSetInteger(kOotFirstPersonSensitivityMaster, 1);
        if (apply) {
            apply(kOotFirstPersonSensitivityMaster);
        }
    }
}

// Writes `value` to one key unless the game already uses it.
bool WriteIfDifferent(const char* key, LinkType type, float defaultValue, float value, Fn_MenuApplyCVar apply) {
    if (EffectiveOrDefault(ReadKey(key, type), defaultValue) == value) {
        return false;
    }
    WriteKey(key, type, value, apply);
    return true;
}

bool CopyValue(const LinkRow& row, GameSide from, float value, const Applies& fns) {
    bool wrote = false;
    if (from == GameSide::Oot) {
        for (const char* key : row.mm) {
            if (key) {
                wrote |= WriteIfDifferent(key, row.type, row.mmDefault, value, fns.mm);
            }
        }
        return wrote;
    }
    wrote = WriteIfDifferent(row.oot, row.type, row.ootDefault, value, fns.oot);
    if (wrote) {
        EnableOotFirstPersonSensitivityIfNeeded(row, fns.oot);
    }
    // MM's second key (its left-stick/gyro twin) follows its right-stick partner.
    for (int k = 1; k < 3 && row.mm[k]; ++k) {
        wrote |= WriteIfDifferent(row.mm[k], row.type, row.mmDefault, value, fns.mm);
    }
    return wrote;
}

// A key counts as "moved" only when it is set to something other than its default.
bool MovedOffDefault(float stored, float defaultValue) {
    return !std::isnan(stored) && stored != defaultValue;
}

// One-time merge of two existing configs, judged on the values the games use (unset keys count as their default).
//   * A switch that is off by default in either game: on in either means on in both.
//   * Anything else: the side that moved off its default is copied to the other; when both moved, OOT wins.
// A pair that already agrees is never written.
void SeedRow(const LinkRow& row, const Applies& fns, bool* wrote) {
    const float ootStored = ReadKey(row.oot, row.type);
    const float mmStored = ReadKey(row.mm[0], row.type);
    const float oot = EffectiveOrDefault(ootStored, row.ootDefault);
    const float mm = EffectiveOrDefault(mmStored, row.mmDefault);
    if (oot == mm) {
        return;
    }
    const bool onWins = row.type == LinkType::Switch && (row.ootDefault == 0 || row.mmDefault == 0);
    GameSide from = GameSide::Oot;
    if (onWins) {
        from = (oot != 0) ? GameSide::Oot : GameSide::Mm;
    } else if (!MovedOffDefault(ootStored, row.ootDefault)) {
        if (!MovedOffDefault(mmStored, row.mmDefault)) {
            return;
        }
        from = GameSide::Mm;
    }
    const float value = (from == GameSide::Oot) ? oot : mm;
    if (CopyValue(row, from, value, fns)) {
        SPDLOG_INFO("[ComboShip] settings link seeded {} from {} ({})", row.oot, from == GameSide::Oot ? "OOT" : "MM",
                    value);
        *wrote = true;
    }
}

bool BothGamesLoaded() {
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();
    return model.Oot().loaded && model.Mm().loaded;
}

void SeedWave(int wave, const Applies& fns) {
    bool wrote = false;
    for (const LinkRow& row : kLinkRows) {
        if (row.seedWave == wave) {
            SeedRow(row, fns, &wrote);
        }
    }
    CVarSetInteger(kSeedMarkers[wave - 1], 1);
    SPDLOG_INFO("[ComboShip] settings link: seed wave {} done ({})", wave, wrote ? "values copied" : "nothing to copy");
}

bool WaveDone(int wave) {
    return CVarGetInteger(kSeedMarkers[wave - 1], 0) != 0;
}

} // namespace

float LinkEffectiveValue(const LinkRow& row, GameSide side) {
    return EffectiveOrDefault(ReadKey(SideKey(row, side), row.type), SideDefault(row, side));
}

bool LinkValuesDiffer(const LinkRow& row) {
    return LinkEffectiveValue(row, GameSide::Oot) != LinkEffectiveValue(row, GameSide::Mm);
}

bool LinkCopyToOtherGame(const LinkRow& row, GameSide from) {
    const bool wrote = CopyValue(row, from, LinkEffectiveValue(row, from), ResolveApplies());
    if (wrote) {
        RequestCVarSave();
    }
    return wrote;
}

void LinkSeedIfNeeded() {
    static bool sAllDone = false;
    if (sAllDone) {
        return;
    }
    bool pending = false;
    for (int wave = kFirstSeedWave; wave <= kSeedWaveCount; ++wave) {
        pending = pending || !WaveDone(wave);
    }
    if (!pending) {
        sAllDone = true;
        return;
    }
    if (!BothGamesLoaded()) {
        return; // seeding needs both games' live-apply hooks; try again next frame
    }
    const Applies fns = ResolveApplies();
    for (int wave = kFirstSeedWave; wave <= kSeedWaveCount; ++wave) {
        if (!WaveDone(wave)) {
            SeedWave(wave, fns);
        }
    }
    RequestCVarSave(); // persists the markers even when no value changed
    sAllDone = true;
}

} // namespace ComboRando
