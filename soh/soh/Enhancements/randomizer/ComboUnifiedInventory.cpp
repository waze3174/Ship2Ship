// soh/soh/Enhancements/randomizer/ComboUnifiedInventory.cpp — ComboShip "Unified Inventory" (owner ruling
// 2026-11-11, docs/plans/full-coop-sync.md Findings log): both Links get every item. Combo-owned file, picked up
// by soh's GLOB_RECURSE; only built into the combo DLL's behavior when the seed turns it on.
//
// OOT side of the ruling: child and adult can use every OOT item the engine allows at either age. This is SoH's
// Timeless Equipment behavior (the same three age-requirement VB hooks), driven by the SEED (gComboUnifiedInventory,
// pushed by the launcher from the seed's `unifiedInventory`) instead of a local cheat CVar, so both players in a
// room and the logic agree. Logic.cpp's CanUse drops the matching age gates (see ComboUnifiedItemIsTimeless).

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/randomizer/randomizerTypes.h"

extern "C" int gComboUnifiedInventory = 0;

// The items whose age gate Unified Inventory removes in logic. Kept out: the Silver/Golden Gauntlets (child
// Link's lift strength is age-based in the engine, not an item check) and the adult trade sequence (its NPCs
// only exist in the adult era, so the item is never the gate).
bool ComboUnifiedItemIsTimeless(RandomizerGet item) {
    if (!gComboUnifiedInventory)
        return false;
    switch (item) {
        case RG_FAIRY_BOW:
        case RG_MEGATON_HAMMER:
        case RG_IRON_BOOTS:
        case RG_HOVER_BOOTS:
        case RG_HOOKSHOT:
        case RG_LONGSHOT:
        case RG_GORON_TUNIC:
        case RG_ZORA_TUNIC:
        case RG_MIRROR_SHIELD:
        case RG_MASTER_SWORD:
        case RG_PROGRESSIVE_GORONSWORD:
        case RG_GIANTS_KNIFE:
        case RG_BIGGORON_SWORD:
        case RG_FAIRY_SLINGSHOT:
        case RG_BOOMERANG:
        case RG_KOKIRI_SWORD:
        case RG_DEKU_SHIELD:
            return true;
        default:
            return false;
    }
}

static void RegisterComboUnifiedInventory() {
    // Registered once; the seed flag is read per call, so loading another slot switches it without re-init.
    COND_VB_SHOULD(VB_PLAYER_MEETS_AGE_REQ, true, {
        if (gComboUnifiedInventory)
            *should = true;
    });
    COND_VB_SHOULD(VB_ITEM_MEETS_AGE_REQ, true, {
        if (gComboUnifiedInventory)
            *should = true;
    });
    COND_VB_SHOULD(VB_SLOT_MEETS_AGE_REQ, true, {
        if (gComboUnifiedInventory)
            *should = true;
    });
}

static RegisterShipInitFunc initFunc(RegisterComboUnifiedInventory, {});
