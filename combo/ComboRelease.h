// combo/ComboRelease.h
//
// ComboShip: "Release checks on boss kill". With gCombo.ReleaseOnBossKill on, beating a game's final boss hands the
// player every check of that game that is still uncollected, all at once and silently. The launcher
// (Combo_ReleaseGameChecks) decides when; each game's SOH_/MM_ReleaseRemainingChecks does the giving. This header
// holds what the launcher and both games agree on. Header-only and std only. Deviation record:
// docs/deviations/rando.md "Release checks on boss kill".
#pragma once

#include <string>

namespace ComboRelease {

// Play-time setting (off by default). Deliberately outside gCombo.Rando.*, which is seed-bound: the owner's
// current seed gets it with no new seed.
inline constexpr const char* kSettingCVar = "gCombo.ReleaseOnBossKill";

// Return value of SOH_/MM_ReleaseRemainingChecks when the game could not release (no rando save loaded).
inline constexpr int kCannotRelease = -1;

// Key under the container's "combo" section: {"oot": bool, "mm": bool}, set once a game's checks were released.
inline constexpr const char* kReleasedContainerKey = "released";

inline constexpr const char* kOotName = "Ocarina of Time";
inline constexpr const char* kMmName = "Majora's Mask";

// The one notification a release raises, e.g. "Released 143 checks from" + "Ocarina of Time".
inline std::string ReleasedMessage(int count) {
    return "Released " + std::to_string(count) + (count == 1 ? " check from" : " checks from");
}

} // namespace ComboRelease
