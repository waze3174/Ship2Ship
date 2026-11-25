// mm/2s2h/Rando/MiscBehavior/ComboDiscordState.cpp — ComboShip: MM's half of Discord Rich Presence. Combo-owned file
// (2ship GLOB_RECURSE). Fills the plain struct in combo/discord/DiscordGameState.h for the launcher, which decides what
// reaches Discord. Called on the game thread from the launcher's shared tick.
#include <cstdint>
#include <cstring>

#include "ComboExport.h"
#include "discord/DiscordGameState.h"
#include "2s2h/ShipUtils.h"
#include "Rando/Rando.h"
#include "ComboAdultBody.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

extern "C" int MM_GetComboForm(void);       // ComboAdultBody.cpp
extern "C" uint64_t MM_GetPlaytimeMs(void); // BenPort.cpp

namespace {

constexpr uint64_t kMillisecondsPerSecond = 1000;

// The same count as CheckTracker.cpp's GetTotalCheckCount: obtained or skipped over shuffled.
void FillChecks(ComboDiscordGameState* out) {
    int32_t collected = 0;
    int32_t shuffled = 0;
    for (const auto& [_, check] : Rando::StaticData::Checks) {
        const auto& saved = RANDO_SAVE_CHECKS[check.randoCheckId];
        if (saved.obtained || saved.skipped)
            ++collected;
        if (saved.shuffled)
            ++shuffled;
    }
    out->checksGotten = collected;
    out->checksTotal = shuffled;
}

void FillSaveFields(ComboDiscordGameState* out) {
    if (IS_RANDO)
        FillChecks(out);
    out->playSeconds = static_cast<int64_t>(MM_GetPlaytimeMs() / kMillisecondsPerSecond);
}

void FillLiveFields(ComboDiscordGameState* out) {
    out->inPlay = (gPlayState != nullptr) && (gSaveContext.gameMode == GAMEMODE_NORMAL);
    if (!out->inPlay)
        return;
    out->adult = Combo_MM_LinkIsAdult();
    out->form = MM_GetComboForm();
    out->maxHearts = gSaveContext.save.saveInfo.playerData.healthCapacity / COMBO_DISCORD_HEALTH_PER_HEART;
    out->day = CURRENT_DAY;
    out->minuteOfDay = static_cast<int32_t>(TIME_TO_MINUTES_F(CURRENT_TIME));
    const char* scene = Ship_GetSceneName(gPlayState->sceneId);
    std::strncpy(out->sceneName, scene, sizeof(out->sceneName) - 1);
}

} // namespace

extern "C" COMBO_EXPORT int MM_DiscordGetState(ComboDiscordGameState* out, int live) try {
    if (out == nullptr)
        return 0;
    ComboDiscord_InitState(out);
    if (live != 0)
        FillLiveFields(out);
    FillSaveFields(out);
    return 1;
} catch (...) { return 0; }
