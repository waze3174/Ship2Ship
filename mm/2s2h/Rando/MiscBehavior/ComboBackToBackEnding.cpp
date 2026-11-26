// mm/2s2h/Rando/MiscBehavior/ComboBackToBackEnding.cpp — ComboShip: the end of Majora's Mask's half of a back-to-back
// slot (combo/rando/ComboCrossGate.h). Combo-owned file (2ship GLOB_RECURSE). See docs/deviations/back2back.md.
//
// Majora's death (z_boss_07.c) records Majora as beaten, which saves the slot's completion record (ComboShip.cpp), and
// plays MM's own ending and credits. MM never leaves them: the credits go from scene to scene (z_demo.c,
// CutsceneCmd_ChooseCreditsScenes), never to the title, and their last shot stays up until the console is reset. MM's
// code has two cutscene commands that hold a cutscene on one frame for good, CS_MISC_FINALE and
// CS_MISC_LONG_SCARECROW_SONG (z_demo.c), and nothing in it leaves either; which of them holds the last shot is in the
// scene data, so both count. Once that hold has been up for a few seconds, or a button is pressed, the slot goes back
// to Ocarina of Time's title the way an owl-save quit does. MM's save is not written.
#ifdef COMBO_BUILD

#include <spdlog/spdlog.h>

#include "rando/ComboCrossGate.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern int gMMComboCrossGate;     // BenPort.cpp
void Combo_MM_CreditsEnded(void); // BenPort.cpp
}

namespace {

namespace CrossGate = ComboRando::CrossGate;

// How long the last shot stays up before the slot moves on by itself, at MM's 20 game frames a second.
constexpr int kLastShotHoldFrames = 5 * 20;

// Any of these moves on from the last shot at once.
constexpr u16 kLastShotContinueButtons = BTN_A | BTN_B | BTN_START;

// From Majora's death until the slot leaves MM.
bool sEndingPlays = false;
// Frames the current scene's cutscene has been held; 0 until it is.
int sHeldFrames = 0;
// The slot has asked to go back to Ocarina of Time's title.
bool sLeaving = false;

bool CreditsReturnToOot() {
    return CrossGate::MmCreditsReturnToOot(gMMComboCrossGate);
}

// The stump and lighting at the end of the credits (z_obj_ending.c), logged as a check on which shot holds.
bool StumpInScene(PlayState* play) {
    return SubS_FindActor(play, nullptr, ACTORCAT_BG, ACTOR_OBJ_ENDING) != nullptr;
}

// OnGameCompletion: Majora's Wrath has started its death cutscene (z_boss_07.c), which goes on to the credits.
void StartTheEnding() {
    if (!CreditsReturnToOot()) {
        return;
    }
    sEndingPlays = true;
    sHeldFrames = 0;
    sLeaving = false;
    SPDLOG_INFO("[ComboBackToBack] Majora is beaten: MM's ending plays, then the slot returns to OoT's title");
}

// Each scene of the ending, logged so a run shows the credits' order and their last scene.
void NoteEndingScene(s8 sceneId, s8 spawnNum) {
    if (!sEndingPlays) {
        return;
    }
    sHeldFrames = 0;
    SPDLOG_INFO("[ComboBackToBack] MM's ending: scene 0x{:X}, layer {}, spawn {}, entrance 0x{:X}", (int)sceneId,
                (int)gSaveContext.sceneLayer, (int)spawnNum, (int)gSaveContext.save.entrance);
}

// At every save load (each entry into MM fires one), so nothing of a previous session is left over.
void ForgetTheEnding(s16 fileNum) {
    sEndingPlays = false;
    sHeldFrames = 0;
    sLeaving = false;
}

void RegisterBackToBackEnding() {
    COND_HOOK(OnGameCompletion, true, StartTheEnding);
    COND_HOOK(OnSceneInit, true, NoteEndingScene);
    COND_HOOK(OnSaveLoad, true, ForgetTheEnding);
}

RegisterShipInitFunc sBackToBackEndingInit(RegisterBackToBackEnding, {});

} // namespace

// z_demo.c, on every frame a cutscene is held for good (miscType is the CS_MISC_* command holding it). During MM's
// ending in a back-to-back slot that is the credits' last shot: after kLastShotHoldFrames, or on a button, the slot
// goes to Ocarina of Time's title (Combo_MM_CreditsEnded). Not while a transition runs, so a scene change that is
// already under way finishes first.
extern "C" void Combo_MM_CutsceneHeldForever(PlayState* play, u16 miscType) {
    if (!sEndingPlays || sLeaving) {
        return;
    }
    if (sHeldFrames++ == 0) {
        SPDLOG_INFO("[ComboBackToBack] MM's ending holds its last shot: scene 0x{:X}, layer {}, script {}, frame {}, "
                    "misc 0x{:X}, stump {}, music 0x{:X}",
                    (int)play->sceneId, (int)gSaveContext.sceneLayer, (int)play->csCtx.scriptIndex,
                    (int)play->csCtx.curFrame, (int)miscType, StumpInScene(play) ? "shown" : "absent",
                    (int)AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN));
    }
    const bool pressed = CHECK_BTN_ANY(play->state.input[0].press.button, kLastShotContinueButtons);
    if (!pressed && sHeldFrames < kLastShotHoldFrames) {
        return;
    }
    if (play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF) {
        return;
    }
    sLeaving = true;
    SPDLOG_INFO("[ComboBackToBack] MM's last shot {}: on to Ocarina of Time's title", pressed ? "skipped" : "shown");
    Combo_MM_CreditsEnded();
}

#endif // COMBO_BUILD
