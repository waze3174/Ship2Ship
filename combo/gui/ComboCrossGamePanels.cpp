// combo/gui/ComboCrossGamePanels.cpp — see ComboCrossGamePanels.h
#include "ComboCrossGamePanels.h"
#include "ComboPresetPanels.h" // DrawHintText
#include "ComboWidgetStyle.h"
#include <imgui.h>
#include <libultraship/libultraship.h> // CVar bridge
#include <ship/Context.h>
#include <ship/window/Window.h>

namespace ComboRando {

namespace {

constexpr const char* kBottleShelfOpenCVar = "gCombo.BottleShelf.Open";

void SaveSettingsNextFrame() {
    auto ctx = Ship::Context::GetRawInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

const std::vector<LiveSwitch> kSwitches = {
    { CrossPage::ItemsAndForms, "gCombo.Forms.InHyrule", 1, "Transformations in Hyrule",
      "In randomizer files, Goron/Zora Masks transform in OoT too; every form carries between the games.", nullptr,
      nullptr },
    { CrossPage::ItemsAndForms, "gCombo.Forms.InVanillaOot", 0, "Transformations in vanilla OoT",
      "Applies to vanilla saves only.",
      "In a Normal or Master Quest save, OoT's Goron and Zora Masks transform Link.\n"
      "Off: the masks are plain masks, as in vanilla OoT.",
      nullptr },
    { CrossPage::ItemsAndForms, "gCombo.Forms.ItemAnalogs", 1, "Form moves count as items",
      "Goron lifts like Golden Gauntlets, punch/pound = Megaton Hammer, Zora fins =\n"
      "Boomerang, Deku bubble = Slingshot: solve Hyrule's puzzles with form moves.",
      nullptr, nullptr },
    { CrossPage::ItemsAndForms, "gCombo.DpadEquips", 1, "D-pad item slots (both games)",
      "Four more item buttons on the D-pad, in Hyrule and Termina.", nullptr, &ApplyDpadEquips },
    { CrossPage::SharedHealthAndAmmo, "gCombo.Shared.HealthMagicAmmo", 0, "Shared health, magic and ammo",
      "One health bar, magic meter and ammo count for both games; every heart piece or\n"
      "container is worth half. Double Defense carries over. Logic is not told about it.\n"
      "Has no effect in a one-game seed.",
      nullptr, nullptr },
    { CrossPage::Look, "gCombo.MmHud.OotStyle", 1, "Majora's Mask HUD looks like Ocarina of Time's", nullptr,
      "In Termina, the HUD uses Ocarina of Time's button sprites, labels and layout,\n"
      "and the same colour and HUD Placement options as Hyrule -- set them once\n"
      "in the Ocarina of Time cosmetics and both games follow. The three-day clock\n"
      "stays exactly as Majora's Mask draws it.",
      nullptr },
    { CrossPage::Look, "gCombo.Cosmetics.AnchorTunicSync", 0, "Anchor color on every tunic",
      "Your Anchor color becomes every tunic in both games: Kokiri/Goron/Zora Tunic and the\n"
      "Deku/Goron/Zora/Fierce Deity forms. Set it once in the Anchor panel (Settings > Online).",
      nullptr, nullptr },
    { CrossPage::Look, "gCombo.Rando.SyncCosmetics", 0, "Sync randomized cosmetics",
      "Applies only when BOTH games' \"randomize cosmetics on randomizer generation\" options are\n"
      "enabled. Shared elements (buttons, hearts, magic, minimap, Link's tunic, ...) take Ocarina\n"
      "of Time's colors in Majora's Mask.",
      nullptr, nullptr },
};

const std::vector<CrossPage> kPages = { CrossPage::ItemsAndForms, CrossPage::SharedHealthAndAmmo, CrossPage::Look };

void DrawBottleShelfButton() {
    if (ImGui::Button("Open Bottle Shelf")) {
        CVarSetInteger(kBottleShelfOpenCVar, 1);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("In-game: pause, put the cursor on a bottle and press A, then left/right to cycle all 10.\n"
                        "This window can also fill an empty bottle slot from the shelf.");
}

} // namespace

const std::vector<LiveSwitch>& LiveSwitches() {
    return kSwitches;
}

const std::vector<CrossPage>& CrossPages() {
    return kPages;
}

const char* CrossPageLabel(CrossPage page) {
    switch (page) {
        case CrossPage::ItemsAndForms:
            return "Items and forms";
        case CrossPage::SharedHealthAndAmmo:
            return "Shared health and ammo";
        case CrossPage::Look:
            return "Look";
    }
    return "";
}

void DrawLiveSwitch(const LiveSwitch& sw, bool hintBeside) {
    bool on = CVarGetInteger(sw.cvar, sw.defaultValue) != 0;
    ComboMenu_PushCheckbox(ComboMenu_ThemeColor());
    const bool changed = ImGui::Checkbox(sw.label, &on);
    ComboMenu_PopCheckbox();
    if (sw.tooltip) {
        ImGui::SetItemTooltip("%s", sw.tooltip);
    }
    if (changed) {
        CVarSetInteger(sw.cvar, on ? 1 : 0);
        if (sw.onChange) {
            sw.onChange();
        }
        SaveSettingsNextFrame();
    }
    if (!sw.hint) {
        return;
    }
    if (hintBeside) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", sw.hint);
    } else {
        DrawHintText(sw.hint);
    }
}

void DrawCrossGamePage(CrossPage page) {
    ImGui::TextDisabled("These work while you play, in both games. Changing one needs no new seed.");
    for (const LiveSwitch& sw : kSwitches) {
        if (sw.page == page) {
            DrawLiveSwitch(sw);
        }
    }
    if (page == CrossPage::ItemsAndForms) {
        DrawBottleShelfButton();
    }
}

void ApplyDpadEquips() {
    const int on = CVarGetInteger("gCombo.DpadEquips", 1) ? 1 : 0;
    CVarSetInteger("gEnhancements.DpadEquips", on);      // OoT (SoH)
    CVarSetInteger("gEnhancements.Dpad.DpadEquips", on); // MM (2ship)
}

} // namespace ComboRando
