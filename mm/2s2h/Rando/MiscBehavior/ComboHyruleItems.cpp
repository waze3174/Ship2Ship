// mm/2s2h/Rando/MiscBehavior/ComboHyruleItems.cpp — ComboShip: MM items used in Hyrule (OOT host). Small exports
// OOT calls on MM's resident (dormant) save while an MM item is used in OOT: a bottle drunk / poured / filled in
// Hyrule changes MM's own bottle slot, so Termina sees the same bottle. Resolved from soh with
// Combo_ResolveSym("2ship", ...) (soh/soh/Enhancements/randomizer/ComboPause.cpp). Purely additive.
#include <cstdint>

#include "ComboExport.h"
#include "ComboPauseABI.h"
#include "2s2h/SaveManager/SaveManager.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}

namespace {
bool sDormantSaveChanged = false; // MM's dormant save was changed from Hyrule and not yet written
} // namespace

// Writes MM item `mmItem` (0xFF = none) into MM inventory slot `mmSlot` (0-47). Returns 1 on success.
extern "C" COMBO_EXPORT int MM_ComboItems_SetSlot(int mmSlot, int mmItem) {
    if ((mmSlot < 0) || (mmSlot >= COMBO_PAUSE_MM_SLOTS) || (mmItem < 0) || (mmItem > 0xFF)) {
        return 0;
    }
    gSaveContext.save.saveInfo.inventory.items[mmSlot] = (u8)mmItem;
    // MM's own buttons on that slot show the new content, as MM's Inventory_UpdateBottleItem leaves the button it
    // was used from; a stale potion there would be drunk again in Termina.
    for (int b = EQUIP_SLOT_C_LEFT; b <= EQUIP_SLOT_C_RIGHT; b++) {
        if (C_SLOT_EQUIP(0, b) == mmSlot) {
            BUTTON_ITEM_EQUIP(0, b) = (u8)mmItem;
        }
    }
    for (int d = EQUIP_SLOT_D_RIGHT; d <= EQUIP_SLOT_D_UP; d++) {
        if (DPAD_SLOT_EQUIP(0, d) == mmSlot) {
            DPAD_BUTTON_ITEM_EQUIP(0, d) = (u8)mmItem;
        }
    }
    sDormantSaveChanged = true;
    return 1;
}

// MM Inventory_ChangeAmmo for an ammo item (Powder Keg used in Hyrule): the same bounds as MM (0 .. capacity).
extern "C" COMBO_EXPORT int MM_ComboItems_ChangeAmmo(int mmItem, int n) {
    if ((mmItem < 0) || (mmItem >= ITEM_FISHING_ROD) || (SLOT(mmItem) >= ITEM_NUM_SLOTS)) {
        return 0;
    }
    s8* ammo = &gSaveContext.save.saveInfo.inventory.ammo[SLOT(mmItem)];
    s32 next = *ammo + n;
    if (next < 0) {
        next = 0;
    }
    *ammo = (s8)next;
    sDormantSaveChanged = true;
    return 1;
}

// MM loads its save again from the slot when Link goes to Termina (title_setup.c Setup_InitImpl) and when OOT loads
// another file (MM_LoadSaveForCombo), so a change made to the dormant save from Hyrule is written first, or a potion
// drunk there would be back in the bottle. The mirror of OOT's Combo_TerminaPersistDormantSave.
extern "C" void Combo_HyrulePersistDormantSave(void) {
    if (!sDormantSaveChanged) {
        return;
    }
    sDormantSaveChanged = false;
    SaveManager_SaveCurrentForCombo(); // refuses a dormant save that is no loaded slot
}
