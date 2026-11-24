// combo/gui/ComboConfigImportPanel.cpp
//
// ComboShip: Settings > Import from ComboShip. See ComboConfigImport.h.
#include "ComboExport.h"
#include "ComboFilePicker.h"  // native Browse dialog (comdlg32 / pfd), shared with the setup screens
#include "ComboWidgetStyle.h" // ComboMenu_ThemeColor / PushButton / PushCheckbox

#include <libultraship/libultraship.h>
#include <imgui.h>

#include "ComboConfigImport.h"
#include "ComboPlayPreset.h"
#include "ComboPresetPanels.h"

#include <cstddef>
#include <cstdio>
#include <string>

namespace ComboRando {

namespace {

constexpr float kPathFieldWidth = 520.0f;
constexpr size_t kPathBufferSize = 1024;

constexpr const char* kIntroNote =
    "Bring over the settings of another ComboShip install, for example ComboShip v0.3.1. Choose its comboship.json. "
    "Your current comboship.json is copied to comboship.json.bak-<date and time> first, and the file's window "
    "settings are never imported.";
constexpr const char* kModsAndControlsTip =
    "Imports the other install's mod lists and controller mappings too.\n"
    "Mod list changes take effect after a restart. Turn this off to keep this install's mods and controls.";
constexpr const char* kNoPickerNote =
    "No file-picker helper found (install zenity or kdialog). Type or paste the path to comboship.json instead.";

char sPathBuf[kPathBufferSize] = {};
bool sIncludeModsAndControls = true;
std::string sStatus;
bool sStatusIsError = false;

std::string PickComboShipConfig() {
    return ComboFilePicker::PickFile("Select a ComboShip settings file (comboship.json)",
                                     "Settings (*.json)\0*.json\0All Files (*.*)\0*.*\0",
                                     { "Settings (*.json)", "*.json", "All Files", "*" });
}

void RunImport() {
    ComboPresets::ImportOptions options;
    options.includeModsAndControls = sIncludeModsAndControls;
    options.allowSeedBound = ComboPresets::SeedBoundChangesAllowed();
    const ComboPresets::ImportOutcome outcome = ComboPresets::ImportComboShipConfig(sPathBuf, options);
    sStatusIsError = !outcome.error.empty();
    if (sStatusIsError) {
        sStatus = outcome.error;
        return;
    }
    sStatus = "Imported. " + ComboPresets::DescribeApplyResult(outcome.applied);
    sStatus += " Previous settings saved as " + outcome.backupName + ".";
}

} // namespace

void DrawConfigImportSharedPanel() {
    const ImVec4 theme = ComboMenu_ThemeColor();
    ImGui::TextWrapped("%s", kIntroNote);
    ImGui::Spacing();

    ComboMenu_PushCheckbox(theme);
    ImGui::Checkbox("Also import mods list and controls", &sIncludeModsAndControls);
    ComboMenu_PopCheckbox();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", kModsAndControlsTip);
    }

    ImGui::SetNextItemWidth(kPathFieldWidth);
    ImGui::InputTextWithHint("##combocfgpath", "Path to comboship.json", sPathBuf, sizeof(sPathBuf));
    ImGui::SameLine();
    ComboMenu_PushButton(theme);
    ImGui::BeginDisabled(!ComboFilePicker::Available());
    if (ImGui::Button("Browse...")) {
        const std::string picked = PickComboShipConfig();
        if (!picked.empty()) {
            std::snprintf(sPathBuf, sizeof(sPathBuf), "%s", picked.c_str());
        }
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(sPathBuf[0] == '\0');
    if (ImGui::Button("Import settings from ComboShip...")) {
        RunImport();
    }
    ImGui::EndDisabled();
    ComboMenu_PopButton();

    if (!ComboFilePicker::Available()) {
        DrawHintText(kNoPickerNote);
    }
    DrawStatusLine(sStatus, sStatusIsError);
}

} // namespace ComboRando
