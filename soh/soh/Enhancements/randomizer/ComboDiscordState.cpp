// soh/soh/Enhancements/randomizer/ComboDiscordState.cpp — ComboShip: OOT's half of Discord Rich Presence. Combo-owned
// file (soh GLOB_RECURSE). Fills the plain struct in combo/discord/DiscordGameState.h for the launcher, which decides
// what reaches Discord. Called on the game thread from the launcher's shared tick.
#include <cstdint>

#include "ComboExport.h"
#include "discord/DiscordGameState.h"
#include "soh/OTRGlobals.h"
#include "soh/util.h"
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "soh/Enhancements/randomizer/randomizer_check_tracker.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

extern "C" uint64_t SOH_GetPlaytimeDeciseconds(void); // OTRGlobals.cpp

namespace {

constexpr uint8_t kLastSaveSlot = 2;
constexpr uint64_t kDecisecondsPerSecond = 10;

bool SaveIsLoaded() {
    return gSaveContext.fileNum <= kLastSaveSlot;
}

void FillSaveFields(ComboDiscordGameState* out) {
    if (IS_RANDO && CheckTracker::GetTotalChecks() > 0) {
        out->checksGotten = CheckTracker::GetTotalChecksGotten();
        out->checksTotal = CheckTracker::GetTotalChecks();
    }
    out->playSeconds = static_cast<int64_t>(SOH_GetPlaytimeDeciseconds() / kDecisecondsPerSecond);
}

void FillLiveFields(ComboDiscordGameState* out) {
    out->inPlay = (gPlayState != nullptr) && SaveIsLoaded() && (gSaveContext.gameMode == GAMEMODE_NORMAL);
    if (!out->inPlay)
        return;
    out->adult = LINK_IS_ADULT ? 1 : 0;
    out->form = Combo_OotForm();
    out->maxHearts = gSaveContext.healthCapacity / COMBO_DISCORD_HEALTH_PER_HEART;
    const int scene = gPlayState->sceneNum;
    if (scene >= 0 && scene < SCENE_ID_MAX)
        SohUtils::CopyStringToCharArray(out->sceneName, SohUtils::GetSceneName(scene), sizeof(out->sceneName));
}

} // namespace

extern "C" COMBO_EXPORT int SOH_DiscordGetState(ComboDiscordGameState* out, int live) try {
    if (out == nullptr)
        return 0;
    ComboDiscord_InitState(out);
    if (live != 0)
        FillLiveFields(out);
    if (SaveIsLoaded())
        FillSaveFields(out);
    return 1;
} catch (...) { return 0; }
