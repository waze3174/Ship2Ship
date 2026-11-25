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

#endif // COMBO_BUILD
