// ComboShip: the end of OoT's half of a back-to-back slot (combo/rando/ComboCrossGate.h). Combo-owned file (soh
// GLOB_RECURSE). See docs/deviations/back2back.md.
//
// The final blow to Ganon records him as beaten, which opens the Lost Woods path into Termina for child Link, and saves
// the slot as it will be once the ending is over: child Link arriving at the Temple of Time's pedestal. Only the saved
// copy is changed, so OoT's full ending plays on as before (z_boss_ganon2.c: the Chamber of the Sages, Zelda's
// farewell, the credits). Until the ending is over every other save of the slot is held (SaveManager.cpp), so a reset
// or quit at any point of it loads that arrival (z_sram.c).
//
// OoT's ending stops on "The End" in the castle courtyard and stays there until the console is reset. Once "The End"
// has been up for a few seconds, or a button is pressed, the slot fades to that same arrival and is saved there when
// the scene has settled: the player walks to the Lost Woods and takes the path to MM.
#ifdef COMBO_BUILD

#include <atomic>
#include <memory>

#include <spdlog/spdlog.h>

#include "rando/ComboCrossGate.h"
#include "rando/CrossForeign.h" // GameId
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "z64.h" // declares its own extern "C", and pulls in <memory> under C++

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "src/overlays/actors/ovl_End_Title/z_end_title.h"
extern PlayState* gPlayState;
extern int gComboCrossGate;                                   // OTRGlobals.cpp
extern int (*gComboFinalBossDefeated)(int game, int fileNum); // OTRGlobals.cpp
}

namespace {

namespace CrossGate = ComboRando::CrossGate;

// The pedestal arrival a child Link gets when he returns through the Master Sword (z_demo.c, terminator 8).
constexpr s16 kChildArrivalEntrance = ENTR_TEMPLE_OF_TIME_2;

// Every credits scene mutes these sound banks (z_demo.c); 0 unmutes them all.
constexpr u16 kNoMutedSfxBanks = 0;

// gSaveContext.cutsceneIndex from here on plays one of the scene's own cutscenes (Play_Init).
constexpr u16 kFirstSceneCutscene = 0xFFF0;

// gSaveContext.nextCutsceneIndex when no cutscene is queued for the next scene (Play_Init).
constexpr u16 kNoNextCutscene = 0xFFEF;

// "The End" fades in by 7 a frame and stops once it has reached 200 (EndTitle_DrawFull).
constexpr u8 kTheEndShownAlpha = 200;

// How long "The End" stays up before the slot moves on by itself, at OoT's 20 game frames a second.
constexpr int kTheEndHoldFrames = 5 * 20;

// Any of these moves on from "The End" at once.
constexpr u16 kTheEndContinueButtons = BTN_A | BTN_B | BTN_START;

// OoT's ending has been reached in this slot, so a save of child Link arriving at the Temple of Time's pedestal loads
// there. Its own save section: a save without it (or an older build, which skips it) loads as before.
constexpr const char* kSaveSection = "comboBackToBack";
constexpr int kSaveSectionVersion = 1;
constexpr const char* kEndingReachedKey = "endingReached";

// Read by the save thread. Set once per slot, so no save can race it.
std::atomic<bool> sEndingReached{ false };
// From the save made at the final blow until the ending is over.
std::atomic<bool> sEndingPlays{ false };
bool sFinalBlowDealt = false;
bool sLeavingTheEnd = false;
int sTheEndShownFrames = 0;
bool sSavePending = false;

bool InBackToBackSlot() {
    return gSaveContext.fileNum <= 2 && CrossGate::IsBackToBack(gComboCrossGate);
}

// What the slot is once the ending is over. Applied to gSaveContext only for as long as the save takes to copy it.
void BecomeChildArrivingInTemple() {
    if (LINK_IS_ADULT) {
        Inventory_SwapAgeEquipment(); // trades the worn adult gear for the child's, as the pedestal does
    }
    gSaveContext.linkAge = LINK_AGE_CHILD;
    gSaveContext.entranceIndex = kChildArrivalEntrance;
    gSaveContext.savedSceneNum = SCENE_TEMPLE_OF_TIME;
    gSaveContext.cutsceneIndex = 0;
}

// The end of the frame of the final blow, once every hook on it has run (the save's Ganon timestamp is one).
void SaveArrivalAtFinalBlow() {
    PlayState* play = gPlayState;

    if (!sFinalBlowDealt) {
        return;
    }
    sFinalBlowDealt = false;
    if (play == nullptr || gComboFinalBossDefeated == nullptr) {
        return;
    }
    gComboFinalBossDefeated(ComboRando::GAME_OOT, gSaveContext.fileNum); // records Ganon as beaten (ComboShip.cpp)
    if (!CrossGate::OotCreditsContinueInTemple(gComboCrossGate)) {
        return;
    }
    Play_SaveSceneFlags(play);
    // SaveManager::SaveSection copies gSaveContext before it returns, so the live game gets its own state back at once.
    const auto live = std::make_unique<SaveContext>(gSaveContext);
    BecomeChildArrivingInTemple();
    sEndingReached = true;
    SaveManager::Instance->SaveFile(gSaveContext.fileNum);
    gSaveContext = *live;
    sEndingPlays = true;
    sTheEndShownFrames = 0;
    SPDLOG_INFO("[ComboBackToBack] Ganon's final blow: the slot is saved as child Link arriving at the Temple of Time");
}

// OoT's ending never leaves "The End" in the castle courtyard. This fades from it to the arrival as the pedestal does
// (z_demo.c, terminator 8) and, as a cutscene's own exit does, keeps the courtyard's script from ending under the
// fade.
void LeaveTheEndOnceShown() {
    PlayState* play = gPlayState;

    if (!sEndingPlays || sLeavingTheEnd || play == nullptr || play->sceneNum != SCENE_CASTLE_COURTYARD_ZELDA ||
        play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF) {
        return;
    }
    const EndTitle* theEnd = (const EndTitle*)Actor_Find(&play->actorCtx, ACTOR_END_TITLE, ACTORCAT_ITEMACTION);
    if (theEnd == nullptr || theEnd->endAlpha < kTheEndShownAlpha) {
        return;
    }
    const bool pressed = CHECK_BTN_ANY(play->state.input[0].press.button, kTheEndContinueButtons);
    if (!pressed && ++sTheEndShownFrames < kTheEndHoldFrames) {
        return;
    }
    sLeavingTheEnd = true;
    if (play->csCtx.state != CS_STATE_IDLE) {
        play->csCtx.state = CS_STATE_UNSKIPPABLE_EXEC;
    }
    Audio_SetCutsceneFlag(0);
    gSaveContext.cutsceneIndex = 0;
    play->linkAgeOnLoad = LINK_AGE_CHILD;
    play->nextEntranceIndex = kChildArrivalEntrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_WHITE;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_WHITE;
    SPDLOG_INFO("[ComboBackToBack] \"The End\" {}: on to the Temple of Time", pressed ? "skipped" : "shown");
}

// Play_Destroy at the end of that fade, before Play_Init reads the game mode and the cutscene for the new scene.
void EndTheEnding() {
    if (!sLeavingTheEnd) {
        return;
    }
    sLeavingTheEnd = false;
    if (gSaveContext.entranceIndex != kChildArrivalEntrance) {
        return; // reset or quit during the fade: the save made at the final blow stands
    }
    Audio_SetSfxBanksMute(kNoMutedSfxBanks);
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.nextCutsceneIndex = kNoNextCutscene;
    gSaveContext.respawnFlag = 0;
    sEndingPlays = false;
    sSavePending = true;
    SPDLOG_INFO("[ComboBackToBack] OoT's ending is over: child Link arrives at the Temple of Time");
}

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

// Every scene of OoT's ending plays one of its own cutscenes or the credits, so a scene in normal play means the ending
// was left some other way (a warp): the slot's saves are its own again.
void EndTheEndingIfLeft(int16_t sceneNum) {
    const bool normalPlay =
        gSaveContext.gameMode == GAMEMODE_NORMAL && gSaveContext.cutsceneIndex < kFirstSceneCutscene;
    if (sEndingPlays && normalPlay) {
        sEndingPlays = false;
        SPDLOG_INFO("[ComboBackToBack] OoT's ending was left early: the slot saves as usual again");
    }
}

// At the start of every save load and on every new save, so nothing of the previous slot is left over.
void ForgetPreviousSlot(bool isDebug) {
    sEndingReached = false;
    sEndingPlays = false;
    sFinalBlowDealt = false;
    sLeavingTheEnd = false;
    sSavePending = false;
}

void SaveEndingSection(const SaveContext& saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData(kEndingReachedKey, sEndingReached.load());
}

void LoadEndingSection() {
    bool reached = false;
    SaveManager::Instance->LoadData(kEndingReachedKey, reached, false);
    sEndingReached = reached;
}

// ShipInit runs RegisterBackToBackEnding again whenever a preset is applied; the save section is added once.
void AddEndingSaveSection() {
    static bool sAdded = false;

    if (sAdded) {
        return;
    }
    sAdded = true;
    SaveManager::Instance->AddInitFunction(ForgetPreviousSlot);
    SaveManager::Instance->AddLoadFunction(kSaveSection, kSaveSectionVersion, LoadEndingSection);
    SaveManager::Instance->AddSaveFunction(kSaveSection, kSaveSectionVersion, SaveEndingSection, true,
                                           SECTION_PARENT_NONE);
}

void RegisterBackToBackEnding() {
    AddEndingSaveSection();
    COND_ID_HOOK(OnBossDefeat, ACTOR_BOSS_GANON2, true, [](void* ganon) {
        if (!sEndingPlays && InBackToBackSlot()) {
            sFinalBlowDealt = true;
        }
    });
    COND_HOOK(OnGameFrameUpdate, true, SaveArrivalAtFinalBlow);
    COND_HOOK(OnGameFrameUpdate, true, LeaveTheEndOnceShown);
    COND_HOOK(OnGameFrameUpdate, true, SaveOnceSettledInTemple);
    COND_HOOK(OnPlayDestroy, true, EndTheEnding);
    COND_HOOK(OnSceneInit, true, EndTheEndingIfLeft);
}

RegisterShipInitFunc sBackToBackEndingInit(RegisterBackToBackEnding, {});

} // namespace

// SaveManager::SaveSection: while the ending plays, the save made at the final blow is the slot's.
extern "C" int Combo_BackToBack_HoldsSave(void) {
    return sEndingPlays ? 1 : 0;
}

// Sram_OpenSave: once OoT's ending has been reached, a save of child Link arriving at the Temple of Time's pedestal
// loads there, not in Link's house.
extern "C" int Combo_BackToBack_KeepsSavedArrival(void) {
    return sEndingReached && LINK_IS_CHILD && gSaveContext.savedSceneNum == SCENE_TEMPLE_OF_TIME &&
           gSaveContext.entranceIndex == kChildArrivalEntrance;
}

#endif // COMBO_BUILD
