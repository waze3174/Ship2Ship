// soh/soh/Enhancements/randomizer/ComboTerminaItems.cpp — ComboShip: OOT as the OWNING game of items used in Termina
// (owner ask 2026-11-13: "every item has to work from both games in both games"). Combo-owned, additive file (soh
// GLOB_RECURSE). The MM side (mm/2s2h/Rando/MiscBehavior/ComboOotItems.cpp) runs OOT's item code inside MM; what it
// needs from OOT's own save (dormant while MM runs, still resident in this process) comes through these exports, so
// ammo, upgrades and equipment stay OOT's: a Deku Seed fired in Termina is gone from OOT's bullet bag.
//
// C ABI, POD only, resolved by MM with Combo_ResolveSym("soh", ...).
#include <cstdint>
#include <cstdio>

#include <libultraship/bridge/consolevariablebridge.h>

#include "ComboExport.h"
#include "ComboPause.h"
#include "soh/cvar_prefixes.h"
#include "soh/SaveManager.h"
#include "soh/Enhancements/AdultMasks.h"
#include "soh/Enhancements/randomizer/SeedContext.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern u8 gItemAgeReqs[];
}

namespace {
bool SaveResident() {
    return gSaveContext.fileNum <= 2; // 0xFF = no OOT save loaded yet
}

bool sDormantSaveChanged = false; // OOT's dormant save was changed from Termina and not yet written

u8& NayrusLoveUses() {
    return gSaveContext.ship.quest.data.randomizer.nayrusLoveUses;
}

// The Nayru's Love Uses rule (ConsumableNayrusLove.cpp) applies to a randomizer save with the setting above 0.
bool NayrusLoveRuleOn() {
    return SaveResident() && IS_RANDO && RAND_GET_OPTION(RSK_NAYRUS_LOVE_USES).Get() > 0;
}
} // namespace

// OOT's own age rule for an item (gItemAgeReqs, the table OOT's pause menu greys items with), lifted by the same
// switches as in Hyrule (Combo_AgeRulesLifted: Unified Inventory, Timeless Equipment, Unrestricted Items) and by
// Masks as Adult for masks. adult: Link's age in Termina.
extern "C" COMBO_EXPORT int SOH_ComboTermina_ItemAgeOk(int ootItem, int adult) {
    if (ootItem < 0 || ootItem >= ITEM_NONE)
        return 0;
    // Any age. SoH writes this rule into gItemAgeReqs in Play_Init (z_play.c), which hasn't run yet when the file is
    // played in Termina first; until then the table's unset entry reads adult-only.
    if (ootItem == ITEM_ROCS_FEATHER)
        return 1;
    if (Combo_AgeRulesLifted())
        return 1;
    // OOT's masks as adult (SoH enhancement / rando "Masks as Adult"): the same switch OOT's own pause honours.
    if ((ootItem >= ITEM_MASK_KEATON) && (ootItem <= ITEM_MASK_TRUTH) && Ship_MasksEquippableAsAdult())
        return 1;
    const u8 req = gItemAgeReqs[ootItem];
    return (req == 9) || (req == (adult ? LINK_AGE_ADULT : LINK_AGE_CHILD));
}

// OOT's Inventory_ChangeAmmo on OOT's save: the bag/quiver caps are OOT's. (OOT's ammo itself: SOH_ComboAmmoBag.)
extern "C" COMBO_EXPORT void SOH_ComboTermina_ChangeAmmo(int ootItem, int delta) {
    if (!SaveResident())
        return;
    Inventory_ChangeAmmo((s16)ootItem, (s16)delta);
    sDormantSaveChanged = true;
}

// An OOT bottle used in Termina (MM's bottle code, mm ComboPause.cpp Combo_MmPauseSetForeignBottle) takes new content:
// OOT's Inventory_UpdateBottleItem on OOT's save, for every OOT button on that slot (OOT is dormant: no icon to load).
// 0 = not a bottle slot holding a bottle, or not a bottle content.
extern "C" COMBO_EXPORT int SOH_ComboTermina_SetBottle(int ootSlot, int ootItem) {
    if (!SaveResident() || (ootSlot < SLOT_BOTTLE_1) || (ootSlot > SLOT_BOTTLE_4) || (ootItem < ITEM_BOTTLE) ||
        (ootItem > ITEM_POE) || (ootItem == ITEM_LETTER_RUTO)) {
        return 0;
    }
    const u8 cur = gSaveContext.inventory.items[ootSlot];
    if ((cur < ITEM_BOTTLE) || (cur > ITEM_POE)) {
        return 0;
    }
    gSaveContext.inventory.items[ootSlot] = (u8)ootItem;
    for (int b = 1; b < ARRAY_COUNT(gSaveContext.equips.buttonItems); b++) {
        if (gSaveContext.equips.cButtonSlots[b - 1] == ootSlot) {
            gSaveContext.equips.buttonItems[b] = (u8)ootItem;
        }
    }
    sDormantSaveChanged = true;
    return 1;
}

// OOT loads its save again from the slot when Link comes back from Termina, so a change made to the dormant save from
// Termina is written first (SOH_ResumeGame), or the seeds shot there would be back in the bag.
extern "C" void Combo_TerminaPersistDormantSave(void) {
    if (!sDormantSaveChanged || !SaveResident() || (SaveManager::Instance == nullptr)) {
        return;
    }
    sDormantSaveChanged = false;
    SaveManager::Instance->SaveFile(gSaveContext.fileNum);
    SaveManager::Instance->ThreadPoolWait();
}

// Nayru's Love Uses (ConsumableNayrusLove.cpp) for a cast in Termina: the uses OOT's save holds, -1 when the rule is
// off (no limit). maxUses (may be NULL): the setting's value, the count OOT draws green at.
extern "C" COMBO_EXPORT int SOH_ComboTermina_NayrusLoveUses(int* maxUses) {
    if (!NayrusLoveRuleOn())
        return -1;
    if (maxUses != nullptr)
        *maxUses = RAND_GET_OPTION(RSK_NAYRUS_LOVE_USES).Get();
    return NayrusLoveUses();
}

// OOT's OnActorInit(ACTOR_MAGIC_DARK) spend, for a cast that starts in Termina: one use, never below 0.
extern "C" COMBO_EXPORT void SOH_ComboTermina_SpendNayrusLove(void) {
    if (!NayrusLoveRuleOn() || (NayrusLoveUses() == 0))
        return;
    NayrusLoveUses()--;
    sDormantSaveChanged = true;
}

// OOT's equipped value of an equipment row (CUR_EQUIP_VALUE: 0 sword, 1 shield, 2 tunic, 3 boots), 0 = none.
extern "C" COMBO_EXPORT int SOH_ComboTermina_EquipValue(int equipType) {
    if (!SaveResident() || equipType < 0 || equipType > EQUIP_TYPE_BOOTS)
        return 0;
    return CUR_EQUIP_VALUE(equipType);
}

// OOT's equipment page (z_kaleido_equipment.c), on OOT's save, for the Hyrule equipment face of Termina's pause menu:
// equip the tunic or boots in `value` (1 Kokiri, 2 Goron / Iron, 3 Zora / Hover) when OOT owns it and Link's age may
// wear it, or, with SoH's "Equipment Can Be Removed" on, put the equipped one back to Kokiri. The swords and shields on
// that page are MM's own in Termina and are not handled here. Returns 1 when OOT would play its decide sound, 0 when
// it would refuse.
extern "C" COMBO_EXPORT int SOH_ComboTermina_SetEquipValue(int equipType, int value, int adult) {
    if (!SaveResident() || ((equipType != EQUIP_TYPE_TUNIC) && (equipType != EQUIP_TYPE_BOOTS)) || (value < 1) ||
        (value > 3)) {
        return 0;
    }
    const int item = ((equipType == EQUIP_TYPE_TUNIC) ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI) + (value - 1);
    if (!SOH_ComboTermina_ItemAgeOk(item, adult)) {
        return 0;
    }
    if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentCanBeRemoved"), 0) && (value == CUR_EQUIP_VALUE(equipType))) {
        Inventory_ChangeEquipment(equipType, 1); // OOT: "unequip" = the Kokiri piece
        sDormantSaveChanged = true;
        return 1;
    }
    if (!CHECK_OWNED_EQUIP(equipType, value - 1)) {
        return 0;
    }
    Inventory_ChangeEquipment(equipType, value);
    sDormantSaveChanged = true;
    return 1;
}

// SoH's "Assignable Tunics and Boots" (soh/soh/Enhancements/Items/AssignableTunicsAndBoots.cpp, UseTunicBoots) for a
// tunic or boots button pressed in Termina: put the piece in `value` on, or back to Kokiri when it is already on.
// Returns the value now worn, 0 when OOT does not own the piece or Link's age may not wear it.
extern "C" COMBO_EXPORT int SOH_ComboTermina_ToggleEquipValue(int equipType, int value, int adult) {
    if (!SaveResident() || ((equipType != EQUIP_TYPE_TUNIC) && (equipType != EQUIP_TYPE_BOOTS)) || (value < 1) ||
        (value > 3) || !CHECK_OWNED_EQUIP(equipType, value - 1)) {
        return 0;
    }
    const int item = ((equipType == EQUIP_TYPE_TUNIC) ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI) + (value - 1);
    if (!SOH_ComboTermina_ItemAgeOk(item, adult)) {
        return 0;
    }
    const int worn = (CUR_EQUIP_VALUE(equipType) == value) ? 1 : value; // 1 = the Kokiri piece
    Inventory_ChangeEquipment(equipType, worn);
    sDormantSaveChanged = true;
    return worn;
}
