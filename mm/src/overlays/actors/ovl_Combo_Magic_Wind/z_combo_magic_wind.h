#ifndef Z_COMBO_MAGIC_WIND_H
#define Z_COMBO_MAGIC_WIND_H

#include "global.h"

struct ComboMagicWind;

typedef void (*ComboMagicWindFunc)(struct ComboMagicWind* this, PlayState* play);

// ComboShip: OOT's Farore's Wind (OOT MagicWind layout).
typedef struct ComboMagicWind {
    /* 0x000 */ Actor actor;
    /* 0x144 */ SkelCurve skelCurve;
    /* 0x164 */ s16 timer;
    /* 0x168 */ ComboMagicWindFunc actionFunc;
} ComboMagicWind;

#endif
