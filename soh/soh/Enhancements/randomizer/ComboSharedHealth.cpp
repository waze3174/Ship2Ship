// soh/soh/Enhancements/randomizer/ComboSharedHealth.cpp — ComboShip: OOT's side of the one health bar for both games
// (rationale and balancing: combo/ComboSharedHealth.h). The launcher hands the shared state in when a save is read
// and takes it back when a save is written (combo.health in the container).
#include <cstdlib>
#include <string>

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

#include "ComboExport.h"
#include "ComboSharedHealth.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/ComboForms.h" // Combo_DebugEnv / Combo_DebugLog (log-only trace)

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace {
ComboHealth::Runtime sRt;
std::string sOut;

// OOT's ammo items: the shared consumable each one is, and its bag (Item_Give caps chus at 50, the bean seller at 10;
// MM holds 20 beans).
struct OotBag {
    u8 item;
    int kind;
};
const OotBag kOotBags[] = {
    { ITEM_BOW, ComboHealth::kAmmoArrows },       { ITEM_BOMB, ComboHealth::kAmmoBombs },
    { ITEM_BOMBCHU, ComboHealth::kAmmoBombchus }, { ITEM_STICK, ComboHealth::kAmmoSticks },
    { ITEM_NUT, ComboHealth::kAmmoNuts },         { ITEM_BEAN, ComboHealth::kAmmoBeans },
    { ITEM_SLINGSHOT, ComboHealth::kAmmoSeeds },
};

int OotBagCap(u8 item) {
    switch (item) {
        case ITEM_BOW:
            return CUR_CAPACITY(UPG_QUIVER);
        case ITEM_BOMB:
            return CUR_CAPACITY(UPG_BOMB_BAG);
        case ITEM_BOMBCHU:
            return 50;
        case ITEM_STICK:
            return CUR_CAPACITY(UPG_STICKS);
        case ITEM_NUT:
            return CUR_CAPACITY(UPG_NUTS);
        case ITEM_BEAN:
            return 20;
        case ITEM_SLINGSHOT:
            return CUR_CAPACITY(UPG_BULLET_BAG);
        default:
            return 0;
    }
}
} // namespace

// The shared heart-piece meter in eighths (0-7) for this game's pause screen; -1 before the first tick of a save.
extern "C" int Combo_HeartPieceEighths(void) {
    return (sRt.lastCapacity < 0) ? -1 : ComboHealth::LeftoverEighths(sRt.s);
}

extern "C" COMBO_EXPORT int SOH_ComboSharedHealthEnabled(void) {
    return ComboHealth::Enabled() ? 1 : 0;
}

extern "C" COMBO_EXPORT const char* SOH_ComboHealthGetState(void) {
    sOut = ComboHealth::ToJson(sRt);
    return sOut.c_str();
}

extern "C" COMBO_EXPORT void SOH_ComboHealthSetState(const char* json) {
    ComboHealth::FromJson(sRt, json);
}

// MM's watched-transformation record (ComboSharedHealth.h seenTransform), MM form numbering.
extern "C" int Combo_TransformSeen(int mmForm) {
    return (sRt.s.seenTransform >> mmForm) & 1;
}
extern "C" void Combo_TransformSetSeen(int mmForm) {
    sRt.s.seenTransform |= (1 << mmForm);
}

// OTRGlobals.cpp registers this once (with the other combo frame hooks).
void Combo_SharedHealthRegister() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if ((gPlayState == nullptr) || (gSaveContext.fileNum > 2) || (gSaveContext.gameMode != GAMEMODE_NORMAL)) {
            return;
        }
        ComboHealth::Native n{ &gSaveContext.healthCapacity, &gSaveContext.health, &gSaveContext.inventory.questItems,
                               &gSaveContext.isDoubleDefenseAcquired, &gSaveContext.inventory.defenseHearts };
        n.growthWeight = ComboHealth::NativeGrowthWeight();
        n.magic = &gSaveContext.magic;
        n.magicLevel = &gSaveContext.magicLevel;
        n.isMagicAcquired = &gSaveContext.isMagicAcquired;
        n.magicState = &gSaveContext.magicState;
        n.magicFillTarget = &gSaveContext.magicFillTarget;
        n.magicTarget = &gSaveContext.magicTarget;
        for (const OotBag& b : kOotBags) { // shared consumables: OOT's own bags
            n.ammo[b.kind].count = &AMMO(b.item);
            n.ammo[b.kind].owned = (INV_CONTENT(b.item) == b.item);
            n.ammo[b.kind].cap = OotBagCap(b.item);
        }
        {
            // Test hook (launch env only, Combo_HarnessEnv): COMBO_DEBUG_MAGIC=<n> sets OOT's meter to n once it is
            // idle, so a crossing can show the amount arriving in Termina.
            static bool sMagicSet = false;
            const char* mp = Combo_HarnessEnv("COMBO_DEBUG_MAGIC") ? std::getenv("COMBO_DEBUG_MAGIC") : nullptr;
            if ((mp != nullptr) && !sMagicSet && gSaveContext.isMagicAcquired && (gSaveContext.magicLevel > 0) &&
                (gSaveContext.magicState == 0)) {
                sMagicSet = true;
                gSaveContext.magic = (s8)std::strtol(mp, nullptr, 0);
            }
        }
        const ComboHealth::HeartsBefore heartsBefore = ComboHealth::HeartsBeforeTick(sRt, n);
        ComboHealth::TickIfEnabled(sRt, ComboHealth::kGameOot, n);
        if (const std::string line = ComboHealth::HeartsLogLine(sRt, heartsBefore, "OOT"); !line.empty()) {
            SPDLOG_INFO("{}", line);
        }
        static int sDbg = 0;
        if (Combo_DebugEnv("COMBO_DEBUG_HEALTH") && ((++sDbg % 60) == 0)) {
            Combo_DebugLog("[ComboShip] health OOT: %d/%d (q oot %d mm %d, dd %d) magic %d shared %d state %d\n",
                           gSaveContext.health, gSaveContext.healthCapacity, sRt.s.quarters[0], sRt.s.quarters[1],
                           sRt.s.doubleDefense, gSaveContext.magic, sRt.s.magic, gSaveContext.magicState);
            Combo_DebugLog(
                "[ComboShip] ammo OOT: arrows %d/%d bombs %d/%d chus %d/%d sticks %d/%d nuts %d/%d beans %d/%d\n",
                AMMO(ITEM_BOW), sRt.s.ammo[0], AMMO(ITEM_BOMB), sRt.s.ammo[1], AMMO(ITEM_BOMBCHU), sRt.s.ammo[2],
                AMMO(ITEM_STICK), sRt.s.ammo[3], AMMO(ITEM_NUT), sRt.s.ammo[4], AMMO(ITEM_BEAN), sRt.s.ammo[5]);
            Combo_DebugLog("[ComboShip] ammo OOT: seeds %d/%d\n", AMMO(ITEM_SLINGSHOT), sRt.s.ammo[6]);
        }
    });
}

// Shared consumables: a refill the launcher found for MM while Hyrule is being played goes into OOT's bag now (the
// count is shared; the tick carries the gain). 1 = taken, 0 = OOT doesn't carry that consumable (deliver it to MM).
extern "C" COMBO_EXPORT int SOH_ComboAmmoAdd(int kind, int count) {
    static const u8 kItem[ComboHealth::kAmmoKinds] = { ITEM_BOW, ITEM_BOMB, ITEM_BOMBCHU,  ITEM_STICK,
                                                       ITEM_NUT, ITEM_BEAN, ITEM_SLINGSHOT };
    if ((gPlayState == nullptr) || (gSaveContext.fileNum > 2) || (kind < 0) || (kind >= ComboHealth::kAmmoKinds) ||
        (INV_CONTENT(kItem[kind]) != kItem[kind])) {
        return 0;
    }
    const int caps[ComboHealth::kAmmoKinds] = { CUR_CAPACITY(UPG_QUIVER),    CUR_CAPACITY(UPG_BOMB_BAG), 50,
                                                CUR_CAPACITY(UPG_STICKS),    CUR_CAPACITY(UPG_NUTS),     20,
                                                CUR_CAPACITY(UPG_BULLET_BAG) };
    int v = AMMO(kItem[kind]) + count;
    AMMO(kItem[kind]) = (s8)((v > caps[kind]) ? caps[kind] : v);
    return 1;
}

// OOT's bag for an OOT ammo item, for Termina's HUD and pause menu (OOT items on MM's buttons, MM's Hyrule face): its
// count in OOT's save, -1 = not an ammo item OOT holds or no OOT save loaded. `cap`: the bag (OOT's HUD turns the count
// green there); `kind`: its shared consumable.
extern "C" COMBO_EXPORT int SOH_ComboAmmoBag(int ootItem, int* cap, int* kind) {
    if (gSaveContext.fileNum > 2) {
        return -1;
    }
    for (const OotBag& b : kOotBags) {
        if ((b.item == ootItem) && (INV_CONTENT(b.item) == b.item)) {
            *cap = OotBagCap(b.item);
            *kind = b.kind;
            return AMMO(b.item);
        }
    }
    return -1;
}

// The infinite upgrades OOT's save holds (RandomizerOnGameFrameUpdateHandler keeps those bags, the meter and the wallet
// full in Hyrule), as ComboHealth's bits, for Termina: OOT's own items used there and MM's side of what both games hold
// one of (ComboHealth::MmShareInfinite). 0 = no OOT rando save loaded.
extern "C" COMBO_EXPORT uint32_t SOH_ComboInfiniteMask(void) {
    struct Infinite {
        RandomizerInf flag;
        uint32_t bit;
    };
    static const Infinite kInfinite[] = {
        { RAND_INF_HAS_INFINITE_QUIVER, ComboHealth::InfiniteAmmoBit(ComboHealth::kAmmoArrows) },
        { RAND_INF_HAS_INFINITE_BOMB_BAG, ComboHealth::InfiniteAmmoBit(ComboHealth::kAmmoBombs) },
        { RAND_INF_HAS_INFINITE_BOMBCHUS, ComboHealth::InfiniteAmmoBit(ComboHealth::kAmmoBombchus) },
        { RAND_INF_HAS_INFINITE_STICK_UPGRADE, ComboHealth::InfiniteAmmoBit(ComboHealth::kAmmoSticks) },
        { RAND_INF_HAS_INFINITE_NUT_UPGRADE, ComboHealth::InfiniteAmmoBit(ComboHealth::kAmmoNuts) },
        { RAND_INF_HAS_INFINITE_BULLET_BAG, ComboHealth::InfiniteAmmoBit(ComboHealth::kAmmoSeeds) },
        { RAND_INF_HAS_INFINITE_MAGIC_METER, ComboHealth::kInfiniteMagic },
        { RAND_INF_HAS_INFINITE_MONEY, ComboHealth::kInfiniteMoney },
    };
    if ((gSaveContext.fileNum > 2) || !IS_RANDO) {
        return 0;
    }
    uint32_t mask = 0;
    for (const Infinite& i : kInfinite) {
        if (Flags_GetRandomizerInf(i.flag)) {
            mask |= i.bit;
        }
    }
    return mask;
}

// MM's bags seen from Hyrule (MM items on OOT's buttons and OOT's Termina face, ComboPause.cpp): with sharing on, the
// count MM will show on arrival (combo/ComboSharedHealth.h DormantAmmo).
extern "C" int Combo_SharedMmAmmo(int kind, int mmCount, int mmCap) {
    return ComboHealth::DormantAmmo(sRt, ComboHealth::kGameMm, kind, mmCount, mmCap);
}

// An MM ammo item used in Hyrule (MM's Deku Stick or Deku Nut) spends from the shared count when sharing is on, the
// mirror of Termina's Combo_SharedOotAmmoSpend. 0 = sharing is off (spend from MM's save).
extern "C" int Combo_SharedMmAmmoSpend(int kind, int change, int mmCount) {
    return ComboHealth::SpendDormantAmmo(sRt, ComboHealth::kGameMm, kind, change, mmCount) ? 1 : 0;
}
