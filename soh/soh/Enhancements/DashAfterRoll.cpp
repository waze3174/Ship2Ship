// Port of 2S2H's mm/2s2h/Enhancements/Player/DashAfterRoll.cpp: keep holding A after a roll to keep dashing.

#ifdef COMBO_BUILD
#include "soh/Enhancements/randomizer/ComboPause.h"
#endif

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/BunnyHood.h"
#include "soh/Enhancements/DashAfterRoll.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
void Player_Action_Idle(Player* player, PlayState* play);
void Player_Action_Roll(Player* player, PlayState* play);
void Player_Action_80842180(Player* player, PlayState* play); // running, not Z-targeting
}

static constexpr float DASH_SPEED_FACTOR = 1.5f;

static bool sIsDashing = false;
static PlayerActionFunc sPrevActionFunc = nullptr;

static DashAfterRollMode GetMode() {
    return (DashAfterRollMode)CVarGetInteger(CVAR_DASH_AFTER_ROLL_NAME, DASH_AFTER_ROLL_OFF);
}

// A function, because MSVC rejects preprocessor directives inside the COND_VB_SHOULD macro's arguments.
static bool IsMmBunnyHoodWorn() {
#ifdef COMBO_BUILD
    return Combo_MmMaskIs(COMBO_MM_ITEM_MASK_BUNNY) != 0;
#else
    return false;
#endif
}

float Ship_GetDashAfterRollRunFactor(Player* player) {
    if (!sIsDashing) {
        return 1.0f;
    }

    switch (GetMode()) {
        case DASH_AFTER_ROLL_ON:
            // Dashing is Bunny Hood speed; wearing the hood already gives that
            return (Ship_GetBunnyHoodRunFactor(player) != 1.0f || IsMmBunnyHoodWorn()) ? 1.0f : DASH_SPEED_FACTOR;
        case DASH_AFTER_ROLL_STACK:
            return DASH_SPEED_FACTOR;
        default:
            return 1.0f;
    }
}

// Runs at the end of the player's update, so it sees the action the roll handed over to.
static void UpdateDashLatch() {
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    // After a roll the player is in the idle action for one update even if the stick stays held. That update must not
    // end the dash.
    bool rollJustEnded = sPrevActionFunc == Player_Action_Roll && player->actionFunc == Player_Action_Idle;

    if (!CHECK_BTN_ALL(gPlayState->state.input[0].cur.button, BTN_A)) {
        sIsDashing = false;
    } else if (player->actionFunc == Player_Action_Roll) {
        // a bonk keeps the roll action, but marks it
        sIsDashing = !player->av2.bonked;
    } else if (player->actionFunc != Player_Action_80842180 && !rollJustEnded) {
        sIsDashing = false;
    }
    sPrevActionFunc = player->actionFunc;
}

static void RegisterDashAfterRoll() {
    sIsDashing = false;
    sPrevActionFunc = nullptr;

    COND_HOOK(OnPlayerUpdate, GetMode() != DASH_AFTER_ROLL_OFF, UpdateDashLatch);
}

static RegisterShipInitFunc initFunc(RegisterDashAfterRoll, { CVAR_DASH_AFTER_ROLL_NAME });
