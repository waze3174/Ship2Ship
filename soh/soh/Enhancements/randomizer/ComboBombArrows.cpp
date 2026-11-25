// soh/soh/Enhancements/randomizer/ComboBombArrows.cpp — ComboShip: 2S2H's Bomb Arrows on OOT's bow in Hyrule.
//
// Ported from 2S2H's mm/2s2h/Enhancements/Equipment/BombArrows.cpp (per-button flag shipSaveInfo.bombArrowsEquipped,
// the bomb slot's pause cycle, the HUD badge and count, the fuse and the blast), reading 2S2H's own CVar
// (gEnhancements.Equipment.BombArrows, one shared CVar store). Hyrule's bow and bombs are OOT's own, so a shot spends
// OOT's arrow and OOT's bomb (the shared-consumables tick, if on, carries both counts to MM).
//
// Here: the switch, the per-button flag kept in OOT's save (ship.bombArrowsEquipped, bit = cButtonSlots index), and
// the pause menu's session state. The seams are in z_kaleido_item.c (pause), z_parameter.c (HUD) and
// z_player.c / ovl_En_Arrow/z_en_arrow_combo_bomb.c (the shot).
#ifdef COMBO_BUILD

#include <memory> // z64.h pulls <memory> in C++; include it before the extern "C" block

#include <libultraship/bridge/consolevariablebridge.h>

#include "ComboBombArrows.h"
#include "ComboPause.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
int Combo_GamesSolo(void); // OTRGlobals.cpp
}

namespace {

constexpr const char* kBombArrowsCVar = "gEnhancements.Equipment.BombArrows";
constexpr int kButtonCount = 7;      // equips.cButtonSlots: three C buttons and four D-pad directions
constexpr int kBButtonItemCount = 1; // equips.buttonItems[0] is B; button n is at n + 1

bool sSlotShowsBombArrows =
    false;                      // the bomb slot of the item page shows bow + bomb badge (2S2H sBombSlotIsBombArrowMode)
bool sEquipIsBombArrow = false; // the equip animation in progress puts the bow on its button as bomb arrows
bool sSlotDrawSwapped = false;  // between Begin / EndSlotDraw: the bomb slot holds the bow for the draw

bool ValidButton(int button) {
    return (button >= 0) && (button < kButtonCount);
}

// A flag only means something while its button holds the plain bow: another item put there (a layout from MM, an age
// change, a magic arrow) drops it.
void ClearFlagsOfButtonsWithoutBow() {
    for (int button = 0; button < kButtonCount; button++) {
        if (Combo_BombArrowsFlag(button) && (gSaveContext.equips.buttonItems[button + kBButtonItemCount] != ITEM_BOW)) {
            Combo_BombArrowsSetFlag(button, false);
        }
    }
}

void RegisterComboBombArrows() {
    COND_HOOK(OnGameFrameUpdate, true, []() {
        if (GameInteractor::IsSaveLoaded(true)) {
            ClearFlagsOfButtonsWithoutBow();
        }
    });
}

RegisterShipInitFunc sInit(RegisterComboBombArrows, {});

} // namespace

// Behaviour is untouched in vanilla slots and in a one-game seed: the option only exists in a both-games seed.
extern "C" int Combo_BombArrowsEnabled(void) {
    return CVarGetInteger(kBombArrowsCVar, 0) && IS_RANDO && !Combo_GamesSolo();
}

extern "C" int Combo_BombArrowsFlag(int button) {
    return ValidButton(button) && (((gSaveContext.ship.bombArrowsEquipped >> button) & 1) != 0);
}

extern "C" void Combo_BombArrowsSetFlag(int button, int on) {
    if (!ValidButton(button)) {
        return;
    }
    if (on) {
        gSaveContext.ship.bombArrowsEquipped |= (u8)(1 << button);
    } else {
        gSaveContext.ship.bombArrowsEquipped &= (u8) ~(1 << button);
    }
}

extern "C" int Combo_BombArrowsButtonActive(int button) {
    return Combo_BombArrowsEnabled() && Combo_BombArrowsFlag(button) &&
           (gSaveContext.equips.buttonItems[button + kBButtonItemCount] == ITEM_BOW);
}

extern "C" int Combo_BombArrowsCanCycle(void) {
    const bool ownsBombs = sSlotDrawSwapped || (INV_CONTENT(ITEM_BOMB) == ITEM_BOMB);

    return Combo_BombArrowsEnabled() && (INV_CONTENT(ITEM_BOW) == ITEM_BOW) && ownsBombs &&
           Combo_PauseItemAgeOk(ITEM_BOW, gSaveContext.linkAge);
}

extern "C" int Combo_BombArrowsSlotShowsBow(void) {
    return sSlotShowsBombArrows && Combo_BombArrowsCanCycle();
}

extern "C" void Combo_BombArrowsToggleSlot(void) {
    sSlotShowsBombArrows = !sSlotShowsBombArrows;
}

extern "C" int Combo_BombArrowsBeginSlotDraw(void) {
    if (!Combo_BombArrowsSlotShowsBow()) {
        return false;
    }
    INV_CONTENT(ITEM_BOMB) = ITEM_BOW;
    sSlotDrawSwapped = true;
    return true;
}

extern "C" void Combo_BombArrowsEndSlotDraw(void) {
    if (sSlotDrawSwapped) {
        INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
        sSlotDrawSwapped = false;
    }
}

extern "C" int Combo_BombArrowsPickEquip(unsigned short cursorSlot, unsigned short* item, unsigned short* slot) {
    const bool bombArrow = (cursorSlot == SLOT_BOMB) && Combo_BombArrowsSlotShowsBow();

    if (bombArrow) {
        *item = ITEM_BOW;
        *slot = SLOT_BOW;
    }
    return bombArrow;
}

extern "C" void Combo_BombArrowsEquipReset(void) {
    sEquipIsBombArrow = false;
}

extern "C" void Combo_BombArrowsEquipQueued(int bombArrow) {
    sEquipIsBombArrow = bombArrow != 0;
}

extern "C" int Combo_BombArrowsEquipPending(void) {
    return sEquipIsBombArrow;
}

extern "C" int Combo_BombArrowsOutlineSlot(int button, int slot) {
    return (Combo_BombArrowsButtonActive(button) && Combo_BombArrowsSlotShowsBow()) ? SLOT_BOMB : slot;
}

extern "C" int Combo_BombArrowsOutlineShown(int button) {
    return !Combo_BombArrowsButtonActive(button) || Combo_BombArrowsSlotShowsBow();
}

extern "C" void Combo_BombArrowsCount(int hudFallback, short* ammo, int* full) {
    const short arrows = AMMO(ITEM_BOW);
    const short bombs = AMMO(ITEM_BOMB);
    const short arrowCapacity = CUR_CAPACITY(UPG_QUIVER);
    const short bombCapacity = CUR_CAPACITY(UPG_BOMB_BAG);

    if (hudFallback && (bombs <= 0)) {
        *ammo = arrows;
        *full = (arrows == arrowCapacity);
        return;
    }
    *ammo = MIN(arrows, bombs);
    *full = (*ammo == MIN(arrowCapacity, bombCapacity));
}

extern "C" int Combo_BombArrowsHudAmmo(int hudButton, short* ammo, int* full) {
    if ((hudButton < kBButtonItemCount) || !Combo_BombArrowsButtonActive(hudButton - kBButtonItemCount)) {
        return false;
    }
    Combo_BombArrowsCount(true, ammo, full);
    return true;
}

#endif // COMBO_BUILD
