// mm/2s2h/Rando/MiscBehavior/ComboOotItems.cpp — ComboShip: OOT items used in Termina. Combo-owned file (2ship
// GLOB_RECURSE). Deviation record: docs/deviations/ui-menu.md "OOT items in Termina".
//
// Owner bar (2026-11-13): "Every item has to work from both games in both games, go down the list, correct models,
// correct sounds, animations, all of it, no sloppy analogs". Each OOT item without a Termina twin is a PORT of OOT's
// own code running inside MM:
//   - the player side (OOT's item action: upper-body action chain, animations from OOT's archive, OOT's timings) in
//     mm/src/overlays/actors/ovl_player_actor/z_player_combo_oot.inc, on PLAYER_IA_COMBO_OOT_* item actions;
//   - OOT's actors as combo files in MM's overlay dirs (OOT's Boomerang = MM EnBoom params COMBO_OOT_BOOMERANG, ...);
//   - OOT's models through "__OTR__@oot:" routed display lists (OOT's ResourceManager stays resident);
//   - OOT's save stays the owner: ammo (SOH_ComboAmmoBag), equipment and age rules come from OOT through its exports
//     (soh/soh/Enhancements/randomizer/ComboTerminaItems.cpp, ComboSharedHealth.cpp).
// Only damage / puzzle credit maps to MM (e.g. the Boomerang's damage bit is MM's Zora-boomerang bit).
#ifdef COMBO_BUILD

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <memory>

#include <libultraship/libultraship.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceManagerScope.h>

#include "ComboResolve.h"
#include "ComboPauseABI.h"
#include "ComboOotItems.h"
#include "ComboAdultBody.h"
#include "ComboPause.h"
#include "2s2h/CustomMessage/CustomMessage.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace {

typedef int (*FnIntInt)(int);
typedef int (*FnIntIntInt)(int, int);
typedef void (*FnVoidIntInt)(int, int);

template <typename T> T Sym(const char* name) {
    return (T)Combo_ResolveSym("soh", name);
}

bool LinkAdult() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr &&
           Combo_MM_AdultBodyActive(&GET_PLAYER(gPlayState)->actor);
}

const char* RoutedChild(const char* name) {
    static std::unordered_map<std::string, std::string> sInterned;
    auto it = sInterned.find(name);
    if (it == sInterned.end())
        it = sInterned.emplace(name, std::string("__OTR__@oot:objects/object_link_child/") + name).first;
    return it->second.c_str();
}

const char* RoutedAdult(const char* name) {
    static std::unordered_map<std::string, std::string> sInterned;
    auto it = sInterned.find(name);
    if (it == sInterned.end())
        it = sInterned.emplace(name, std::string("__OTR__@oot:objects/object_link_boy/") + name).first;
    return it->second.c_str();
}

} // namespace

extern "C" void Combo_MmOotLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ComboOot] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

extern "C" int Combo_MmOotItemOf(int foreignItem) {
    const int slot = ComboPause_MmDecode(foreignItem);
    if (slot < 0)
        return -1;
    const int item = Combo_MmPauseOotItemAt(slot);
    return (item == ITEM_NONE) ? -1 : item;
}

extern "C" int Combo_MmOotItemAction(int foreignItem) {
    switch (Combo_MmOotItemOf(foreignItem)) {
        case COMBO_OOT_ITEM_BOOMERANG:
            return PLAYER_IA_COMBO_OOT_BOOMERANG;
        case COMBO_OOT_ITEM_HAMMER:
            return PLAYER_IA_COMBO_OOT_HAMMER;
        case COMBO_OOT_ITEM_SLINGSHOT:
            return PLAYER_IA_COMBO_OOT_SLINGSHOT;
        case COMBO_OOT_ITEM_HOOKSHOT:
            return PLAYER_IA_COMBO_OOT_HOOKSHOT;
        case COMBO_OOT_ITEM_MASK_SKULL:
        case COMBO_OOT_ITEM_MASK_SPOOKY:
        case COMBO_OOT_ITEM_MASK_GERUDO:
            return PLAYER_IA_COMBO_OOT_MASK; // worn by Combo_MmOotToggleMask; never held
        case COMBO_OOT_ITEM_LONGSHOT:
            return PLAYER_IA_COMBO_OOT_LONGSHOT;
        case COMBO_OOT_ITEM_DINS_FIRE:
            return PLAYER_IA_COMBO_OOT_DINS_FIRE;
        case COMBO_OOT_ITEM_FARORES_WIND:
            return PLAYER_IA_COMBO_OOT_FARORES_WIND;
        case COMBO_OOT_ITEM_NAYRUS_LOVE:
            return PLAYER_IA_COMBO_OOT_NAYRUS_LOVE;
        default:
            return PLAYER_IA_NONE;
    }
}

// OOT's boots on Link in Termina (CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) of OOT's save: 1 Kokiri, 2 Iron, 3 Hover).
extern "C" int Combo_MmOotBootsValue(void) {
    static FnIntInt sEquip = Sym<FnIntInt>("SOH_ComboTermina_EquipValue");
    return sEquip ? sEquip(3) : 0;
}

// OOT's tunic on Link in Termina (CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC): 1 Kokiri, 2 Goron, 3 Zora), 0 without OOT's save.
extern "C" int Combo_MmOotTunicValue(void) {
    static FnIntInt sEquip = Sym<FnIntInt>("SOH_ComboTermina_EquipValue");
    return sEquip ? sEquip(2) : 0;
}

// OOT's underwater-floor rule for Link in Iron Boots: the Hookshot and Longshot only (MM's own Hookshot, or OOT's).
extern "C" int Combo_MmIronFloorItemOk(int item) {
    if (item == ITEM_HOOKSHOT) {
        return 1;
    }
    const int oot = Combo_MmOotItemOf(item);
    return (oot == 0x0A) || (oot == 0x0B); // OOT ITEM_HOOKSHOT / ITEM_LONGSHOT
}

extern "C" int Combo_MmOotLinkAdult(void) {
    return LinkAdult() ? 1 : 0;
}

extern "C" int Combo_MmOotItemAgeOk(int foreignItem) {
    static FnIntIntInt sAgeOk = Sym<FnIntIntInt>("SOH_ComboTermina_ItemAgeOk");
    const int item = Combo_MmOotItemOf(foreignItem);
    if (item < 0 || sAgeOk == nullptr)
        return 1;
    return sAgeOk(item, LinkAdult() ? 1 : 0);
}

// With "Shared health, magic and ammo" on, OOT's consumables are one count with MM's (combo/ComboSharedHealth.h), and
// that count travels with the shared state, not in OOT's dormant save: OOT's save alone goes stale. MM's shared side
// (ComboSharedHealth.cpp) answers for it; with sharing off it hands back OOT's own count.
extern "C" int Combo_SharedOotAmmo(int kind, int ootCount, int ootCap);
extern "C" int Combo_SharedOotAmmoSpend(int kind, int change, int ootCount);

namespace {
struct OotBag {
    int count = -1; // what a use spends (and what OOT will show on arrival), -1 = not an ammo item OOT holds
    int cap = 0;    // OOT's bag
    int kind = -1;  // the shared consumable
    int own = -1;   // OOT's save's own count
};

OotBag BagOf(int ootItem) {
    typedef int (*FnAmmoBag)(int, int*, int*);
    static FnAmmoBag sBag = Sym<FnAmmoBag>("SOH_ComboAmmoBag");
    OotBag b;
    if (sBag != nullptr) {
        b.own = sBag(ootItem, &b.cap, &b.kind);
    }
    if (b.own >= 0) {
        b.count = Combo_SharedOotAmmo(b.kind, b.own, b.cap);
    }
    return b;
}
} // namespace

extern "C" int Combo_MmOotAmmo(int ootItem) {
    return BagOf(ootItem).count;
}

extern "C" void Combo_MmOotChangeAmmo(int ootItem, int delta) {
    static FnVoidIntInt sChange = Sym<FnVoidIntInt>("SOH_ComboTermina_ChangeAmmo");
    const OotBag b = BagOf(ootItem);
    if (b.count < 0) {
        return;
    }
    // OOT's Inventory_ChangeAmmo: never below 0, never above OOT's bag.
    int next = b.count + delta;
    next = (next < 0) ? 0 : ((next > b.cap) ? b.cap : next);
    // Sharing off: OOT's own save, which OOT writes before it loads the save again (SOH_ResumeGame).
    if (!Combo_SharedOotAmmoSpend(b.kind, next - b.count, b.own) && (sChange != nullptr)) {
        sChange(ootItem, delta);
    }
}

extern "C" int Combo_MmForeignAmmo(int foreignItem, int* full) {
    const int oot = Combo_MmOotItemOf(foreignItem);
    const OotBag b = (oot >= 0) ? BagOf(oot) : OotBag{};
    if (full != nullptr) {
        *full = (b.count >= 0) && (b.count >= b.cap);
    }
    return b.count;
}

// OOT's hand models (soh z_player_lib.c sPlayerDListGroups) for the OOT items Link holds in Termina. OOT has each
// model for one age only; the other age (SoH's Timeless Equipment) borrows it, as SoH's "Equipment Always Visible"
// does.
extern "C" const void* Combo_MmOotHeldDL(Player* player, int limbIndex) {
    if (player->stateFlags3 & PLAYER_STATE3_20000000) {
        // The Bremen march keeps the held item but draws MM's ocarina hands (MM func_80839978).
        return nullptr;
    }
    const bool adult = Combo_MM_AdultBodyActive(&player->actor);
    switch (player->heldItemAction) {
        case PLAYER_IA_COMBO_OOT_BOOMERANG:
            // OOT Player_OverrideLimbDrawGameplayDefault: the open hand while the boomerang is out.
            if ((limbIndex == PLAYER_LIMB_LEFT_HAND) && !(player->stateFlags1 & PLAYER_STATE1_ZORA_BOOMERANG_THROWN)) {
                return RoutedChild("gLinkChildLeftFistAndBoomerangNearDL");
            }
            break;
        case PLAYER_IA_COMBO_OOT_HAMMER:
            // OOT PLAYER_MODELTYPE_LH_HAMMER (adult model; a child with Timeless Equipment holds the same one).
            if (limbIndex == PLAYER_LIMB_LEFT_HAND) {
                return RoutedAdult("gLinkAdultLeftHandHoldingHammerNearDL");
            }
            break;
        case PLAYER_IA_COMBO_OOT_SLINGSHOT:
            // OOT PLAYER_MODELTYPE_RH_BOW_SLINGSHOT, child column.
            if (limbIndex == PLAYER_LIMB_RIGHT_HAND) {
                return RoutedChild("gLinkChildRightHandHoldingSlingshotNearDL");
            }
            break;
        case PLAYER_IA_COMBO_OOT_HOOKSHOT:
        case PLAYER_IA_COMBO_OOT_LONGSHOT:
            // OOT PLAYER_MODELTYPE_RH_HOOKSHOT: adult only in OOT (the adult body's remap already shows it; a child
            // with Timeless Equipment holds the same adult model, as SoH's "Equipment Always Visible" does).
            if ((limbIndex == PLAYER_LIMB_RIGHT_HAND) && !adult) {
                return RoutedAdult("gLinkAdultRightHandHoldingHookshotNearDL");
            }
            break;
        default:
            break;
    }
    (void)adult;
    return nullptr;
}

// An OOT resource's raw data from OOT's resident ResourceManager ("overlays/ovl_Magic_Fire/sSphereVtx"), pinned for
// the life of the process. The same object OOT's display lists reference, so writing it (OOT's spells fade their
// vertex alphas) shows in the routed draw, as it does in OOT.
extern "C" void* Combo_MmOotRawResource(const char* path) {
    static auto& sPinned = *new std::unordered_map<std::string, std::shared_ptr<Ship::IResource>>();
    auto it = sPinned.find(path);
    if (it == sPinned.end()) {
        std::shared_ptr<Ship::IResource> res;
        if (auto rm = Ship::CrossRMRegistry::Get("oot")) {
            try {
                Ship::ResourceManagerScope scope(rm);
                res = rm->LoadResource(std::string("__OTR__") + path);
            } catch (...) { res.reset(); }
        }
        if (res == nullptr)
            Combo_MmOotLog("OOT resource '%s' not found", path);
        it = sPinned.emplace(path, res).first;
    }
    return it->second ? it->second->GetRawPointer() : nullptr;
}

// OOT message 0x3B ("You cast Farore's Wind!" + the green three-way choice), in Termina's message box: MM codes for
// OOT's (newline 0x11, three choices 0xC3, green %g).
extern "C" void Combo_MmOotFaroresWindMessage(PlayState* play) {
    CustomMessage::Entry entry;
    entry.autoFormat = false;
    std::string msg = "You cast Farore's Wind!\x11\xC3%gReturn to the Warp Point\x11"
                      "Dispel the Warp Point\x11"
                      "Exit%w";
    CustomMessage::ReplaceColorChars(&msg);
    CustomMessage::StartTextbox(msg + "\xBF", entry);
}

// ---- OOT's own masks with no Termina twin (Skull Mask, Spooky Mask, Gerudo Mask). OOT puts a mask on or takes it off
// the moment its button is pressed (Player_UseItem's mask branch: no animation, NA_SE_PL_CHANGE_ARMS), one mask at a
// time; the model is OOT's (sMaskDlists, object_link_child) on the head limb, drawn as OOT draws it (head limb space).
static int sWornOotMask = -1; // OOT item id, -1 = none

extern "C" int Combo_MmOotWornMask(void) {
    return sWornOotMask;
}

extern "C" void Combo_MmOotClearMask(void) {
    sWornOotMask = -1;
}

// An OOT mask arriving from Hyrule on Link's face (the masks Termina has no copy of: Skull / Spooky / Gerudo).
extern "C" int Combo_MmOotWearMask(int ootItem) {
    if ((ootItem != COMBO_OOT_ITEM_MASK_SKULL) && (ootItem != COMBO_OOT_ITEM_MASK_SPOOKY) &&
        (ootItem != COMBO_OOT_ITEM_MASK_GERUDO)) {
        return 0;
    }
    sWornOotMask = ootItem;
    return 1;
}

extern "C" int Combo_MmOotToggleMask(int foreignItem) {
    const int item = Combo_MmOotItemOf(foreignItem);
    if ((item != COMBO_OOT_ITEM_MASK_SKULL) && (item != COMBO_OOT_ITEM_MASK_SPOOKY) &&
        (item != COMBO_OOT_ITEM_MASK_GERUDO)) {
        return 0;
    }
    sWornOotMask = (sWornOotMask == item) ? -1 : item;
    Combo_MmOotLog("OOT mask 0x%02X %s", item, (sWornOotMask >= 0) ? "worn (OOT model)" : "taken off");
    return 1;
}

extern "C" const void* Combo_MmOotWornMaskDL(void) {
    switch (sWornOotMask) {
        case COMBO_OOT_ITEM_MASK_SKULL:
            return RoutedChild("gLinkChildSkullMaskDL");
        case COMBO_OOT_ITEM_MASK_SPOOKY:
            return RoutedChild("gLinkChildSpookyMaskDL");
        case COMBO_OOT_ITEM_MASK_GERUDO:
            return RoutedChild("gLinkChildGerudoMaskDL");
        default:
            return nullptr;
    }
}

static int sFwArrivalPending = 0;
extern "C" int Combo_MmOotFaroresWindArrivalPending(int set) {
    if (set >= 0)
        sFwArrivalPending = set;
    return sFwArrivalPending;
}

#endif
