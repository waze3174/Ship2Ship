// combo/gui/ComboMenuNav.cpp — see ComboMenuNav.h
#include "ComboMenuNav.h"
#include "ComboBothGames.h"
#include "ComboMenuModel.h"
#include <cstring>

namespace ComboRando {

namespace {

// Section and page names as the ports build them.
constexpr const char* kSettingsSection = "Settings";
constexpr const char* kNetworkSection = "Network";
constexpr const char* kOotRandoSection = "Randomizer";
constexpr const char* kMmRandoSection = "Rando";
constexpr const char* kModMenuPage = "Mod Menu";
constexpr const char* kPresetsPage = "Presets";
constexpr const char* kEntranceTrackerPage = "Entrance Tracker";

// Group headings of the hub tabs.
constexpr const char* kGroupProgram = "";
constexpr const char* kGroupTrackers = "Trackers and overlays";
constexpr const char* kGroupOnline = "Online";
constexpr const char* kGroupSameInBoth = "Same setting in both";
constexpr const char* kGroupCrossGame = "Cross-game";
constexpr const char* kGroupOot = "Ocarina of Time";
constexpr const char* kGroupMm = "Majora's Mask";
constexpr const char* kGroupCombo = "Combo";

struct Alias {
    const char* portLabel;
    const char* shown;
};
constexpr Alias kAliases[] = {
    { "Difficulty Options", "Difficulty" },
    { "Cosmetic Editor", "Cosmetics Editor" },
};

// The Settings pages that are the program's own, in display order.
constexpr const char* kProgramPages[] = { "General", "Graphics", "Audio", "Controls" };

bool Same(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

HubEntry MakeEntry(const char* scope, const char* group, const char* label, HubEntry::Kind kind,
                   const char* keywords = "") {
    HubEntry entry;
    entry.label = label;
    entry.group = group;
    entry.scope = scope;
    entry.kind = kind;
    entry.keywords = keywords;
    return entry;
}

bool FindSidebar(const GameMenu& game, const char* section, const char* sidebar, int& sectionIndex, int& sidebarIndex) {
    if (!game.loaded || !game.menu) {
        return false;
    }
    for (int s = 0; s < game.menu->sectionCount; ++s) {
        const CwSection& sec = game.menu->sections[s];
        if (!Same(sec.sectionLabel, section)) {
            continue;
        }
        for (int sb = 0; sb < sec.sidebarCount; ++sb) {
            if (Same(sec.sidebars[sb].sidebarName, sidebar)) {
                sectionIndex = s;
                sidebarIndex = sb;
                return true;
            }
        }
        return false;
    }
    return false;
}

// One of a game's own pages, listed under `label` (the page's own name when null). A game still loading adds nothing.
void AppendEnginePage(std::vector<HubEntry>& out, const char* scope, const char* group, const GameMenu& game,
                      const char* section, const char* sidebar, const char* label = nullptr) {
    HubEntry entry = MakeEntry(scope, group, label ? label : sidebar, HubEntry::ENGINE);
    if (!FindSidebar(game, section, sidebar, entry.sectionIndex, entry.sidebarIndex)) {
        return;
    }
    entry.game = &game;
    out.push_back(std::move(entry));
}

// The tracker pages are the combo's own, in Settings.
bool IsTrackerPage(const char* name) {
    return name && std::strstr(name, "Tracker") != nullptr;
}

// Every page of the game's randomizer section except the trackers.
void AppendRandomizerPages(std::vector<HubEntry>& out, const char* scope, const char* group, const GameMenu& game,
                           const char* section) {
    if (!game.loaded || !game.menu) {
        return;
    }
    for (int s = 0; s < game.menu->sectionCount; ++s) {
        const CwSection& sec = game.menu->sections[s];
        if (!Same(sec.sectionLabel, section)) {
            continue;
        }
        for (int sb = 0; sb < sec.sidebarCount; ++sb) {
            const char* name = sec.sidebars[sb].sidebarName;
            if (!IsTrackerPage(name)) {
                AppendEnginePage(out, scope, group, game, section, name ? name : "Section");
            }
        }
        return;
    }
}

void AddGroup(std::vector<HubGroup>& groups, const char* label, std::vector<HubEntry> entries) {
    if (!entries.empty()) {
        groups.push_back({ label, std::move(entries) });
    }
}

std::vector<HubGroup> SettingsGroups(const ComboMenuModel& model) {
    const char* scope = kScopeSettings;
    std::vector<HubGroup> groups;

    std::vector<HubEntry> program;
    for (const char* page : kProgramPages) {
        AppendEnginePage(program, scope, kGroupProgram, model.Oot(), kSettingsSection, page);
    }
    program.push_back(
        MakeEntry(scope, kGroupProgram, kPresetsPage, HubEntry::COMBO_PRESETS, "save named switch settings"));
    program.push_back(MakeEntry(scope, kGroupProgram, "Import settings", HubEntry::COMBO_CONFIG_IMPORT,
                                "import Ship2Ship ComboShip ship2ship.json comboship.json config copy install"));
    AddGroup(groups, kGroupProgram, std::move(program));

    std::vector<HubEntry> trackers;
    trackers.push_back(MakeEntry(scope, kGroupTrackers, "Item Tracker", HubEntry::COMBO_TRACKER,
                                 "items window swap game popout background"));
    trackers.push_back(MakeEntry(scope, kGroupTrackers, "Check Tracker", HubEntry::COMBO_CHECK_TRACKER,
                                 "checks window swap game popout background"));
    trackers.push_back(MakeEntry(scope, kGroupTrackers, "Hint Tracker", HubEntry::COMBO_HINT_TRACKER, "hints window"));
    AppendEnginePage(trackers, scope, kGroupTrackers, model.Oot(), kOotRandoSection, kEntranceTrackerPage,
                     "Entrance Tracker (OoT)");
    trackers.push_back(
        MakeEntry(scope, kGroupTrackers, "Timers", HubEntry::COMBO_TIMERS, "overlay play time clock stopwatch window"));
    AppendEnginePage(trackers, scope, kGroupTrackers, model.Oot(), kSettingsSection, "Input Viewer");
    AppendEnginePage(trackers, scope, kGroupTrackers, model.Oot(), kSettingsSection, "Notifications");
    AddGroup(groups, kGroupTrackers, std::move(trackers));

    std::vector<HubEntry> online;
    online.push_back(MakeEntry(scope, kGroupOnline, "Anchor co-op", HubEntry::COMBO_NETWORK,
                               "anchor online multiplayer room team host port name color resync diagnostics"));
    online.push_back(MakeEntry(scope, kGroupOnline, "Discord", HubEntry::COMBO_DISCORD,
                               "rich presence show on discord area name checks collected form age hearts day time "
                               "co-op player count session timer play time"));
    AddGroup(groups, kGroupOnline, std::move(online));
    return groups;
}

std::vector<HubGroup> BothGamesGroups() {
    const char* scope = kScopeBothGames;
    std::vector<HubGroup> groups;

    std::vector<HubEntry> same;
    for (const char* tab : BothGamesTabs()) {
        same.push_back(MakeEntry(scope, kGroupSameInBoth, tab, HubEntry::COMBO_BOTH_GAMES));
    }
    AddGroup(groups, kGroupSameInBoth, std::move(same));

    std::vector<HubEntry> cross;
    for (CrossPage page : CrossPages()) {
        HubEntry entry = MakeEntry(scope, kGroupCrossGame, CrossPageLabel(page), HubEntry::COMBO_CROSS_GAME,
                                   page == CrossPage::ItemsAndForms ? "bottle shelf bottles" : "");
        entry.crossPage = page;
        cross.push_back(std::move(entry));
    }
    cross.push_back(MakeEntry(scope, kGroupCrossGame, "Back to back", HubEntry::COMBO_BACK_TO_BACK,
                              "vanilla Ocarina of Time then Majora's Mask one save file Master Quest carried hearts "
                              "magic bottles wallet rupees songs usable in Termina early sticks nuts bombs bow "
                              "arrows hookshot lens beans ocarina"));
    AddGroup(groups, kGroupCrossGame, std::move(cross));
    return groups;
}

std::vector<HubGroup> RandomizerGroups(const ComboMenuModel& model) {
    const char* scope = kScopeRandomizer;
    std::vector<HubGroup> groups;

    std::vector<HubEntry> oot;
    AppendRandomizerPages(oot, scope, kGroupOot, model.Oot(), kOotRandoSection);
    AddGroup(groups, kGroupOot, std::move(oot));

    std::vector<HubEntry> mm;
    AppendRandomizerPages(mm, scope, kGroupMm, model.Mm(), kMmRandoSection);
    AddGroup(groups, kGroupMm, std::move(mm));

    std::vector<HubEntry> combo;
    combo.push_back(MakeEntry(scope, kGroupCombo, "Generate", HubEntry::COMBO_GEN,
                              "seed games goal triforce hunt boss release starting game unified inventory both links "
                              "logic form abilities lost woods link entrance randomizer cross-game entrances pots "
                              "grass shared items one game only"));
    combo.push_back(MakeEntry(scope, kGroupCombo, "Plandomizer", HubEntry::COMBO_PLANDO,
                              "spoiler place items edit placements play"));
    AddGroup(groups, kGroupCombo, std::move(combo));
    return groups;
}

} // namespace

const std::vector<MenuScope>& MenuScopes() {
    static const std::vector<MenuScope> scopes = {
        { kScopeSettings, "Settings" }, { kScopeBothGames, "Both games" },  { kScopeOot, "Ocarina of Time" },
        { kScopeMm, "Majora's Mask" },  { kScopeRandomizer, "Randomizer" },
    };
    return scopes;
}

bool IsMenuScope(const std::string& id) {
    for (const MenuScope& scope : MenuScopes()) {
        if (id == scope.id) {
            return true;
        }
    }
    return false;
}

const char* MenuScopeLabel(const std::string& id) {
    for (const MenuScope& scope : MenuScopes()) {
        if (id == scope.id) {
            return scope.label;
        }
    }
    return "";
}

std::vector<HubGroup> BuildHubGroups(const std::string& scope) {
    ComboMenuModel& model = ComboMenuModel::Get();
    model.EnsureLoaded();
    if (scope == kScopeBothGames) {
        return BothGamesGroups();
    }
    if (scope == kScopeRandomizer) {
        return RandomizerGroups(model);
    }
    return SettingsGroups(model);
}

std::vector<HubEntry> AllHubEntries() {
    std::vector<HubEntry> all;
    for (const char* scope : { kScopeSettings, kScopeBothGames, kScopeRandomizer }) {
        for (HubGroup& group : BuildHubGroups(scope)) {
            for (HubEntry& entry : group.entries) {
                all.push_back(std::move(entry));
            }
        }
    }
    return all;
}

bool GameSidebarShown(bool isOot, const char* section, const char* sidebar) {
    if (!section) {
        return true;
    }
    const bool inSettings = Same(section, kSettingsSection);
    // The program's own Settings pages are in the Settings tab; only the Mod Menu stays with OoT.
    if (isOot && inSettings) {
        return Same(sidebar, kModMenuPage);
    }
    // Sail and Crowd Control do not work in a combo build yet.
    if (isOot && Same(section, kNetworkSection)) {
        return false;
    }
    // MM's copies of the program's pages are the same CVars; its stock Presets is replaced by the combo Presets.
    if (!isOot && inSettings) {
        for (const char* page : kProgramPages) {
            if (Same(sidebar, page)) {
                return false;
            }
        }
        return !Same(sidebar, kPresetsPage);
    }
    // The randomizer settings and the trackers are in the Randomizer and Settings tabs.
    return !Same(section, isOot ? kOotRandoSection : kMmRandoSection);
}

const char* DisplayName(const char* portLabel) {
    for (const Alias& alias : kAliases) {
        if (Same(portLabel, alias.portLabel)) {
            return alias.shown;
        }
    }
    return portLabel;
}

} // namespace ComboRando
