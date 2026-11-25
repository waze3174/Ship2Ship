// combo/gui/ComboBothGames.cpp
//
// ComboShip: draws Settings > Both games and tells the per-game tabs and the search which controls
// live there. See ComboBothGames.h for the rules. Controls are found by CVar in the "Enhancements"
// section of each game's menu model: that is where both games keep every setting listed here.
#include "ComboBothGames.h"
#include "ComboLinkRows.h"
#include "ComboMenuModel.h"
#include "ComboSettingsLink.h"
#include "ComboWidgetRender.h"
#include <imgui.h>
#include <cstring>
#include <unordered_map>

namespace ComboRando {

namespace {

constexpr const char* kEnhancementsSection = "Enhancements";
// Where OOT's control for a SharedTab row sits: the Shared tab's own Settings sections.
constexpr const char* kSharedSettingsTab = "Settings";

struct SectionInfo {
    BothSection id;
    const char* tab;
    const char* heading;
};

// Display order of the group: tabs in first-appearance order, sections in table order.
constexpr SectionInfo kSections[] = {
    { BothSection::Infinite, "Cheats", "Infinite" },
    { BothSection::MovementAndItems, "Cheats", "Movement and items" },
    { BothSection::GlitchAids, "Cheats", "Glitch aids" },
    { BothSection::SpeedModifier, "Cheats", "Speed modifier" },
    { BothSection::Player, "Gameplay", "Link" },
    { BothSection::Items, "Gameplay", "Items" },
    { BothSection::Ocarina, "Gameplay", "Ocarina" },
    { BothSection::Difficulty, "Gameplay", "Difficulty" },
    { BothSection::Saving, "Quality of life", "Saving" },
    { BothSection::TimeSavers, "Quality of life", "Time savers" },
    { BothSection::Input, "Quality of life", "Input" },
    { BothSection::Display, "Graphics", "Display" },
    { BothSection::ModSupport, "Graphics", "Mod support" },
};

// A CVar both games read under the same name: one setting already, so it needs no link.
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

const SectionInfo* SectionById(BothSection id) {
    for (const SectionInfo& info : kSections) {
        if (info.id == id) {
            return &info;
        }
    }
    return nullptr;
}

bool LinkIsOn() {
    return ComboUI_SettingsLinkEnabled() != 0;
}

const SharedItem* FindSharedItem(const char* cvar) {
    for (const SharedItem& item : kSharedItems) {
        if (std::strcmp(item.cvar, cvar) == 0) {
            return &item;
        }
    }
    return nullptr;
}

bool RowHasMmKey(const LinkRow& row, const char* cvar) {
    for (const char* key : row.mm) {
        if (key && std::strcmp(key, cvar) == 0) {
            return true;
        }
    }
    return false;
}

// The link row that owns this CVar on the given side, or nullptr.
const LinkRow* FindLinkRow(const char* cvar, GameSide side) {
    for (const LinkRow& row : kLinkRows) {
        const bool match = (side == GameSide::Oot) ? std::strcmp(row.oot, cvar) == 0 : RowHasMmKey(row, cvar);
        if (match) {
            return &row;
        }
    }
    return nullptr;
}

bool IsCVarWidget(const CwWidget& w) {
    return w.cvar && w.cvar[0] && w.kind != CW_SEPARATOR && w.kind != CW_SEPARATOR_TEXT && w.kind != CW_TEXT &&
           w.kind != CW_CUSTOM && w.kind != CW_WINDOW_BUTTON;
}

// Looks up the control of a CVar in one game's Enhancements section. The menu model is process-stable, so the
// index is rebuilt only when the model pointer changes.
class EnhancementsIndex {
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
            const CwSection& section = menu.sections[s];
            if (!section.sectionLabel || std::strcmp(section.sectionLabel, kEnhancementsSection) != 0) {
                continue;
            }
            for (int sb = 0; sb < section.sidebarCount; ++sb) {
                const CwSidebar& sidebar = section.sidebars[sb];
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

// One control ready to draw.
struct Resolved {
    const CwWidget* widget = nullptr;
    const GameMenu* game = nullptr;
    const char* gameId = "";
    const char* label = nullptr;
};

// OOT's control when it has one, else MM's.
Resolved Resolve(const char* ootCVar, const char* mmCVar, const char* label) {
    static EnhancementsIndex sOotIndex;
    static EnhancementsIndex sMmIndex;
    auto& model = ComboMenuModel::Get();
    model.EnsureLoaded();
    if (const CwWidget* w = sOotIndex.Find(model.Oot(), ootCVar)) {
        return { w, &model.Oot(), "oot", label };
    }
    if (const CwWidget* w = sMmIndex.Find(model.Mm(), mmCVar)) {
        return { w, &model.Mm(), "mm", label };
    }
    return {};
}

std::vector<Resolved> ResolveSection(BothSection section) {
    std::vector<Resolved> items;
    for (const SharedItem& item : kSharedItems) {
        if (item.section != section) {
            continue;
        }
        const Resolved r = Resolve(item.cvar, item.cvar, item.label);
        if (r.widget) {
            items.push_back(r);
        }
    }
    if (!LinkIsOn()) {
        return items;
    }
    for (const LinkRow& row : kLinkRows) {
        if (row.placement != LinkPlacement::BothGamesPanel || row.section != section) {
            continue;
        }
        const Resolved r = Resolve(row.oot, row.mm[0], nullptr);
        if (r.widget) {
            items.push_back(r);
        }
    }
    return items;
}

bool HidesCVar(const char* cvar, GameSide side) {
    if (!cvar || !cvar[0]) {
        return false;
    }
    if (FindSharedItem(cvar)) {
        return true;
    }
    if (!LinkIsOn()) {
        return false;
    }
    const LinkRow* row = FindLinkRow(cvar, side);
    if (!row) {
        return false;
    }
    // OOT's control only leaves its tab when the group draws it; MM's leaves whenever OOT's copy is the one shown.
    return side == GameSide::Mm ? row->placement != LinkPlacement::PerGame
                                : row->placement == LinkPlacement::BothGamesPanel;
}

bool IsHeader(const CwWidget& w) {
    return w.kind == CW_SEPARATOR || w.kind == CW_SEPARATOR_TEXT;
}

bool IsContent(const CwWidget& w) {
    return !IsHeader(w) && w.kind != CW_TEXT;
}

} // namespace

bool BothGamesHidesWidget(const CwWidget& widget, GameSide side) {
    return IsCVarWidget(widget) && HidesCVar(widget.cvar, side);
}

bool BothGamesFilterSidebar(const CwSidebar& sidebar, GameSide side, std::vector<CwWidget>& out) {
    const int count = sidebar.widgetCount;
    std::vector<char> keep(count, 1);
    bool anyHidden = false;
    for (int i = 0; i < count; ++i) {
        if (BothGamesHidesWidget(sidebar.widgets[i], side)) {
            keep[i] = 0;
            anyHidden = true;
        }
    }
    if (!anyHidden) {
        return false;
    }
    // Widgets are stored column by column. Walk each column bottom-up so a header with no control left under it
    // goes too.
    bool contentBelow = false;
    int column = -1;
    for (int i = count - 1; i >= 0; --i) {
        const CwWidget& w = sidebar.widgets[i];
        if (w.column != column) {
            column = w.column;
            contentBelow = false;
        }
        if (!keep[i]) {
            continue;
        }
        if (IsHeader(w)) {
            keep[i] = contentBelow ? 1 : 0;
            contentBelow = false;
        } else {
            contentBelow = true;
        }
    }
    out.clear();
    for (int i = 0; i < count; ++i) {
        if (keep[i]) {
            out.push_back(sidebar.widgets[i]);
        }
    }
    return true;
}

bool BothGamesLeavesSidebarEmpty(const CwSidebar& sidebar, GameSide side) {
    bool anyHidden = false;
    for (int i = 0; i < sidebar.widgetCount; ++i) {
        const CwWidget& w = sidebar.widgets[i];
        if (BothGamesHidesWidget(w, side)) {
            anyHidden = true;
        } else if (IsContent(w)) {
            return false;
        }
    }
    return anyHidden;
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

void DrawBothGamesTab(const char* tab) {
    ImGui::TextDisabled("These settings apply to Ocarina of Time and Majora's Mask alike.");
    for (const SectionInfo& info : kSections) {
        if (std::strcmp(info.tab, tab) != 0) {
            continue;
        }
        const std::vector<Resolved> items = ResolveSection(info.id);
        if (items.empty()) {
            continue;
        }
        ImGui::SeparatorText(info.heading);
        for (const Resolved& item : items) {
            if (item.label) {
                ImGui::TextUnformatted(item.label);
            }
            // Both games number their widgets from zero, so scope the ImGui ID per game.
            ImGui::PushID(item.gameId);
            RenderWidget(*item.widget, *item.game);
            ImGui::PopID();
        }
    }
    if (!LinkIsOn()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Turn on \"Use one setting for both games\" to list the paired settings here too.");
    }
}

const char* BothGamesTabFor(const char* cvar) {
    if (!cvar || !cvar[0]) {
        return nullptr;
    }
    if (const SharedItem* item = FindSharedItem(cvar)) {
        return SectionById(item->section)->tab;
    }
    if (!LinkIsOn()) {
        return nullptr;
    }
    for (const GameSide side : { GameSide::Oot, GameSide::Mm }) {
        const LinkRow* row = FindLinkRow(cvar, side);
        if (row && row->placement == LinkPlacement::BothGamesPanel) {
            return SectionById(row->section)->tab;
        }
        if (row && row->placement == LinkPlacement::SharedTab && side == GameSide::Mm) {
            return kSharedSettingsTab;
        }
    }
    return nullptr;
}

std::string BothGamesSearchKey(const char* cvar) {
    if (LinkIsOn()) {
        if (const LinkRow* row = FindLinkRow(cvar, GameSide::Mm)) {
            return row->oot;
        }
    }
    return cvar;
}

} // namespace ComboRando
