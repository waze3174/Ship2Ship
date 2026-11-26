#pragma once

// combo/ComboSharedHealthSlots.h — ComboShip: which save slot each game's shared health state belongs to.
//
// Each game holds one shared health / magic / ammo state (combo/ComboSharedHealth.h): the one it last took from a
// slot's container (combo.health) or from the other game at a switch. The launcher moves it at save reads, save writes
// and game switches. The file select reads every slot, so the state a game holds can be another slot's. Such a state is
// never kept for, or stored into, a slot it did not come from: before this, the boot reads spread slot 0's hearts into
// slots 1 and 2, and a slot loaded after another one was read played with that one's hearts.
//
// Pure bookkeeping, no game DLL, so combo/tests/ComboSharedHealthSlotsTest.cpp drives the same functions the launcher
// calls. Games are numbered as ComboRando::GameId: 0 = OoT, 1 = MM. The caller serializes access (g_containerMutex).

namespace ComboHealth {

inline constexpr int kNoSlot = -1;
inline constexpr int kHealthGames = 2;

class SlotOwnership {
  public:
    // The slot whose state `game` holds, or kNoSlot (none yet, or an erased file's).
    int SlotOf(int game) const {
        return IsGame(game) ? mSlotOf[game] : kNoSlot;
    }

    // Whether `game`'s state came from `slot`, so it may be stored as that slot's.
    bool Owns(int game, int slot) const {
        return slot != kNoSlot && SlotOf(game) == slot;
    }

    // The reading game took `slot`'s state from its container.
    void Pushed(int game, int slot) {
        if (IsGame(game))
            mSlotOf[game] = slot;
    }

    // A game switch: the game Link enters takes the state of the game he leaves.
    void HandedOff(int fromGame, int toGame) {
        if (IsGame(toGame))
            mSlotOf[toGame] = SlotOf(fromGame);
    }

    // A new file in `slot`: both games' states have just been emptied for it.
    void NewFile(int slot) {
        mSlotOf[0] = mSlotOf[1] = slot;
    }

    // `slot` was erased, or its contents replaced (a copy onto it): a state taken from it belongs to no file now.
    void Forget(int slot) {
        for (int& owner : mSlotOf) {
            if (owner == slot)
                owner = kNoSlot;
        }
    }

  private:
    static bool IsGame(int game) {
        return game >= 0 && game < kHealthGames;
    }

    int mSlotOf[kHealthGames] = { kNoSlot, kNoSlot };
};

// A save read of `slot` by `readGame` while `activeGame` is the game in play, whose state is the live one (liveValid:
// it holds one). When the live state is this slot's, it is newer than the container: store() refreshes the slot's
// combo.health from it, and the game in play keeps it (a read mid-play must not roll the game back to its last save
// write). Otherwise, and for the other game, push() hands the reader the slot's own combo.health. Returns true when
// the reader kept its live state.
template <typename Store, typename Push>
bool ApplySaveRead(SlotOwnership& owners, int activeGame, bool liveValid, int readGame, int slot, Store&& store,
                   Push&& push) {
    const bool liveIsSlots = liveValid && owners.Owns(activeGame, slot);
    if (liveIsSlots)
        store();
    if (liveIsSlots && (readGame == activeGame))
        return true;
    push();
    owners.Pushed(readGame, slot);
    return false;
}

} // namespace ComboHealth
