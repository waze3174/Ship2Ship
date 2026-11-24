// combo/gui/ComboPresetsPanel.cpp
//
// ComboShip: Settings > Presets. See ComboPresets.h for what a preset is.
#include "ComboPresetPanels.h"
#include "ComboPlayPreset.h"
#include "ComboPresets.h"
#include "ComboWidgetStyle.h"

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

namespace ComboRando {

namespace {

const ImVec4 kStatusOkColor(0.5f, 1.0f, 0.5f, 1.0f);
const ImVec4 kStatusErrorColor(1.0f, 0.4f, 0.4f, 1.0f);
constexpr float kNameFieldWidth = 260.0f;
constexpr float kListNameColumnWidth = 300.0f;

constexpr const char* kScopeNote =
    "A preset keeps your randomizer, gameplay, cosmetic, audio, cheat, tracker and Anchor settings for both games. "
    "It never keeps controller mappings, mod lists, graphics or window settings. Applying one changes only the "
    "settings it holds.";
constexpr const char* kSeedBoundNote =
    "Randomizer options in a preset are applied only on the OoT file-select screen, with no save loaded.";

char sNameBuf[ComboPresets::kMaxNameLength + 1] = {};
std::vector<std::string> sNames;
bool sNamesStale = true;
std::string sPendingOverwrite;
std::string sPendingDelete;
std::string sStatus;
bool sStatusIsError = false;

void SetStatus(std::string text, bool isError) {
    sStatus = std::move(text);
    sStatusIsError = isError;
}

void RefreshNamesIfStale() {
    if (sNamesStale) {
        sNames = ComboPresets::ListNames();
        sNamesStale = false;
    }
}

void SavePresetNamed(std::string name) {
    const std::string problem = ComboPresets::Save(name);
    sPendingOverwrite.clear();
    sNamesStale = true;
    SetStatus(problem.empty() ? "Saved preset '" + name + "'." : problem, !problem.empty());
}

void RequestSave() {
    const std::string name = sNameBuf;
    const std::string problem = ComboPresets::ValidateName(name);
    if (!problem.empty()) {
        SetStatus(problem, true);
    } else if (ComboPresets::Exists(name)) {
        sPendingOverwrite = name;
    } else {
        SavePresetNamed(name);
    }
}

void ApplyPresetNamed(const std::string& name) {
    ComboPresets::Leaves leaves;
    const std::string problem = ComboPresets::Load(name, leaves);
    if (!problem.empty()) {
        SetStatus(problem, true);
        return;
    }
    const ComboPresets::ApplyOptions options{ ComboPresets::SeedBoundChangesAllowed() };
    const ComboPresets::ApplyResult result = ComboPresets::WriteLeaves(leaves, options);
    SetStatus("Preset '" + name + "': " + ComboPresets::DescribeApplyResult(result), false);
}

void DeletePresetNamed(std::string name) {
    const std::string problem = ComboPresets::Remove(name);
    sPendingDelete.clear();
    sNamesStale = true;
    SetStatus(problem.empty() ? "Deleted preset '" + name + "'." : problem, !problem.empty());
}

void DrawSaveRow(const ImVec4& theme) {
    ImGui::SetNextItemWidth(kNameFieldWidth);
    ImGui::InputTextWithHint("##presetname", "Preset name", sNameBuf, sizeof(sNameBuf));
    if (sPendingOverwrite != sNameBuf) {
        sPendingOverwrite.clear();
    }
    ImGui::SameLine();
    ComboMenu_PushButton(theme);
    if (ImGui::Button("Save")) {
        RequestSave();
    }
    if (!sPendingOverwrite.empty()) {
        ImGui::TextWrapped("A preset named '%s' already exists. Replace it with the current settings?",
                           sPendingOverwrite.c_str());
        if (ImGui::Button("Overwrite")) {
            SavePresetNamed(sPendingOverwrite);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            sPendingOverwrite.clear();
        }
    }
    ComboMenu_PopButton();
}

void DrawPresetRow(const std::string& name) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());
    ImGui::SameLine(kListNameColumnWidth);
    if (sPendingDelete == name) {
        ImGui::TextUnformatted("Delete this preset?");
        ImGui::SameLine();
        if (ImGui::Button("Yes, delete")) {
            DeletePresetNamed(name);
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep")) {
            sPendingDelete.clear();
        }
        return;
    }
    if (ImGui::Button("Apply")) {
        ApplyPresetNamed(name);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        sPendingDelete = name;
    }
}

void DrawPresetList(const ImVec4& theme) {
    RefreshNamesIfStale();
    ImGui::SeparatorText("Saved presets");
    if (sNames.empty()) {
        ImGui::TextDisabled("No presets saved yet.");
        return;
    }
    ComboMenu_PushButton(theme);
    for (const std::string& name : sNames) {
        ImGui::PushID(name.c_str());
        DrawPresetRow(name);
        ImGui::PopID();
    }
    ComboMenu_PopButton();
}

} // namespace

void DrawStatusLine(const std::string& text, bool isError) {
    if (text.empty()) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, isError ? kStatusErrorColor : kStatusOkColor);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void DrawHintText(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void DrawPresetsSharedPanel() {
    const ImVec4 theme = ComboMenu_ThemeColor();
    ImGui::TextWrapped("%s", kScopeNote);
    if (!ComboPresets::SeedBoundChangesAllowed()) {
        DrawHintText(kSeedBoundNote);
    }
    ImGui::Spacing();
    DrawSaveRow(theme);
    DrawStatusLine(sStatus, sStatusIsError);
    DrawPresetList(theme);
    ImGui::Spacing();
    DrawHintText("Folder: " + ComboPresets::PresetDir().string());
}

} // namespace ComboRando
