// mm/2s2h/Rando/MiscBehavior/ComboDebugGive.cpp — ComboShip debug (test harness only): COMBO_DEBUG_MM_GIVE=<hex
// item>[,...] puts MM items into MM's inventory at their home slots whenever MM loads a save (a give from OOT's side
// only reaches MM's dormant memory, which a switch into MM reloads from disk). Env var at launch only.
#include <cstdlib>
#include <string>

#include <libultraship/libultraship.h>

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "variables.h"
#include "functions.h"
}

static void RegisterComboDebugGive() {
    const char* env = std::getenv("COMBO_DEBUG_MM_GIVE");
    if (env == nullptr) {
        return;
    }
    static std::string sList = env;
    COND_HOOK(OnSaveLoad, true, [](s16 fileNum) {
        size_t pos = 0;
        while (pos < sList.size()) {
            size_t end = sList.find(',', pos);
            const std::string tok = sList.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos);
            pos = (end == std::string::npos) ? sList.size() : end + 1;
            const int item = (int)std::strtol(tok.c_str(), nullptr, 16);
            if ((item >= 0) && (item < ITEM_NONE)) {
                gSaveContext.save.saveInfo.inventory.items[SLOT(item)] = (u8)item;
                SPDLOG_INFO("[ComboShip] COMBO_DEBUG_MM_GIVE: MM item 0x{:X} -> slot {}", item, SLOT(item));
            }
        }
    });
}
static RegisterShipInitFunc sComboDebugGiveInit(RegisterComboDebugGive, {});

// ComboShip debug (test harness only): COMBO_TEST_MM_ACTOR="<hex actor id>,<hex params>[,<hex C-left item>]" spawns
// that actor 70 units in front of Link 60 frames into the first MM scene (and puts the item -- e.g. a foreign OOT id
// -- on C-left), then logs every 20 frames whether it is still alive (objects vs form moves / OOT items in Termina).
static s16 sComboTestId = 0;
static s16 sComboTestParams = 0;
static s32 sComboTestCLeft = -1;

static void ComboTestMmActorTick() {
    static s32 sFrames = 0;
    static Actor* sActor = nullptr;
    if ((gPlayState == nullptr) || (GET_PLAYER(gPlayState) == nullptr)) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    sFrames++;
    if (sFrames == 60) {
        const f32 s = Math_SinS(player->actor.shape.rot.y), c = Math_CosS(player->actor.shape.rot.y);
        sActor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, sComboTestId, player->actor.world.pos.x + s * 70.0f,
                             player->actor.world.pos.y, player->actor.world.pos.z + c * 70.0f, 0, 0, 0,
                             sComboTestParams); // rot.y 0: some objects index a type table with it
        if (sComboTestCLeft >= 0) {
            gSaveContext.save.saveInfo.equips.buttonItems[0][EQUIP_SLOT_C_LEFT] = (u8)sComboTestCLeft;
        }
        SPDLOG_INFO("[ComboShip] COMBO_TEST_MM_ACTOR: actor 0x{:X} params 0x{:X} -> {}", sComboTestId, sComboTestParams,
                    (void*)sActor);
    } else if ((sFrames > 60) && (((sFrames % 20) == 0) || (player->meleeWeaponState != 0)) && (sActor != nullptr)) {
        bool alive = false;
        for (s32 cat = 0; cat < ACTORCAT_MAX && !alive; cat++) {
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].first; a != nullptr; a = a->next) {
                if (a == sActor) {
                    alive = true;
                    break;
                }
            }
        }
        SPDLOG_INFO("[ComboShip] COMBO_TEST_MM_ACTOR: frame {} alive {} held {} mwa {} state {}", sFrames, alive,
                    player->heldItemAction, player->meleeWeaponAnimation, player->meleeWeaponState);
        if (!alive) {
            sActor = nullptr;
        }
    }
}

static void RegisterComboTestMmActor() {
    const char* env = std::getenv("COMBO_TEST_MM_ACTOR");
    if (env == nullptr) {
        return;
    }
    sComboTestId = (s16)std::strtol(env, nullptr, 16);
    if (const char* c = std::strchr(env, ',')) {
        sComboTestParams = (s16)std::strtol(c + 1, nullptr, 16);
        if (const char* c2 = std::strchr(c + 1, ',')) {
            sComboTestCLeft = (s32)std::strtol(c2 + 1, nullptr, 16);
        }
    }
    COND_HOOK(OnGameStateUpdate, true, ComboTestMmActorTick);
}
static RegisterShipInitFunc sComboTestMmActorInit(RegisterComboTestMmActor, {});
