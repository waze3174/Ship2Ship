// mm/2s2h/Rando/MiscBehavior/ComboSharedHealth.cpp — ComboShip: MM's side of the one health bar for both games
// (rationale and balancing: combo/ComboSharedHealth.h).
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <libultraship/libultraship.h>

#include "ComboExport.h"
#include "ComboResolve.h"
#include "ComboSharedHealth.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/Rando/Rando.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern int gMMComboSharedMask; // BenPort.cpp: the loaded slot's Shared Items
}

namespace {
ComboHealth::Runtime sRt;
std::string sOut;

// MM's ammo items: the shared consumable each one is (-1 = not shared), and its bag as MM's own HUD fills it (chus
// share the bomb bag, Item_Give stops beans at 20, the Powder Keg and the picture are one).
struct MmBag {
    u8 item;
    int kind;
};
const MmBag kMmBags[] = {
    { ITEM_BOW, ComboHealth::kAmmoArrows },
    { ITEM_BOMB, ComboHealth::kAmmoBombs },
    { ITEM_BOMBCHU, ComboHealth::kAmmoBombchus },
    { ITEM_DEKU_STICK, ComboHealth::kAmmoSticks },
    { ITEM_DEKU_NUT, ComboHealth::kAmmoNuts },
    { ITEM_MAGIC_BEANS, ComboHealth::kAmmoBeans },
    { ITEM_POWDER_KEG, -1 },
    { ITEM_PICTOGRAPH_BOX, -1 },
};

int MmBagCap(u8 item) {
    switch (item) {
        case ITEM_BOW:
            return CUR_CAPACITY(UPG_QUIVER);
        case ITEM_BOMB:
        case ITEM_BOMBCHU:
            return CUR_CAPACITY(UPG_BOMB_BAG);
        case ITEM_DEKU_STICK:
            return CUR_CAPACITY(UPG_DEKU_STICKS);
        case ITEM_DEKU_NUT:
            return CUR_CAPACITY(UPG_DEKU_NUTS);
        case ITEM_MAGIC_BEANS:
            return 20;
        case ITEM_POWDER_KEG:
        case ITEM_PICTOGRAPH_BOX:
            return 1;
        default:
            return 0;
    }
}

// Ship of Harkinian's Infinite Upgrades as OOT's save holds them (SOH_ComboInfiniteMask), 0 without an OOT rando save.
uint32_t OotInfinite() {
    typedef uint32_t (*FnMask)(void);
    static const FnMask sMask = (FnMask)Combo_ResolveSym("soh", "SOH_ComboInfiniteMask");
    return (IS_RANDO && (sMask != nullptr)) ? sMask() : 0;
}

bool MmInfinite(uint32_t ootInfinite, uint32_t bit) {
    return ComboHealth::MmShareInfinite(ootInfinite, bit, (uint32_t)gMMComboSharedMask);
}

// MM's own ammo item whose bag is infinite through OOT's (its magic arrows draw on the quiver).
bool MmItemInfinite(s16 item) {
    if ((item >= ITEM_BOW_FIRE) && (item <= ITEM_BOW_LIGHT)) {
        item = ITEM_BOW;
    }
    for (const MmBag& b : kMmBags) {
        if ((b.item == item) && (b.kind >= 0)) {
            return MmInfinite(OotInfinite(), ComboHealth::InfiniteAmmoBit(b.kind));
        }
    }
    return false;
}

// SoH's RandomizerOnGameFrameUpdateHandler for MM's side of what OOT holds infinite: the bag full (never lowered), the
// meter full (as 2S2H's Infinite Magic cheat fills it, once it has grown), the wallet full. A use in Termina then
// spends nothing, as in Hyrule, and with the count shared the shared count does not drop either.
void FillInfinite() {
    const uint32_t oot = OotInfinite();
    if (oot == 0) {
        return;
    }
    for (const MmBag& b : kMmBags) {
        if ((b.kind < 0) || (INV_CONTENT(b.item) != b.item) || !MmInfinite(oot, ComboHealth::InfiniteAmmoBit(b.kind))) {
            continue;
        }
        const int cap = MmBagCap(b.item);
        if (AMMO(b.item) < cap) {
            AMMO(b.item) = (s8)cap;
        }
    }
    if (MmInfinite(oot, ComboHealth::kInfiniteMagic)) {
        const u8 level = gSaveContext.save.saveInfo.playerData.magicLevel;
        if ((level == 1) || (level == 2)) {
            gSaveContext.save.saveInfo.playerData.magic = (level == 1) ? MAGIC_NORMAL_METER : MAGIC_DOUBLE_METER;
        }
    }
    if (MmInfinite(oot, ComboHealth::kInfiniteMoney) &&
        (gSaveContext.save.saveInfo.playerData.rupees < CUR_CAPACITY(UPG_WALLET))) {
        gSaveContext.save.saveInfo.playerData.rupees = CUR_CAPACITY(UPG_WALLET);
    }
}

// SoH's VB_DRAW_AMMO_COUNT: an infinite bag shows no count, on the C buttons and the D-pad. The B button keeps it
// (minigame arrows count there), as in Varuuna/ComboShip PR #246 f749c3cac (MiscBehavior/InfiniteUpgrades.cpp).
void HideInfiniteHudCount(bool* should, va_list args) {
    const s16 button = (s16)va_arg(args, int);
    (void)va_arg(args, int); // alpha
    const bool isDpad = va_arg(args, int) != 0;
    if (!isDpad && (button == EQUIP_SLOT_B)) {
        return;
    }
    if (MmItemInfinite(isDpad ? DPAD_GET_CUR_FORM_BTN_ITEM(button) : GET_CUR_FORM_BTN_ITEM(button))) {
        *should = false;
    }
}

void HideInfinitePauseCount(bool* should, va_list args) {
    (void)va_arg(args, void*); // PauseContext*
    (void)va_arg(args, void*); // GraphicsContext*
    if (MmItemInfinite((s16)va_arg(args, int))) {
        *should = false;
    }
}
} // namespace

// OOT's own bag of a shared consumable is infinite (OOT's items used in Termina spend nothing from it).
extern "C" int Combo_OotAmmoInfinite(int kind) {
    return (kind >= 0) && (kind < ComboHealth::kAmmoKinds) &&
           ((OotInfinite() & ComboHealth::InfiniteAmmoBit(kind)) != 0);
}

// The shared heart-piece meter in eighths (0-7) for this game's pause screen; -1 before the first tick of a save.
extern "C" int Combo_HeartPieceEighths(void) {
    return (sRt.lastCapacity < 0) ? -1 : ComboHealth::LeftoverEighths(sRt.s);
}

extern "C" COMBO_EXPORT const char* MM_ComboHealthGetState(void) {
    sOut = ComboHealth::ToJson(sRt);
    return sOut.c_str();
}

extern "C" COMBO_EXPORT void MM_ComboHealthSetState(const char* json) {
    ComboHealth::FromJson(sRt, json);
}

static void ComboSharedHealthTick() {
    if ((gPlayState == nullptr) || (gSaveContext.gameMode != GAMEMODE_NORMAL)) {
        return;
    }
    ComboHealth::Native n{ &gSaveContext.save.saveInfo.playerData.healthCapacity,
                           &gSaveContext.save.saveInfo.playerData.health,
                           &gSaveContext.save.saveInfo.inventory.questItems,
                           &gSaveContext.save.saveInfo.playerData.doubleDefense,
                           &gSaveContext.save.saveInfo.inventory.defenseHearts };
    n.magic = &gSaveContext.save.saveInfo.playerData.magic;
    n.magicLevel = &gSaveContext.save.saveInfo.playerData.magicLevel;
    n.isMagicAcquired = &gSaveContext.save.saveInfo.playerData.isMagicAcquired;
    n.magicState = &gSaveContext.magicState;
    n.magicFillTarget = &gSaveContext.magicFillTarget;
    n.magicToAdd = &gSaveContext.magicToAdd;
    n.magicToConsume = &gSaveContext.magicToConsume;
    for (const MmBag& b : kMmBags) {
        if (b.kind >= 0) { // shared consumables: MM's own bags (MM has no bullet bag)
            n.ammo[b.kind].count = &AMMO(b.item);
            n.ammo[b.kind].owned = (INV_CONTENT(b.item) == b.item);
            n.ammo[b.kind].cap = MmBagCap(b.item);
        }
    }
    {
        // Test hook (launch env): COMBO_DEBUG_AMMO=<kind>,<n> sets that MM count to n at frame 150, as a shot or a
        // pickup would, so a crossing shows the change arriving in Hyrule.
        static int sFrames = 0;
        static const char* sAmmo = std::getenv("COMBO_DEBUG_AMMO");
        int kind = -1, v = 0;
        if ((sAmmo != nullptr) && (++sFrames == 150) && (std::sscanf(sAmmo, "%d,%d", &kind, &v) == 2) && (kind >= 0) &&
            (kind < ComboHealth::kAmmoKinds) && (n.ammo[kind].count != nullptr)) {
            *n.ammo[kind].count = (int8_t)v;
            SPDLOG_INFO("[ComboShip] COMBO_DEBUG_AMMO: MM ammo kind {} set to {}", kind, v);
        }
    }
    const ComboHealth::HeartsBefore heartsBefore = ComboHealth::HeartsBeforeTick(sRt, n);
    ComboHealth::TickIfEnabled(sRt, ComboHealth::kGameMm, n);
    if (const std::string line = ComboHealth::HeartsLogLine(sRt, heartsBefore, "MM"); !line.empty()) {
        SPDLOG_INFO("{}", line);
    }
    FillInfinite();
    if (!Combo_GamesSolo()) {
        // The watched-transformation record is one for both games: MM's weekEventReg flags (z_player.c D_8085D908)
        // and the shared bits, merged both ways, so a transformation watched in Hyrule is skippable in Termina. A
        // one-game slot has no Hyrule to share with, and the record may still hold another slot's.
        static const u16 sFlags[] = { WEEKEVENTREG_30_80, WEEKEVENTREG_30_20, WEEKEVENTREG_30_40, WEEKEVENTREG_30_10 };
        for (int f = 0; f < 4; f++) {
            if (CHECK_WEEKEVENTREG(sFlags[f])) {
                sRt.s.seenTransform |= (1 << f);
            } else if (sRt.s.seenTransform & (1 << f)) {
                SET_WEEKEVENTREG(sFlags[f]);
            }
        }
    }
    static const bool sDbg = std::getenv("COMBO_DEBUG_HEALTH") != nullptr;
    static int sCount = 0;
    if (sDbg && ((++sCount % 60) == 0)) {
        SPDLOG_INFO("[ComboShip] health MM: {}/{} (q oot {} mm {}, dd {}) magic {} shared {} state {}",
                    gSaveContext.save.saveInfo.playerData.health, gSaveContext.save.saveInfo.playerData.healthCapacity,
                    sRt.s.quarters[0], sRt.s.quarters[1], sRt.s.doubleDefense,
                    gSaveContext.save.saveInfo.playerData.magic, sRt.s.magic, gSaveContext.magicState);
        SPDLOG_INFO("[ComboShip] ammo MM: arrows {}/{} bombs {}/{} chus {}/{} sticks {}/{} nuts {}/{} beans {}/{}",
                    AMMO(ITEM_BOW), sRt.s.ammo[0], AMMO(ITEM_BOMB), sRt.s.ammo[1], AMMO(ITEM_BOMBCHU), sRt.s.ammo[2],
                    AMMO(ITEM_DEKU_STICK), sRt.s.ammo[3], AMMO(ITEM_DEKU_NUT), sRt.s.ammo[4], AMMO(ITEM_MAGIC_BEANS),
                    sRt.s.ammo[5]);
        SPDLOG_INFO("[ComboShip] ammo MM: seeds shared {}, OOT infinite 0x{:x}", sRt.s.ammo[ComboHealth::kAmmoSeeds],
                    OotInfinite());
    }
}

extern "C" void (*gMMComboSharedChanged)(int game, int fileNum); // BenPort.cpp

static void ComboSharedOnLoad(s16 fileNum) {
    // Shared Items: reconcile on every MM load too (a raise owed from Hyrule is made on arrival, not at the next
    // pickup), as OOT does on OnLoadGame.
    if ((gMMComboSharedChanged != nullptr) && (fileNum >= 0) && (fileNum != 0xFF)) {
        gMMComboSharedChanged(1, fileNum);
    }
}

static void RegisterComboSharedHealth() {
    COND_HOOK(OnSaveLoad, true, ComboSharedOnLoad);
    COND_HOOK(OnGameStateUpdate, true, []() { ComboSharedHealthTick(); });
    COND_VB_SHOULD(VB_DRAW_HUD_AMMO_COUNT, true, HideInfiniteHudCount(should, args));
    COND_VB_SHOULD(VB_KALEIDO_DRAW_AMMO_COUNT, true, HideInfinitePauseCount(should, args));
}
static RegisterShipInitFunc sComboSharedHealthInit(RegisterComboSharedHealth, {});

// Shared consumables: a refill the launcher found for OOT while Termina is being played goes into MM's bag now.
// 1 = taken, 0 = MM doesn't carry that consumable (deliver it to OOT).
extern "C" COMBO_EXPORT int MM_ComboAmmoAdd(int kind, int count) {
    static const u8 kItem[ComboHealth::kAmmoKinds - 1] = { ITEM_BOW,        ITEM_BOMB,     ITEM_BOMBCHU,
                                                           ITEM_DEKU_STICK, ITEM_DEKU_NUT, ITEM_MAGIC_BEANS };
    if ((gPlayState == nullptr) || (kind < 0) || (kind >= ComboHealth::kAmmoSeeds) ||
        (INV_CONTENT(kItem[kind]) != kItem[kind])) {
        return 0;
    }
    const int caps[ComboHealth::kAmmoKinds - 1] = { CUR_CAPACITY(UPG_QUIVER),    CUR_CAPACITY(UPG_BOMB_BAG),
                                                    CUR_CAPACITY(UPG_BOMB_BAG),  CUR_CAPACITY(UPG_DEKU_STICKS),
                                                    CUR_CAPACITY(UPG_DEKU_NUTS), 20 };
    int v = AMMO(kItem[kind]) + count;
    AMMO(kItem[kind]) = (s8)((v > caps[kind]) ? caps[kind] : v);
    return 1;
}

// OOT's bags seen from Termina (OOT's items on MM's buttons and MM's Hyrule face, ComboOotItems.cpp): with sharing on,
// the count OOT will show on arrival, which is the count a use spends (combo/ComboSharedHealth.h DormantAmmo).
extern "C" int Combo_SharedOotAmmo(int kind, int ootCount, int ootCap) {
    return ComboHealth::DormantAmmo(sRt, ComboHealth::kGameOot, kind, ootCount, ootCap);
}

// OOT's Slingshot fired in Termina spends from the shared count when sharing is on (MM has no bullet bag, so no native
// cell: the count comes in from Hyrule with the shared state and goes back with it). 0 = sharing is off.
extern "C" int Combo_SharedOotAmmoSpend(int kind, int change, int ootCount) {
    return ComboHealth::SpendDormantAmmo(sRt, ComboHealth::kGameOot, kind, change, ootCount) ? 1 : 0;
}

// MM's bag for an MM ammo item, for Hyrule's HUD and pause menu (MM items on OOT's buttons, OOT's Termina face): its
// count in MM's save, -1 = not an ammo item MM holds. `cap`: the bag (MM's HUD turns the count green there); `kind`:
// its shared consumable, -1 = not shared.
extern "C" COMBO_EXPORT int MM_ComboAmmoBag(int mmItem, int* cap, int* kind) {
    for (const MmBag& b : kMmBags) {
        if ((b.item != mmItem) || (INV_CONTENT(b.item) != b.item)) {
            continue;
        }
        *cap = MmBagCap(b.item);
        *kind = b.kind;
        if (b.item == ITEM_PICTOGRAPH_BOX) {
            return CHECK_QUEST_ITEM(QUEST_PICTOGRAPH) ? 1 : 0; // MM's HUD: a picture taken or not
        }
        return AMMO(b.item);
    }
    return -1;
}
