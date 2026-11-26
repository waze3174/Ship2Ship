// combo/gui/ComboBothGames.h
//
// ComboShip: the "Both games" tab. One control per setting both games own; changing it sets the
// setting in Ocarina of Time and in Majora's Mask. The per-game tabs keep their own controls and change one game.
//   * exact matches: the same CVar in both games (gCheats.NoClip, ...). One CVar, so the games always agree.
//   * linked pairs: two CVars, listed in ComboLinkRows.h. The control is OOT's (MM's when OOT has none); a change
//     is copied to the other game, and a line under the control says when the two games disagree.
// An explicit list decides what appears; nothing is matched by name.
#pragma once

#include "ComboLinkRows.h"
#include "ComboMenuABI.h"
#include <vector>

namespace ComboRando {

struct GameMenu;

// One control of the group, ready to draw.
struct BothGamesItem {
    const CwWidget* widget = nullptr;
    const GameMenu* game = nullptr; // the game whose menu model owns `widget`
    GameSide drawnFrom = GameSide::Oot;
    const char* tab = "";
    const char* label = nullptr;  // printed above the control; nullptr for none
    const LinkRow* row = nullptr; // nullptr for an exact match
};

// Every control of the group in display order (the search lists them all).
std::vector<BothGamesItem> BothGamesItems();

// Tab names for the group, in display order; only tabs with a control to draw.
std::vector<const char*> BothGamesTabs();
void DrawBothGamesTab(const char* tab);

// Draws the control and, for a linked pair, copies a change to the other game and notes when they differ.
void DrawBothGamesItem(const BothGamesItem& item, int widthBudget = 90);

} // namespace ComboRando
