#ifndef Z_EN_NWC_H
#define Z_EN_NWC_H

#include <libultraship/libultra.h>
#include "global.h"

struct EnNwc;
struct EnNwcChick;

typedef void (*EnNwcUpdateFunc)(struct EnNwc*, PlayState*);
typedef void (*EnNwcChickFunc)(struct EnNwcChick*, struct EnNwc*, PlayState*);

typedef struct EnNwcChick {
    /* 0x00 */ s8 type;
    /* 0x01 */ u8 bgFlags;
    /* 0x04 */ f32 floorY;
    /* 0x08 */ Vec3f pos;
    /* 0x14 */ char unk_14[8];
    /* 0x1C */ Vec3f lastPos;
    /* 0x28 */ char unk_28[4];
    /* 0x2C */ f32 velY;
    /* 0x30 */ Vec3s rot;
    /* 0x36 */ u16 height;
    /* 0x38 */ CollisionPoly* floorPoly;
    /* 0x44 */ char unk_3C[0x20];
    u32 epoch;
#ifdef COMBO_BUILD
    // ComboShip: MM's Bremen Mask. Each chick runs MM's En_Nwc march states (mm z_en_nwc.c).
    s16 comboState;      // 0 = OOT's own chick; else MM NWC_STATE_FOLLOWING (3) / NWC_STATE_RUNNING (4)
    s16 comboOrder;      // MM home.rot.z: the chick's place in the line (1, 3, 5... = index into the trail)
    s16 comboStateTimer; // MM stateTimer
    s16 comboRandomRot;  // MM randomRot
    s16 comboYaw;        // MM world.rot.y (rot.y is MM's shape.rot.y)
    f32 comboSpeed;      // MM actor.speed
    u8 comboGrounded;    // MM BGCHECKFLAG_GROUND
#endif

} EnNwcChick; // size = 0x5C

typedef struct EnNwc {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderJntSph collider;
    /* 0x016C */ u8 count;
    /* 0x0170 */ EnNwcChick chicks[16];
    /* 0x0730 */ EnNwcUpdateFunc updateFunc;
#ifdef COMBO_BUILD
    // ComboShip: MM's Bremen Mask. MM's chicks march behind Grog's bookkeeping (MM EnHs nwcPos / home.rot.z,
    // mm z_en_hs.c:105-141); Hyrule has no Grog, so the chick cluster keeps it for its own chicks.
    Vec3f comboTrail[20]; // MM EnHs nwcPos: the line's points, the first one behind Link
    s16 comboCount;       // MM EnHs home.rot.z: 2 x the number of chicks in the line
    u8 comboTrailReady;   // MM EnHs_Init has seeded the trail (func_80952C50)
#endif
} EnNwc; // size = 0x0734

#endif
