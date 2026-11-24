/**
 * @file audio_mm_sfx_params.c
 *
 * ComboShip: Majora's Mask's sfx parameter tables (importance, distance range, random frequency, flags) for the
 * native MM sfx player (audio_mm_sfx.c). Built from MM's own source tables (mm/include/tables/sfx/*.h, as MM's
 * sfx_params.c builds them). This file deliberately includes no OOT headers: MM's flag macro names would collide.
 */
#include <stdint.h>

typedef struct {
    uint8_t importance;
    uint8_t flags;
    uint16_t params;
} MmSfxParams; // MM SfxParams

// MM include/sfx.h
#define SFX_PARAM_DIST_RANGE_SHIFT 0
#define SFX_PARAM_DIST_RANGE_MASK (7 << SFX_PARAM_DIST_RANGE_SHIFT)
#define SFX_FLAG_LOWER_VOLUME_BGM (1 << 3)
#define SFX_FLAG_PRIORITY_NO_DIST (1 << 4)
#define SFX_FLAG_BLOCK_EQUAL_IMPORTANCE (1 << 5)
#define SFX_PARAM_RAND_FREQ_RAISE_SHIFT 6
#define SFX_PARAM_RAND_FREQ_RAISE_MASK (3 << SFX_PARAM_RAND_FREQ_RAISE_SHIFT)
#define SFX_FLAG_8 (1 << 8)
#define SFX_FLAG_SURROUND_LOWPASS_FILTER (1 << 9)
#define SFX_FLAG_BEHIND_SCREEN_Z_INDEX (1 << 10)
#define SFX_PARAM_RAND_FREQ_SCALE (1 << 11)
#define SFX_FLAG_REVERB_NO_DIST (1 << 12)
#define SFX_FLAG_VOLUME_NO_DIST (1 << 13)
#define SFX_PARAM_RAND_FREQ_LOWER (1 << 14)
#define SFX_FLAG_FREQ_NO_DIST (1 << 15)
#define SFX_FLAG2_FORCE_RESET (1 << 0)
#define SFX_FLAG2_UNUSED2 (1 << 2)
#define SFX_FLAG2_UNUSED4 (1 << 4)
#define SFX_FLAG2_SURROUND_NO_HIGHPASS_FILTER (1 << 5)
#define SFX_FLAG2_UNUSED6 (1 << 6)
#define SFX_FLAG2_APPLY_LOWPASS_FILTER (1 << 7)

#define DEFINE_SFX(_0, importance, distParam, randParam, flags2, flags1)           \
    { importance, flags2,                                                          \
      ((((distParam) << SFX_PARAM_DIST_RANGE_SHIFT) & SFX_PARAM_DIST_RANGE_MASK) | \
       (((randParam) << SFX_PARAM_RAND_FREQ_RAISE_SHIFT) & SFX_PARAM_RAND_FREQ_RAISE_MASK) | (flags1)) },

static const MmSfxParams sMmEnemyBankParams[] = {
#include "../../../mm/include/tables/sfx/enemybank_table.h"
};

static const MmSfxParams sMmPlayerBankParams[] = {
#include "../../../mm/include/tables/sfx/playerbank_table.h"
};

static const MmSfxParams sMmItemBankParams[] = {
#include "../../../mm/include/tables/sfx/itembank_table.h"
};

static const MmSfxParams sMmEnvBankParams[] = {
#include "../../../mm/include/tables/sfx/environmentbank_table.h"
};

static const MmSfxParams sMmSystemBankParams[] = {
#include "../../../mm/include/tables/sfx/systembank_table.h"
};

static const MmSfxParams sMmOcarinaBankParams[] = {
#include "../../../mm/include/tables/sfx/ocarinabank_table.h"
};

static const MmSfxParams sMmVoiceBankParams[] = {
#include "../../../mm/include/tables/sfx/voicebank_table.h"
};

#undef DEFINE_SFX

#define COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

const MmSfxParams* const gMmSfxParams[7] = {
    sMmPlayerBankParams, sMmItemBankParams,    sMmEnvBankParams,   sMmEnemyBankParams,
    sMmSystemBankParams, sMmOcarinaBankParams, sMmVoiceBankParams,
};

const uint16_t gMmSfxParamsCount[7] = {
    COUNT(sMmPlayerBankParams), COUNT(sMmItemBankParams),    COUNT(sMmEnvBankParams),   COUNT(sMmEnemyBankParams),
    COUNT(sMmSystemBankParams), COUNT(sMmOcarinaBankParams), COUNT(sMmVoiceBankParams),
};
