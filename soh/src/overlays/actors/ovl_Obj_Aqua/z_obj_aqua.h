#ifndef Z_OBJ_AQUA_H
#define Z_OBJ_AQUA_H

// ComboShip: MM's ObjAqua (mm/src/overlays/actors/ovl_Obj_Aqua/z_obj_aqua.h) in Hyrule. See z_obj_aqua.c.

#include <libultraship/libultra.h>
#include "global.h"

#ifdef COMBO_BUILD

#define AQUA_GET_TYPE(thisx) ((thisx)->params & 1)

#define AQUA_PARAMS(type) ((type) & 1)

typedef enum {
    /* 0 */ AQUA_TYPE_COLD,
    /* 1 */ AQUA_TYPE_HOT
} AquaType;

struct ObjAqua;

typedef void (*ObjAquaActionFunc)(struct ObjAqua*, PlayState*);

// One puff of MM's EffectSsIceSmoke (mm/src/overlays/effects/ovl_Effect_Ss_Ice_Smoke), run by the water itself: OOT's
// EffectSsIceSmoke is a different effect (it needs Freezard's object_fz and draws gFreezardSteamStartDL).
typedef struct ObjAquaSmoke {
    Vec3f pos;
    Vec3f velocity;
    s16 life; // -1 = free slot
    s16 alpha;
    s16 scale;
    s16 scrollX;
    s16 scrollY;
    u32 epoch; // frame interpolation
} ObjAquaSmoke;

#define OBJ_AQUA_SMOKE_MAX 40 // MM spawns one puff a frame for the ~33 frames the hot water is still opaque (alpha > 90)

typedef struct ObjAqua {
    /* 0x000 */ Actor actor;
    /*       */ ColliderCylinder collider;
    /*       */ ObjAquaActionFunc actionFunc;
    /*       */ s16 counter;
    /*       */ u8 alpha;
    /*       */ s16 unk_198;
    // ComboShip
    s32 sfxSlot;   // MM sfx source slot (ComboGuestAudio.cpp), 0 = none yet
    u8 waterGone;  // MM killed the water; the actor lives on only for its steam puffs
    ObjAquaSmoke smoke[OBJ_AQUA_SMOKE_MAX];
} ObjAqua;

void ObjAqua_Init(Actor* thisx, PlayState* play);
void ObjAqua_Destroy(Actor* thisx, PlayState* play);
void ObjAqua_Update(Actor* thisx, PlayState* play);
void ObjAqua_Draw(Actor* thisx, PlayState* play);

#endif // COMBO_BUILD

#endif // Z_OBJ_AQUA_H
