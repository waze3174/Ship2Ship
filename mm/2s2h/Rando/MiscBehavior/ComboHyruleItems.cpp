// mm/2s2h/Rando/MiscBehavior/ComboHyruleItems.cpp — ComboShip: MM items used in Hyrule (OOT host). Small exports
// OOT calls on MM's resident (dormant) save while an MM item is used in OOT: a bottle drunk / poured / filled in
// Hyrule changes MM's own bottle slot, so Termina sees the same bottle. Resolved from soh with
// Combo_ResolveSym("2ship", ...) (soh/soh/Enhancements/randomizer/ComboPause.cpp). Purely additive.
#include <cstdint>

#include "ComboExport.h"
#include "ComboPauseABI.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}

// Writes MM item `mmItem` (0xFF = none) into MM inventory slot `mmSlot` (0-47). Returns 1 on success.
extern "C" COMBO_EXPORT int MM_ComboItems_SetSlot(int mmSlot, int mmItem) {
    if ((mmSlot < 0) || (mmSlot >= COMBO_PAUSE_MM_SLOTS) || (mmItem < 0) || (mmItem > 0xFF)) {
        return 0;
    }
    gSaveContext.save.saveInfo.inventory.items[mmSlot] = (u8)mmItem;
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
    return 1;
}
