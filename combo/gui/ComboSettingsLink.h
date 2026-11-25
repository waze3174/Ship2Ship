// combo/gui/ComboSettingsLink.h
//
// ComboShip: reads, copies and seeds the settings both games own under different CVar names. The pairs are
// in ComboLinkRows.h; ComboBothGames.cpp is the menu that uses them. See ComboSettingsLink.cpp.
#pragma once

#include "ComboLinkRows.h"

namespace ComboRando {

// The value the game uses for this row: its stored value, or the row's default while the key is unset.
float LinkEffectiveValue(const LinkRow& row, GameSide side);

// True when the two games use different values for this row.
bool LinkValuesDiffer(const LinkRow& row);

// Gives the other game `from`'s value through that game's live-apply path, and saves the config. Keys already
// equal in effect are left unwritten. Returns true when a key was written.
bool LinkCopyToOtherGame(const LinkRow& row, GameSide from);

// Merges the existing values of both games once per seed wave (marker CVars gCombo.LinkSettings*Seeded). Cheap
// once done; waits until both games' menus are loaded.
void LinkSeedIfNeeded();

} // namespace ComboRando
