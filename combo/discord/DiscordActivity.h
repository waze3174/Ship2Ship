// combo/discord/DiscordActivity.h
//
// ComboShip: turns what the launcher knows about the running game into the activity Discord shows on the profile.
// Pure: no I/O and no clock, so it is tested on its own (tests/DiscordActivityTest.cpp). Header-only; compiled into
// the launcher. See docs/deviations/discord.md.
//
// Everything this file lets out of the process is public on the player's Discord profile. It never carries paths,
// seeds, names, room names or ids, or save contents.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DiscordSettings.h"
#include "rando/ComboMode.h"

namespace ComboDiscord {

inline constexpr const char* kApplicationId = "1575091654120050811"; // public; the Discord developer portal app
inline constexpr const char* kLargeImageKey = "ship2ship";
inline constexpr const char* kButtonLabel = "Get Ship2Ship";
inline constexpr const char* kButtonUrl = "https://github.com/waze3174/Ship2Ship/releases";
inline constexpr const char* kMenuStatus = "In the menus";
inline constexpr const char* kProductName = "Ship2Ship";
inline constexpr const char* kUnknownArea = "Unknown";
inline constexpr const char* kFieldSeparator = " \xC2\xB7 "; // a middle dot between spaces
inline constexpr const char* kEllipsis = "\xE2\x80\xA6";
// Discord's limit for details, state and image text. Counted in bytes, which is the stricter reading.
inline constexpr size_t kMaxFieldBytes = 128;
// The in-game clock is shown to this step, so the text (and with it the presence) changes once per game hour, not
// once per game minute.
inline constexpr int kClockStepMinutes = 60;
inline constexpr int kMinutesPerHour = 60;
inline constexpr int kFirstDay = 1;
inline constexpr int kLastDay = 3;

enum class Game { kOot, kMm };

// What the launcher assembled for one moment. A field a game can't report keeps its "unavailable" default.
struct PresenceState {
    bool inPlay = false; // false at the title, file select, credits and between games
    Game game = Game::kOot;
    std::string mode;             // ModeLabel
    std::string area;             // the scene name, empty when unknown
    bool locationsHidden = false; // an Anchor room hides locations
    int checksGotten = -1;
    int checksTotal = -1;
    int form = 0; // soh's ComboForm: 0 human, 1 Goron, 2 Zora, 3 Deku, 4 Fierce Deity
    bool adult = false;
    int maxHearts = -1;
    int day = 0;          // Majora's Mask: 1 to 3
    int minuteOfDay = -1; // Majora's Mask
    int coopPlayers = 0;  // players online in the Anchor room; 0 outside a room
    int64_t playMinutes = -1;
};

// The one place a slot's mode becomes a label. A new mode is a new case here.
inline std::string ModeLabel(const ComboRando::ComboMode& mode) {
    if (mode.IsVanillaMm())
        return "Majora's Mask";
    if (mode.IsVanillaOot())
        return "Ocarina of Time";
    return "Randomizer";
}

inline const char* GameName(Game game) {
    return game == Game::kMm ? "Majora's Mask" : "Ocarina of Time";
}

// Cuts to at most maxBytes without splitting a UTF-8 character, ending in an ellipsis when it cut.
inline std::string TrimToBytes(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes)
        return text;
    size_t cut = maxBytes - std::strlen(kEllipsis);
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    std::string out = text.substr(0, cut);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out + kEllipsis;
}

// "Market (Child - Day)" is "Market": the variant in brackets says nothing a profile needs.
inline std::string CleanAreaName(std::string name) {
    const size_t open = name.rfind(" (");
    if (open != std::string::npos && !name.empty() && name.back() == ')')
        name.erase(open);
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name == kUnknownArea ? std::string() : name;
}

inline std::string FormName(int form) {
    static const char* const kNames[] = { "Goron", "Zora", "Deku", "Fierce Deity" }; // forms 1 to 4; 0 is human
    const int index = form - 1;
    return (index >= 0 && index < static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))) ? kNames[index] : "";
}

inline std::string PlayTimeText(int64_t minutes) {
    const int64_t hours = minutes / kMinutesPerHour;
    const std::string rest = std::to_string(minutes % kMinutesPerHour) + "m played";
    return hours > 0 ? std::to_string(hours) + "h " + rest : rest;
}

inline std::string ClockText(int day, int minuteOfDay) {
    const int minute = minuteOfDay - (minuteOfDay % kClockStepMinutes);
    char hhmm[8];
    std::snprintf(hhmm, sizeof(hhmm), "%02d:%02d", minute / kMinutesPerHour, minute % kMinutesPerHour);
    return "Day " + std::to_string(day) + ", " + hhmm;
}

// The pieces of the state line, most important first. Whatever doesn't fit goes to the image's hover text.
inline std::vector<std::string> StateSegments(const PresenceState& s, uint32_t settings) {
    std::vector<std::string> out;
    if (!s.mode.empty())
        out.push_back(s.mode);
    if (SettingOn(settings, kSettingChecks) && s.checksGotten >= 0 && s.checksTotal > 0)
        out.push_back(std::to_string(s.checksGotten) + " / " + std::to_string(s.checksTotal) + " checks");
    if (SettingOn(settings, kSettingLink)) {
        const std::string form = FormName(s.form);
        out.push_back(form.empty() ? (s.adult ? "Adult Link" : "Child Link") : form + " Link");
        if (s.maxHearts > 0)
            out.push_back(std::to_string(s.maxHearts) + (s.maxHearts == 1 ? " heart" : " hearts"));
    }
    if (SettingOn(settings, kSettingClock) && s.game == Game::kMm && s.day >= kFirstDay && s.day <= kLastDay &&
        s.minuteOfDay >= 0)
        out.push_back(ClockText(s.day, s.minuteOfDay));
    if (SettingOn(settings, kSettingCoop) && s.coopPlayers > 0)
        out.push_back(std::string("Co-op (") + std::to_string(s.coopPlayers) +
                      (s.coopPlayers == 1 ? " player)" : " players)"));
    if (SettingOn(settings, kSettingPlayTime) && s.playMinutes >= 0)
        out.push_back(PlayTimeText(s.playMinutes));
    return out;
}

inline std::string DetailsText(const PresenceState& s, uint32_t settings) {
    if (!s.inPlay)
        return kMenuStatus;
    const std::string area = CleanAreaName(s.area);
    const bool showArea = SettingOn(settings, kSettingArea) && !s.locationsHidden && !area.empty();
    return showArea ? std::string(GameName(s.game)) + ": " + area : GameName(s.game);
}

// The activity object for SET_ACTIVITY. sessionStartEpoch is when the program started, in Unix seconds.
inline nlohmann::json FormatActivity(const PresenceState& s, uint32_t settings, const std::string& version,
                                     int64_t sessionStartEpoch) {
    std::string state;
    std::string hover = std::string(kProductName) + " v" + version;
    if (s.inPlay) {
        for (const std::string& segment : StateSegments(s, settings)) {
            const std::string joined = state.empty() ? segment : state + kFieldSeparator + segment;
            if (joined.size() <= kMaxFieldBytes)
                state = joined;
            else
                hover += kFieldSeparator + segment;
        }
    }

    nlohmann::json activity;
    activity["details"] = TrimToBytes(DetailsText(s, settings), kMaxFieldBytes);
    if (!state.empty())
        activity["state"] = state;
    if (SettingOn(settings, kSettingSessionTimer))
        activity["timestamps"] = { { "start", sessionStartEpoch } };
    activity["assets"] = { { "large_image", kLargeImageKey }, { "large_text", TrimToBytes(hover, kMaxFieldBytes) } };
    activity["buttons"] = nlohmann::json::array({ { { "label", kButtonLabel }, { "url", kButtonUrl } } });
    return activity;
}

} // namespace ComboDiscord
