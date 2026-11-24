// mm/2s2h/Rando/MiscBehavior/ComboEquipShare.cpp — ComboShip: shared button layout between games (owner ask
// 2026-11-12, "share equips between games"). Combo-owned file (2ship GLOB_RECURSE).
//
// What sits on C-left/down/right and the D-pad travels with Link across a game switch. Each game describes its
// layout in game-neutral keys ("bow", "hookshot", "bottle:milk", "mask_bunny"...); the launcher carries the JSON
// across; the receiving game puts a key on the same button ONLY if it owns that item, otherwise the button keeps
// what it had. Layout JSON: {"c":[L,D,R], "d":{"up":k,"down":k,"left":k,"right":k}} ("" = nothing to share).
//
// Merged pause menus (ComboPause.cpp): a button can hold a FOREIGN (OOT) item, so each button also travels with its
// origin ("cx":[{g,s}...], "dx":{dir:{g,s}}: the game owning the inventory slot + that slot). The receiver puts its
// own item there when it has one (its own slot, or a shared item's native twin by key) and otherwise the other
// game's item as a foreign id, so the SAME layout appears in both games. A button empty on the sender is emptied.

#include <cstdio>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <libultraship/bridge/consolevariablebridge.h>

#include "ComboExport.h"
#include "ComboPauseABI.h"
#include "ComboPause.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
void Interface_LoadItemIconImpl(PlayState* play, u8 btn);
void Interface_Dpad_LoadItemIconImpl(PlayState* play, u8 btn);
}

namespace {

struct KeyItem {
    u8 item;
    const char* key;
};

// Several items can share a key (any ocarina, hookshot/longshot): the receiving game uses whichever it owns.
const KeyItem kKeys[] = {
    { ITEM_DEKU_STICK, "sticks" },
    { ITEM_DEKU_NUT, "nuts" },
    { ITEM_BOMB, "bombs" },
    { ITEM_BOMBCHU, "bombchus" },
    { ITEM_BOW, "bow" },
    { ITEM_ARROW_FIRE, "arrow_fire" },
    { ITEM_ARROW_ICE, "arrow_ice" },
    { ITEM_ARROW_LIGHT, "arrow_light" },
    { ITEM_OCARINA_OF_TIME, "ocarina" },
    { ITEM_OCARINA_FAIRY, "ocarina" },
    { ITEM_HOOKSHOT, "hookshot" },
    { ITEM_LONGSHOT, "hookshot" },
    { ITEM_LENS_OF_TRUTH, "lens" },
    { ITEM_MAGIC_BEANS, "beans" },
    { ITEM_SLINGSHOT, "slingshot" },
    { ITEM_POWDER_KEG, "powder_keg" },
    { ITEM_PICTOGRAPH_BOX, "pictograph" },
    { ITEM_SWORD_GREAT_FAIRY, "great_fairy_sword" },
    { ITEM_MASK_KEATON, "mask_keaton" },
    { ITEM_MASK_BUNNY, "mask_bunny" },
    { ITEM_MASK_GORON, "mask_goron" },
    { ITEM_MASK_ZORA, "mask_zora" },
    { ITEM_MASK_TRUTH, "mask_truth" },
    { ITEM_MASK_DEKU, "mask_deku" },
    { ITEM_MASK_FIERCE_DEITY, "mask_fierce_deity" },
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
    return item >= ITEM_BOTTLE && item <= ITEM_OBABA_DRINK;
}

// Owned item + its inventory slot for a key, or false.
bool Resolve(const std::string& key, u8* itemOut, u8* slotOut) {
    if (key.empty())
        return false;
    for (const auto& k : kKeys) {
        if (key != k.key)
            continue;
        if (IsBottleContent(k.item)) {
            for (int s = SLOT_BOTTLE_1; s <= SLOT_BOTTLE_6; ++s) {
                if (gSaveContext.save.saveInfo.inventory.items[s] == k.item) {
                    *itemOut = k.item;
                    *slotOut = (u8)s;
                    return true;
                }
            }
            continue;
        }
        const u8 slot = gItemSlots[k.item];
        if (slot < ARRAY_COUNT(gSaveContext.save.saveInfo.inventory.items) &&
            gSaveContext.save.saveInfo.inventory.items[slot] == k.item) {
            *itemOut = k.item;
            *slotOut = slot;
            return true;
        }
    }
    return false;
}

std::string sPending;
std::string sCache;

// One button of a pushed layout: MM's own item if it has one, else OOT's as a foreign id. false = leave the button.
// `clear` = the sender's button was empty (new-format layouts only).
bool ResolveButton(const nlohmann::json& key, const nlohmann::json* origin, u8* itemOut, u8* slotOut, bool* clear) {
    *clear = false;
    if (origin != nullptr && origin->is_object()) {
        const std::string g = origin->value("g", std::string());
        const int s = origin->value("s", -1);
        const Inventory& inv = gSaveContext.save.saveInfo.inventory;
        if (g == "mm" && s >= 0 && s < COMBO_PAUSE_MM_SLOTS && inv.items[s] != ITEM_NONE) {
            *itemOut = inv.items[s];
            *slotOut = (u8)s;
            return true;
        }
        if (key.is_string() && Resolve(key.get<std::string>(), itemOut, slotOut))
            return true; // shared item: MM's own copy
        if (g == "oot" && s >= 0 && s < COMBO_PAUSE_OOT_SLOTS && Combo_MmPauseOotItemAt(s) != ITEM_NONE) {
            const u8 foreign = (u8)ComboPause_MmEncode(s);
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

// Where a button's item lives: {"g":"oot"|"mm","s":slot}, or null for an empty button.
nlohmann::json Origin(u8 item, u8 slot) {
    const int ootSlot = Combo_MmPauseForeignSlot(item);
    if (ootSlot >= 0)
        return nlohmann::json{ { "g", "oot" }, { "s", ootSlot } };
    if (item == ITEM_NONE || slot >= COMBO_PAUSE_MM_SLOTS)
        return nullptr;
    return nlohmann::json{ { "g", "mm" }, { "s", slot } };
}

bool DpadOn() {
    return CVarGetInteger("gEnhancements.Dpad.DpadEquips", 0) != 0;
}

// The human form's layout (index 0 = human in MM's per-form button arrays).
void Apply() {
    if (sPending.empty() || gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr)
        return;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(sPending);
    } catch (...) {
        sPending.clear();
        return;
    }
    sPending.clear();
    // MM keeps C-button and D-pad items in form 0's arrays for EVERY form (GET_CUR_FORM_BTN_ITEM /
    // DPAD_GET_CUR_FORM_BTN_ITEM read index 0 unless the button is B, which this never touches). So the icons are
    // reloaded whatever form Link arrives in -- gating them on CUR_FORM == 0 left a Goron/Zora/Deku/FD Link who
    // crossed in transformed with stale icons over the new items.
    const int form = 0;
    auto clearDuplicates = [&](u8 slot, int keepC, int keepD) {
        for (int b = EQUIP_SLOT_C_LEFT; b <= EQUIP_SLOT_C_RIGHT; ++b)
            if (b != keepC && C_SLOT_EQUIP(form, b) == slot) {
                C_SLOT_EQUIP(form, b) = SLOT_NONE;
                BUTTON_ITEM_EQUIP(form, b) = ITEM_NONE;
                Interface_LoadItemIconImpl(gPlayState, b);
            }
        for (int d = EQUIP_SLOT_D_RIGHT; d <= EQUIP_SLOT_D_UP; ++d)
            if (d != keepD && DPAD_SLOT_EQUIP(form, d) == slot) {
                DPAD_SLOT_EQUIP(form, d) = SLOT_NONE;
                DPAD_BUTTON_ITEM_EQUIP(form, d) = ITEM_NONE;
                Interface_Dpad_LoadItemIconImpl(gPlayState, d);
            }
    };
    const auto c = j.value("c", nlohmann::json::array());
    const auto cx = j.value("cx", nlohmann::json());
    for (int i = 0; i < 3 && i < (int)c.size(); ++i) {
        u8 item, slot;
        bool clear;
        const int b = EQUIP_SLOT_C_LEFT + i;
        if (!ResolveButton(c[i], cx.is_array() && i < (int)cx.size() ? &cx[i] : nullptr, &item, &slot, &clear)) {
            if (clear) {
                C_SLOT_EQUIP(form, b) = SLOT_NONE;
                BUTTON_ITEM_EQUIP(form, b) = ITEM_NONE;
                Interface_LoadItemIconImpl(gPlayState, b);
            }
            continue;
        }
        clearDuplicates(slot, b, -1);
        C_SLOT_EQUIP(form, b) = slot;
        BUTTON_ITEM_EQUIP(form, b) = item;
        Interface_LoadItemIconImpl(gPlayState, b);
    }
    if (DpadOn()) {
        const auto d = j.value("d", nlohmann::json::object());
        const auto dx = j.value("dx", nlohmann::json());
        const std::pair<const char*, int> kDirs[] = { { "right", EQUIP_SLOT_D_RIGHT },
                                                      { "left", EQUIP_SLOT_D_LEFT },
                                                      { "down", EQUIP_SLOT_D_DOWN },
                                                      { "up", EQUIP_SLOT_D_UP } };
        for (const auto& [name, btn] : kDirs) {
            u8 item, slot;
            bool clear;
            const nlohmann::json key = d.value(name, std::string());
            if (!ResolveButton(key, dx.is_object() && dx.contains(name) ? &dx[name] : nullptr, &item, &slot, &clear)) {
                if (clear) {
                    DPAD_SLOT_EQUIP(form, btn) = SLOT_NONE;
                    DPAD_BUTTON_ITEM_EQUIP(form, btn) = ITEM_NONE;
                    Interface_Dpad_LoadItemIconImpl(gPlayState, btn);
                }
                continue;
            }
            clearDuplicates(slot, -1, btn);
            DPAD_SLOT_EQUIP(form, btn) = slot;
            DPAD_BUTTON_ITEM_EQUIP(form, btn) = item;
            Interface_Dpad_LoadItemIconImpl(gPlayState, btn);
        }
    }
    fprintf(
        stderr,
        "[ComboPause/MM] shared layout applied in Termina: C %02X/%02X/%02X D-up/down/left/right %02X/%02X/%02X/%02X\n",
        BUTTON_ITEM_EQUIP(form, EQUIP_SLOT_C_LEFT), BUTTON_ITEM_EQUIP(form, EQUIP_SLOT_C_DOWN),
        BUTTON_ITEM_EQUIP(form, EQUIP_SLOT_C_RIGHT), DPAD_BUTTON_ITEM_EQUIP(form, EQUIP_SLOT_D_UP),
        DPAD_BUTTON_ITEM_EQUIP(form, EQUIP_SLOT_D_DOWN), DPAD_BUTTON_ITEM_EQUIP(form, EQUIP_SLOT_D_LEFT),
        DPAD_BUTTON_ITEM_EQUIP(form, EQUIP_SLOT_D_RIGHT));
}

void RegisterComboEquipShare() {
    COND_HOOK(OnGameStateUpdate, true, []() { Apply(); });
}

static RegisterShipInitFunc sInit(RegisterComboEquipShare, {});

} // namespace

// The human form's current layout as game-neutral keys.
extern "C" COMBO_EXPORT const char* MM_GetComboButtons(void) {
    try {
        const int form = 0;
        nlohmann::json c = nlohmann::json::array();
        nlohmann::json cx = nlohmann::json::array();
        for (int b = EQUIP_SLOT_C_LEFT; b <= EQUIP_SLOT_C_RIGHT; ++b) {
            c.push_back(KeyFor(BUTTON_ITEM_EQUIP(form, b)));
            cx.push_back(Origin(BUTTON_ITEM_EQUIP(form, b), C_SLOT_EQUIP(form, b)));
        }
        nlohmann::json d = nlohmann::json::object();
        nlohmann::json dx = nlohmann::json::object();
        if (DpadOn()) {
            const std::pair<const char*, int> kDirs[] = { { "right", EQUIP_SLOT_D_RIGHT },
                                                          { "left", EQUIP_SLOT_D_LEFT },
                                                          { "down", EQUIP_SLOT_D_DOWN },
                                                          { "up", EQUIP_SLOT_D_UP } };
            for (const auto& [name, btn] : kDirs) {
                d[name] = KeyFor(DPAD_BUTTON_ITEM_EQUIP(form, btn));
                dx[name] = Origin(DPAD_BUTTON_ITEM_EQUIP(form, btn), DPAD_SLOT_EQUIP(form, btn));
            }
        }
        sCache = nlohmann::json{ { "c", c }, { "d", d }, { "cx", cx }, { "dx", dx } }.dump();
    } catch (...) { sCache = "{}"; }
    return sCache.c_str();
}

// Launcher push before MM boots/resumes: applied on the first frame with Link in play.
extern "C" COMBO_EXPORT void MM_SetComboButtons(const char* json) {
    sPending = json ? json : "";
}
