// mm/2s2h/Rando/MiscBehavior/ComboDebugDrownLoop.cpp — ComboShip debug (launch env only):
// COMBO_DEBUG_MM_DROWNLOOP=1 rebuilds the owner's Woodfall softlock in whatever MM scene the run is in. Once play has
// run 60 frames, Link is moved into the middle of the scene's largest water box, that spot becomes his respawn point
// (what a shuffled exit over deep water gives), he is made a Deku, and he voids out, so he respawns there as a Deku.
// The softlock checker (z_player.c Combo_SoftlockCheckDrown) must turn him human on that first drown.
#include <cstdlib>

#include <libultraship/libultraship.h>

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "variables.h"
#include "functions.h"
}

extern "C" s32 sComboHoverBootsTimer; // z_player.c
static void RegisterComboDebugDrownLoop() {
    // COMBO_DEBUG_MM_WATER=1: just drop Link into the largest water box (no respawn point, no form change).
    static const bool sWaterOnly = std::getenv("COMBO_DEBUG_MM_WATER") != nullptr;
    if ((std::getenv("COMBO_DEBUG_MM_DROWNLOOP") == nullptr) && !sWaterOnly &&
        !std::getenv("COMBO_DEBUG_MM_TRACE_ONLY")) {
        return;
    }
    COND_HOOK(OnGameStateUpdate, true, []() {
        static int sFrames = 0;
        static bool sDone = false;
        static u32 sLastScene = 0xFFFF;
        if (gPlayState == nullptr || gGameState != &gPlayState->state || GET_PLAYER(gPlayState) == nullptr) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        if ((gPlayState->sceneId != sLastScene) ||
            ((sFrames % (std::getenv("COMBO_DEBUG_MM_TRACE_EVERY") ? 1 : 20)) == 0)) {
            sLastScene = gPlayState->sceneId;
            SPDLOG_INFO(
                "[ComboShip] drownloop: scene {} form {} pos {:.0f} {:.0f} {:.0f} depth {:.0f} boots {} hover {} "
                "hazard {} breath {} ground {} health {} regs27 {} hoverT {} vy {:.1f}",
                (int)gPlayState->sceneId, (int)player->transformation, player->actor.world.pos.x,
                player->actor.world.pos.y, player->actor.world.pos.z, player->actor.depthInWater,
                (int)player->currentBoots, (int)gComboHoverBoots, Player_GetEnvironmentalHazard(gPlayState),
                (int)player->underwaterTimer, (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0,
                (int)gSaveContext.save.saveInfo.playerData.health, (int)REG(27), (int)sComboHoverBootsTimer,
                player->actor.velocity.y);
        }
        if (sDone || (++sFrames < 60) || std::getenv("COMBO_DEBUG_MM_TRACE_ONLY")) {
            return;
        }
        sDone = true;
        CollisionHeader* col = gPlayState->colCtx.colHeader;
        WaterBox* best = nullptr;
        for (int i = 0; (col != nullptr) && (i < col->numWaterBoxes); i++) {
            WaterBox* w = &col->waterBoxes[i];
            if ((best == nullptr) || ((s32)w->xLength * w->zLength > (s32)best->xLength * best->zLength)) {
                best = w;
            }
        }
        if (best == nullptr) {
            SPDLOG_INFO("[ComboShip] drownloop: no water in this scene");
            return;
        }
        player->actor.world.pos.x = best->minPos.x + best->xLength * 0.5f;
        player->actor.world.pos.z = best->minPos.z + best->zLength * 0.5f;
        // COMBO_DEBUG_MM_WATER=2: 20 above the surface instead (Hover Boots over water)
        player->actor.world.pos.y =
            best->minPos.y +
            ((std::atoi(std::getenv("COMBO_DEBUG_MM_WATER") ? std::getenv("COMBO_DEBUG_MM_WATER") : "1") == 2) ? 20.0f
                                                                                                               : -5.0f);
        player->actor.prevPos = player->actor.world.pos;
        if (sWaterOnly) {
            SPDLOG_INFO("[ComboShip] drownloop: dropped into the water at {:.0f} {:.0f} {:.0f}",
                        player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z);
            return;
        }
        Play_SetupRespawnPoint(gPlayState, RESPAWN_MODE_DOWN, PLAYER_PARAMS(0xFF, PLAYER_START_MODE_B));
        func_80169EFC(gPlayState); // queued first: this set-up void-out is not the loop the checker must catch
        gSaveContext.save.playerForm = PLAYER_FORM_DEKU;
        gSaveContext.save.equippedMask = PLAYER_MASK_DEKU;
        SPDLOG_INFO("[ComboShip] drownloop: respawn point set in the water at {:.0f} {:.0f} {:.0f}, Deku, voiding out",
                    player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z);
    });
}
// COMBO_DEBUG_MM_MAGICFILL=1: once MM play has run 40 frames, drain the meter to 4, then at 80 refill it the way MM's
// Great Fairy fountains do (Magic_Add(MAGIC_FILL_TO_CAPACITY), BgDyYoseizo). Logs the meter every 10 frames.
static void RegisterComboDebugMagicFill() {
    if (std::getenv("COMBO_DEBUG_MM_MAGICFILL") == nullptr) {
        return;
    }
    COND_HOOK(OnGameStateUpdate, true, []() {
        static int sFrames = 0;
        if (gPlayState == nullptr || gGameState != &gPlayState->state || GET_PLAYER(gPlayState) == nullptr) {
            return;
        }
        sFrames++;
        if (sFrames == 20) {
            gSaveContext.save.saveInfo.playerData.isMagicAcquired = true; // the debug save has no meter
            gSaveContext.save.saveInfo.playerData.magicLevel = 1;
            gSaveContext.magicCapacity = 0x30;
        } else if ((sFrames == 40) && (atoi(std::getenv("COMBO_DEBUG_MM_MAGICFILL")) != 2)) {
            gSaveContext.save.saveInfo.playerData.magic = 4;
        } else if ((sFrames == 150) && (atoi(std::getenv("COMBO_DEBUG_MM_MAGICFILL")) == 2)) {
            // =2: half a meter once the arrival fill is done, and leave it (a crossing with an idle, part-full meter)
            gSaveContext.save.saveInfo.playerData.magic = 24;
        } else if ((sFrames == 80) && (atoi(std::getenv("COMBO_DEBUG_MM_MAGICFILL")) != 2)) {
            Magic_Add(gPlayState, MAGIC_FILL_TO_CAPACITY);
        }
        if ((sFrames % 10) == 0) {
            SPDLOG_INFO("[ComboShip] magicfill: f{} magic {} state {} toAdd {} requested {}", sFrames,
                        (int)gSaveContext.save.saveInfo.playerData.magic, (int)gSaveContext.magicState,
                        (int)gSaveContext.magicToAdd, (int)gSaveContext.isMagicRequested);
        }
    });
}
static RegisterShipInitFunc sComboDebugMagicFillInit(RegisterComboDebugMagicFill, {});

static RegisterShipInitFunc sComboDebugDrownLoopInit(RegisterComboDebugDrownLoop, {});
