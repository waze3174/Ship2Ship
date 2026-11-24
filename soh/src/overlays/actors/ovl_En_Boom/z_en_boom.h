#ifndef Z_EN_BOOM_H
#define Z_EN_BOOM_H

#include <libultraship/libultra.h>
#include "global.h"

struct EnBoom;

typedef void (*EnBoomActionFunc)(struct EnBoom*, PlayState*);

typedef struct EnBoom {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderQuad collider;
    /* 0x01CC */ Actor* moveTo; // actor boomerang moves toward
    /* 0x01D0 */ Actor* grabbed; // actor grabbed by the boomerang
    /* 0x01D4 */ u8 returnTimer; // returns to Link when 0
    /* 0x01D5 */ u8 activeTimer; // increments once every update
    /* 0x01D8 */ s32 effectIndex;
    /* 0x01DC */ WeaponInfo boomerangInfo;
    /* 0x01F8 */ EnBoomActionFunc actionFunc;
#ifdef COMBO_BUILD
    // ComboShip: MM's Zora fins (z_en_boom_combo_fins.c). activeTimer doubles as MM unk_1CD (spin).
    s8 comboCurve; // MM unk_1CE: frames curving out on the fin's own side
    s8 comboHome;  // MM unk_1CF: homing-turn frames toward a locked target
#endif
} EnBoom;

#ifdef COMBO_BUILD
#define COMBO_ZORA_FIN_LEFT 0x10
#define COMBO_ZORA_FIN_RIGHT 0x11
#define COMBO_IS_ZORA_FIN(params) (((params) == COMBO_ZORA_FIN_LEFT) || ((params) == COMBO_ZORA_FIN_RIGHT))
extern s32 gComboZoraFinCaught;
void EnBoom_ComboFinInit(EnBoom* self, PlayState* play);
void EnBoom_ComboFinDestroy(EnBoom* self, PlayState* play);
void EnBoom_ComboFinUpdate(EnBoom* self, PlayState* play);
void EnBoom_ComboFinDraw(EnBoom* self, PlayState* play);
#endif

#endif
