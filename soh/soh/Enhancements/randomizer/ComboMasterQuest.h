#pragma once

namespace Rando {
class Dungeons;
} // namespace Rando

// ComboShip: the Master Quest dungeon set of the combo seed in play (ComboMasterQuest.cpp).

// Called by Context::FinalizeSettings after its own MQ roll. Replaces the roll with the pinned set when
// one exists and the loaded archives allow it. Returns true when the pinned set was applied.
bool ComboMasterQuest_ApplyPinned(Rando::Dungeons& dungeons);

// Pins the set FinalizeSettings just rolled, unless a seed's stored set is already pinned.
void ComboMasterQuest_PinCurrent();

// A new combo seed starts unpinned.
void ComboMasterQuest_Unpin();
