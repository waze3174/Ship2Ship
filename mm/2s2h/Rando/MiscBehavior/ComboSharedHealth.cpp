// mm/2s2h/Rando/MiscBehavior/ComboSharedHealth.cpp — ComboShip: MM's side of the one health bar for both games
// (rationale and balancing: combo/ComboSharedHealth.h).
#include <cstdio>
#include <cstdlib>
#include <string>

#include <libultraship/libultraship.h>

#include "ComboExport.h"
#include "ComboSharedHealth.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}

namespace {
ComboHealth::Runtime sRt;
std::string sOut;
} // namespace

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
    {
        // Shared consumables: MM's own bags (chus share the bomb bag; Item_Give stops beans at 20).
        auto slot = [](u8 item, int cap) {
            ComboHealth::AmmoSlot a;
            a.count = &AMMO(item);
            a.owned = (INV_CONTENT(item) == item);
            a.cap = cap;
            return a;
        };
        n.ammo[ComboHealth::kAmmoArrows] = slot(ITEM_BOW, CUR_CAPACITY(UPG_QUIVER));
        n.ammo[ComboHealth::kAmmoBombs] = slot(ITEM_BOMB, CUR_CAPACITY(UPG_BOMB_BAG));
        n.ammo[ComboHealth::kAmmoBombchus] = slot(ITEM_BOMBCHU, CUR_CAPACITY(UPG_BOMB_BAG));
        n.ammo[ComboHealth::kAmmoSticks] = slot(ITEM_DEKU_STICK, CUR_CAPACITY(UPG_DEKU_STICKS));
        n.ammo[ComboHealth::kAmmoNuts] = slot(ITEM_DEKU_NUT, CUR_CAPACITY(UPG_DEKU_NUTS));
        n.ammo[ComboHealth::kAmmoBeans] = slot(ITEM_MAGIC_BEANS, 20);
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
    ComboHealth::TickIfEnabled(sRt, ComboHealth::kGameMm, n);
    {
        // The watched-transformation record is one for both games: MM's weekEventReg flags (z_player.c D_8085D908)
        // and the shared bits, merged both ways, so a transformation watched in Hyrule is skippable in Termina.
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
                    gSaveContext.save.saveInfo.playerData.health,
                    gSaveContext.save.saveInfo.playerData.healthCapacity, sRt.s.quarters[0], sRt.s.quarters[1],
                    sRt.s.doubleDefense, gSaveContext.save.saveInfo.playerData.magic, sRt.s.magic,
                    gSaveContext.magicState);
        SPDLOG_INFO("[ComboShip] ammo MM: arrows {}/{} bombs {}/{} chus {}/{} sticks {}/{} nuts {}/{} beans {}/{}",
                    AMMO(ITEM_BOW), sRt.s.ammo[0], AMMO(ITEM_BOMB), sRt.s.ammo[1], AMMO(ITEM_BOMBCHU), sRt.s.ammo[2],
                    AMMO(ITEM_DEKU_STICK), sRt.s.ammo[3], AMMO(ITEM_DEKU_NUT), sRt.s.ammo[4],
                    AMMO(ITEM_MAGIC_BEANS), sRt.s.ammo[5]);
        SPDLOG_INFO("[ComboShip] ammo MM: seeds shared {}", sRt.s.ammo[ComboHealth::kAmmoSeeds]);
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
    const int caps[ComboHealth::kAmmoKinds - 1] = { CUR_CAPACITY(UPG_QUIVER),      CUR_CAPACITY(UPG_BOMB_BAG),
                                                CUR_CAPACITY(UPG_BOMB_BAG),    CUR_CAPACITY(UPG_DEKU_STICKS),
                                                CUR_CAPACITY(UPG_DEKU_NUTS),   20 };
    int v = AMMO(kItem[kind]) + count;
    AMMO(kItem[kind]) = (s8)((v > caps[kind]) ? caps[kind] : v);
    return 1;
}

// Shared seeds for OOT's Slingshot in Termina (MM has no bullet bag, so no native cell): the count comes in from
// Hyrule with the shared state and goes back with it. -1 = not known (no seed count shared yet).
extern "C" int MM_ComboSharedSeeds(void) {
    return sRt.s.ammo[ComboHealth::kAmmoSeeds];
}

extern "C" void MM_ComboSharedSeedsSet(int count) {
    sRt.s.ammo[ComboHealth::kAmmoSeeds] = (count < 0) ? 0 : count;
}
