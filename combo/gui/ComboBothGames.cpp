// combo/gui/ComboBothGames.cpp
//
// ComboShip: draws Both games > Same setting in both. See ComboBothGames.h for the rules. Controls are found by CVar in
// each game's menu model.
#include "ComboBothGames.h"
#include "ComboMenuModel.h"
#include "ComboSettingsLink.h"
#include "ComboWidgetRender.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace ComboRando {

namespace {

const ImVec4 kDiffersColor(1.0f, 0.75f, 0.2f, 1.0f);

struct SectionInfo {
    BothSection id;
    const char* tab;
    const char* heading;
};

// Display order of the group: a tab's sections are listed together, tabs in first-appearance order.
constexpr SectionInfo kSections[] = {
    { BothSection::Infinite, "Cheats", "Infinite" },
    { BothSection::MovementAndItems, "Cheats", "Movement and items" },
    { BothSection::GlitchAids, "Cheats", "Glitch aids" },
    { BothSection::SpeedModifier, "Cheats", "Speed modifier" },
    { BothSection::Player, "Gameplay", "Link" },
    { BothSection::Items, "Gameplay", "Items" },
    { BothSection::Difficulty, "Gameplay", "Difficulty" },
    { BothSection::Camera, "Controls", "Third-person camera" },
    { BothSection::Aiming, "Controls", "Aiming and first person" },
    { BothSection::Ocarina, "Controls", "Ocarina" },
    { BothSection::Saving, "Quality of life", "Saving" },
    { BothSection::TimeSavers, "Quality of life", "Time savers" },
    { BothSection::Cutscenes, "Quality of life", "Cutscenes" },
    { BothSection::Input, "Quality of life", "Input" },
    { BothSection::Display, "Graphics", "Display" },
    { BothSection::ModSupport, "Graphics", "Mod support" },
    { BothSection::Accessibility, "Accessibility", "Accessibility" },
    { BothSection::Notifications, "Accessibility", "Notifications" },
};

// A CVar both games read under the same name: one setting already, so it needs no copy.
struct SharedItem {
    const char* cvar;
    BothSection section;
    const char* label; // printed above the control; nullptr for none
};

constexpr SharedItem kSharedItems[] = {
    { "gCheats.InfiniteHealth", BothSection::Infinite, nullptr },
    { "gCheats.InfiniteMagic", BothSection::Infinite, nullptr },
    { "gCheats.NoClip", BothSection::MovementAndItems, nullptr },
    { "gCheats.MoonJumpOnL", BothSection::MovementAndItems, nullptr },
    { "gCheats.EasyFrameAdvance", BothSection::GlitchAids, nullptr },
    // MM-only gate for the two shared controls below; OOT applies the multiplier whenever its button is held.
    { "gCheats.SpeedModifier.Mode", BothSection::SpeedModifier, "Majora's Mask: when the speed modifier applies" },
    { "gCheats.SpeedModifier.Value", BothSection::SpeedModifier, nullptr },
    { "gCheats.SpeedModifier.Btn", BothSection::SpeedModifier, nullptr },
    { "gEnhancements.Player.DashAfterRoll", BothSection::Player, nullptr },
    { "gSettings.SimulatedInputLag", BothSection::Input, nullptr },
    { "gSettings.LowResMode", BothSection::Display, nullptr },
};

const SectionInfo& SectionById(BothSection id) {
    for (const SectionInfo& info : kSections) {
        if (info.id == id) {
            return info;
        }
    }
    return kSections[0];
}

bool IsCVarWidget(const CwWidget& w) {
    return w.cvar && w.cvar[0] && w.kind != CW_SEPARATOR && w.kind != CW_SEPARATOR_TEXT && w.kind != CW_TEXT &&
           w.kind != CW_CUSTOM && w.kind != CW_WINDOW_BUTTON;
}

// Finds the control of a CVar in one game's menu model. The model is process-stable, so the index is rebuilt only
// when the model pointer changes. A CVar can be drawn in more than one place; the first control wins.
class MenuIndex {
  public:
    const CwWidget* Find(const GameMenu& game, const char* cvar) {
        if (!game.loaded || !game.menu) {
            return nullptr;
        }
        if (mMenu != game.menu) {
            Rebuild(*game.menu);
        }
        const auto it = mByCVar.find(cvar);
        return it == mByCVar.end() ? nullptr : it->second;
    }

  private:
    void Rebuild(const CwMenu& menu) {
        mMenu = &menu;
        mByCVar.clear();
        for (int s = 0; s < menu.sectionCount; ++s) {
            for (int sb = 0; sb < menu.sections[s].sidebarCount; ++sb) {
                const CwSidebar& sidebar = menu.sections[s].sidebars[sb];
                for (int w = 0; w < sidebar.widgetCount; ++w) {
                    if (IsCVarWidget(sidebar.widgets[w])) {
                        mByCVar.emplace(sidebar.widgets[w].cvar, &sidebar.widgets[w]);
                    }
                }
            }
        }
    }

    const CwMenu* mMenu = nullptr;
    std::unordered_map<std::string, const CwWidget*> mByCVar;
};

// OOT's control when it has one, else MM's. The OOT camera and aiming controls are drawn by the game's input
// editor, so for those rows the control is MM's.
BothGamesItem Resolve(const char* ootCVar, const char* mmCVar, BothSection section, const char* label,
                      const LinkRow* row) {
    static MenuIndex sOotIndex;
    static MenuIndex sMmIndex;
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();
    const char* tab = SectionById(section).tab;
    if (const CwWidget* w = sOotIndex.Find(model.Oot(), ootCVar)) {
        return { w, &model.Oot(), GameSide::Oot, tab, label, row };
    }
    if (const CwWidget* w = sMmIndex.Find(model.Mm(), mmCVar)) {
        return { w, &model.Mm(), GameSide::Mm, tab, label, row };
    }
    return {};
}

std::vector<BothGamesItem> ResolveSection(BothSection section) {
    std::vector<BothGamesItem> items;
    for (const SharedItem& item : kSharedItems) {
        if (item.section != section) {
            continue;
        }
        const BothGamesItem resolved = Resolve(item.cvar, item.cvar, section, item.label, nullptr);
        if (resolved.widget) {
            items.push_back(resolved);
        }
    }
    for (const LinkRow& row : kLinkRows) {
        if (row.section != section) {
            continue;
        }
        const BothGamesItem resolved = Resolve(row.oot, row.mm[0], section, nullptr, &row);
        if (resolved.widget) {
            items.push_back(resolved);
        }
    }
    return items;
}

std::string FormatValue(const BothGamesItem& item, float value) {
    if (item.row->type == LinkType::Switch) {
        return value != 0 ? "On" : "Off";
    }
    if (item.widget->kind == CW_COMBOBOX) {
        for (int i = 0; i < item.widget->choiceCount; ++i) {
            if (item.widget->choices[i].value == static_cast<int32_t>(value)) {
                return item.widget->choices[i].label;
            }
        }
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    return text;
}

void DrawDiffers(const BothGamesItem& item) {
    ImGui::TextColored(kDiffersColor, "Differs: Ocarina of Time %s, Majora's Mask %s",
                       FormatValue(item, LinkEffectiveValue(*item.row, GameSide::Oot)).c_str(),
                       FormatValue(item, LinkEffectiveValue(*item.row, GameSide::Mm)).c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Changing this control sets both games.");
    }
}

} // namespace

std::vector<BothGamesItem> BothGamesItems() {
    std::vector<BothGamesItem> all;
    for (const SectionInfo& info : kSections) {
        const std::vector<BothGamesItem> items = ResolveSection(info.id);
        all.insert(all.end(), items.begin(), items.end());
    }
    return all;
}

std::vector<const char*> BothGamesTabs() {
    std::vector<const char*> tabs;
    for (const SectionInfo& info : kSections) {
        const bool listed = !tabs.empty() && std::strcmp(tabs.back(), info.tab) == 0;
        if (!listed && !ResolveSection(info.id).empty()) {
            tabs.push_back(info.tab);
        }
    }
    return tabs;
}

void DrawBothGamesItem(const BothGamesItem& item, int widthBudget) {
    if (item.label) {
        ImGui::TextUnformatted(item.label);
    }
    const float before = item.row ? LinkEffectiveValue(*item.row, item.drawnFrom) : 0.0f;
    // Both games number their widgets from zero, so scope the ImGui ID per game.
    ImGui::PushID(item.drawnFrom == GameSide::Oot ? "oot" : "mm");
    RenderWidget(*item.widget, *item.game, widthBudget);
    ImGui::PopID();
    if (!item.row) {
        return;
    }
    if (LinkEffectiveValue(*item.row, item.drawnFrom) != before) {
        LinkCopyToOtherGame(*item.row, item.drawnFrom);
    }
    if (LinkValuesDiffer(*item.row)) {
        DrawDiffers(item);
    }
}

void DrawBothGamesTab(const char* tab) {
    ImGui::TextDisabled("Changing a control here sets it in Ocarina of Time and in Majora's Mask. The game tabs "
                        "still change only their own game.");
    for (const SectionInfo& info : kSections) {
        if (std::strcmp(info.tab, tab) != 0) {
            continue;
        }
        const std::vector<BothGamesItem> items = ResolveSection(info.id);
        if (items.empty()) {
            continue;
        }
        ImGui::SeparatorText(info.heading);
        for (const BothGamesItem& item : items) {
            DrawBothGamesItem(item);
        }
    }
}

} // namespace ComboRando
