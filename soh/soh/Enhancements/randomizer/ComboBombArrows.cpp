// soh/soh/Enhancements/randomizer/ComboBombArrows.cpp — ComboShip: 2S2H's Bomb Arrows on OOT's bow in Hyrule.
//
// Ported from 2S2H's mm/2s2h/Enhancements/Equipment/BombArrows.cpp (per-button flag shipSaveInfo.bombArrowsEquipped,
// the bomb slot's pause cycle, the HUD badge and count, the fuse and the blast), reading 2S2H's own CVar
// (gEnhancements.Equipment.BombArrows, one shared CVar store). A shot spends the arrow and the bomb of the bow's game:
// OOT's bow, OOT's arrow and OOT's bomb; MM's bow on a button (a foreign id, run as OOT's bow), MM's arrow and MM's
// bomb (the shared-consumables tick, if on, carries the counts to the other game).
//
// Here: the switch, the per-button flag kept in OOT's save (ship.bombArrowsEquipped, bit = cButtonSlots index), and
// the pause menus' session state (OOT's own item page and MM's page). The seams are in z_kaleido_item.c and
// z_kaleido_combo.c (pause), z_parameter.c (HUD) and z_player.c / ovl_En_Arrow/z_en_arrow_combo_bomb.c (the shot).
#ifdef COMBO_BUILD

#include <memory> // z64.h pulls <memory> in C++; include it before the extern "C" block

#include <libultraship/bridge/consolevariablebridge.h>

#include "ComboBombArrows.h"
#include "ComboPause.h"
#include "ComboPauseABI.h"
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
    false; // the bomb slot of the item page shows bow + bomb badge (2S2H sBombSlotIsBombArrowMode)
bool sMmSlotShowsBombArrows = false; // the same for the bomb slot of MM's page (MM's own bow and bombs)
bool sEquipIsBombArrow = false;      // the equip animation in progress puts the bow on its button as bomb arrows
bool sSlotDrawSwapped = false;       // between Begin / EndSlotDraw: the bomb slot holds the bow for the draw

bool ValidButton(int button) {
    return (button >= 0) && (button < kButtonCount);
}

int ButtonItem(int button) {
    return gSaveContext.equips.buttonItems[button + kBButtonItemCount];
}

bool ButtonHoldsBow(int button) {
    return ValidButton(button) && Combo_BombArrowsIsBowItem(ButtonItem(button));
}

// A flag only means something while its button holds a plain bow: another item put there (a layout from MM, an age
// change, a magic arrow) drops it.
void ClearFlagsOfButtonsWithoutBow() {
    for (int button = 0; button < kButtonCount; button++) {
        if (Combo_BombArrowsFlag(button) && !ButtonHoldsBow(button)) {
            Combo_BombArrowsSetFlag(button, false);
        }
    }
}

// MM's bow and bombs, by the foreign ids of the slots of MM's item page that hold them (ITEM_NONE when MM has none).
int MmBowItem() {
    return Combo_PauseMmItemForeignId(COMBO_MM_ITEM_BOW);
}

int MmBombItem() {
    return Combo_PauseMmItemForeignId(COMBO_MM_ITEM_BOMB);
}

// A shot spends one of each, so the lower of the two counts; `full`: the bag the lower count is in is full.
// `hudFallback` (HUD only): with no bombs the arrow count is shown instead.
void MmCount(int bowItem, bool hudFallback, int* ammo, int* full) {
    const int bombItem = MmBombItem();
    int arrowsFull = 0;
    int bombsFull = 0;
    const int arrows = MAX(0, Combo_PauseForeignAmmoDigits(bowItem, &arrowsFull));
    const int bombs = (bombItem == ITEM_NONE) ? 0 : MAX(0, Combo_PauseForeignAmmoDigits(bombItem, &bombsFull));

    if (hudFallback && (bombs <= 0)) {
        *ammo = arrows;
        *full = arrowsFull;
        return;
    }
    *ammo = MIN(arrows, bombs);
    *full = ((arrows <= bombs) && arrowsFull) || ((bombs <= arrows) && bombsFull);
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

extern "C" int Combo_BombArrowsIsBowItem(int item) {
    return (item == ITEM_BOW) || (Combo_PauseIsForeign(item) && (Combo_PauseForeignHostItem(item) == ITEM_BOW));
}

extern "C" int Combo_BombArrowsButtonActive(int button) {
    return Combo_BombArrowsEnabled() && Combo_BombArrowsFlag(button) && ButtonHoldsBow(button);
}

extern "C" int Combo_BombArrowsBombAmmo(int bowItem) {
    if (bowItem == ITEM_BOW) {
        return AMMO(ITEM_BOMB);
    }
    const int bombItem = MmBombItem();
    return (bombItem == ITEM_NONE) ? 0 : MAX(0, Combo_PauseForeignAmmo(bombItem));
}

extern "C" int Combo_BombArrowsButtonHasBombs(int button) {
    return ValidButton(button) && (Combo_BombArrowsBombAmmo(ButtonItem(button)) > 0);
}

extern "C" void Combo_BombArrowsSpendBomb(int bowItem) {
    if (bowItem == ITEM_BOW) {
        Inventory_ChangeAmmo(ITEM_BOMB, -1);
        return;
    }
    const int bombItem = MmBombItem();
    if (bombItem != ITEM_NONE) {
        Combo_PauseForeignAmmoChange(bombItem, -1);
    }
}

extern "C" void Combo_BombArrowsSpendArrow(int bowItem) {
    if (bowItem == ITEM_BOW) {
        Inventory_ChangeAmmo(ITEM_BOW, -1);
    } else {
        Combo_PauseForeignAmmoChange(bowItem, -1);
    }
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
    const int button = hudButton - kBButtonItemCount;

    if ((button < 0) || !Combo_BombArrowsButtonActive(button) || (ButtonItem(button) != ITEM_BOW)) {
        return false;
    }
    Combo_BombArrowsCount(true, ammo, full);
    return true;
}

extern "C" int Combo_BombArrowsMmHudAmmo(int hudButton, int* ammo, int* full) {
    const int button = hudButton - kBButtonItemCount;

    if ((button < 0) || !Combo_BombArrowsButtonActive(button) || (ButtonItem(button) == ITEM_BOW)) {
        return false;
    }
    MmCount(ButtonItem(button), true, ammo, full);
    return true;
}

extern "C" int Combo_BombArrowsMmCanCycle(void) {
    const int bowItem = MmBowItem();

    return Combo_BombArrowsEnabled() && (Combo_PauseTab() == COMBO_PAUSE_TAB_TERMINA) && (bowItem != ITEM_NONE) &&
           (MmBombItem() != ITEM_NONE) && !Combo_PauseHasNativeTwin(bowItem) &&
           Combo_PauseItemAgeOk(bowItem, gSaveContext.linkAge);
}

extern "C" int Combo_BombArrowsMmSlotShowsBow(void) {
    return sMmSlotShowsBombArrows && Combo_BombArrowsMmCanCycle();
}

extern "C" void Combo_BombArrowsMmToggleSlot(void) {
    sMmSlotShowsBombArrows = !sMmSlotShowsBombArrows;
}

extern "C" int Combo_BombArrowsMmIsBombSlot(int pageSlot) {
    const int bombItem = MmBombItem();

    return (bombItem != ITEM_NONE) && (pageSlot == ComboPause_OotDecode(bombItem));
}

extern "C" unsigned char Combo_BombArrowsMmSlotItem(int pageSlot, unsigned char item) {
    if (!Combo_BombArrowsMmIsBombSlot(pageSlot) || !Combo_BombArrowsMmSlotShowsBow()) {
        return item;
    }
    return (unsigned char)MmBowItem();
}

extern "C" int Combo_BombArrowsMmSlotAmmo(int pageSlot, int ammo) {
    int ammoOfBoth = ammo;
    int full = 0;

    if (!Combo_BombArrowsMmIsBombSlot(pageSlot) || !Combo_BombArrowsMmSlotShowsBow()) {
        return ammo;
    }
    MmCount(MmBowItem(), false, &ammoOfBoth, &full);
    return ammoOfBoth;
}

extern "C" unsigned char Combo_BombArrowsMmOutlineItem(int button, unsigned char item) {
    if (!Combo_BombArrowsButtonActive(button)) {
        return item;
    }
    return Combo_BombArrowsMmSlotShowsBow() ? (unsigned char)MmBombItem() : (unsigned char)ITEM_NONE;
}

#endif // COMBO_BUILD
