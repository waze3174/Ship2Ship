#ifndef Z_COMBO_MAGIC_DARK_H
#define Z_COMBO_MAGIC_DARK_H

#include "global.h"
#include "2s2h/Rando/MiscBehavior/ComboAdultBody.h"

// ComboShip: OOT's Nayru's Love (OOT MagicDark layout).
typedef struct ComboMagicDark {
    /* 0x000 */ Actor actor;
    /* 0x144 */ s16 timer;
    /* 0x146 */ u8 primAlpha;
    /* 0x148 */ Vec3f orbOffset;
    /* 0x154 */ f32 scale;
} ComboMagicDark;

#endif
