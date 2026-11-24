// mm/2s2h/Rando/MiscBehavior/ComboBottleShelf.cpp — ComboShip shared bottles, MM side (owner ruling 2026-11-12:
// 10 bottles, all usable in both games). Combo-owned file (2ship GLOB_RECURSE). Mirror of soh's
// ComboBottleShelf.cpp: MM's 6 native bottle slots are the working set, bottles past them wait on a shelf.
//   held  = native bottle slots in use + shelved bottles
//   found = held - mirroredIn (bottles found IN MM)
// MM_GetSharedTier(SF_BOTTLES) reports `found`; MM_RaiseSharedTier(SF_BOTTLES, total) adds mirrored empty bottles
// until `held == total`. Shelf + mirroredIn persist in the combo save container (MM_BottleShelfGet/SetState).

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ComboExport.h"
#include "Rando/Rando.h"
#include "rando/SharedItems.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
void Interface_LoadItemIconImpl(PlayState* play, u8 btn);
void Interface_Dpad_LoadItemIconImpl(PlayState* play, u8 btn);
}

extern "C" int gMMComboSharedMask; // BenPort.cpp

namespace {
std::vector<uint8_t> sShelf;
int sMirroredIn = 0;
std::string sStateCache;

constexpr int kBottleSlots = SLOT_BOTTLE_6 - SLOT_BOTTLE_1 + 1;

bool IsBottleItem(uint8_t item) {
    return item >= ITEM_BOTTLE && item <= ITEM_OBABA_DRINK;
}

int NativeHeld() {
    int n = 0;
    for (int i = 0; i < kBottleSlots; ++i)
        if (gSaveContext.save.saveInfo.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_NONE)
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
        case ITEM_DEKU_PRINCESS:
            return "Deku Princess";
        case ITEM_MILK_BOTTLE:
            return "Milk";
        case ITEM_MILK_HALF:
            return "Half Milk";
        case ITEM_FISH:
            return "Fish";
        case ITEM_BUG:
            return "Bugs";
        case ITEM_BLUE_FIRE:
            return "Blue Fire";
        case ITEM_POE:
            return "Poe";
        case ITEM_BIG_POE:
            return "Big Poe";
        case ITEM_SPRING_WATER:
            return "Spring Water";
        case ITEM_HOT_SPRING_WATER:
            return "Hot Spring Water";
        case ITEM_ZORA_EGG:
            return "Zora Egg";
        case ITEM_GOLD_DUST:
            return "Gold Dust";
        case ITEM_MUSHROOM:
            return "Magical Mushroom";
        case ITEM_SEAHORSE:
            return "Seahorse";
        case ITEM_CHATEAU:
            return "Chateau Romani";
        case ITEM_HYLIAN_LOACH:
            return "Hylian Loach";
        case ITEM_OBABA_DRINK:
            return "Mysterious Potion";
        default:
            return "?";
    }
}

// Keep every C / D-pad button (all forms) that points at `slot` showing the slot's new contents.
void RefreshButtonsForSlot(int slot) {
    const u8 item = gSaveContext.save.saveInfo.inventory.items[slot];
    for (int form = 0; form < 4; ++form) {
        for (int b = EQUIP_SLOT_C_LEFT; b <= EQUIP_SLOT_C_RIGHT; ++b) {
            if (C_SLOT_EQUIP(form, b) != slot)
                continue;
            BUTTON_ITEM_EQUIP(form, b) = item;
            if (gPlayState != nullptr && form == CUR_FORM)
                Interface_LoadItemIconImpl(gPlayState, b);
        }
        for (int d = EQUIP_SLOT_D_RIGHT; d <= EQUIP_SLOT_D_UP; ++d) {
            if (DPAD_SLOT_EQUIP(form, d) != slot)
                continue;
            DPAD_BUTTON_ITEM_EQUIP(form, d) = item;
            if (gPlayState != nullptr && form == CUR_FORM)
                Interface_Dpad_LoadItemIconImpl(gPlayState, d);
        }
    }
}
} // namespace

bool Combo_MM_BottlesActive() {
    return (gMMComboSharedMask >> ComboRando::SF_BOTTLES) & 1;
}

// RandoItemId bottle items -> the inventory item they put in a slot (ITEM_NONE = not a bottle item).
uint8_t Combo_MM_BottleItemForRi(RandoItemId ri) {
    switch (ri) {
        case RI_BOTTLE_EMPTY:
            return ITEM_BOTTLE;
        case RI_BOTTLE_MILK:
            return ITEM_MILK_BOTTLE;
        case RI_BOTTLE_CHATEAU_ROMANI:
            return ITEM_CHATEAU;
        case RI_BOTTLE_GOLD_DUST:
            return ITEM_GOLD_DUST;
        case RI_BOTTLE_RED_POTION:
            return ITEM_POTION_RED;
        default:
            return ITEM_NONE;
    }
}

int Combo_MM_BottlesHeld() {
    return NativeHeld() + static_cast<int>(sShelf.size());
}

int Combo_MM_BottlesFound() {
    return std::max(0, Combo_MM_BottlesHeld() - sMirroredIn);
}

void Combo_MM_BottlesAddMirrored() {
    for (int i = 0; i < kBottleSlots; ++i) {
        u8& it = gSaveContext.save.saveInfo.inventory.items[SLOT_BOTTLE_1 + i];
        if (it == ITEM_NONE) {
            it = ITEM_BOTTLE;
            RefreshButtonsForSlot(SLOT_BOTTLE_1 + i);
            ++sMirroredIn;
            return;
        }
    }
    sShelf.push_back(ITEM_BOTTLE);
    ++sMirroredIn;
}

// A bottle given while all six slots are in use goes to the shelf. Returns true if shelved.
bool Combo_MM_BottlesShelveIfFull(uint8_t item) {
    if (!IsBottleItem(item) || NativeHeld() < kBottleSlots)
        return false;
    sShelf.push_back(item);
    return true;
}

extern "C" COMBO_EXPORT const char* MM_BottleShelfGetState(void) {
    try {
        sStateCache = nlohmann::json{ { "shelf", sShelf }, { "mirroredIn", sMirroredIn } }.dump();
    } catch (...) { sStateCache = "{}"; }
    return sStateCache.c_str();
}

extern "C" COMBO_EXPORT void MM_BottleShelfSetState(const char* json) {
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

extern "C" COMBO_EXPORT const char* MM_BottleShelfDescribe(void) {
    static std::string cache;
    try {
        nlohmann::json native = nlohmann::json::array(), shelf = nlohmann::json::array();
        for (int i = 0; i < kBottleSlots; ++i) {
            uint8_t it = gSaveContext.save.saveInfo.inventory.items[SLOT_BOTTLE_1 + i];
            native.push_back({ { "item", it }, { "name", it == ITEM_NONE ? "(none)" : BottleName(it) } });
        }
        for (uint8_t it : sShelf)
            shelf.push_back({ { "item", it }, { "name", BottleName(it) } });
        cache = nlohmann::json{ { "native", native }, { "shelf", shelf } }.dump();
    } catch (...) { cache = "{}"; }
    return cache.c_str();
}

extern "C" COMBO_EXPORT int MM_BottleShelfSwap(int shelfIdx, int slotIdx) {
    if (shelfIdx < 0 || shelfIdx >= static_cast<int>(sShelf.size()) || slotIdx < 0 || slotIdx >= kBottleSlots)
        return 0;
    u8& native = gSaveContext.save.saveInfo.inventory.items[SLOT_BOTTLE_1 + slotIdx];
    uint8_t shelved = sShelf[shelfIdx];
    if (native == ITEM_NONE) {
        native = shelved;
        sShelf.erase(sShelf.begin() + shelfIdx);
    } else {
        sShelf[shelfIdx] = native;
        native = shelved;
    }
    RefreshButtonsForSlot(SLOT_BOTTLE_1 + slotIdx);
    return 1;
}

// ── Pause-menu "box": MM's own item cycling (KaleidoItemPage.cpp) on bottle slots ──
// Previous = shelf back, next = shelf front; a step moves that bottle into the slot and the old one to the other end.
bool Combo_MM_BottleCycleSlot(int slot) {
    return Combo_MM_BottlesActive() && !sShelf.empty() && slot >= SLOT_BOTTLE_1 && slot <= SLOT_BOTTLE_6 &&
           gSaveContext.save.saveInfo.inventory.items[slot] != ITEM_NONE;
}
uint8_t Combo_MM_BottleCyclePeek(bool next) {
    return sShelf.empty() ? ITEM_NONE : (next ? sShelf.front() : sShelf.back());
}
void Combo_MM_BottleCycleStep(int slot, bool next) {
    if (sShelf.empty())
        return;
    u8& it = gSaveContext.save.saveInfo.inventory.items[slot];
    const uint8_t before = it;
    if (next) {
        it = sShelf.front();
        sShelf.erase(sShelf.begin());
        sShelf.push_back(before);
    } else {
        it = sShelf.back();
        sShelf.pop_back();
        sShelf.insert(sShelf.begin(), before);
    }
    RefreshButtonsForSlot(slot);
}
