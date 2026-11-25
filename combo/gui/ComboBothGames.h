// combo/gui/ComboBothGames.h
//
// ComboShip: the "Both games" group of Settings. A setting both games own is drawn once there, from
// OOT's menu model (MM's when OOT has no such control), and left out of the two per-game tabs.
//   * exact matches: the same CVar in both games (gCheats.NoClip, ...). Always shown here.
//   * linked pairs: ComboLinkRows.h. Shown here, and hidden from the per-game tabs, while the link is on.
// An explicit list decides what appears; nothing is matched by name.
#pragma once

#include "ComboMenuABI.h"
#include <string>
#include <vector>

namespace ComboRando {

enum class GameSide { Oot, Mm };

// True when the control is drawn once under Both games, so the per-game tab leaves it out.
bool BothGamesHidesWidget(const CwWidget& widget, GameSide side);

// Copies the sidebar's widgets into `out` without the hidden ones, and without the headers they leave
// empty. Returns false (and leaves `out` alone) when nothing in the sidebar is hidden.
bool BothGamesFilterSidebar(const CwSidebar& sidebar, GameSide side, std::vector<CwWidget>& out);

// True when hiding leaves the sidebar with no control at all, so the tab should drop its entry.
bool BothGamesLeavesSidebarEmpty(const CwSidebar& sidebar, GameSide side);

// Tab names for the Both games group, in display order; only tabs with a control to draw.
std::vector<const char*> BothGamesTabs();
void DrawBothGamesTab(const char* tab);

// The Both games tab that draws this CVar's control, or nullptr when it is not drawn there.
const char* BothGamesTabFor(const char* cvar);

// Search dedupe key: a linked MM CVar maps to its OOT partner, so one pair is listed once.
std::string BothGamesSearchKey(const char* cvar);

} // namespace ComboRando
