// combo/gui/ComboDiscordPanel.cpp — see ComboDiscordPanel.h
#include "ComboDiscordPanel.h"

#include <cstdint>

#include <imgui.h>
#include <libultraship/libultraship.h> // CVar bridge
#include <ship/Context.h>
#include <ship/window/Window.h>

#include "ComboExport.h"
#include "ComboWidgetStyle.h"
#include "discord/DiscordSettings.h"

namespace ComboRando {

namespace {

using ComboDiscord::SettingBit;

struct SettingRow {
    SettingBit bit;
    const char* label;
    const char* tooltip;
};

constexpr SettingRow kMasterRow = {
    ComboDiscord::kSettingEnabled,
    "Show on Discord",
    "Shows the game and mode you are playing on your Discord profile, through the Discord desktop app. "
    "Whatever is shown is public on your Discord profile. Off disconnects from Discord completely.",
};

constexpr SettingRow kFieldRows[] = {
    { ComboDiscord::kSettingArea, "Area name",
      "The area you are in, such as Clock Town. Hidden whenever a co-op room hides locations." },
    { ComboDiscord::kSettingChecks, "Checks collected", "Checks collected out of the total, in randomizer saves." },
    { ComboDiscord::kSettingLink, "Link's form, age and hearts", "For example \"Goron Link\" and \"12 hearts\"." },
    { ComboDiscord::kSettingClock, "Majora's Mask day and time",
      "The day of the cycle and the hour, in Termina only." },
    { ComboDiscord::kSettingCoop, "Co-op and player count",
      "\"Co-op (2 players)\" while you are in a co-op room. The room's name is never sent." },
    { ComboDiscord::kSettingSessionTimer, "Session timer",
      "Discord's own counter of how long Ship2Ship has been running." },
    { ComboDiscord::kSettingPlayTime, "Save's total play time",
      "The play time of the save, both games added up, to the minute." },
};

bool DrawSetting(const SettingRow& row) {
    const char* cvar = ComboDiscord::CVarOf(row.bit);
    bool on = CVarGetInteger(cvar, ComboDiscord::kSettingDefault) != 0;
    const bool changed = ImGui::Checkbox(row.label, &on);
    if (changed)
        CVarSetInteger(cvar, on ? 1 : 0);
    ImGui::SetItemTooltip("%s", row.tooltip);
    return changed;
}

} // namespace

void DrawDiscordSharedPanel() {
    const ImVec4 theme = ComboMenu_ThemeColor();
    bool changed = false;

    ComboMenu_PushCheckbox(theme);
    changed |= DrawSetting(kMasterRow);
    ImGui::TextDisabled("The game and the mode are always shown while this is on. Discord desktop only.");

    const bool enabled =
        CVarGetInteger(ComboDiscord::CVarOf(ComboDiscord::kSettingEnabled), ComboDiscord::kSettingDefault) != 0;
    ImGui::SeparatorText("Also show");
    ImGui::BeginDisabled(!enabled);
    for (const SettingRow& row : kFieldRows)
        changed |= DrawSetting(row);
    ImGui::EndDisabled();
    ComboMenu_PopCheckbox();

    if (changed) {
        if (auto ctx = Ship::Context::GetRawInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
}

} // namespace ComboRando

// The launcher has no CVar API, so it reads every switch here as one bit mask (ComboDiscord::SettingBit).
extern "C" COMBO_EXPORT uint32_t ComboUI_GetDiscordSettings(void) {
    uint32_t settings = 0;
    for (const ComboDiscord::SettingCVar& entry : ComboDiscord::kSettingCVars) {
        if (CVarGetInteger(entry.cvar, ComboDiscord::kSettingDefault) != 0)
            settings |= entry.bit;
    }
    return settings;
}
