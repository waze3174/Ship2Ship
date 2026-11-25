#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Rando/MiscBehavior/ComboOotItems.h"

extern "C" {
#include "variables.h"
#include "src/overlays/actors/ovl_En_Bom_Chu/z_en_bom_chu.h"

void EnBomChu_Move(EnBomChu*, PlayState*);
void EnBomChu_Explode(EnBomChu*, PlayState*);
void EnBomChu_UpdateRotation(EnBomChu*);
}

#define CVAR_NAME "gEnhancements.PlayerActions.RemoteBombchu"
#define CVAR_SOH_NAME COMBO_SOH_CVAR_REMOTE_BOMBCHU
#define CVAR_MM CVarGetInteger(CVAR_NAME, 0)
#define CVAR_SOH CVarGetInteger(CVAR_SOH_NAME, 0)
#define CVAR (CVAR_MM || CVAR_SOH)

static bool focused = false;
static EnBomChu* activeBombchu = nullptr;

// 2S2H's switch steers every bombchu; SoH's steers the ones taken out on OOT's bag.
static bool CanSteer(Actor* bombchu) {
    return CVAR_MM || (CVAR_SOH && ((EnBomChu*)bombchu)->comboIsOotOrigin);
}

bool IsBombchuFocused() {
    return focused;
}

static void ReleaseBombchuFocus() {
    Player* player = GET_PLAYER(gPlayState);
    Camera_SetFocalActor(Play_GetCamera(gPlayState, player->subCamId), &player->actor);
    player->stateFlags1 &= ~PLAYER_STATE1_20;
    focused = false;
}

void RegisterRemoteBombchu() {
    // Link can deploy multiple bombchus at once. Only assume control of the last placed one.
    COND_ID_HOOK(OnActorInit, ACTOR_EN_BOM_CHU, CVAR, [](Actor* actor) {
        if (CanSteer(actor)) {
            activeBombchu = (EnBomChu*)actor;
        }
    });

    COND_ID_HOOK(OnActorDestroy, ACTOR_EN_BOM_CHU, CVAR, [](Actor* actor) {
        Player* player = GET_PLAYER(gPlayState);

        if (actor == (Actor*)activeBombchu) {
            activeBombchu = nullptr;

            if (focused) {
                ReleaseBombchuFocus();
            }
        }
    });

    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_BOM_CHU, CVAR, [](Actor* actor) {
        if (actor != (Actor*)activeBombchu) {
            return;
        }

        Player* player = GET_PLAYER(gPlayState);
        Input* input = &gPlayState->state.input[0];

        if (activeBombchu->actionFunc == EnBomChu_Move) {
            if (!focused) {
                Camera_SetFocalActor(Play_GetCamera(gPlayState, CutsceneManager_GetCurrentSubCamId(actor->csId)),
                                     actor);
                player->stateFlags1 |= PLAYER_STATE1_20;
                focused = true;
            }

            f32 turnRate = 1500.0f;
            f32 stickX = input->cur.stick_x;
            if (fabsf(stickX) > 10.0f) {
                s16 turnAngle = (s16)(turnRate * (stickX / 85.0f));

                Matrix_RotateAxisF(BINANG_TO_RAD(-turnAngle), &activeBombchu->axisUp, MTXMODE_NEW);
                Vec3f newAxisForwards;
                Matrix_MultVec3f(&activeBombchu->axisForwards, &newAxisForwards);
                Math_Vec3f_Copy(&activeBombchu->axisForwards, &newAxisForwards);
                Math3D_Vec3f_Cross(&activeBombchu->axisUp, &activeBombchu->axisForwards, &activeBombchu->axisLeft);

                EnBomChu_UpdateRotation(activeBombchu);
                activeBombchu->actor.shape.rot.x = -activeBombchu->actor.world.rot.x;
                activeBombchu->actor.shape.rot.y = activeBombchu->actor.world.rot.y;
                activeBombchu->actor.shape.rot.z = activeBombchu->actor.world.rot.z;
            }

            if (input->press.button & BTN_B) {
                EnBomChu_Explode(activeBombchu, gPlayState);
            }

            if (input->press.button & BTN_A) {
                activeBombchu = nullptr;
                ReleaseBombchuFocus();
            }
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterRemoteBombchu, { CVAR_NAME, CVAR_SOH_NAME });
