#ifndef Z_EN_BOOM_H
#define Z_EN_BOOM_H

#include "global.h"

typedef enum {
    /* 0 */ ZORA_BOOMERANG_LEFT,
    /* 1 */ ZORA_BOOMERANG_RIGHT
} EnBoomType;

#ifdef COMBO_BUILD
// ComboShip (OOT items in Termina): OOT's own Boomerang, thrown by human Link (z_en_boom_combo_oot.c).
#define COMBO_OOT_BOOMERANG 0x10
struct EnBoom;
// SoH's Fast Boomerang: the item button pressed while OOT's Boomerang is out calls it back at once.
void EnBoom_ComboOotRequestQuickRecall(void);
void EnBoom_ComboOotInit(struct EnBoom* boom, PlayState* play);
void EnBoom_ComboOotDestroy(struct EnBoom* boom, PlayState* play);
void EnBoom_ComboOotUpdate(struct EnBoom* boom, PlayState* play);
void EnBoom_ComboOotDraw(struct EnBoom* boom, PlayState* play);
#endif

struct EnBoom;

typedef void (*EnBoomActionFunc)(struct EnBoom*, PlayState*);

typedef struct EnBoom {
    /* 0x000 */ Actor actor;
    /* 0x144 */ ColliderQuad collider;
    /* 0x1C4 */ Actor* moveTo;
    /* 0x1C8 */ Actor* unk_1C8;
    /* 0x1CC */ u8 unk_1CC;
    /* 0x1CD */ u8 unk_1CD;
    /* 0x1CE */ u8 unk_1CE;
    /* 0x1CF */ s8 unk_1CF;
    /* 0x1D0 */ s32 effectIndex;
    /* 0x1D4 */ WeaponInfo weaponInfo;
    /* 0x1F0 */ EnBoomActionFunc actionFunc;
} EnBoom; // size = 0x1F4

#endif // Z_EN_BOOM_H
