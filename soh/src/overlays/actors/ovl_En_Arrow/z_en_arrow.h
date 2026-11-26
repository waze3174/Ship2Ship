#ifndef Z_EN_ARROW_H
#define Z_EN_ARROW_H

#include <libultraship/libultra.h>
#include "global.h"

struct EnArrow;

typedef void (*EnArrowActionFunc)(struct EnArrow*, PlayState*);

typedef struct EnArrow {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ SkelAnime skelAnime;
    /* 0x0190 */ ColliderQuad collider;
    /* 0x0210 */ Vec3f unk_210;
    /* 0x021C */ Vec3f unk_21C;
    /* 0x0228 */ s32 effectIndex;
    /* 0x022C */ WeaponInfo weaponInfo;
    /* 0x0248 */ u8 timer; // used for dissapearing when flying or hitting a wall
    /* 0x0249 */ u8 hitFlags;
    /* 0x024A */ u8 touchedPoly;
    /* 0x024B */ u8 isCsNut;
    /* 0x024C */ Actor* hitActor;
    /* 0x0250 */ Vec3f unk_250;
    /* 0x025C */ EnArrowActionFunc actionFunc;
#ifdef COMBO_BUILD
    // ComboShip: MM's Deku Bubble (params ARROW_COMBO_DEKU_BUBBLE, z_en_arrow_combo_bubble.c). MM EnArrowBubble.
    f32 comboBubbleScale; // MM bubble.unk_144: size
    u8 comboBubbleTimer;  // MM bubble.unk_148: frames at full size while held
    s8 comboBubbleState;  // MM bubble.unk_149: 0 new, 1 flying, -1 bounced (no more hits)
    s16 comboBubbleWobX;  // MM bubble.unk_14A
    s16 comboBubbleWobY;  // MM bubble.unk_14C
    s32 comboSfxSlot;     // MM sfx source slot (ComboGuestAudio.cpp), 0 = none yet
    struct ComboBubbleSplat {
        Vec3f pos;
        CollisionPoly* colPoly;
        s16 scroll, scrollStep, texIndex, alpha, reg4, reg4Step, rotAngle, scale, reg8, life;
        u8 sliding;
    } comboSplat; // MM EffectSsSbn, run by the popped bubble's actor
    // ComboShip: 2S2H's bomb arrows (z_en_arrow_combo_bomb.c). Zero (unused) on every other arrow.
    u8 comboBombArrow;       // this arrow carries a bomb: fuse, flash, blast
    u8 comboBombSpent;       // the shot's bomb has been taken from the bag
    u8 comboBombBowItem;     // the bow it was nocked from: ITEM_BOW, or MM's bow (a foreign id) with MM's bags
    s16 comboBombFuse;       // frames left on the fuse
    s16 comboBombFlashSpeed; // EnBom.flashSpeedScale
    f32 comboBombFlash;      // EnBom.flashIntensity
#endif
} EnArrow;

#ifdef COMBO_BUILD
typedef struct ComboBubbleSplat ComboBubbleSplat;
#define ARROW_COMBO_DEKU_BUBBLE 0x30
void EnArrow_ComboBubbleInit(EnArrow* self, PlayState* play);
void EnArrow_ComboBubbleDestroy(EnArrow* self, PlayState* play);
void EnArrow_ComboBubbleUpdate(EnArrow* self, PlayState* play);
void EnArrow_ComboBubbleDraw(EnArrow* self, PlayState* play);
s32 EnArrow_ComboBubbleHeadPose(Actor* heldActor, Vec3f* scale, s16* rotZ, s16* upperRotX);

// ComboShip: 2S2H's bomb arrows (z_en_arrow_combo_bomb.c).
// Tag: right after the player nocked `self` (spawned as his held arrow from the HUD button `heldItemButton`, 0 = B).
void EnArrow_ComboBombArrowTag(EnArrow* self, PlayState* play, s32 heldItemButton, s32 arrowType);
// Release: the player lets go of his held actor; a bomb arrow takes its bomb from the bag.
void EnArrow_ComboBombArrowRelease(Actor* heldActor);
// Update: the fuse, the flash and the blast. true = the arrow has gone off (the rest of its update is skipped).
s32 EnArrow_ComboBombArrowUpdate(EnArrow* self, PlayState* play);
void EnArrow_ComboBombArrowDraw(EnArrow* self, PlayState* play);
#endif

typedef enum {
    /* -10 */ ARROW_CS_NUT = -10, // cutscene deku nuts are allowed to update in blocking mode
    /* -1  */ ARROW_NORMAL_SILENT = -1, // normal arrow that does not make a sound when being shot
    /*  0  */ ARROW_NORMAL_LIT, // normal arrow lit on fire
    /*  1  */ ARROW_NORMAL_HORSE, // normal arrow shot while riding a horse
    /*  2  */ ARROW_NORMAL,
    /*  3  */ ARROW_FIRE,
    /*  4  */ ARROW_ICE,
    /*  5  */ ARROW_LIGHT,
    /*  6  */ ARROW_0C,
    /*  7  */ ARROW_0D,
    /*  8  */ ARROW_0E,
    /*  9  */ ARROW_SEED,
    /*  10 */ ARROW_NUT
} ArrowType;

#endif
