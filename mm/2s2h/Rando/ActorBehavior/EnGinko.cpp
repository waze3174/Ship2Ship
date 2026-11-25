#include "ActorBehavior.h"
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "variables.h"
#include "overlays/actors/ovl_En_Ginko_Man/z_en_ginko_man.h"

void EnGinkoMan_SetupIdle(EnGinkoMan* enGinkoMan);
#ifdef COMBO_BUILD
void EnGinkoMan_Idle(EnGinkoMan* enGinkoMan, PlayState* play);
#endif
}

#ifdef COMBO_BUILD
namespace {

struct BankRewardThresholds {
    u32 wallet;
    u32 interest;
    u32 heartPiece;
};

constexpr BankRewardThresholds kVanillaBankThresholds = { 200, 1000, 5000 };
constexpr BankRewardThresholds kLowerBankThresholds = { 100, 500, 1000 };

// Sets the same flag Auto Bank Deposit sets for this reward, so neither path grants it again.
void OfferBankRewardIfReached(RandoCheckId checkId, u32 threshold, s32 weekEventRegFlag) {
    auto& saveCheck = RANDO_SAVE_CHECKS[checkId];
    if (HS_GET_BANK_RUPEES() < threshold || !saveCheck.shuffled || saveCheck.obtained || saveCheck.eligible) {
        return;
    }
    SET_WEEKEVENTREG(weekEventRegFlag);
    saveCheck.eligible = true;
}

void OfferReachedBankRewards() {
    const BankRewardThresholds& thresholds =
        CVarGetInteger("gEnhancements.DifficultyOptions.LowerBankRewardThresholds", 0) ? kLowerBankThresholds
                                                                                       : kVanillaBankThresholds;
    OfferBankRewardIfReached(RC_CLOCK_TOWN_WEST_BANK_ADULTS_WALLET, thresholds.wallet,
                             WEEKEVENTREG_RECEIVED_BANK_WALLET_UPGRADE);
    OfferBankRewardIfReached(RC_CLOCK_TOWN_WEST_BANK_INTEREST, thresholds.interest, WEEKEVENTREG_59_80);
    OfferBankRewardIfReached(RC_CLOCK_TOWN_WEST_BANK_PIECE_OF_HEART, thresholds.heartPiece,
                             WEEKEVENTREG_RECEIVED_BANK_HEART_PIECE);
}

} // namespace
#endif

void Rando::ActorBehavior::InitEnGinkoBehavior() {
#ifdef COMBO_BUILD
    // ComboShip: the banker and Auto Bank Deposit reward only the deposit that crosses a threshold, so a
    // reward missed then never comes back. Talking to him offers every threshold the balance has reached.
    COND_VB_SHOULD(VB_CONTINUE_BANKER_DIALOGUE, IS_RANDO, {
        EnGinkoMan* enGinkoMan = va_arg(args, EnGinkoMan*);
        // The hook also runs on every later line of the conversation; act only on the talk start.
        if (enGinkoMan->actionFunc == EnGinkoMan_Idle) {
            OfferReachedBankRewards();
        }
    });
#endif

    COND_VB_SHOULD(VB_BANKER_GIVE_REWARD, IS_RANDO, {
        EnGinkoMan* enGinkoMan = va_arg(args, EnGinkoMan*);

        EnGinkoMan_SetupIdle(enGinkoMan);

        if (GameInteractor_Should(VB_PASS_FIRST_BANK_THRESHOLD,
                                  (HS_GET_BANK_RUPEES() >= 200) && (enGinkoMan->previousBankValue < 200), enGinkoMan)) {
            SET_WEEKEVENTREG(WEEKEVENTREG_RECEIVED_BANK_WALLET_UPGRADE);
        }

        if (GameInteractor_Should(VB_PASS_INTEREST_BANK_THRESHOLD,
                                  (HS_GET_BANK_RUPEES() >= 1000) && (enGinkoMan->previousBankValue < 1000),
                                  enGinkoMan) &&
            !RANDO_SAVE_CHECKS[RC_CLOCK_TOWN_WEST_BANK_INTEREST].cycleObtained) {
            RANDO_SAVE_CHECKS[RC_CLOCK_TOWN_WEST_BANK_INTEREST].eligible = true;
        }

        if (GameInteractor_Should(VB_PASS_SECOND_BANK_THRESHOLD, HS_GET_BANK_RUPEES() >= 5000, enGinkoMan)) {
            SET_WEEKEVENTREG(WEEKEVENTREG_RECEIVED_BANK_HEART_PIECE);
        }

        *should = false;
    });
}
