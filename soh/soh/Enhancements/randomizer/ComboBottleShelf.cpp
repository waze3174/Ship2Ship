// soh/soh/Enhancements/randomizer/ComboBottleShelf.cpp — ComboShip shared bottles, OOT side (owner ruling
// 2026-11-12: 10 bottles, all usable in both games). Combo-owned file (soh GLOB_RECURSE).
//
// OOT's save has 4 bottle slots and changing that would break saves and Anchor team-state JSON. So the 4 native
// slots stay the working set, and bottles past them wait on a shelf; a swap brings any shelved bottle into a slot.
//   held  = native bottle slots in use + shelved bottles
//   found = held - mirroredIn  (bottles found IN OOT; mirroredIn = bottles granted because MM found them)
// SOH_GetSharedTier(SF_BOTTLES) reports `found`; SOH_RaiseSharedTier(SF_BOTTLES, total) adds mirrored empty bottles
// until `held == total` (the launcher passes oot found + mm found). The shelf and mirroredIn persist in the combo
// save container, written by the launcher whenever OOT saves (SOH_BottleShelfGetState / SetState).

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "rando/SharedItems.h"
#include "ComboExport.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace {
std::vector<uint8_t> sShelf; // item ids (ITEM_BOTTLE = empty, ITEM_FAIRY, ...)
int sMirroredIn = 0;
std::string sStateCache;

constexpr int kFirstBottleSlot = SLOT_BOTTLE_1;
constexpr int kBottleSlots = 4;

bool IsBottleItem(uint8_t item) {
    return item >= ITEM_BOTTLE && item <= ITEM_POE;
}

int NativeHeld() {
    int n = 0;
    for (int i = 0; i < kBottleSlots; ++i)
        if (gSaveContext.inventory.items[kFirstBottleSlot + i] != ITEM_NONE)
            ++n;
    return n;
}

const char* BottleName(uint8_t item) {
    switch (item) {
        case ITEM_BOTTLE:
            return "Empty Bottle";
        case ITEM_POTION_RED:
            return "Red Potion";
        case ITEM_POTION_GREEN:
            return "Green Potion";
        case ITEM_POTION_BLUE:
            return "Blue Potion";
        case ITEM_FAIRY:
            return "Fairy";
        case ITEM_FISH:
            return "Fish";
        case ITEM_MILK_BOTTLE:
            return "Milk";
        case ITEM_MILK_HALF:
            return "Half Milk";
        case ITEM_LETTER_RUTO:
            return "Ruto's Letter";
        case ITEM_BLUE_FIRE:
            return "Blue Fire";
        case ITEM_BUG:
            return "Bugs";
        case ITEM_BIG_POE:
            return "Big Poe";
        case ITEM_POE:
            return "Poe";
        default:
            return "?";
    }
}

// Keep any button that points at `slot` showing the slot's new contents.
void RefreshButtonsForSlot(int slot) {
    for (int b = 1; b < 8; ++b) {
        if (gSaveContext.equips.cButtonSlots[b - 1] != slot)
            continue;
        gSaveContext.equips.buttonItems[b] = gSaveContext.inventory.items[slot];
        if (gPlayState != nullptr)
            Interface_LoadItemIcon1(gPlayState, b);
    }
}
} // namespace

// The seed's effective Shared Items mask (OTRGlobals.cpp) decides whether bottles are pooled at all.
extern "C" int gComboSharedMask;
bool ComboBottles_Active() {
    return (gComboSharedMask >> ComboRando::SF_BOTTLES) & 1;
}

// RandomizerGet bottle items -> the inventory item they put in a slot (ITEM_NONE = not a bottle item).
uint8_t ComboBottles_ItemForRg(int rg) {
    switch (rg) {
        case RG_EMPTY_BOTTLE:
            return ITEM_BOTTLE;
        case RG_BOTTLE_WITH_MILK:
            return ITEM_MILK_BOTTLE;
        case RG_BOTTLE_WITH_RED_POTION:
            return ITEM_POTION_RED;
        case RG_BOTTLE_WITH_GREEN_POTION:
            return ITEM_POTION_GREEN;
        case RG_BOTTLE_WITH_BLUE_POTION:
            return ITEM_POTION_BLUE;
        case RG_BOTTLE_WITH_FAIRY:
            return ITEM_FAIRY;
        case RG_BOTTLE_WITH_FISH:
            return ITEM_FISH;
        case RG_BOTTLE_WITH_BLUE_FIRE:
            return ITEM_BLUE_FIRE;
        case RG_BOTTLE_WITH_BUGS:
            return ITEM_BUG;
        case RG_BOTTLE_WITH_POE:
            return ITEM_POE;
        case RG_BOTTLE_WITH_BIG_POE:
            return ITEM_BIG_POE;
        case RG_RUTOS_LETTER:
            return ITEM_LETTER_RUTO;
        default:
            return ITEM_NONE;
    }
}

int ComboBottles_Held() {
    return NativeHeld() + static_cast<int>(sShelf.size());
}

int ComboBottles_Found() {
    int f = ComboBottles_Held() - sMirroredIn;
    return f < 0 ? 0 : f;
}

// One mirrored empty bottle: a free native slot first, else the shelf.
void ComboBottles_AddMirrored() {
    for (int i = 0; i < kBottleSlots; ++i) {
        u8& it = gSaveContext.inventory.items[kFirstBottleSlot + i];
        if (it == ITEM_NONE) {
            it = ITEM_BOTTLE;
            RefreshButtonsForSlot(kFirstBottleSlot + i);
            ++sMirroredIn;
            return;
        }
    }
    sShelf.push_back(ITEM_BOTTLE);
    ++sMirroredIn;
}

// When a native bottle is GIVEN while all four slots are full (Item_Give finds no empty slot and gives
// nothing), the shared total would never grow. Catching that is the shelf's other job: called from the
// combo give path for bottle items. Returns true if it shelved the item.
bool ComboBottles_ShelveIfFull(uint8_t item) {
    if (!IsBottleItem(item) || NativeHeld() < kBottleSlots)
        return false;
    sShelf.push_back(item);
    return true;
}

extern "C" COMBO_EXPORT const char* SOH_BottleShelfGetState(void) {
    try {
        sStateCache = nlohmann::json{ { "shelf", sShelf }, { "mirroredIn", sMirroredIn } }.dump();
    } catch (...) { sStateCache = "{}"; }
    return sStateCache.c_str();
}

extern "C" COMBO_EXPORT void SOH_BottleShelfSetState(const char* json) {
    sShelf.clear();
    sMirroredIn = 0;
    if (!json)
        return;
    try {
        auto j = nlohmann::json::parse(json);
        for (auto& v : j.value("shelf", nlohmann::json::array()))
            if (v.is_number_integer() && IsBottleItem(static_cast<uint8_t>(v.get<int>())))
                sShelf.push_back(static_cast<uint8_t>(v.get<int>()));
        sMirroredIn = std::max(0, j.value("mirroredIn", 0));
    } catch (...) {}
}

// UI snapshot: the 4 native slots then the shelf, each {name, item}.
extern "C" COMBO_EXPORT const char* SOH_BottleShelfDescribe(void) {
    static std::string cache;
    try {
        nlohmann::json native = nlohmann::json::array(), shelf = nlohmann::json::array();
        for (int i = 0; i < kBottleSlots; ++i) {
            uint8_t it = gSaveContext.inventory.items[kFirstBottleSlot + i];
            native.push_back({ { "item", it }, { "name", it == ITEM_NONE ? "(none)" : BottleName(it) } });
        }
        for (uint8_t it : sShelf)
            shelf.push_back({ { "item", it }, { "name", BottleName(it) } });
        cache = nlohmann::json{ { "native", native }, { "shelf", shelf } }.dump();
    } catch (...) { cache = "{}"; }
    return cache.c_str();
}

// Swap shelf entry `shelfIdx` with native bottle slot `slotIdx` (0..3). An empty native slot simply takes the
// shelved bottle. Returns 1 on success. Main thread only.
extern "C" COMBO_EXPORT int SOH_BottleShelfSwap(int shelfIdx, int slotIdx) {
    if (shelfIdx < 0 || shelfIdx >= static_cast<int>(sShelf.size()) || slotIdx < 0 || slotIdx >= kBottleSlots)
        return 0;
    u8& native = gSaveContext.inventory.items[kFirstBottleSlot + slotIdx];
    uint8_t shelved = sShelf[shelfIdx];
    if (native == ITEM_NONE) {
        native = shelved;
        sShelf.erase(sShelf.begin() + shelfIdx);
    } else {
        sShelf[shelfIdx] = native;
        native = shelved;
    }
    RefreshButtonsForSlot(kFirstBottleSlot + slotIdx);
    return 1;
}

// ── Pause-menu "box": each bottle slot cycles through the shelf in OOT's own item cycling ──
// (z_kaleido_item.c KaleidoScope_HandleItemCycleExtras / DrawItemCycleExtras). Previous = shelf back, next =
// shelf front; after the generic handler flips the slot (and its C-button), the shelf rotates so the bottle that
// left the slot takes the other end. Every bottle comes round in turn.
extern "C" {
void KaleidoScope_HandleItemCycleExtras(PlayState* play, u8 slot, bool canCycle, u8 leftItem, u8 rightItem,
                                        bool replaceCButtons);
void KaleidoScope_DrawItemCycleExtras(PlayState* play, u8 slot, u8 canCycle, u8 leftItem, u8 rightItem);
}

extern "C" void Combo_HandleBottleCycles(PlayState* play) {
    if (!ComboBottles_Active() || sShelf.empty())
        return;
    for (int i = 0; i < kBottleSlots; ++i) {
        const u8 slot = static_cast<u8>(kFirstBottleSlot + i);
        const u8 before = gSaveContext.inventory.items[slot];
        if (before == ITEM_NONE)
            continue; // an empty slot is filled from the shelf by the swap UI / next raise, not by cycling
        const u8 left = sShelf.back(), right = sShelf.front();
        KaleidoScope_HandleItemCycleExtras(play, slot, true, left, right, true);
        const u8 after = gSaveContext.inventory.items[slot];
        if (after == before)
            continue;
        if (after == right && !(after == left && sShelf.size() > 1)) {
            sShelf.erase(sShelf.begin()); // took the front; the old bottle goes to the back
            sShelf.push_back(before);
        } else {
            sShelf.pop_back(); // took the back; the old bottle goes to the front
            sShelf.insert(sShelf.begin(), before);
        }
        RefreshButtonsForSlot(slot);
    }
}

extern "C" void Combo_DrawBottleCycles(PlayState* play) {
    if (!ComboBottles_Active() || sShelf.empty())
        return;
    for (int i = 0; i < kBottleSlots; ++i) {
        const u8 slot = static_cast<u8>(kFirstBottleSlot + i);
        if (gSaveContext.inventory.items[slot] == ITEM_NONE)
            continue;
        KaleidoScope_DrawItemCycleExtras(play, slot, true, sShelf.back(), sShelf.front());
    }
}
