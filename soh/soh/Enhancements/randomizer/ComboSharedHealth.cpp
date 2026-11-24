// soh/soh/Enhancements/randomizer/ComboSharedHealth.cpp — ComboShip: OOT's side of the one health bar for both games
// (rationale and balancing: combo/ComboSharedHealth.h). The launcher hands the shared state in when a save is read
// and takes it back when a save is written (combo.health in the container).
#include <cstdlib>
#include <string>

#include <libultraship/libultraship.h>

#include "ComboExport.h"
#include "ComboSharedHealth.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/ComboForms.h" // Combo_DebugEnv / Combo_DebugLog (log-only trace)

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {
ComboHealth::Runtime sRt;
std::string sOut;
} // namespace

// The shared heart-piece meter in eighths (0-7) for this game's pause screen; -1 before the first tick of a save.
extern "C" int Combo_HeartPieceEighths(void) {
    return (sRt.lastCapacity < 0) ? -1 : ComboHealth::LeftoverEighths(sRt.s);
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
        ComboHealth::Native n{ &gSaveContext.healthCapacity, &gSaveContext.health,
                               &gSaveContext.inventory.questItems, &gSaveContext.isDoubleDefenseAcquired,
                               &gSaveContext.inventory.defenseHearts };
        n.magic = &gSaveContext.magic;
        n.magicLevel = &gSaveContext.magicLevel;
        n.isMagicAcquired = &gSaveContext.isMagicAcquired;
        n.magicState = &gSaveContext.magicState;
        n.magicFillTarget = &gSaveContext.magicFillTarget;
        n.magicTarget = &gSaveContext.magicTarget;
        {
            // Shared consumables: OOT's own bags (Item_Give caps chus at 50, the bean seller at 10; MM holds 20 beans).
            auto slot = [](u8 item, int cap) {
                ComboHealth::AmmoSlot a;
                a.count = &AMMO(item);
                a.owned = (INV_CONTENT(item) == item);
                a.cap = cap;
                return a;
            };
            n.ammo[ComboHealth::kAmmoArrows] = slot(ITEM_BOW, CUR_CAPACITY(UPG_QUIVER));
            n.ammo[ComboHealth::kAmmoBombs] = slot(ITEM_BOMB, CUR_CAPACITY(UPG_BOMB_BAG));
            n.ammo[ComboHealth::kAmmoBombchus] = slot(ITEM_BOMBCHU, 50);
            n.ammo[ComboHealth::kAmmoSticks] = slot(ITEM_STICK, CUR_CAPACITY(UPG_STICKS));
            n.ammo[ComboHealth::kAmmoNuts] = slot(ITEM_NUT, CUR_CAPACITY(UPG_NUTS));
            n.ammo[ComboHealth::kAmmoBeans] = slot(ITEM_BEAN, 20);
            n.ammo[ComboHealth::kAmmoSeeds] = slot(ITEM_SLINGSHOT, CUR_CAPACITY(UPG_BULLET_BAG));
        }
        {
            // Test hook (launch env only, Combo_HarnessEnv): COMBO_DEBUG_MAGIC=<n> sets OOT's meter to n once it is idle,
            // so a crossing can show the amount arriving in Termina.
            static bool sMagicSet = false;
            const char* mp = Combo_HarnessEnv("COMBO_DEBUG_MAGIC") ? std::getenv("COMBO_DEBUG_MAGIC") : nullptr;
            if ((mp != nullptr) && !sMagicSet && gSaveContext.isMagicAcquired && (gSaveContext.magicLevel > 0) &&
                (gSaveContext.magicState == 0)) {
                sMagicSet = true;
                gSaveContext.magic = (s8)std::strtol(mp, nullptr, 0);
            }
        }
        ComboHealth::Tick(sRt, ComboHealth::kGameOot, n);
        static int sDbg = 0;
        if (Combo_DebugEnv("COMBO_DEBUG_HEALTH") && ((++sDbg % 60) == 0)) {
            Combo_DebugLog("[ComboShip] health OOT: %d/%d (q oot %d mm %d, dd %d) magic %d shared %d state %d\n",
                           gSaveContext.health, gSaveContext.healthCapacity, sRt.s.quarters[0], sRt.s.quarters[1],
                           sRt.s.doubleDefense, gSaveContext.magic, sRt.s.magic, gSaveContext.magicState);
            Combo_DebugLog("[ComboShip] ammo OOT: arrows %d/%d bombs %d/%d chus %d/%d sticks %d/%d nuts %d/%d beans %d/%d\n",
                           AMMO(ITEM_BOW), sRt.s.ammo[0], AMMO(ITEM_BOMB), sRt.s.ammo[1], AMMO(ITEM_BOMBCHU),
                           sRt.s.ammo[2], AMMO(ITEM_STICK), sRt.s.ammo[3], AMMO(ITEM_NUT), sRt.s.ammo[4],
                           AMMO(ITEM_BEAN), sRt.s.ammo[5]);
            Combo_DebugLog("[ComboShip] ammo OOT: seeds %d/%d\n", AMMO(ITEM_SLINGSHOT), sRt.s.ammo[6]);
        }
    });
}

// Shared consumables: a refill the launcher found for MM while Hyrule is being played goes into OOT's bag now (the
// count is shared; the tick carries the gain). 1 = taken, 0 = OOT doesn't carry that consumable (deliver it to MM).
extern "C" COMBO_EXPORT int SOH_ComboAmmoAdd(int kind, int count) {
    static const u8 kItem[ComboHealth::kAmmoKinds] = { ITEM_BOW, ITEM_BOMB, ITEM_BOMBCHU, ITEM_STICK,
                                                       ITEM_NUT, ITEM_BEAN, ITEM_SLINGSHOT };
    if ((gPlayState == nullptr) || (gSaveContext.fileNum > 2) || (kind < 0) || (kind >= ComboHealth::kAmmoKinds) ||
        (INV_CONTENT(kItem[kind]) != kItem[kind])) {
        return 0;
    }
    const int caps[ComboHealth::kAmmoKinds] = { CUR_CAPACITY(UPG_QUIVER), CUR_CAPACITY(UPG_BOMB_BAG), 50,
                                                CUR_CAPACITY(UPG_STICKS), CUR_CAPACITY(UPG_NUTS), 20,
                                                CUR_CAPACITY(UPG_BULLET_BAG) };
    int v = AMMO(kItem[kind]) + count;
    AMMO(kItem[kind]) = (s8)((v > caps[kind]) ? caps[kind] : v);
    return 1;
}
