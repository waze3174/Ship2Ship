// soh/soh/Enhancements/randomizer/ComboEquipShare.cpp — ComboShip: shared button layout between games (owner ask
// 2026-11-12, "share equips between games"). Combo-owned file (soh GLOB_RECURSE). OOT side of
// mm/2s2h/Rando/MiscBehavior/ComboEquipShare.cpp: same game-neutral keys, same JSON
// ({"c":[L,D,R], "d":{"up","down","left","right"}}). OOT's save context is torn down by the time the launcher asks,
// so the switch hook captures the layout at the door (ComboEquipShare_CaptureAtSwitch).
//
// Merged pause menus (ComboPause.cpp): a button can hold a FOREIGN item, so each button also travels with its origin
// ("cx":[{g,s}...], "dx":{dir:{g,s}}: the game that owns the inventory slot + that slot). The receiver puts its own
// item there when it has one (its own slot, or a shared item's native twin by key) and otherwise the other game's
// item as a foreign id, so the SAME layout appears in both games. A button empty on the sender is emptied.
//
// In Hyrule, OOT's age rule holds on every item button (owner 2026-11-25, playtest #21): an item Link's age can't use
// there (the bow, hookshot or magic arrows for child; the slingshot, boomerang or sticks for adult) doesn't stay on a
// button after a crossing, a save load or an age swap. See KeepAgeRule.

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "ComboExport.h"
#include "ComboPauseABI.h"
#include "ComboPause.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace {

struct KeyItem {
    u8 item;
    const char* key;
};

const KeyItem kKeys[] = {
    { ITEM_STICK, "sticks" },
    { ITEM_NUT, "nuts" },
    { ITEM_BOMB, "bombs" },
    { ITEM_BOMBCHU, "bombchus" },
    { ITEM_BOW, "bow" },
    { ITEM_ARROW_FIRE, "arrow_fire" },
    { ITEM_ARROW_ICE, "arrow_ice" },
    { ITEM_ARROW_LIGHT, "arrow_light" },
    { ITEM_OCARINA_TIME, "ocarina" },
    { ITEM_OCARINA_FAIRY, "ocarina" },
    { ITEM_HOOKSHOT, "hookshot" },
    { ITEM_LONGSHOT, "hookshot" },
    { ITEM_LENS, "lens" },
    { ITEM_BEAN, "beans" },
    { ITEM_SLINGSHOT, "slingshot" },
    { ITEM_BOOMERANG, "boomerang" },
    { ITEM_HAMMER, "hammer" },
    { ITEM_DINS_FIRE, "dins_fire" },
    { ITEM_FARORES_WIND, "farores_wind" },
    { ITEM_NAYRUS_LOVE, "nayrus_love" },
    { ITEM_MASK_KEATON, "mask_keaton" },
    { ITEM_MASK_BUNNY, "mask_bunny" },
    { ITEM_MASK_GORON, "mask_goron" },
    { ITEM_MASK_ZORA, "mask_zora" },
    { ITEM_MASK_TRUTH, "mask_truth" },
    { ITEM_BOTTLE, "bottle:empty" },
    { ITEM_POTION_RED, "bottle:red" },
    { ITEM_POTION_GREEN, "bottle:green" },
    { ITEM_POTION_BLUE, "bottle:blue" },
    { ITEM_FAIRY, "bottle:fairy" },
    { ITEM_FISH, "bottle:fish" },
    { ITEM_MILK_BOTTLE, "bottle:milk" },
    { ITEM_MILK_HALF, "bottle:milk_half" },
    { ITEM_BLUE_FIRE, "bottle:blue_fire" },
    { ITEM_BUG, "bottle:bugs" },
    { ITEM_BIG_POE, "bottle:big_poe" },
    { ITEM_POE, "bottle:poe" },
};

const char* KeyFor(u8 item) {
    for (const auto& k : kKeys)
        if (k.item == item)
            return k.key;
    return "";
}

bool IsBottleContent(u8 item) {
    return item >= ITEM_BOTTLE && item <= ITEM_POE;
}

bool Resolve(const std::string& key, u8* itemOut, u8* slotOut) {
    if (key.empty())
        return false;
    for (const auto& k : kKeys) {
        if (key != k.key)
            continue;
        if (IsBottleContent(k.item)) {
            for (int s = SLOT_BOTTLE_1; s <= SLOT_BOTTLE_4; ++s) {
                if (gSaveContext.inventory.items[s] == k.item) {
                    *itemOut = k.item;
                    *slotOut = (u8)s;
                    return true;
                }
            }
            continue;
        }
        const u8 slot = gItemSlots[k.item];
        if (slot < ARRAY_COUNT(gSaveContext.inventory.items) && gSaveContext.inventory.items[slot] == k.item) {
            *itemOut = k.item;
            *slotOut = slot;
            return true;
        }
    }
    return false;
}

std::string sPending, sCaptured, sCache;
bool sAgeRulePending = false; // a save was loaded: hold its buttons to the age rule once Link is in play

constexpr int kFirstItemButton = 1; // buttonItems: 0 B, 1-3 C left/down/right, 4-7 D up/down/left/right
constexpr int kButtonCount = ARRAY_COUNT(gSaveContext.equips.buttonItems);

bool DpadOn() {
    return CVarGetInteger("gEnhancements.DpadEquips", 0) != 0;
}

// A tunic or boots button from Termina (OOT slots SLOT_TUNIC_KOKIRI..SLOT_BOOTS_HOVER, as OOT stores them): kept with
// SoH's "Assignable Tunics and Boots" on, while OOT owns the piece.
bool GearButtonOk(int slot) {
    if ((slot < SLOT_TUNIC_KOKIRI) || (slot > SLOT_BOOTS_HOVER) ||
        !CVarGetInteger(CVAR_ENHANCEMENT("AssignableTunicsAndBoots"), 0)) {
        return false;
    }
    const int k = slot - SLOT_TUNIC_KOKIRI;
    return CHECK_OWNED_EQUIP(EQUIP_TYPE_TUNIC + k / 3, k % 3) != 0;
}

// Where a button's item lives: {"g":"oot"|"mm","s":slot}, or null for an empty button / no inventory slot.
nlohmann::json Origin(int b) {
    const u8 item = gSaveContext.equips.buttonItems[b];
    const int mmSlot = ComboPause_OotDecode(item);
    if (mmSlot >= 0)
        return nlohmann::json{ { "g", "mm" }, { "s", mmSlot } };
    const u8 slot = gSaveContext.equips.cButtonSlots[b - 1];
    if (item == ITEM_NONE || slot >= COMBO_PAUSE_OOT_SLOTS + COMBO_PAUSE_OOT_GEAR_SLOTS)
        return nullptr;
    return nlohmann::json{ { "g", "oot" }, { "s", slot } };
}

std::string Describe() {
    // buttonItems: 0 B, 1-3 C left/down/right, 4-7 D up/down/left/right.
    nlohmann::json c = nlohmann::json::array();
    nlohmann::json cx = nlohmann::json::array();
    for (int b = 1; b <= 3; ++b) {
        c.push_back(KeyFor(gSaveContext.equips.buttonItems[b]));
        cx.push_back(Origin(b));
    }
    nlohmann::json d = nlohmann::json::object();
    nlohmann::json dx = nlohmann::json::object();
    if (DpadOn()) {
        const std::pair<const char*, int> kDirs[] = { { "up", 4 }, { "down", 5 }, { "left", 6 }, { "right", 7 } };
        for (const auto& [name, b] : kDirs) {
            d[name] = KeyFor(gSaveContext.equips.buttonItems[b]);
            dx[name] = Origin(b);
        }
    }
    return nlohmann::json{ { "c", c }, { "d", d }, { "cx", cx }, { "dx", dx } }.dump();
}

// One button of a pushed layout: OOT's own item if it has one, else MM's as a foreign id. false = leave the button.
// `clear` = the sender's button was empty (new-format layouts only).
bool ResolveButton(const nlohmann::json& key, const nlohmann::json* origin, u8* itemOut, u8* slotOut, bool* clear) {
    *clear = false;
    if (origin != nullptr && origin->is_object()) {
        const std::string g = origin->value("g", std::string());
        const int s = origin->value("s", -1);
        if (g == "oot" && s >= 0 && s < COMBO_PAUSE_OOT_SLOTS && gSaveContext.inventory.items[s] != ITEM_NONE) {
            *itemOut = gSaveContext.inventory.items[s];
            *slotOut = (u8)s;
            return true;
        }
        if (g == "oot" && GearButtonOk(s)) {
            *itemOut = (u8)(ITEM_TUNIC_KOKIRI + (s - SLOT_TUNIC_KOKIRI));
            *slotOut = (u8)s;
            return true;
        }
        if (key.is_string() && Resolve(key.get<std::string>(), itemOut, slotOut))
            return true; // shared item: OOT's own copy
        if (g == "mm" && s >= 0 && s < COMBO_PAUSE_MM_SLOTS) {
            const u8 foreign = (u8)ComboPause_OotEncode(s);
            if (Combo_PauseSlotItemAny(s) == ITEM_NONE)
                return false; // MM no longer holds it
            *itemOut = foreign;
            *slotOut = foreign; // a foreign button's slot byte is its foreign id (ComboPause.cpp)
            return true;
        }
        return false;
    }
    if (origin != nullptr && origin->is_null()) {
        *clear = true;
        return false;
    }
    return key.is_string() && Resolve(key.get<std::string>(), itemOut, slotOut);
}

void Assign(int b, u8 item, u8 slot) {
    for (int o = 1; o <= 7; ++o) {
        if (o != b && gSaveContext.equips.cButtonSlots[o - 1] == slot) {
            gSaveContext.equips.cButtonSlots[o - 1] = SLOT_NONE;
            gSaveContext.equips.buttonItems[o] = ITEM_NONE;
        }
    }
    gSaveContext.equips.buttonItems[b] = item;
    gSaveContext.equips.cButtonSlots[b - 1] = slot;
}

bool SlotOnAButton(u8 slot) {
    for (int b = kFirstItemButton; b < kButtonCount; ++b) {
        if (gSaveContext.equips.cButtonSlots[b - 1] == slot)
            return true;
    }
    return false;
}

// What a button that held `item` from `slot` would hold now: a bottle as its slot holds it now (OOT's age swap re-reads
// bottles the same way), an OOT item its slot still holds, a tunic or boots OOT still owns, an MM item MM still holds.
// ITEM_NONE when it's gone.
u8 StillHeld(u8 item, u8 slot) {
    if ((item == ITEM_NONE) || (slot == SLOT_NONE))
        return ITEM_NONE;
    const int mmSlot = ComboPause_OotDecode(item);
    if (mmSlot >= 0)
        return (Combo_PauseSlotItemAny(mmSlot) != ITEM_NONE) ? item : ITEM_NONE;
    const u8 held = (slot < COMBO_PAUSE_OOT_SLOTS) ? gSaveContext.inventory.items[slot] : ITEM_NONE;
    if ((slot >= SLOT_BOTTLE_1) && (slot <= SLOT_BOTTLE_4))
        return IsBottleContent(held) ? held : ITEM_NONE;
    if (slot < COMBO_PAUSE_OOT_SLOTS)
        return (held == item) ? item : ITEM_NONE;
    return (GearButtonOk(slot) && (item == ITEM_TUNIC_KOKIRI + (slot - SLOT_TUNIC_KOKIRI))) ? item : ITEM_NONE;
}

// OOT's age rule on the C buttons and the D-pad for Link at `age` (Combo_PauseItemAgeOk; nothing is refused while
// Combo_AgeRulesLifted). Vanilla OOT never lets such an item onto a button: its pause menu refuses it, and its age swap
// trades whole button sets. A refused button goes back to what `fallback` had there (what that age had before) if
// Link can use that, still has it and has it on no other button, else it's emptied. true = a button changed.
bool KeepAgeRule(int age, const u8* fallbackItems, const u8* fallbackSlots) {
    auto& equips = gSaveContext.equips;
    bool refused[kButtonCount] = {};
    bool changed = false;
    for (int b = kFirstItemButton; b < kButtonCount; ++b) {
        if (Combo_PauseItemAgeOk(equips.buttonItems[b], age))
            continue;
        fprintf(stderr, "[ComboPause] OOT's age rule takes item %02X off button %d (%s Link)\n", equips.buttonItems[b],
                b, (age == LINK_AGE_CHILD) ? "child" : "adult");
        refused[b] = true;
        changed = true;
        equips.buttonItems[b] = ITEM_NONE;
        equips.cButtonSlots[b - 1] = SLOT_NONE;
    }
    for (int b = kFirstItemButton; b < kButtonCount; ++b) {
        if (!refused[b])
            continue;
        const u8 slot = fallbackSlots[b - 1];
        const u8 item = StillHeld(fallbackItems[b], slot);
        if ((item == ITEM_NONE) || !Combo_PauseItemAgeOk(item, age) || SlotOnAButton(slot))
            continue;
        equips.buttonItems[b] = item;
        equips.cButtonSlots[b - 1] = slot;
    }
    return changed;
}

// A layout pushed by the launcher, onto Link's buttons. false = nothing pushed (or unreadable).
bool ApplyPendingLayout() {
    if (sPending.empty())
        return false;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(sPending);
    } catch (...) {
        sPending.clear();
        return false;
    }
    sPending.clear();
    const auto c = j.value("c", nlohmann::json::array());
    const auto cx = j.value("cx", nlohmann::json());
    auto apply = [](int b, const nlohmann::json& key, const nlohmann::json* origin) {
        u8 item, slot;
        bool clear;
        if (ResolveButton(key, origin, &item, &slot, &clear)) {
            Assign(b, item, slot);
        } else if (clear) {
            gSaveContext.equips.buttonItems[b] = ITEM_NONE;
            gSaveContext.equips.cButtonSlots[b - 1] = SLOT_NONE;
        }
    };
    for (int i = 0; i < 3 && i < (int)c.size(); ++i)
        apply(1 + i, c[i], cx.is_array() && i < (int)cx.size() ? &cx[i] : nullptr);
    if (DpadOn()) {
        const auto d = j.value("d", nlohmann::json::object());
        const auto dx = j.value("dx", nlohmann::json());
        const std::pair<const char*, int> kDirs[] = { { "up", 4 }, { "down", 5 }, { "left", 6 }, { "right", 7 } };
        for (const auto& [name, b] : kDirs) {
            const nlohmann::json key = d.value(name, std::string());
            apply(b, key, dx.is_object() && dx.contains(name) ? &dx[name] : nullptr);
        }
    }
    return true;
}

void RegisterComboEquipShare() {
    COND_HOOK(OnLoadGame, true, [](int32_t) { sAgeRulePending = true; });
}

RegisterShipInitFunc sInit(RegisterComboEquipShare, {});

} // namespace

// Per frame (OTRGlobals' combo frame hook), once Link is in play: apply a pushed layout, then (randomizer saves) hold
// the buttons to OOT's age rule. A button the layout filled with an item Link's age can't use gets back what OOT's
// save had there; a save loaded with such an item on a button (from an older build) loses it.
void ComboEquipShare_Tick() {
    if ((sPending.empty() && !sAgeRulePending) || gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr ||
        gSaveContext.fileNum > 2)
        return;
    sAgeRulePending = false;
    u8 savedItems[kButtonCount];
    u8 savedSlots[ARRAY_COUNT(gSaveContext.equips.cButtonSlots)];
    memcpy(savedItems, gSaveContext.equips.buttonItems, sizeof(savedItems));
    memcpy(savedSlots, gSaveContext.equips.cButtonSlots, sizeof(savedSlots));
    const bool applied = ApplyPendingLayout();
    const bool aged = IS_RANDO && KeepAgeRule(gSaveContext.linkAge, savedItems, savedSlots);
    if (applied) {
        fprintf(stderr, "[ComboPause] shared layout applied in Hyrule: C %02X/%02X/%02X D %02X/%02X/%02X/%02X\n",
                gSaveContext.equips.buttonItems[1], gSaveContext.equips.buttonItems[2],
                gSaveContext.equips.buttonItems[3], gSaveContext.equips.buttonItems[4],
                gSaveContext.equips.buttonItems[5], gSaveContext.equips.buttonItems[6],
                gSaveContext.equips.buttonItems[7]);
    }
    if (!applied && !aged)
        return;
    for (int b = kFirstItemButton; b < kButtonCount; ++b)
        Interface_LoadItemIcon1(gPlayState, b);
}

// OOT's age swap in a randomizer save keeps one C-button and D-pad layout for both ages (z_parameter.c,
// Inventory_SwapAgeEquipment). `own` is the new age's own set, what vanilla's swap puts on the buttons: it takes back a
// button the new age can't use.
extern "C" void ComboEquipShare_KeepAgeRuleAtSwap(int newAge, const u8* ownItems, const u8* ownSlots) {
    KeepAgeRule(newAge, ownItems, ownSlots);
}

void ComboEquipShare_CaptureAtSwitch() {
    try {
        sCaptured = Describe();
    } catch (...) { sCaptured.clear(); }
}

extern "C" COMBO_EXPORT const char* SOH_GetComboButtons(void) {
    if (!sCaptured.empty()) {
        sCache = sCaptured;
        sCaptured.clear(); // consumed by this switch
        return sCache.c_str();
    }
    try {
        sCache = gSaveContext.fileNum <= 2 ? Describe() : std::string("{}");
    } catch (...) { sCache = "{}"; }
    return sCache.c_str();
}

extern "C" COMBO_EXPORT void SOH_SetComboButtons(const char* json) {
    sPending = json ? json : "";
}
