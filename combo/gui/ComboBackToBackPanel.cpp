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
constexpr const char* kMasterQuestTooltip =
    "Ocarina of Time's half of a new back-to-back file plays Master Quest. Fixed when the file is created.";
constexpr const char* kMasterQuestUnavailable =
    "Needs the Master Quest ROM (oot-mq.o2r). Until it is extracted, new back-to-back files play Normal Quest.";

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

} // namespace

void DrawBackToBackSharedPanel() {
    ImGui::TextWrapped("%s", kPageNote);
    ImGui::SeparatorText("Ocarina of Time");
    DrawMasterQuestSetting();
}

} // namespace ComboRando
