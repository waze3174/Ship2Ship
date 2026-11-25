// combo/discord/DiscordSettings.h
//
// ComboShip: the Discord Rich Presence switches. Each is a CVar under gCombo.Discord.* (a subtree: gCombo.Discord is
// never a CVar of its own) that defaults to on. comboui draws and reads them (ComboDiscordPanel.cpp) and hands the
// launcher one bit mask, because the launcher has no CVar API. Header-only and std only.
// See docs/deviations/discord.md.
#pragma once

#include <cstdint>

namespace ComboDiscord {

enum SettingBit : uint32_t {
    kSettingEnabled = 1u << 0,      // the master switch; off means no connection to Discord at all
    kSettingArea = 1u << 1,         // the area name
    kSettingChecks = 1u << 2,       // checks collected / total
    kSettingLink = 1u << 3,         // Link's form, age and hearts
    kSettingClock = 1u << 4,        // Majora's Mask's day and time
    kSettingCoop = 1u << 5,         // co-op and the player count
    kSettingSessionTimer = 1u << 6, // Discord's elapsed counter since the program started
    kSettingPlayTime = 1u << 7,     // the save's total play time
};

struct SettingCVar {
    SettingBit bit;
    const char* cvar;
};

inline constexpr SettingCVar kSettingCVars[] = {
    { kSettingEnabled, "gCombo.Discord.Enabled" },
    { kSettingArea, "gCombo.Discord.Area" },
    { kSettingChecks, "gCombo.Discord.Checks" },
    { kSettingLink, "gCombo.Discord.Link" },
    { kSettingClock, "gCombo.Discord.Clock" },
    { kSettingCoop, "gCombo.Discord.Coop" },
    { kSettingSessionTimer, "gCombo.Discord.SessionTimer" },
    { kSettingPlayTime, "gCombo.Discord.PlayTime" },
};

inline constexpr int kSettingDefault = 1;

inline const char* CVarOf(SettingBit bit) {
    for (const SettingCVar& entry : kSettingCVars) {
        if (entry.bit == bit)
            return entry.cvar;
    }
    return nullptr;
}

inline bool SettingOn(uint32_t settings, SettingBit bit) {
    return (settings & bit) != 0;
}

} // namespace ComboDiscord
