// ComboShip: the end of OoT's half of a back-to-back slot (combo/rando/ComboCrossGate.h). Combo-owned file (soh
// GLOB_RECURSE). See docs/deviations/back2back.md.
//
// Ganon's defeat plays OoT's full ending (z_boss_ganon2.c: the Chamber of the Sages, Zelda's farewell, the credits), as
// the launcher answers "play the full ending" for a back-to-back slot (ComboShip.cpp Combo_OnFinalBossDefeated), and it
// records Ganon as beaten, which opens the Lost Woods path into Termina for child Link.
//
// The credits end the way the game ends: Play_Init is handed the invalid entrance that exits to the title
// (ENTR_LOAD_OPENING). Play_Init asks Combo_BackToBack_ResumeAfterCredits first. Where the slot has beaten Ganon and
// not Majora, that call turns the exit into an arrival at the Temple of Time's pedestal, as child Link, and the slot is
// saved there once the scene has settled: the player walks back to the Lost Woods and takes the path to MM.
#ifdef COMBO_BUILD

#include <spdlog/spdlog.h>

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
int Combo_OotCreditsContinueInTemple(void); // OTRGlobals.cpp
}

namespace {

// The pedestal arrival a child Link gets when he returns through the Master Sword (z_demo.c, terminator 8).
constexpr s16 kChildArrivalEntrance = ENTR_TEMPLE_OF_TIME_2;

// Every credits scene mutes these sound banks (z_demo.c); 0 unmutes them all.
constexpr u16 kNoMutedSfxBanks = 0;

bool sSavePending = false;

// The slot is saved once, with child Link standing in the Temple of Time and nothing in the way: no transition or
// cutscene running.
void SaveOnceSettledInTemple() {
    PlayState* play = gPlayState;

    if (!sSavePending || play == nullptr || play->sceneNum != SCENE_TEMPLE_OF_TIME ||
        gSaveContext.gameMode != GAMEMODE_NORMAL || play->transitionMode != TRANS_MODE_OFF ||
        play->csCtx.state != CS_STATE_IDLE) {
        return;
    }
    sSavePending = false;
    Play_PerformSave(play);
    SPDLOG_INFO("[ComboBackToBack] OoT's half is saved: child Link at the Temple of Time");
}

void RegisterBackToBackEnding() {
    COND_HOOK(OnGameFrameUpdate, true, SaveOnceSettledInTemple);
    COND_HOOK(OnLoadGame, true, [](int32_t fileNum) { sSavePending = false; });
}

RegisterShipInitFunc sBackToBackEndingInit(RegisterBackToBackEnding, {});

} // namespace

extern "C" int Combo_BackToBack_ResumeAfterCredits(void) {
    if (!Combo_OotCreditsContinueInTemple()) {
        return 0;
    }
    SPDLOG_INFO("[ComboBackToBack] credits ended: child Link to the Temple of Time instead of the title");
    Audio_SetSfxBanksMute(kNoMutedSfxBanks);
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.nextCutsceneIndex = 0;
    gSaveContext.respawnFlag = 0;
    if (LINK_IS_ADULT) {
        Inventory_SwapAgeEquipment(); // trades the worn adult gear for the child's, as the pedestal does
    }
    gSaveContext.linkAge = LINK_AGE_CHILD;
    gSaveContext.entranceIndex = kChildArrivalEntrance;
    sSavePending = true;
    return 1;
}

#endif // COMBO_BUILD
