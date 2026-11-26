// combo/gui/ComboBackToBackPanel.cpp — see ComboBackToBackPanel.h
#include "ComboBackToBackPanel.h"
#include "ComboExport.h"
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

constexpr const char* kNewFilesNote =
    "These choices apply to new back-to-back files when they're created. A file keeps the carries it was created "
    "with; the item switches can be changed later on This file.";
constexpr const char* kThisFileNote =
    "The choices of the back-to-back file that is loaded. The item switches take effect at once and are saved with the "
    "file. The greyed ones were made when the file was created and stay as they are.";
constexpr const char* kNoFileLoaded = "No back-to-back file is loaded. Load one to see and change its choices here.";
constexpr const char* kPuristTooltip =
    "Every carry off and every item switch off: Ocarina of Time stays in Hyrule, and Majora's Mask is played as it is.";
constexpr const char* kDefaultTooltip = "The defaults of this page.";
constexpr const char* kEverythingTooltip = "Every choice on.";
constexpr const char* kCycleResetSection = "When the Song of Time resets the cycle";
constexpr const char* kCarrySectionThisFile = "Carried into Termina (fixed when the file was created)";
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

// The loaded file's options, which the launcher owns (registered with ComboUI_SetBackToBackStore). The getter answers
// whether a back-to-back file is loaded.
using FnGetFileOptions = int (*)(BackToBack::OptionBits*);
using FnSetFileOptions = void (*)(BackToBack::OptionBits);
FnGetFileOptions sGetFileOptions = nullptr;
FnSetFileOptions sSetFileOptions = nullptr;

enum class View { NewFiles, ThisFile };
View sView = View::NewFiles;

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

// One option's checkbox. Two options may share a label (Bottles carries, and is usable early), so each is scoped by its
// CVar. Returns true when the player toggled it.
bool DrawOptionBox(const BackToBack::OptionDef& option, bool* on, bool enabled) {
    ImGui::PushID(option.cvar);
    ImGui::BeginDisabled(!enabled);
    ComboMenu_PushCheckbox(ComboMenu_ThemeColor());
    const bool toggled = ImGui::Checkbox(option.label, on);
    ComboMenu_PopCheckbox();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", option.tooltip);
    }
    ImGui::PopID();
    return toggled;
}

// An option of the page for new files, kept in its CVar.
void DrawOption(const BackToBack::OptionDef& option) {
    bool on = CVarGetInteger(option.cvar, option.defaultOn ? 1 : 0) != 0;
    if (DrawOptionBox(option, &on, true)) {
        CVarSetInteger(option.cvar, on ? 1 : 0);
        SaveSettingsNextFrame();
    }
}

void DrawOptionGroup(BackToBack::OptionGroup group) {
    for (const BackToBack::OptionDef& option : BackToBack::kOptions) {
        if (option.group == group) {
            DrawOption(option);
        }
    }
}

void ApplyQuickSet(BackToBack::QuickSet set) {
    const BackToBack::OptionBits options = BackToBack::QuickSetOptions(set);
    for (const BackToBack::OptionDef& option : BackToBack::kOptions) {
        CVarSetInteger(option.cvar, (options & option.bit) != 0 ? 1 : 0);
    }
    SaveSettingsNextFrame();
}

void DrawQuickSetButton(const char* label, const char* tooltip, BackToBack::QuickSet set) {
    ComboMenu_PushButton(ComboMenu_ThemeColor());
    if (ImGui::Button(label)) {
        ApplyQuickSet(set);
    }
    ComboMenu_PopButton();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
}

void DrawQuickSetButtons() {
    ImGui::TextUnformatted("Set every choice below:");
    ImGui::SameLine();
    DrawQuickSetButton("Purist", kPuristTooltip, BackToBack::QuickSet::Purist);
    ImGui::SameLine();
    DrawQuickSetButton("Default", kDefaultTooltip, BackToBack::QuickSet::Default);
    ImGui::SameLine();
    DrawQuickSetButton("Everything", kEverythingTooltip, BackToBack::QuickSet::Everything);
}

void DrawViewSwitch() {
    ComboMenu_PushCheckbox(ComboMenu_ThemeColor());
    if (ImGui::RadioButton("New files", sView == View::NewFiles)) {
        sView = View::NewFiles;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("This file", sView == View::ThisFile)) {
        sView = View::ThisFile;
    }
    ComboMenu_PopCheckbox();
}

void DrawThisFileGroup(BackToBack::OptionGroup group, BackToBack::OptionBits* options) {
    for (const BackToBack::OptionDef& option : BackToBack::kOptions) {
        if (option.group != group) {
            continue;
        }
        bool on = (*options & option.bit) != 0;
        if (DrawOptionBox(option, &on, BackToBack::EditableOnLoadedFile(option))) {
            *options = on ? (*options | option.bit) : (*options & ~option.bit);
            sSetFileOptions(*options);
        }
    }
}

void DrawThisFileView() {
    BackToBack::OptionBits options = 0;
    if (!sGetFileOptions || !sSetFileOptions || sGetFileOptions(&options) == 0) {
        DrawHintText(kNoFileLoaded);
        return;
    }
    ImGui::TextWrapped("%s", kThisFileNote);
    ImGui::SeparatorText(kCarrySectionThisFile);
    DrawThisFileGroup(BackToBack::OptionGroup::Carry, &options);
    ImGui::SeparatorText("Ocarina of Time items usable in Termina");
    DrawThisFileGroup(BackToBack::OptionGroup::OotOnlyItems, &options);
    ImGui::SeparatorText("Usable in Termina before Majora's Mask's own is found");
    DrawThisFileGroup(BackToBack::OptionGroup::EarlyUse, &options);
    ImGui::SeparatorText(kCycleResetSection);
    DrawThisFileGroup(BackToBack::OptionGroup::CycleReset, &options);
}

void DrawNewFilesView() {
    ImGui::TextWrapped("%s", kNewFilesNote);
    DrawQuickSetButtons();
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
    ImGui::SeparatorText(kCycleResetSection);
    DrawOptionGroup(BackToBack::OptionGroup::CycleReset);
}

} // namespace

void DrawBackToBackSharedPanel() {
    DrawShowSetting();
    DrawViewSwitch();
    if (sView == View::ThisFile) {
        DrawThisFileView();
    } else {
        DrawNewFilesView();
    }
}

// The launcher hands comboui the loaded file's options accessors at startup, right before ComboUI_Register: the getter
// returns 1 and the options when a back-to-back file is loaded, the setter changes the editable choices of that file
// (BackToBack::ApplyEditedOptions) and saves them with it.
extern "C" COMBO_EXPORT void ComboUI_SetBackToBackStore(int (*getter)(BackToBack::OptionBits*),
                                                        void (*setter)(BackToBack::OptionBits)) {
    sGetFileOptions = getter;
    sSetFileOptions = setter;
}

} // namespace ComboRando
