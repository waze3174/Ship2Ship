// combo/gui/ComboBackToBackPanel.cpp — see ComboBackToBackPanel.h
#include "ComboBackToBackPanel.h"
#include "ComboPresetPanels.h" // DrawHintText
#include "ComboResolve.h"      // Combo_ResolveSym for the SOH_ archive query
#include "ComboWidgetStyle.h"
#include "rando/ComboBackToBackSettings.h"
#include <imgui.h>
#include <libultraship/libultraship.h> // CVar bridge
#include <ship/Context.h>
#include <ship/window/Window.h>

namespace ComboRando {

namespace {

constexpr const char* kPageNote = "These choices apply to new back-to-back files when they're created. A file keeps "
                                  "the choices it was created with.";
constexpr const char* kShowTooltip =
    "Offers Back to back as a choice when you make a new file. Files you already made keep working either way.";
constexpr const char* kMasterQuestTooltip =
    "Ocarina of Time's half of a new back-to-back file plays Master Quest. Fixed when the file is created.";
constexpr const char* kMasterQuestUnavailable =
    "Needs the Master Quest ROM (oot-mq.o2r). Until it is extracted, new back-to-back files play Normal Quest.";

constexpr const char* kCarryNote =
    "The quiver, bomb bag, Deku stick and Deku nut upgrades always carry over, once Link has that item in "
    "Majora's Mask.";
constexpr const char* kOotOnlyItemsNote =
    "Items only Ocarina of Time has. Off, each stays in Hyrule until both games are beaten.";
constexpr const char* kEarlyUseNote =
    "Items Majora's Mask has too wait in Termina until Link finds Majora's Mask's own. Tick one to use "
    "Ocarina of Time's from the start. The Goron and Zora masks can always be worn there as plain masks; tick them "
    "to let them transform Link from the start, not only once he has Majora's Mask's own. Magic gives Link Ocarina of "
    "Time's magic meter from the start.";

using FnHasArchive = int (*)();

bool OotMasterQuestArchiveLoaded() {
    static FnHasArchive hasArchive = nullptr;
    if (!hasArchive) {
        hasArchive = reinterpret_cast<FnHasArchive>(Combo_ResolveSym("soh", "SOH_GameHasMasterQuest"));
    }
    return hasArchive && hasArchive() != 0;
}

void SaveSettingsNextFrame() {
    auto ctx = Ship::Context::GetRawInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

void DrawShowSetting() {
    bool show = CVarGetInteger(kBackToBackShowCVar, kBackToBackShowDefault) != 0;
    ComboMenu_PushCheckbox(ComboMenu_ThemeColor());
    if (ImGui::Checkbox("Show Back to back on the file select", &show)) {
        CVarSetInteger(kBackToBackShowCVar, show ? 1 : 0);
        SaveSettingsNextFrame();
    }
    ComboMenu_PopCheckbox();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", kShowTooltip);
    }
}

// The setting is only offered when the archive is there; with it missing the box shows unchecked and the stored value
// is left alone.
void DrawMasterQuestSetting() {
    const bool available = OotMasterQuestArchiveLoaded();
    bool masterQuest = available && CVarGetInteger(kBackToBackMasterQuestCVar, 0) != 0;
    ImGui::BeginDisabled(!available);
    ComboMenu_PushCheckbox(ComboMenu_ThemeColor());
    if (ImGui::Checkbox("Master Quest", &masterQuest)) {
        CVarSetInteger(kBackToBackMasterQuestCVar, masterQuest ? 1 : 0);
        SaveSettingsNextFrame();
    }
    ComboMenu_PopCheckbox();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", available ? kMasterQuestTooltip : kMasterQuestUnavailable);
    }
    if (!available) {
        DrawHintText(kMasterQuestUnavailable);
    }
}

// Two options may share a label (Bottles carries, and is usable early), so each is scoped by its CVar.
void DrawOption(const BackToBack::OptionDef& option) {
    bool on = CVarGetInteger(option.cvar, option.defaultOn ? 1 : 0) != 0;
    ImGui::PushID(option.cvar);
    ComboMenu_PushCheckbox(ComboMenu_ThemeColor());
    if (ImGui::Checkbox(option.label, &on)) {
        CVarSetInteger(option.cvar, on ? 1 : 0);
        SaveSettingsNextFrame();
    }
    ComboMenu_PopCheckbox();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", option.tooltip);
    }
    ImGui::PopID();
}

void DrawOptionGroup(BackToBack::OptionGroup group) {
    for (const BackToBack::OptionDef& option : BackToBack::kOptions) {
        if (option.group == group) {
            DrawOption(option);
        }
    }
}

} // namespace

void DrawBackToBackSharedPanel() {
    DrawShowSetting();
    ImGui::TextWrapped("%s", kPageNote);
    ImGui::SeparatorText("Ocarina of Time");
    DrawMasterQuestSetting();
    ImGui::SeparatorText("Carried into Termina");
    DrawHintText(kCarryNote);
    DrawOptionGroup(BackToBack::OptionGroup::Carry);
    ImGui::SeparatorText("Ocarina of Time items usable in Termina");
    DrawHintText(kOotOnlyItemsNote);
    DrawOptionGroup(BackToBack::OptionGroup::OotOnlyItems);
    ImGui::SeparatorText("Usable in Termina before Majora's Mask's own is found");
    DrawHintText(kEarlyUseNote);
    DrawOptionGroup(BackToBack::OptionGroup::EarlyUse);
}

} // namespace ComboRando
