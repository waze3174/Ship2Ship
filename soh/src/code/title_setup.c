#include "global.h"
#include <stdlib.h>
#include <string.h>

#ifdef COMBO_BUILD
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/randomizer_entrance.h" // Entrance_OverrideNextIndex
#include "soh/Enhancements/randomizer/randomizer_grotto.h"   // Grotto_OverrideSpecialEntranceForCross
// Set by SOH_ResumeGame (OTRGlobals.cpp) on a combo MM->OOT return. >= 0 means: skip the title /
// file-select screens, load this save slot, and spawn straight into Play in the Market outside the
// Happy Mask Shop (the fixed arrival point for the cross-game portal return).
extern s32 gComboReturnFileNum;
// ComboShip (teleport songs): arrival entrance pushed by the launcher (>= 0), consumed once.
extern s32 gComboTargetEntrance;
extern s32 gComboCrossArrival;
int Combo_CancelPendingCrossSwitch(void); // OTRGlobals.cpp
void Combo_DebugLoadMmSave(int fileNum);  // OTRGlobals.cpp (test harness)
void Sram_InitDebugSave(void);            // z_sram.c (no header declares it; newer GCC rejects the implicit call)
#endif

void TitleSetup_InitImpl(GameState* gameState) {
    osSyncPrintf("ゼルダ共通データ初期化\n"); // "Zelda common data initalization"
    SaveContext_Init();
#ifdef COMBO_BUILD
    {
        // Debug (test harness only): COMBO_DEBUG_BOOT_PLAY=<entrance> skips title and file select: OOT's debug save
        // (slot 1, never written unless the game saves), full magic, straight into Play at that entrance. Once.
        static s32 sDbgBooted = false;
        const char* dbg = getenv("COMBO_DEBUG_BOOT_PLAY");
        if ((dbg != NULL) && !sDbgBooted) {
            sDbgBooted = true;
            Sram_InitDebugSave();
            gSaveContext.fileNum = 0;
            gSaveContext.gameMode = GAMEMODE_NORMAL;
            gSaveContext.isMagicAcquired = true;
            gSaveContext.magicLevel = 0;
            gSaveContext.magicFillTarget = MAGIC_NORMAL_METER;
            gSaveContext.magic = 0;
            gSaveContext.entranceIndex = (s16)strtol(dbg, NULL, 0);
            if (strstr(dbg, "mmsave") != NULL) {
                Combo_DebugLoadMmSave(0); // "<entrance>,mmsave": MM's save of slot 1 behind the debug save
            }
            if (strstr(dbg, "child") != NULL) {
                gSaveContext.linkAge = LINK_AGE_CHILD; // "<entrance>,child": the debug save as child Link
            }
            {
                // "<entrance>,hp=<n>": start with n health (16 = one heart), e.g. hp=40 = two and a half hearts --
                // half and empty hearts for HUD checks.
                if (strstr(dbg, "hover") != NULL) { // "<entrance>,hover": hover boots equipped (form boots checks)
                    gSaveContext.inventory.equipment |= (1 << (EQUIP_INV_BOOTS_HOVER + 12));
                    Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_HOVER);
                }
                if (strstr(dbg, "mask=") != NULL) { // "<entrance>,mask=<OOT item hex>": worn, on C-right
                    extern void SOH_SetComboMask(int token);
                    const s32 maskItem = (s32)strtol(strstr(dbg, "mask=") + 5, NULL, 16);
                    gSaveContext.inventory.items[SLOT_TRADE_CHILD] = maskItem;
                    gSaveContext.equips.buttonItems[3] = maskItem;
                    gSaveContext.equips.cButtonSlots[2] = SLOT_TRADE_CHILD;
                    SOH_SetComboMask(maskItem);
                }
                if (strstr(dbg, "iron") != NULL) { // "<entrance>,iron": Iron Boots owned and equipped
                    gSaveContext.inventory.equipment |= (1 << (EQUIP_INV_BOOTS_IRON + 12));
                    Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_IRON);
                }
                if (strstr(dbg, "hookshot") != NULL) { // "<entrance>,hookshot": the Hookshot owned and on C-left
                    gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_HOOKSHOT;
                    gSaveContext.equips.buttonItems[1] = ITEM_HOOKSHOT;
                    gSaveContext.equips.cButtonSlots[0] = SLOT_HOOKSHOT;
                }
                if (strstr(dbg, "cbtn=") != NULL) { // "<entrance>,cbtn=<hex id>": that id on C-right (foreign ids too)
                    gSaveContext.equips.buttonItems[3] = (u8)strtol(strstr(dbg, "cbtn=") + 5, NULL, 16);
                    gSaveContext.equips.cButtonSlots[2] = SLOT_NONE;
                }
                if (strstr(dbg, "hammer") != NULL) { // "<entrance>,hammer": the Megaton Hammer owned and on C-down
                    gSaveContext.inventory.items[SLOT_HAMMER] = ITEM_HAMMER;
                    gSaveContext.equips.buttonItems[2] = ITEM_HAMMER;
                    gSaveContext.equips.cButtonSlots[1] = SLOT_HAMMER;
                }
                if (strstr(dbg, "nomagic") != NULL) { // "<entrance>,nomagic": no magic meter in OOT's save
                    gSaveContext.isMagicAcquired = false;
                    gSaveContext.isDoubleMagicAcquired = false;
                    gSaveContext.magicFillTarget = 0;
                }
                if (strstr(dbg, "unarmed") != NULL) { // "<entrance>,unarmed": no sword or shield equipped
                    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
                    Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
                    gSaveContext.equips.buttonItems[0] = ITEM_NONE;
                }
                const char* hp = strstr(dbg, "hp=");
                if (hp != NULL) {
                    gSaveContext.health = (s16)strtol(hp + 3, NULL, 0);
                }
            }
            gSaveContext.cutsceneIndex = (strstr(dbg, ",cs") != NULL) ? 0xFFF0 : 0; // ",cs": the entrance's cutscene 0
            LUSLOG_INFO("[ComboShip] COMBO_DEBUG_BOOT_PLAY -> entrance 0x%X", gSaveContext.entranceIndex);
            gameState->running = false;
            SET_NEXT_GAMESTATE(gameState, Play_Init, PlayState);
            return;
        }
    }
    {
        // Debug (test harness only): COMBO_DEBUG_RETURN="<slot>,<oot entrance>" runs the real MM->OOT return path
        // (load that slot, arrive at that entrance as a crossed MM door would) once, from the title.
        static s32 sDbgReturned = false;
        const char* dbgRet = getenv("COMBO_DEBUG_RETURN");
        if ((dbgRet != NULL) && !sDbgReturned) {
            char* comma = NULL;
            sDbgReturned = true;
            gComboReturnFileNum = (s32)strtol(dbgRet, &comma, 0);
            gComboTargetEntrance =
                ((comma != NULL) && (*comma == ',')) ? ((s32)strtol(comma + 1, NULL, 0) | 0x10000) : -1;
            LUSLOG_INFO("[ComboShip] COMBO_DEBUG_RETURN slot %d entrance 0x%X", gComboReturnFileNum,
                        gComboTargetEntrance);
        }
    }
    if (gComboReturnFileNum >= 0) {
        // Combo MM->OOT return: mirror FileChoose_LoadGame (z_file_choose.c) to load the OOT save and
        // jump directly to Play, then override the entrance so Link spawns in the Market outside the
        // Happy Mask Shop (fixed arrival for the cross-game portal return — symmetric with the OOT->MM
        // trigger of entering the shop).
        LUSLOG_INFO("[ComboShip] TitleSetup combo jump -> Play (fileNum=%d)", gComboReturnFileNum);
        gSaveContext.fileNum = gComboReturnFileNum;
        gSaveContext.gameMode = GAMEMODE_NORMAL;
        Sram_OpenSave();
        // Mirror FileChoose_LoadGame's magic staging so Play_Init refills the meter normally.
        gSaveContext.magicFillTarget = gSaveContext.magic;
        gSaveContext.magic = 0;
        gSaveContext.magicLevel = 0;
        // Fire the exit/load hook pair a real quit+load runs: Save_LoadFile (inside Sram_OpenSave)
        // recreated gRandoContext, and consumers like the check tracker must tear down + re-init.
        GameInteractor_ExecuteOnExitGame(gSaveContext.fileNum);
        GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
        Combo_CancelPendingCrossSwitch(); // a return from MM stays in OOT (no crossing from the load itself)
        // Must follow OnLoadGame: the rando handler's Entrance_SetSavewarpEntrance() recomputes from
        // savedSceneNum (never set by the portal handoff) and would clobber this with Link's House.
        if (gComboTargetEntrance >= 0) {
            // ComboShip (teleport songs): cross-game arrival, routed through entrance rando.
            LUSLOG_INFO("[ComboShip] TitleSetup combo arrival at entrance 0x%X", gComboTargetEntrance);
            if (gComboTargetEntrance & 0x10000) {
                // ComboShip (cross-game entrances): a crossed MM door names its FINAL OOT destination —
                // re-overriding it would land elsewhere or bounce straight back into MM. Age is kept.
                // Grotto ids (rando 0x700 loads / 0x800 returns) still become the real grotto entrance.
                gSaveContext.entranceIndex =
                    Grotto_OverrideSpecialEntranceForCross((s16)(gComboTargetEntrance & 0xFFFF));
            } else {
                gSaveContext.entranceIndex = Entrance_OverrideNextIndex((s16)gComboTargetEntrance);
            }
            gComboTargetEntrance = -1;
            gComboCrossArrival = 1;
        } else {
            gSaveContext.entranceIndex = Entrance_OverrideNextIndex(ENTR_MARKET_DAY_OUTSIDE_HAPPY_MASK_SHOP);
        }
        // A portal return is never a cutscene arrival: a never-played MM-start save still holds the
        // pending intro (0xFFF1), which would shift Play_Init's entrance lookup onto a garbage layer.
        gSaveContext.cutsceneIndex = 0;
        gComboReturnFileNum = -1;
        gameState->running = false;
        SET_NEXT_GAMESTATE(gameState, Play_Init, PlayState);
        return;
    }
#endif
    gameState->running = false;
    SET_NEXT_GAMESTATE(gameState, Title_Init, TitleContext);
}

void TitleSetup_Destroy(GameState* gameState) {
}

void TitleSetup_Init(GameState* gameState) {
    gameState->destroy = TitleSetup_Destroy;
    TitleSetup_InitImpl(gameState);
}
