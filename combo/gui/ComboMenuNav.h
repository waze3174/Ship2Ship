// combo/gui/ComboMenuNav.h
//
// ComboShip: the combo menu's navigation tree. The top tabs, the pages of the Settings, Both games and Randomizer tabs
// (the hub), which of the two games' own pages are shown, and our names for the ports' page labels.
#pragma once

#include "ComboCrossGamePanels.h"
#include <string>
#include <vector>

namespace ComboRando {

struct GameMenu;

// Top-level tabs. "oot" and "mm" draw the game's own menu; the others are hubs of pages.
struct MenuScope {
    const char* id;
    const char* label;
};
inline constexpr const char* kScopeSettings = "settings";
inline constexpr const char* kScopeBothGames = "both";
inline constexpr const char* kScopeOot = "oot";
inline constexpr const char* kScopeMm = "mm";
inline constexpr const char* kScopeRandomizer = "randomizer";
const std::vector<MenuScope>& MenuScopes();
bool IsMenuScope(const std::string& id);
const char* MenuScopeLabel(const std::string& id);

struct HubEntry {
    enum Kind {
        ENGINE, // one of a game's own pages, drawn from its menu model
        COMBO_GEN,
        COMBO_PLANDO,
        COMBO_TRACKER,
        COMBO_CHECK_TRACKER,
        COMBO_HINT_TRACKER,
        COMBO_TIMERS,
        COMBO_PRESETS,
        COMBO_CONFIG_IMPORT,
        COMBO_NETWORK,
        COMBO_BOTH_GAMES,
        COMBO_CROSS_GAME,
        COMBO_DISCORD,
        COMBO_ARCHIPELAGO,
        COMBO_BACK_TO_BACK
    };
    std::string label; // the page's name; its sidebar text goes through DisplayName
    std::string group; // owning group, "" for the first group of a tab
    std::string scope; // owning tab id
    Kind kind = ENGINE;
    const char* keywords = "";      // words the search matches besides the label; our own pages only
    const GameMenu* game = nullptr; // ENGINE
    int sectionIndex = -1;
    int sidebarIndex = -1;
    CrossPage crossPage = CrossPage::ItemsAndForms; // COMBO_CROSS_GAME
    std::string key() const {
        return group + "/" + label;
    }
};

struct HubGroup {
    std::string label; // heading in the sidebar; "" for none
    std::vector<HubEntry> entries;
};

// The groups of one hub tab, built fresh from the loaded game menus.
std::vector<HubGroup> BuildHubGroups(const std::string& scope);

// Every page of every hub tab, for the search.
std::vector<HubEntry> AllHubEntries();

// Whether the game's own tab shows a page. Pages a hub tab shows instead are hidden there.
bool GameSidebarShown(bool isOot, const char* section, const char* sidebar);

// Our name for a label the ports own ("Difficulty Options" is shown as "Difficulty").
const char* DisplayName(const char* portLabel);

} // namespace ComboRando
