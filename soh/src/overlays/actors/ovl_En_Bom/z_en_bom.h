#ifndef Z_EN_BOM_H
#define Z_EN_BOM_H

#include <libultraship/libultra.h>
#include "global.h"

struct EnBom;

typedef void (*EnBomActionFunc)(struct EnBom*, struct PlayState*);

typedef struct EnBom {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderCylinder bombCollider;
    /* 0x0198 */ ColliderJntSph explosionCollider;
    /* 0x01B8 */ ColliderJntSphElement explosionColliderItems[1];
    /* 0x01F8 */ s16 timer;
    /* 0x01FA */ s16 flashSpeedScale;
    /* 0x01FC */ f32 flashIntensity;
    /* 0x0200 */ u8 bumpOn;
    /* 0x0204 */ EnBomActionFunc actionFunc;
#ifdef COMBO_BUILD
    // ComboShip: Majora's Mask's Powder Keg and its explosion (z_en_bom_combo_keg.c). Zero (unused) on OOT bombs.
    u8 comboIsKeg;                                // MM EnBom.isPowderKeg
    u8 comboSinkTimer;                            // MM EnBom.unk_1FC (sinking in water)
    s16 comboKegRoll;                             // MM EnBom.unk_1FA (the barrel rolling)
    s32 comboSfxSlot;                             // MM sfx source slot (ComboGuestAudio.cpp), 0 = none yet
    struct EnBomComboClearTag* comboClearTag;     // MM EnClearTag's effects (the explosion-effects instance only)
#endif
} EnBom; // size = 0x0208

typedef enum {
    /* 0x00 */ BOMB_BODY,
    /* 0x01 */ BOMB_EXPLOSION
} EnBomType;

#endif
