#pragma once

#include <libultraship/libultra/types.h>

#ifdef __cplusplus
extern "C" {
#endif
const char* SohFileSelect_GetSettingText(u8 optionIndex, u8 language);
void SohFileSelect_ShowPresetModal();
bool SohFileSelect_IsQuestHidden(u8 quest);
u8 SohFileSelect_CountVisibleQuests();
#ifdef COMBO_BUILD
// The order the new-file choice cycles through (left to right), by position 0 to Length - 1. PositionOf is -1 for a
// quest the carousel never offers.
u8 SohFileSelect_CarouselLength();
u8 SohFileSelect_QuestAtCarouselPosition(u8 position);
s32 SohFileSelect_CarouselPositionOf(u8 quest);
#endif
#ifdef __cplusplus
};
#endif

typedef enum {
    RSM_START_RANDOMIZER,
    RSM_GENERATE_RANDOMIZER,
    RSM_OPEN_RANDOMIZER_SETTINGS,
    RSM_GENERATING,
    RSM_NO_RANDOMIZER_GENERATED,
#ifdef COMBO_BUILD
    RSM_FINALIZING, // ComboShip: post-fill generation phase (see SOH_GetComboGenPhase)
#endif
    RSM_MAX,
} RandomizerSettingsMenuEnums;
