/*
 * File: z_en_bom_combo_keg.c
 * ComboShip: Majora's Mask's Powder Keg in Hyrule, as an OOT EnBom (shape.rot.x bit ENBOM_COMBO_KEG, see
 * z_en_bom_combo.h for the spawn contract).
 *
 * A line-for-line port of the keg branch of MM's EnBom (mm/src/overlays/actors/ovl_En_Bom/z_en_bom.c: EnBom_Init,
 * EnBom_Destroy, EnBom_Move, EnBom_WaitForRelease, EnBom_Explode, EnBom_Update, EnBom_Draw, the fuse rope
 * func_80872648 / func_808726DC / EnBom_DrawKeg) and of the part of MM's EnClearTag that the keg spawns
 * (mm/src/overlays/actors/ovl_En_Clear_Tag/z_en_clear_tag.c: the large explosion and the isolated smoke of a keg that
 * sinks). Same timer (from the player's gComboPowderKegTimer, MM gSaveContext.powderKegTimer, written back while
 * held), the same rolling barrel, bounce, sink-in-sand/snow, the same fuse rope and sparks, MM's barrel, skull and fuse
 * models (routed to MM's ResourceManager), MM's sounds played natively by OOT's audio engine (ComboGuestAudio.cpp /
 * audio_mm_sfx.c), MM's quake, rumble and lights, and MM's explosion effect (flash, smoke, shockwaves, debris, light
 * rays, water ring) drawn with MM's own display lists and textures.
 *
 * OOT's EN_CLEAR_TAG is an unrelated actor (the Arwing), so the keg's explosion effect runs on a second EnBom
 * (params ENBOM_COMBO_CLEARTAG_PARAMS(type), category ITEMACTION as MM's EnClearTag) with the effect pool on the heap.
 *
 * What is OOT's: the hit. The blast carries OOT's explosive damage (DMG_EXPLOSIVE, what OOT's bombable walls,
 * boulders, Dodongos... react to) with MM's keg damage (4) and radius (200), so everything a bomb breaks in Hyrule
 * a keg breaks, as MM's keg breaks its boulders. The body is detonated by what detonates an OOT bomb.
 *
 * Also here: MM's Blast Mask bomb (EnBom_ComboSpawnBlastMask), a plain OOT bomb with its fuse at 0.
 */

#include "z_en_bom.h"
#include "z_en_bom_combo.h"
#include "align_asset_macro.h"
#include "soh/frame_interpolation.h"
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "rando/CrossDamageTags.h"
#include <string.h>

#ifdef COMBO_BUILD

#define MM_OVL_DL(ovl, name) ((Gfx*)Combo_MmDL("overlays/" ovl "/" name))
#define MM_KEEP_DL(name) ((Gfx*)Combo_MmDL("objects/gameplay_keep/" name))

// MM sound ids (mm include/sfx.h); continuous ones without the 0x800 flag, as MM's `sfxId - SFX_FLAG` passes them.
#define MM_SE_PL_TAKE_OUT_SHIELD 0x834
#define MM_SE_IT_BOMB_UNEXPLOSION 0x180F
#define MM_SE_IT_BIG_BOMB_IGNIT_LOOP (0x185B - 0x800)
#define MM_SE_IT_BIG_BOMB_EXPLOSION 0x185C
#define MM_SE_EV_BOMB_DROP_WATER 0x2817
#define MM_SE_EV_TRE_BOX_BOUND 0x2856
#define MM_SE_EV_PUT_DOWN_WOODBOX 0x28AB

// MM's FloorType values (same bitfield in both games' SurfaceType; OOT has no names past 12).
#define MM_FLOOR_TYPE_SAND 4    // FLOOR_TYPE_4
#define MM_FLOOR_TYPE_ICE 5     // FLOOR_TYPE_5
#define MM_FLOOR_TYPE_SNOW 14   // FLOOR_TYPE_14
#define MM_FLOOR_TYPE_SNOW_2 15 // FLOOR_TYPE_15

// MM ClearTagType
#define MM_CLEAR_TAG_SMALL_EXPLOSION 0
#define MM_CLEAR_TAG_LARGE_EXPLOSION 1
#define MM_CLEAR_TAG_POP 2
#define MM_CLEAR_TAG_SPLASH 35
#define MM_CLEAR_TAG_SMOKE 200

// MM macros OOT lacks (or defines differently).
#define KEG_PI 3.14159265358979323846f                     // M_PIf
#define KEG_BINANG_ROT180(angle) ((s16)((angle) + 0x8000)) // OOT's BINANG_ROT180 is off by one
#define KEG_TRUNCF_BINANG(f) (s16)(s32)(f)
#define KEG_ABS_ALT(x) ((x) < 0 ? -(x) : (x))

#define KEG_SCALE 0.03f          // enBomScales[1]
#define KEG_EXPLOSION_RADIUS 200 // D_80872E8C[1]
#define KEG_QUAKE_Y 5            // sQuakeY[1]
#define KEG_QUAKE_DURATION 15    // sQuakeDurations[1]

s16 gComboPowderKegTimer = 0;

static void EnBomKeg_Move(EnBom* this, PlayState* play);
static void EnBomKeg_WaitForRelease(EnBom* this, PlayState* play);
static void EnBomKeg_Explode(EnBom* this, PlayState* play);
static void EnBomKeg_ResolveParent(EnBom* this, PlayState* play);

static Vec3f sKegZeroVec = { 0.0f, 0.0f, 0.0f }; // gZeroVec3f

// MM sCylinderInit (the keg's 20/36 is set in Init, as MM does). The bumper is OOT's bomb bumper: whatever sets off an
// OOT bomb sets off the keg.
static ColliderCylinderInit sKegCylinderInit = {
    {
        COLTYPE_HIT0,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER | AC_TYPE_OTHER,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000000, 0x00, 0x00 },
        { 0x0003F828, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_ON,
    },
    { 6, 11, 14, { 0, 0, 0 } },
};

// MM sJntSphElementsInit2 ({ 0x80000008, 0x00, 0x04 }: MM's keg + explosive bits, damage 4): OOT's explosive bit with
// MM's keg damage.
static ColliderJntSphElementInit sKegJntSphElementsInit[1] = {
    {
        {
            ELEMTYPE_UNK0,
            { DMG_EXPLOSIVE, 0x00, 0x04 },
            { 0x00000000, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_NONE,
            OCELEM_NONE,
        },
        { 0, { { 0, 0, 0 }, 0 }, 100 },
    },
};

static ColliderJntSphInit sKegJntSphInit = {
    {
        COLTYPE_HIT0,
        AT_ON | AT_TYPE_ALL,
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_JNTSPH,
    },
    1,
    sKegJntSphElementsInit,
};

static InitChainEntry sKegInitChain[] = {
    ICHAIN_VEC3F(scale, 0, ICHAIN_CONTINUE),
    ICHAIN_F32(targetArrowOffset, 2000, ICHAIN_CONTINUE),
    ICHAIN_F32_DIV1000(gravity, -4000, ICHAIN_STOP),
};

// ---------------------------------------------------------------------------------------------------------------
// MM math, verbatim where OOT's differs.

// MM Math_Atan2S_XY(x, y): MM's Math_Atan2S(y, x) is OOT's Math_Atan2S(x, y) table walk plus MM's axis cases.
static s16 Keg_Atan2S_XY(f32 x, f32 y) {
    if (y == 0.0f) {
        return (x >= 0.0f) ? 0 : (s16)0x8000;
    }
    if (x == 0.0f) {
        return (y >= 0.0f) ? 0x4000 : (s16)0xC000;
    }
    return Math_Atan2S(x, y);
}

// MM Math_Atan2F_XY(x, y) = BINANG_TO_RAD(Math_Atan2S_XY(x, y)).
static f32 Keg_Atan2F_XY(f32 x, f32 y) {
    return (f32)Keg_Atan2S_XY(x, y) * (KEG_PI / 0x8000);
}

// MM Matrix_MultVecZ: the current matrix applied to (0, 0, z).
static void Keg_MultVecZ(f32 z, Vec3f* dest) {
    Vec3f src = { 0.0f, 0.0f, z };

    Matrix_MultVec3f(&src, dest);
}

// mm z_bgcheck.c func_800C0094 (OOT's func_80038A28 builds this matrix differently).
static void Keg_PolyMatrix(CollisionPoly* poly, f32 tx, f32 ty, f32 tz, MtxF* dest) {
    f32 nx;
    f32 ny;
    f32 nz;
    f32 z_f14;
    f32 phi_f14;
    f32 phi_f12;
    f32 inv_z_f14;

    if (poly == NULL) {
        return;
    }
    nx = COLPOLY_GET_NORMAL(poly->normal.x);
    ny = COLPOLY_GET_NORMAL(poly->normal.y);
    nz = COLPOLY_GET_NORMAL(poly->normal.z);

    z_f14 = sqrtf(SQ(ny) + SQ(nz));
    if (!IS_ZERO(z_f14)) {
        inv_z_f14 = 1.0f / z_f14;
        phi_f14 = ny * inv_z_f14;
        phi_f12 = nz * inv_z_f14;
    } else {
        phi_f14 = 1.0f;
        phi_f12 = 0.0f;
    }
    dest->xx = z_f14;
    dest->yx = (-nx) * phi_f14;
    dest->zx = (-nx) * phi_f12;
    dest->xy = nx;
    dest->yy = ny;
    dest->zy = nz;
    dest->xz = 0.0f;
    dest->yz = -phi_f12;
    dest->zz = phi_f14;
    dest->wx = 0.0f;
    dest->wy = 0.0f;
    dest->wz = 0.0f;
    dest->xw = tx;
    dest->yw = ty;
    dest->zw = tz;
    dest->ww = 1.0f;
}

// MM's Actor_PlaySfx / Audio_PlaySfx_AtPos(&projectedPos): the keg's MM sounds at the keg.
static void Keg_Sfx(EnBom* this, u16 mmSfxId) {
    if (this->comboSfxSlot == 0) {
        this->comboSfxSlot = Combo_GuestAllocSlot();
    }
    Combo_GuestPlaySfxAt(mmSfxId, this->comboSfxSlot, &this->actor.projectedPos.x);
}

// MM func_80123590: the explosion takes the keg out of Link's hands.
static void Keg_DropFromPlayer(PlayState* play, Actor* actor) {
    Player* player = GET_PLAYER(play);

    if ((player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) && (player->heldActor == actor)) {
        player->interactRangeActor = NULL;
        player->heldActor = NULL;
        player->actor.child = NULL;
        player->stateFlags1 &= ~PLAYER_STATE1_CARRYING_ACTOR;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The fuse rope (MM sPowderKegFuseSegments, func_80872648, func_808726DC, EnBom_DrawKeg).

typedef struct {
    /* 0x00 */ Vec3f pos;
    /* 0x0C */ Vec3f velocity;
    /* 0x18 */ s16 rotY;
    /* 0x1A */ s16 rotX;
} PowderKegFuseSegment; // size = 0x1C

static PowderKegFuseSegment sPowderKegFuseSegments[16];

// MM func_80872648
static void EnBomKeg_InitFuse(PlayState* play, Vec3f* arg1) {
    PowderKegFuseSegment* fuseSegmentPtr = &sPowderKegFuseSegments[0];
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sPowderKegFuseSegments); i++, fuseSegmentPtr++) {
        Math_Vec3f_Copy(&fuseSegmentPtr->pos, arg1);
        Math_Vec3f_Copy(&fuseSegmentPtr->velocity, &sKegZeroVec);
        fuseSegmentPtr->rotY = 0;
        fuseSegmentPtr->rotX = 0x4000;
    }
}

// MM func_808726DC
static void EnBomKeg_UpdateFuse(PlayState* play, Vec3f* arg1, Vec3f* arg2, Vec3f* arg3, s32 arg4) {
    s32 i;
    f32 temp_f20;
    Vec3f spCC;
    Vec3f spC0;
    PowderKegFuseSegment* fuseSegmentPtr = &sPowderKegFuseSegments[0];
    PowderKegFuseSegment* fuseSegmentPtr2 = &sPowderKegFuseSegments[1];
    f32 temp_f26 = Math_Vec3f_DistXYZ(arg3, arg1);
    s32 spB0;
    f32 temp_f2;
    f32 distXZ;

    Math_Vec3f_Copy(&fuseSegmentPtr->pos, arg1);
    Math_Vec3f_Diff(arg2, arg1, &spCC);

    fuseSegmentPtr->rotY = Keg_Atan2S_XY(spCC.z, spCC.x);
    distXZ = sqrtf(SQXZ(spCC));
    fuseSegmentPtr->rotX = Keg_Atan2S_XY(distXZ, spCC.y);

    spB0 = (arg4 / 240) + 1;

    for (i = 0; i < spB0; i++, fuseSegmentPtr++, fuseSegmentPtr2++, arg4 -= 240) {
        f32 phi_f22;
        CollisionPoly* spA0;
        s32 bgId;
        Vec3f sp90;

        if (arg4 >= 240) {
            phi_f22 = 8.0f;
        } else {
            phi_f22 = (arg4 % 240) * (1.0f / 240) * 8.0f;
        }

        Math_Vec3f_Sum(&fuseSegmentPtr2->pos, &fuseSegmentPtr2->velocity, &fuseSegmentPtr2->pos);
        temp_f20 = Math_Vec3f_DistXYZAndStoreDiff(arg3, &fuseSegmentPtr2->pos, &spCC);
        if (temp_f20 < temp_f26) {
            if (temp_f20 == 0.0f) {
                spCC.x = 0.0f;
                spCC.y = temp_f26;
                spCC.z = 0.0f;
            } else {
                temp_f20 = temp_f26 / temp_f20;
                spCC.x *= temp_f20;
                spCC.y *= temp_f20;
                spCC.z *= temp_f20;
            }
            Math_Vec3f_Sum(arg3, &spCC, &fuseSegmentPtr2->pos);
        }

        if (Math_Vec3f_DistXYZAndStoreDiff(&fuseSegmentPtr->pos, &fuseSegmentPtr2->pos, &spCC) == 0.0f) {
            spCC.x = 0.0f;
            spCC.y = phi_f22;
            spCC.z = 0.0f;
        }

        fuseSegmentPtr2->rotY = Keg_Atan2S_XY(spCC.z, spCC.x);
        distXZ = sqrtf(SQXZ(spCC));
        fuseSegmentPtr2->rotX = Keg_Atan2S_XY(distXZ, spCC.y);

        fuseSegmentPtr2->rotY =
            (s16)CLAMP(BINANG_SUB(fuseSegmentPtr2->rotY, fuseSegmentPtr->rotY), -8000, 8000) + fuseSegmentPtr->rotY;
        fuseSegmentPtr2->rotX =
            (s16)CLAMP(BINANG_SUB(fuseSegmentPtr2->rotX, fuseSegmentPtr->rotX), -8000, 8000) + fuseSegmentPtr->rotX;

        temp_f20 = Math_CosS(fuseSegmentPtr2->rotX) * phi_f22;
        spC0.x = Math_SinS(fuseSegmentPtr2->rotY) * temp_f20;
        spC0.z = Math_CosS(fuseSegmentPtr2->rotY) * temp_f20;
        spC0.y = Math_SinS(fuseSegmentPtr2->rotX) * phi_f22;

        Math_Vec3f_Sum(&fuseSegmentPtr->pos, &spC0, &fuseSegmentPtr2->pos);
        Math_Vec3f_Copy(&sp90, &fuseSegmentPtr2->pos);

        sp90.y += 50.0f;

        temp_f2 = BgCheck_EntityRaycastFloor3(&play->colCtx, &spA0, &bgId, &sp90) - fuseSegmentPtr2->pos.y;
        if (temp_f2 >= 0.0f) {
            spC0.y += temp_f2;
            if (phi_f22 < spC0.y) {
                spC0.y = phi_f22;
                temp_f2 = 0.0f;
            } else {
                temp_f2 = sqrtf(SQ(phi_f22) - SQ(spC0.y));
            }

            if (temp_f20 == 0.0f) {
                spC0.x = temp_f2;
            } else {
                temp_f2 /= temp_f20;
                spC0.x *= temp_f2;
                spC0.z *= temp_f2;
            }

            Math_Vec3f_Sum(&fuseSegmentPtr->pos, &spC0, &fuseSegmentPtr2->pos);
            Math_Vec3f_Copy(&fuseSegmentPtr2->velocity, &sKegZeroVec);
        }

        fuseSegmentPtr2->velocity.y += -1.0f;
        if (fuseSegmentPtr2->velocity.y < -10.0f) {
            fuseSegmentPtr2->velocity.y = -10.0f;
        }
    }

    Math_Vec3f_Copy(arg1, &fuseSegmentPtr->pos);
}

// 2S2H's enlarged fuse texture (its 2ship.o2r, part of MM's ResourceManager): MM's own is too small for Fast3D.
static const ALIGN_ASSET(2) char sKegFuseTexLarger[] = "__OTR__overlays/ovl_En_Bom/gPowderKegFuseTexLarger";

// MM EnBom_DrawKeg (2S2H's inline fuse material). The fuse's vertices are MM data handed over raw (OOT's gSPVertex
// would look the path up in OOT's archive); its texture and models resolve in MM's ResourceManager (the bracket).
static void EnBomKeg_DrawFuse(PlayState* play, s32 timer) {
    s32 temp_s5;
    s32 i;
    PowderKegFuseSegment* fuseSegmentPtr = &sPowderKegFuseSegments[0];
    PowderKegFuseSegment* fuseSegmentPtr2;
    Vtx* fuseVtx = (Vtx*)Combo_MmRawResource("__OTR__overlays/ovl_En_Bom/gPowderKegFuseVtx");

    if (fuseVtx == NULL) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    gSPComboRMPush(POLY_OPA_DISP++, "mm");

    Matrix_Translate(fuseSegmentPtr->pos.x, fuseSegmentPtr->pos.y, fuseSegmentPtr->pos.z, MTXMODE_NEW);
    Matrix_RotateZYX(fuseSegmentPtr->rotX, fuseSegmentPtr->rotY, 0, MTXMODE_APPLY);
    Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetTextureLUT(POLY_OPA_DISP++, G_TT_NONE);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BOTH);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_MODULATEI_PRIM2);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPLoadTextureBlock(POLY_OPA_DISP++, sKegFuseTexLarger, G_IM_FMT_I, G_IM_SIZ_8b, 8, 8, 0, G_TX_NOMIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, 1, 1, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 200, 200, 0, 255);
    gSPVertex(POLY_OPA_DISP++, (uintptr_t)fuseVtx, 4, 0);

    temp_s5 = (timer / 240) + 1;
    fuseSegmentPtr2 = &sPowderKegFuseSegments[1];
    for (i = 1; i < temp_s5; i++, fuseSegmentPtr2++) {
        Matrix_Translate(fuseSegmentPtr2->pos.x, fuseSegmentPtr2->pos.y, fuseSegmentPtr2->pos.z, MTXMODE_NEW);
        Matrix_RotateZYX(fuseSegmentPtr2->rotX, fuseSegmentPtr2->rotY, 0, MTXMODE_APPLY);
        Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);

        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        if ((i % 2) == 0) {
            gSPDisplayList(POLY_OPA_DISP++, MM_OVL_DL("ovl_En_Bom", "gPowderKegFuseModel1DL"));
        } else {
            gSPDisplayList(POLY_OPA_DISP++, MM_OVL_DL("ovl_En_Bom", "gPowderKegFuseModel2DL"));
        }
    }

    gSPComboRMPop(POLY_OPA_DISP++);

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------------------
// The keg (MM EnBom, isPowderKeg).

static void EnBomKeg_Init(EnBom* this, PlayState* play) {
    Actor_ProcessInitChain(&this->actor, sKegInitChain);
    ActorShape_Init(&this->actor.shape, 700.0f, ActorShadow_DrawCircle, 16.0f);
    this->actor.colChkInfo.mass = 200;
    this->actor.colChkInfo.cylRadius = 5;
    this->actor.colChkInfo.cylHeight = 10;

    this->flashSpeedScale = 7;
    this->comboIsKeg = true;
    // (MM also sets play->actorCtx.flags ACTORCTX_FLAG_0 here: read only by Termina's Goron keg sellers.)
    this->timer = gComboPowderKegTimer;

    Collider_InitCylinder(play, &this->bombCollider);
    Collider_InitJntSph(play, &this->explosionCollider);
    Collider_SetCylinder(play, &this->bombCollider, &this->actor, &sKegCylinderInit);
    Collider_SetJntSph(play, &this->explosionCollider, &this->actor, &sKegJntSphInit, this->explosionColliderItems);
    this->bombCollider.dim.radius = 20;
    this->bombCollider.dim.height = 36;
    EnBomKeg_InitFuse(play, &this->actor.world.pos);

    this->explosionColliderItems[0].info.toucher.damage += (this->actor.shape.rot.z & 0xFF00) >> 8;
    // MM: the keg's blast is DMG_EXPLOSIVES | DMG_POWDER_KEG (0x80000008), which MM's tables read at row 31 (Powder
    // Keg), damage 4 — enemies react as their MM damage table says (CrossDamageMap.h).
    CROSS_MM_TAG_TOUCHER(&this->explosionColliderItems[0].info.toucher, CROSS_MM_TAG_POWDER_KEG,
                         this->explosionColliderItems[0].info.toucher.damage);
    this->actor.shape.rot.z &= 0xFF;
    if (this->actor.shape.rot.z & 0x80) {
        this->actor.shape.rot.z |= 0xFF00;
    }

    this->explosionCollider.elements[0].dim.worldSphere.center.x = this->actor.world.pos.x;
    this->explosionCollider.elements[0].dim.worldSphere.center.y = this->actor.world.pos.y;
    this->explosionCollider.elements[0].dim.worldSphere.center.z = this->actor.world.pos.z;

    // (MM sets ACTOR_FLAG_FREEZE_EXCEPTION while held; OOT's Actor_UpdateAll already runs Link's held actor.)

    if (Actor_HasParent(&this->actor, play)) {
        this->actionFunc = EnBomKeg_WaitForRelease;
        this->actor.room = -1;
        Actor_SetScale(&this->actor, KEG_SCALE);
    } else {
        // MM's Actor_SpawnAsChild links the parent before Init runs; OOT's links it after Init returns. So the held /
        // put-down choice MM makes here is made on the keg's first update (EnBomKeg_ResolveParent).
        this->actionFunc = EnBomKeg_ResolveParent;
    }
}

// The rest of MM's EnBom_Init, once OOT's Actor_SpawnAsChild has linked (or not) the parent.
static void EnBomKeg_ResolveParent(EnBom* this, PlayState* play) {
    if (Actor_HasParent(&this->actor, play)) {
        this->actionFunc = EnBomKeg_WaitForRelease;
        this->actor.room = -1;
        Actor_SetScale(&this->actor, KEG_SCALE);
        EnBomKeg_WaitForRelease(this, play);
    } else {
        this->actionFunc = EnBomKeg_Move;
        gComboPowderKegTimer = 0;
        EnBomKeg_Move(this, play);
    }
}

static void EnBomKeg_Destroy(EnBom* this, PlayState* play) {
    Collider_DestroyJntSph(play, &this->explosionCollider);
    Collider_DestroyCylinder(play, &this->bombCollider);
    if (this->comboSfxSlot != 0) {
        Combo_GuestStopSlot(this->comboSfxSlot); // MM's Actor_Delete: AudioSfx_StopByPos(&projectedPos)
    }
}

static void EnBomKeg_Move(EnBom* this, PlayState* play) {
    static Vec3f D_80872E68[] = {
        { 2.0f, -6.0f, -0.3f },
        { 1.5f, -5.0f, -0.6f },
        { 0.2f, -6.0f, -0.1f },
    };

    // if bomb has a parent actor, the bomb hasnt been released yet
    if (Actor_HasParent(&this->actor, play)) {
        this->actionFunc = EnBomKeg_WaitForRelease;
        this->actor.room = -1;
        return;
    }

    if ((this->actor.velocity.y > 0.0f) && (this->actor.bgCheckFlags & BGCHECKFLAG_CEILING)) {
        this->actor.velocity.y = -this->actor.velocity.y;
    }

    // rebound bomb off the wall it hits
    if ((this->actor.speedXZ != 0.0f) && (this->actor.bgCheckFlags & BGCHECKFLAG_WALL)) {
        s16 yDiff = BINANG_SUB(this->actor.wallYaw, this->actor.world.rot.y);

        if (KEG_ABS_ALT(yDiff) > 0x4000) {
            this->actor.world.rot.y =
                BINANG_SUB(this->actor.wallYaw - this->actor.world.rot.y + this->actor.wallYaw, 0x8000);
        }

        Keg_Sfx(this, MM_SE_EV_PUT_DOWN_WOODBOX);
        Actor_MoveXZGravity(&this->actor);
        this->actor.speedXZ *= 0.7f;
        this->actor.bgCheckFlags &= ~BGCHECKFLAG_WALL;
    }

    if (!(this->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        Math_StepToF(&this->actor.speedXZ, 0.0f, 0.08f);
    } else {
        Vec3f* sp58;
        u32 floorType = SurfaceType_GetFloorType(&play->colCtx, this->actor.floorPoly, this->actor.floorBgId);
        Vec3f slopeNormal;
        f32 sp40;
        f32 sp3C;
        f32 sp38;

        sp58 = &D_80872E68[1]; // [isPowderKeg]

        if (floorType == MM_FLOOR_TYPE_ICE) {
            sp58 = &D_80872E68[2];
        }

        if ((floorType == MM_FLOOR_TYPE_SAND) || (floorType == MM_FLOOR_TYPE_SNOW) ||
            (floorType == MM_FLOOR_TYPE_SNOW_2)) {
            s16 sp36;

            Math_ApproachF(&this->actor.shape.yOffset, 0.0f, 0.1f, 50.0f);
            sp36 = this->actor.shape.shadowAlpha;
            Math_ApproachS(&sp36, 0, 1, 20);
            this->actor.shape.shadowAlpha = sp36;
        } else {
            Math_ApproachF(&this->actor.shape.yOffset, 700.0f, 1.0f, 700.0f);
        }

        sp40 = Math_SinS(this->actor.world.rot.y) * this->actor.speedXZ;
        sp3C = Math_CosS(this->actor.world.rot.y) * this->actor.speedXZ;
        // MM Actor_GetSlopeDirection (the yaw it also returns is unused here)
        slopeNormal.x = COLPOLY_GET_NORMAL(this->actor.floorPoly->normal.x);
        slopeNormal.y = COLPOLY_GET_NORMAL(this->actor.floorPoly->normal.y);
        slopeNormal.z = COLPOLY_GET_NORMAL(this->actor.floorPoly->normal.z);

        sp40 += 3.0f * slopeNormal.x;
        sp3C += 3.0f * slopeNormal.z;
        sp38 = sqrtf(SQ(sp40) + SQ(sp3C));

        if ((sp38 < this->actor.speedXZ) || (SurfaceType_GetFloorEffect(&play->colCtx, this->actor.floorPoly,
                                                                        this->actor.floorBgId) == FLOOR_EFFECT_1)) {
            if (sp38 > 16.0f) {
                this->actor.speedXZ = 16.0f;
            } else {
                this->actor.speedXZ = sp38;
            }
            this->actor.world.rot.y = Keg_Atan2S_XY(sp3C, sp40);
        }

        if (!Math_StepToF(&this->actor.speedXZ, 0.0f, sp58->x)) {
            s16 temp = this->actor.world.rot.y;

            if (KEG_ABS_ALT(BINANG_SUB(this->actor.world.rot.y, this->actor.shape.rot.y)) > 0x4000) {
                temp = KEG_BINANG_ROT180(temp);
            }
            Math_ScaledStepToS(&this->actor.shape.rot.y, temp, this->actor.speedXZ * 100.0f);
            this->comboKegRoll += KEG_TRUNCF_BINANG(this->actor.speedXZ * 800.0f);
        }

        if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND_TOUCH) {
            Keg_Sfx(this, MM_SE_EV_TRE_BOX_BOUND);
            if (this->actor.velocity.y < sp58->y) {
                if ((floorType == MM_FLOOR_TYPE_SAND) || (floorType == MM_FLOOR_TYPE_SNOW) ||
                    (floorType == MM_FLOOR_TYPE_SNOW_2)) {
                    this->actor.velocity.y = 0.0f;
                } else {
                    this->actor.velocity.y *= sp58->z;
                }
                this->actor.bgCheckFlags &= ~BGCHECKFLAG_GROUND;
            }
        } else if (this->timer >= 4) {
            Actor_OfferCarry(&this->actor, play);
        }
    }

    Actor_MoveXZGravity(&this->actor);
}

static void EnBomKeg_WaitForRelease(EnBom* this, PlayState* play) {
    // if parent pointer is NULL, the bomb has been released
    if (Actor_HasNoParent(&this->actor, play)) {
        this->actionFunc = EnBomKeg_Move;
        this->actor.room = play->roomCtx.curRoom.num;
        this->actor.bgCheckFlags &= ~BGCHECKFLAG_GROUND;
        Math_Vec3s_ToVec3f(&this->actor.prevPos, &this->actor.home.rot);
        gComboPowderKegTimer = 0;
        EnBomKeg_Move(this, play);
    } else {
        // MM Math_Vec3f_ToVec3s(&home.rot, &parent->world.pos)
        this->actor.home.rot.x = this->actor.parent->world.pos.x;
        this->actor.home.rot.y = this->actor.parent->world.pos.y;
        this->actor.home.rot.z = this->actor.parent->world.pos.z;
        gComboPowderKegTimer = this->timer;
    }
    Math_ScaledStepToS(&this->comboKegRoll, 0, 0x7D0);
}

static void EnBomKeg_Explode(EnBom* this, PlayState* play) {
    static Color_RGBA8 D_80872E90 = { 185, 140, 70, 255 };
    static Color_RGBA8 D_80872E94 = { 255, 255, 255, 255 };
    s32 i;
    u32 floorType;
    f32 temp_f20;
    f32 spCC;
    Vec3f spC0;
    Vec3f spB4;
    CollisionPoly* spB0;
    s32 bgId;
    Vec3f spA0;
    Vec3f sp94;
    Vec3f sp88;
    Color_RGBA8 sp84;
    Color_RGBA8 sp80;

    if (this->explosionCollider.elements[0].dim.modelSphere.radius == 0) {
        this->actor.flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
        Rumble_Request(this->actor.xzDistToPlayer, 255, 20, 150);
    }

    this->explosionCollider.elements[0].dim.worldSphere.radius = KEG_EXPLOSION_RADIUS;
    if (this->timer == 7) {
        this->explosionCollider.base.atFlags &= ~AT_TYPE_ENEMY;
    }

    if (this->actor.params == BOMB_EXPLOSION) {
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->explosionCollider.base);
    }

    if (play->envCtx.adjLight1Color[0] != 0) {
        play->envCtx.adjLight1Color[0] -= 25;
    }

    if (play->envCtx.adjLight1Color[1] != 0) {
        play->envCtx.adjLight1Color[1] -= 25;
    }

    if (play->envCtx.adjLight1Color[2] != 0) {
        play->envCtx.adjLight1Color[2] -= 25;
    }

    if (play->envCtx.adjAmbientColor[0] != 0) {
        play->envCtx.adjAmbientColor[0] -= 25;
    }

    if (play->envCtx.adjAmbientColor[1] != 0) {
        play->envCtx.adjAmbientColor[1] -= 25;
    }

    if (play->envCtx.adjAmbientColor[2] != 0) {
        play->envCtx.adjAmbientColor[2] -= 25;
    }

    if (this->timer == 0) {
        Keg_DropFromPlayer(play, &this->actor);
        Actor_Kill(&this->actor);
    }

    // Dust rings over sand and snow.
    if ((this->timer & 1) == 0) {
        spCC = Rand_ZeroFloat(KEG_PI);

        for (i = 0; i < 15; i++) {
            Matrix_RotateY(((2.0f * (i * KEG_PI)) / 15.0f) + spCC, MTXMODE_NEW);
            Keg_MultVecZ((10 - this->timer) * 300.0f * 0.1f, &spC0);
            spB4.x = this->actor.world.pos.x + spC0.x;
            spB4.y = this->actor.world.pos.y + 500.0f;
            spB4.z = this->actor.world.pos.z + spC0.z;
            if (BgCheck_EntityRaycastFloor3(&play->colCtx, &spB0, &bgId, &spB4) != BGCHECK_Y_MIN) {
                floorType = SurfaceType_GetFloorType(&play->colCtx, spB0, bgId);
                temp_f20 = BgCheck_EntityRaycastFloor1(&play->colCtx, &spB0, &spB4);

                if ((floorType == MM_FLOOR_TYPE_SAND) || (floorType == MM_FLOOR_TYPE_SNOW_2) ||
                    (floorType == MM_FLOOR_TYPE_SNOW)) {
                    if (floorType == MM_FLOOR_TYPE_SAND) {
                        sp84 = D_80872E90;
                        sp80 = D_80872E90;
                    } else {
                        sp84 = D_80872E94;
                        sp80 = D_80872E94;
                    }
                    Keg_MultVecZ(5.0f, &sp94);
                    sp88.x = sp88.z = 0.0f;
                    sp94.y = 2.0f;
                    sp88.y = 0.2f;

                    spA0.x = spB4.x;
                    spA0.y = temp_f20;
                    spA0.z = spB4.z;

                    temp_f20 = Rand_ZeroFloat(250.0f);
                    // MM func_800B0F18 = OOT func_800283D4 (EffectSsDust, draw flag 1, with a life)
                    func_800283D4(play, &spA0, &sp94, &sp88, &sp84, &sp80, temp_f20 + 950.0f, 10,
                                  Rand_ZeroFloat(5.0f) + 14.0f);
                }
            }
        }
    }
}

static void EnBomKeg_Update(EnBom* this, PlayState* play) {
    static Vec3us D_80872ED4 = { 300, 60, 600 }; // [isPowderKeg]
    Vec3f effVelocity = { 0.0f, 0.0f, 0.0f };
    Vec3f effAccel = { 0.0f, 0.0f, 0.0f };
    Vec3f effPos;
    Vec3f dustAccel = { 0.0f, 0.6f, 0.0f };
    Color_RGBA8 dustColor = { 255, 255, 255, 255 };
    Actor* thisx = &this->actor;

    // (MM returns here while PLAYER_STATE1_2, MM's global freeze, and kills the keg under the Giant's Mask; neither
    // exists in Hyrule.)

    if (this->comboSinkTimer != 0) {
        this->comboSinkTimer--;
        Math_ApproachZeroF(&thisx->speedXZ, 1.0f, 1.0f);
        Actor_MoveXZGravity(thisx);
        Actor_UpdateBgCheckInfo(play, thisx, 35.0f, 10.0f, 36.0f, 4);
        if (this->comboSinkTimer == 0) {
            gComboPowderKegTimer = 0;
            Actor_Kill(thisx);
        }
        return;
    }

    thisx->gravity = -1.2f;
    if (this->timer != 0) {
        // A keg's fuse holds during cutscenes.
        if ((EnBomKeg_Explode == this->actionFunc) || !Play_InCsMode(play)) {
            this->timer--;
        }
    }

    if (this->timer <= 2400) {
        Keg_Sfx(this, MM_SE_PL_TAKE_OUT_SHIELD);
        Actor_SetScale(thisx, KEG_SCALE);
    }

    if ((thisx->xzDistToPlayer >= 20.0f) || (fabsf(thisx->yDistToPlayer) >= 80.0f)) {
        this->bumpOn = true;
    }

    this->actionFunc(this, play);

    Actor_UpdateBgCheckInfo(play, thisx, 35.0f, 10.0f, 36.0f, 0x1F);

    if (thisx->params == BOMB_BODY) {
        Vec3us* sp60 = &D_80872ED4;

        // spawn spark effect on even frames
        dustAccel.y = 0.2f;
        Math_Vec3f_Copy(&effPos, &thisx->home.pos); // the fuse's end (EnBom_Draw)
        if ((play->gameplayFrames % 2) == 0) {
            EffectSsGSpk_SpawnFuse(play, thisx, &effPos, &effVelocity, &effAccel);
        }
        // MM Audio_PlaySfx_AtPosWithChannelIO(&projectedPos, NA_SE_IT_BIG_BOMB_IGNIT - SFX_FLAG,
        //     flashSpeedScale 7 -> 0, 3 -> 1, else 2): see the note at the top of EnBom_ComboUpdate.
        Keg_Sfx(this, MM_SE_IT_BIG_BOMB_IGNIT_LOOP);

        effPos.y += 3.0f;
        func_8002829C(play, &effPos, &effVelocity, &dustAccel, &dustColor, &dustColor, 50, 5); // MM func_800B0DE0

        if ((this->bombCollider.base.acFlags & AC_HIT) || ((this->bombCollider.base.ocFlags1 & OC1_HIT) &&
                                                           ((this->bombCollider.base.oc->category == ACTORCAT_ENEMY) ||
                                                            (this->bombCollider.base.oc->category == ACTORCAT_BOSS)))) {
            this->timer = 0;
            thisx->shape.rot.z = 0;
        } else {
            // if a lit stick touches the bomb, set timer to 100
            if ((this->timer > 100) && Player_IsBurningStickInRange(play, &thisx->world.pos, 30.0f, 50.0f)) {
                this->timer = 100;
            }
        }

        dustAccel.y = 0.2f;
        effPos = thisx->world.pos;
        effPos.y += 10.0f;

        // double bomb flash speed and adjust red color at certain times during the countdown
        if ((this->timer == sp60->x) || (this->timer == sp60->y) || (this->timer == 3)) {
            thisx->shape.rot.z = 0;
            this->flashSpeedScale >>= 1;
        }

        if ((this->timer < sp60->z) && (this->timer & (this->flashSpeedScale + 1))) {
            Math_ApproachF(&this->flashIntensity, 140.0f, 1.0f, 140.0f / this->flashSpeedScale);
        } else {
            Math_ApproachZeroF(&this->flashIntensity, 1.0f, 140.0f / this->flashSpeedScale);
        }

        if (this->timer < 3) {
            Actor_SetScale(thisx, thisx->scale.x + 0.002f);
        }

        if (this->timer == 0) {
            effPos = thisx->world.pos;
            effPos.y += 10.0f;
            if (Actor_HasParent(thisx, play)) {
                effPos.y += 30.0f;
            }
            // MM: Actor_Spawn(ACTOR_EN_CLEAR_TAG, ..., CLEAR_TAG_PARAMS(isPowderKeg)) - the large explosion.
            Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, effPos.x, effPos.y - 10.0f, effPos.z, 0, 0, 0,
                        ENBOM_COMBO_CLEARTAG_PARAMS(MM_CLEAR_TAG_LARGE_EXPLOSION));
            Actor_RequestQuakeAndRumble(thisx, play, KEG_QUAKE_Y, KEG_QUAKE_DURATION);
            play->envCtx.adjLight1Color[0] = play->envCtx.adjLight1Color[1] = play->envCtx.adjLight1Color[2] = 250;
            play->envCtx.adjAmbientColor[0] = play->envCtx.adjAmbientColor[1] = play->envCtx.adjAmbientColor[2] = 250;
            Camera_AddQuake(&play->mainCamera, 2, 11, 8);
            thisx->params = BOMB_EXPLOSION;
            this->timer = 10;
            thisx->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
            this->actionFunc = EnBomKeg_Explode;
            gComboPowderKegTimer = 0;
            Keg_Sfx(this, MM_SE_IT_BIG_BOMB_EXPLOSION);
            if (Combo_DebugEnv("COMBO_DEBUG_ITEMS")) {
                Combo_DebugLog("[ComboShip] MM keg exploded at %.0f %.0f %.0f\n", thisx->world.pos.x,
                               thisx->world.pos.y, thisx->world.pos.z);
            }
        }
    }

    Actor_SetFocus(thisx, 20.0f);

    if (thisx->params <= BOMB_BODY) {
        Collider_UpdateCylinder(thisx, &this->bombCollider);

        // if link is not holding the bomb anymore and bump conditions are met, subscribe to OC
        if (!Actor_HasParent(thisx, play) && (this->bumpOn != 0)) {
            CollisionCheck_SetOC(play, &play->colChkCtx, &this->bombCollider.base);
        }
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->bombCollider.base);
    }

    if ((KEG_SCALE <= thisx->scale.x) && (thisx->params != BOMB_EXPLOSION)) {
        if (thisx->yDistToWater >= 20.0f) {
            Vec3f effPos;

            effPos.x = thisx->world.pos.x;
            effPos.y = thisx->world.pos.y + thisx->yDistToWater;
            effPos.z = thisx->world.pos.z;
            EffectSsGRipple_Spawn(play, &effPos, 70, 500, 0);
            EffectSsGRipple_Spawn(play, &effPos, 70, 500, 10);
            effPos.y += 10.0f;
            EffectSsGSplash_Spawn(play, &effPos, NULL, NULL, 1, 500);
            // MM: Actor_Spawn(ACTOR_EN_CLEAR_TAG, ..., rot.z 1, CLEAR_TAG_PARAMS(CLEAR_TAG_SMOKE))
            Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, effPos.x, effPos.y, effPos.z, 0, 0, 1,
                        ENBOM_COMBO_CLEARTAG_PARAMS(MM_CLEAR_TAG_SMOKE));
            // MM SoundSource_PlaySfxAtFixedWorldPos(play, &world.pos, 30, NA_SE_IT_BOMB_UNEXPLOSION)
            Combo_GuestPlaySfxAtWorld(play, MM_SE_IT_BOMB_UNEXPLOSION, &thisx->world.pos.x);
            this->flashIntensity = 0.0f;
            thisx->velocity.y = (KREG(83) * 0.1f) + -2.0f;
            thisx->gravity = (KREG(84) * 0.1f) + -0.5f;
            this->comboSinkTimer = KREG(81) + 10;
        } else if (thisx->bgCheckFlags & BGCHECKFLAG_WATER_TOUCH) {
            thisx->bgCheckFlags &= ~BGCHECKFLAG_WATER_TOUCH;
            Keg_Sfx(this, MM_SE_EV_BOMB_DROP_WATER);
        }
    }
}

static Vec3f D_80872EEC = { -750.0f, 0.0f, 0.0f };
static Vec3f D_80872EF8 = { -800.0f, 0.0f, 0.0f };
static Vec3f D_80872F04 = { 0.0f, 0.0f, 0.0f };

static void EnBomKeg_Draw(EnBom* this, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    if (this->actor.params == BOMB_BODY) {
        Vec3f sp58;
        Vec3f sp4C;

        Gfx_SetupDL_25Opa(play->state.gfxCtx);

        Collider_UpdateSpheres(0, &this->explosionCollider);

        if (this->comboKegRoll != 0) {
            s16 sp4A = this->actor.world.rot.y - this->actor.shape.rot.y;
            f32 sp44 = (1000.0f / Math_CosS(KEG_ABS_ALT((s16)(this->comboKegRoll % 10922)) - 0x1555)) + -1000.0f;

            Matrix_RotateZYX(0, sp4A, 0, MTXMODE_APPLY); // MM Matrix_RotateYS
            Matrix_Translate(0.0f, sp44, 0.0f, MTXMODE_APPLY);
            Matrix_RotateZYX(this->comboKegRoll, 0, 0, MTXMODE_APPLY); // MM Matrix_RotateXS
            Matrix_RotateZYX(0, -sp4A, 0, MTXMODE_APPLY);
        }

        Matrix_MultVec3f(&D_80872EEC, &this->actor.home.pos);
        Matrix_MultVec3f(&D_80872EF8, &sp58);
        Matrix_MultVec3f(&D_80872F04, &sp4C);

        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, MM_OVL_DL("ovl_En_Bom", "gPowderKegBarrelDL"));
        gSPDisplayList(POLY_OPA_DISP++, MM_OVL_DL("ovl_En_Bom", "gPowderKegGoronSkullDL"));

        EnBomKeg_UpdateFuse(play, &this->actor.home.pos, &sp58, &sp4C, this->timer);
        EnBomKeg_DrawFuse(play, this->timer);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------------------
// The explosion (MM EnClearTag, the parts the keg reaches: the large explosion and the isolated smoke).

typedef enum {
    /* 0x00 */ CLEAR_TAG_EFFECT_AVAILABLE,
    /* 0x01 */ CLEAR_TAG_EFFECT_DEBRIS,
    /* 0x02 */ CLEAR_TAG_EFFECT_FIRE, // never set to, remnant of OoT
    /* 0x03 */ CLEAR_TAG_EFFECT_SMOKE,
    /* 0x04 */ CLEAR_TAG_EFFECT_FLASH,
    /* 0x05 */ CLEAR_TAG_EFFECT_LIGHT_RAYS,
    /* 0x06 */ CLEAR_TAG_EFFECT_SHOCKWAVE,
    /* 0x07 */ CLEAR_TAG_EFFECT_SPLASH,
    /* 0x08 */ CLEAR_TAG_EFFECT_ISOLATED_SMOKE
} KegClearTagEffectType;

typedef struct {
    /* 0x00 */ u8 type;
    /* 0x01 */ u8 actionTimer;
    /* 0x04 */ Vec3f pos;
    /* 0x10 */ Vec3f velocity;
    /* 0x1C */ Vec3f accel;
    /* 0x28 */ Color_RGBAf primColor;
    /* 0x38 */ Color_RGBAf envColor;
    /* 0x48 */ s16 bounces;
    /* 0x4A */ s16 effectsTimer;
    /* 0x4C */ s16 lightRayAlpha;
    /* 0x4E */ s16 lightRayAlphaDecrementSpeed;
    /* 0x50 */ f32 scale;
    /* 0x54 */ f32 maxScale;
    /* 0x58 */ f32 rotationY;
    /* 0x5C */ f32 rotationX;
    /* 0x60 */ f32 rotationZ;
    /* 0x64 */ f32 maxScaleTarget;
    /* 0x68 */ f32 smokeScaleY;
    /* 0x6C */ f32 smokeScaleX;
} KegClearTagEffect; // MM EnClearTagEffect, size = 0x70

#define KEG_CLEAR_TAG_EFFECT_COUNT 103

struct EnBomComboClearTag {
    KegClearTagEffect effect[KEG_CLEAR_TAG_EFFECT_COUNT];
    s16 activeTimer; // Actor Marked for Death when timer runs out
    Color_RGBA8 flashEnvColor;
};

static f32 sFlashMaxScale[] = { 6.0f, 12.0f, 9.0f };
static f32 sSmokeScale[] = { 3.0f, 6.0f };
static f32 sShockwaveMaxScale[] = { 15.0f, 30.0f, 20.0f };
static f32 sDebrisScale[] = { 0.03f, 0.06f, 0.04f };
static f32 sLightRayScale[] = { 1000.0f, 2000.0f, 1500.0f, 800.0f, 1300.0f };
static f32 sLightRayMaxScaleTarget[] = { 15.0f, 30.0f, 20.0f, 10.0f, 20.0f };
static f32 sLightRayMaxScale[] = { 25.0f, 100.0f, 48.0f, 20.0f, 32.0f };

static const ALIGN_ASSET(2) char sWaterSplash1Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash1Tex";
static const ALIGN_ASSET(2) char sWaterSplash2Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash2Tex";
static const ALIGN_ASSET(2) char sWaterSplash3Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash3Tex";
static const ALIGN_ASSET(2) char sWaterSplash4Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash4Tex";
static const ALIGN_ASSET(2) char sWaterSplash5Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash5Tex";
static const ALIGN_ASSET(2) char sWaterSplash6Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash6Tex";
static const ALIGN_ASSET(2) char sWaterSplash7Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash7Tex";
static const ALIGN_ASSET(2) char sWaterSplash8Tex[] = "__OTR__objects/gameplay_keep/gEffWaterSplash8Tex";

static const char* sWaterSplashTextures[] = {
    sWaterSplash1Tex,
    sWaterSplash2Tex,
    sWaterSplash3Tex,
    sWaterSplash4Tex,
    sWaterSplash5Tex,
    sWaterSplash6Tex,
    sWaterSplash7Tex,
    sWaterSplash8Tex,
    NULL,
    NULL,
    NULL,
};

static void ClearTag_CreateDebrisEffect(struct EnBomComboClearTag* this, Vec3f* pos, Vec3f* velocity, Vec3f* accel,
                                        f32 scale, f32 rotationZ) {
    KegClearTagEffect* effect = this->effect;
    s16 i;

    for (i = 0; i < ARRAY_COUNT(this->effect) - 1; i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->type = CLEAR_TAG_EFFECT_DEBRIS;

            effect->pos = *pos;
            effect->velocity = *velocity;
            effect->accel = *accel;

            effect->scale = scale;

            effect->rotationY = Rand_ZeroFloat(KEG_PI * 2);
            effect->rotationX = Rand_ZeroFloat(KEG_PI * 2);

            effect->effectsTimer = effect->bounces = 0;

            effect->rotationZ = rotationZ;

            effect->actionTimer = Rand_ZeroFloat(10.0f);

            break;
        }
    }
}

static void ClearTag_CreateSmokeEffect(struct EnBomComboClearTag* this, Vec3f* pos, f32 scale) {
    s16 i;
    KegClearTagEffect* effect = this->effect;

    for (i = 0; i < ARRAY_COUNT(this->effect); i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->actionTimer = Rand_ZeroFloat(100.0f);
            effect->type = CLEAR_TAG_EFFECT_SMOKE;

            effect->pos = *pos;
            effect->velocity = sKegZeroVec;
            effect->accel = sKegZeroVec;

            effect->scale = scale;
            effect->maxScale = 2.0f * scale;
            effect->smokeScaleX = 1.0f;
            effect->smokeScaleY = 1.0f;

            effect->primColor.a = 255.0f;
            effect->envColor.r = 255.0f;
            effect->envColor.b = 255.0f;
            effect->primColor.r = 200.0f;
            effect->primColor.g = 20.0f;
            effect->primColor.b = 0.0f;
            effect->envColor.g = 215.0f;

            break;
        }
    }
}

static void ClearTag_CreateIsolatedSmokeEffect(struct EnBomComboClearTag* this, Vec3f* pos, f32 scale) {
    s16 i;
    KegClearTagEffect* effect = this->effect;

    for (i = 0; i < ARRAY_COUNT(this->effect); i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->actionTimer = Rand_ZeroFloat(100.0f);
            effect->type = CLEAR_TAG_EFFECT_ISOLATED_SMOKE;

            effect->pos = *pos;
            effect->velocity = sKegZeroVec;
            effect->accel = sKegZeroVec;

            effect->scale = scale;
            effect->maxScale = 2.0f * scale;
            effect->smokeScaleX = 1.0f;
            effect->smokeScaleY = 1.0f;

            if (scale <= 1.1f) {
                effect->scale = ((KREG(23) * 0.1f) + 1.0f) * scale;
                effect->primColor.a = KREG(16) + 150.0f;
                effect->primColor.r = KREG(17);
                effect->primColor.g = KREG(18);
                effect->primColor.b = KREG(19);
                effect->envColor.r = KREG(20);
                effect->envColor.g = KREG(21);
                effect->envColor.b = KREG(22);
            } else {
                effect->envColor.r = effect->envColor.g = effect->envColor.b = 0.0f;
                effect->primColor.r = effect->primColor.g = effect->primColor.b = 0.0f;
                effect->primColor.a = 255.0f;
            }

            break;
        }
    }
}

static void ClearTag_CreateFlashEffect(struct EnBomComboClearTag* this, Vec3f* pos, f32 scale, f32 floorHeight) {
    KegClearTagEffect* effect = this->effect;
    s16 i;

    for (i = 0; i < ARRAY_COUNT(this->effect) - 1; i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->type = CLEAR_TAG_EFFECT_FLASH;

            effect->pos = *pos;
            effect->velocity = sKegZeroVec;
            effect->accel = sKegZeroVec;

            effect->scale = 0.0f;
            effect->maxScale = scale * 3.0f;

            // rotationZ is unused for CLEAR_TAG_EFFECT_FLASH
            effect->rotationZ = floorHeight;

            effect->primColor.a = 255.0f;

            break;
        }
    }
}

static void ClearTag_CreateLightRayEffect(struct EnBomComboClearTag* this, Vec3f* pos, Vec3f* velocity, Vec3f* accel,
                                          f32 scale, f32 maxScaleTarget, s16 alphaDecrementSpeed) {
    KegClearTagEffect* effect = this->effect;
    s16 i;

    for (i = 0; i < ARRAY_COUNT(this->effect) - 1; i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->type = CLEAR_TAG_EFFECT_LIGHT_RAYS;

            effect->pos = *pos;
            effect->velocity = *velocity;
            effect->accel = *accel;

            effect->scale = scale / 1000.0f;
            effect->maxScale = 1.0f;
            effect->maxScaleTarget = maxScaleTarget;

            effect->lightRayAlpha = Rand_ZeroFloat(100.0f) + 200.0f;
            effect->lightRayAlphaDecrementSpeed = alphaDecrementSpeed;

            effect->actionTimer = Rand_ZeroFloat(10.0f);

            effect->rotationY = Keg_Atan2F_XY(effect->velocity.z, effect->velocity.x);
            effect->rotationX = -Keg_Atan2F_XY(sqrtf(SQXZ(effect->velocity)), effect->velocity.y);

            effect->envColor.r = 255.0f;
            effect->envColor.g = 255.0f;
            effect->envColor.b = 0.0f;
            effect->primColor.r = 255.0f;
            effect->primColor.g = 255.0f;
            effect->primColor.b = 255.0f;

            break;
        }
    }
}

static void ClearTag_CreateShockwaveEffect(struct EnBomComboClearTag* this, Vec3f* pos, f32 maxScale, s16 actionTimer) {
    KegClearTagEffect* effect = this->effect;
    s16 i;

    for (i = 0; i < ARRAY_COUNT(this->effect) - 1; i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->type = CLEAR_TAG_EFFECT_SHOCKWAVE;

            effect->pos = *pos;
            effect->velocity = sKegZeroVec;
            effect->accel = sKegZeroVec;

            effect->maxScale = maxScale;
            effect->scale = 0.0f;

            effect->actionTimer = actionTimer;

            effect->primColor.a = 200.0f;

            break;
        }
    }
}

static void ClearTag_CreateSplashEffect(struct EnBomComboClearTag* this, Vec3f* pos, s16 effectsTimer) {
    s16 i;
    KegClearTagEffect* effect = this->effect;

    for (i = 0; i < ARRAY_COUNT(this->effect) - 1; i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->actionTimer = Rand_ZeroFloat(100.0f);
            effect->type = CLEAR_TAG_EFFECT_SPLASH;

            effect->pos = *pos;
            effect->velocity = sKegZeroVec;
            effect->accel = sKegZeroVec;

            effect->scale = 0.0f;
            effect->maxScale = 0.0f;

            effect->actionTimer = 0;
            effect->effectsTimer = effectsTimer;

            effect->rotationX = 0.78f;

            break;
        }
    }
}

// MM EnClearTag_Init (params >= 0; isolated light rays 3/4 are not reachable from the keg and not ported).
static void EnBomClearTag_Init(EnBom* this, PlayState* play) {
    Actor* thisx = &this->actor;
    struct EnBomComboClearTag* tag;
    s32 type = thisx->params & 0xFF;
    f32 lightRayMaxScale;
    u8 i;
    Vec3f pos;
    Vec3f velocity;
    Vec3f accel;

    // MM's EnClearTag is ACTORCAT_ITEMACTION: the effects are not an explosive (bomb limits, Dodongos...).
    Actor_ChangeCategory(play, &play->actorCtx, thisx, ACTORCAT_ITEMACTION);

    tag = ZeldaArena_Malloc(sizeof(struct EnBomComboClearTag));
    if (tag == NULL) {
        Actor_Kill(thisx);
        return;
    }
    memset(tag, 0, sizeof(struct EnBomComboClearTag));
    this->comboClearTag = tag;

    tag->activeTimer = 70;
    Math_Vec3f_Copy(&pos, &thisx->world.pos);

    // Initialize isolated smoke effect
    if (type == MM_CLEAR_TAG_SMOKE) {
        ClearTag_CreateIsolatedSmokeEffect(tag, &pos, thisx->world.rot.z);
        return;
    }

    if (type != MM_CLEAR_TAG_SPLASH) {
        if ((type != MM_CLEAR_TAG_SMALL_EXPLOSION) && (type != MM_CLEAR_TAG_LARGE_EXPLOSION) &&
            (type != MM_CLEAR_TAG_POP)) {
            Actor_Kill(thisx);
            return;
        }

        if ((thisx->world.rot.x != 0) || (thisx->world.rot.y != 0) || (thisx->world.rot.z != 0)) {
            tag->flashEnvColor.r = thisx->world.rot.x;
            tag->flashEnvColor.g = thisx->world.rot.y;
            tag->flashEnvColor.b = thisx->world.rot.z;
        } else {
            tag->flashEnvColor.r = 255;
            tag->flashEnvColor.g = 0;
        }

        // Initialize flash effect
        Actor_UpdateBgCheckInfo(play, thisx, 50.0f, 30.0f, 100.0f, 4);
        pos = thisx->world.pos;
        ClearTag_CreateFlashEffect(tag, &pos, sFlashMaxScale[type], thisx->floorHeight);

        // Is not underwater
        if (!(thisx->bgCheckFlags & BGCHECKFLAG_WATER)) {
            if (type < 10) {
                pos.y = thisx->world.pos.y - 40.0f;

                // Initialize smoke effect
                if (type != MM_CLEAR_TAG_POP) {
                    ClearTag_CreateSmokeEffect(tag, &pos, sSmokeScale[type]);
                }

                // Initialize shockwave effect
                pos.y = thisx->floorHeight + 3.0f;
                ClearTag_CreateShockwaveEffect(tag, &pos, sShockwaveMaxScale[type], 0);
                ClearTag_CreateShockwaveEffect(tag, &pos, sShockwaveMaxScale[type], 3);
                if (type == MM_CLEAR_TAG_LARGE_EXPLOSION) {
                    ClearTag_CreateShockwaveEffect(tag, &pos, sShockwaveMaxScale[type], 6);
                }
            }

            // Initialize debris effect
            if (type != MM_CLEAR_TAG_POP) {
                pos.y = thisx->world.pos.y;
                for (i = 0; i < 18; i++) {
                    velocity.x = sinf(i * (33.0f / 40.0f)) * i * .5f;
                    velocity.z = cosf(i * (33.0f / 40.0f)) * i * .5f;
                    velocity.y = Rand_ZeroFloat(8.0f) + 7.0f;

                    velocity.x += Rand_CenteredFloat(0.5f);
                    velocity.z += Rand_CenteredFloat(0.5f);

                    accel.x = 0.0f;
                    accel.y = -1.0f;
                    accel.z = 0.0f;
                    ClearTag_CreateDebrisEffect(tag, &pos, &velocity, &accel,
                                                sDebrisScale[type] + Rand_ZeroFloat(sDebrisScale[type]),
                                                thisx->floorHeight);
                }
            }
        }

        // Initialize light ray effect
        pos = thisx->world.pos;
        for (i = 0; i < 44; i++) {
            lightRayMaxScale = sLightRayMaxScale[type] + Rand_ZeroFloat(sLightRayMaxScale[type]);
            Matrix_RotateY(Rand_ZeroFloat(2 * KEG_PI), MTXMODE_NEW);
            Matrix_RotateX(Rand_ZeroFloat(2 * KEG_PI), MTXMODE_APPLY);
            Keg_MultVecZ(lightRayMaxScale, &velocity);
            accel.x = velocity.x * -0.03f;
            accel.y = velocity.y * -0.03f;
            accel.z = velocity.z * -0.03f;
            ClearTag_CreateLightRayEffect(tag, &pos, &velocity, &accel,
                                          sLightRayScale[type] + Rand_ZeroFloat(sLightRayScale[type] * 0.5f),
                                          sLightRayMaxScaleTarget[type], Rand_ZeroFloat(10.0f) + 20.0f);
        }
    }

    // Initialize splash effect
    ClearTag_CreateSplashEffect(tag, &pos, 0);
    ClearTag_CreateSplashEffect(tag, &pos, 2);
}

static void EnBomClearTag_Destroy(EnBom* this, PlayState* play) {
    if (this->comboClearTag != NULL) {
        ZeldaArena_Free(this->comboClearTag);
        this->comboClearTag = NULL;
    }
}

// MM EnClearTag_UpdateEffects
static void ClearTag_UpdateEffects(struct EnBomComboClearTag* this, PlayState* play) {
    KegClearTagEffect* effect = this->effect;
    s16 i;
    f32 originalYPosition;
    Vec3f sphereCenter;

    for (i = 0; i < ARRAY_COUNT(this->effect); i++, effect++) {
        if (effect->type != CLEAR_TAG_EFFECT_AVAILABLE) {
            effect->actionTimer++;

            // Perform effect physics.
            effect->pos.x += effect->velocity.x;
            originalYPosition = effect->pos.y;
            effect->pos.y += effect->velocity.y;
            effect->pos.z += effect->velocity.z;
            effect->velocity.x += effect->accel.x;
            effect->velocity.y += effect->accel.y;
            effect->velocity.z += effect->accel.z;

            if (effect->type == CLEAR_TAG_EFFECT_DEBRIS) {
                // Clamp the velocity to -5.0
                if (effect->velocity.y < -5.0f) {
                    effect->velocity.y = -5.0f;
                }

                // While the effect is falling check if it has hit the ground.
                if (effect->velocity.y < 0.0f) {
                    sphereCenter = effect->pos;
                    sphereCenter.y += 5.0f;

                    // Check if the debris has hit the ground.
                    if (BgCheck_SphVsFirstPoly(&play->colCtx, &sphereCenter, 11.0f)) {
                        effect->pos.y = originalYPosition;

                        // Bounce the debris effect.
                        if (effect->bounces <= 0) {
                            effect->bounces++;
                            effect->velocity.y *= -0.5f;
                            effect->effectsTimer = Rand_ZeroFloat(20.0f) + 25.0f;
                        } else {
                            // The Debris effect is done bouncing. Set its velocity and acceleration to 0.
                            effect->velocity.x = effect->velocity.z = effect->accel.y = effect->velocity.y = 0.0f;
                        }
                    }
                }

                // Rotate the debris effect.
                if (effect->accel.y != 0.0f) {
                    effect->rotationY += 0.5f;
                    effect->rotationX += 0.35f;
                }

                if (effect->effectsTimer == 1) {
                    effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                }
            } else if (effect->type == CLEAR_TAG_EFFECT_FIRE) {
                // Fade the fire effect.
                Math_ApproachZeroF(&effect->primColor.a, 1.0f, 15.0f);
                if (effect->primColor.a <= 0.0f) {
                    effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                }
            } else if (effect->type == CLEAR_TAG_EFFECT_SHOCKWAVE) {
                if (effect->actionTimer > 3) {
                    Math_ApproachF(&effect->scale, effect->maxScale, 0.2f, effect->maxScale * 0.6666666f);
                    Math_ApproachZeroF(&effect->primColor.a, 1.0f, 15.0f);
                    if (effect->primColor.a <= 0.0f) {
                        effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                    }
                }
            } else if (effect->type == CLEAR_TAG_EFFECT_SMOKE) {
                // Fade the smoke effects.
                Math_ApproachZeroF(&effect->primColor.r, 1.0f, 20.0f);
                Math_ApproachZeroF(&effect->primColor.g, 1.0f, 2.0f);
                Math_ApproachZeroF(&effect->envColor.r, 1.0f, 25.5f);
                Math_ApproachZeroF(&effect->envColor.g, 1.0f, 21.5f);
                Math_ApproachZeroF(&effect->envColor.b, 1.0f, 25.5f);

                // Smooth scale the smoke effects.
                Math_ApproachF(&effect->scale, effect->maxScale, 0.05f, 0.1f);

                if (effect->primColor.r == 0.0f) {
                    Math_ApproachF(&effect->smokeScaleX, 3.0f, 0.1f, 0.01f);
                    Math_ApproachF(&effect->smokeScaleY, 3.0f, 0.1f, 0.02f);

                    // Fade the smoke effects.
                    Math_ApproachZeroF(&effect->primColor.a, 1.0f, 5.f);

                    // If the smoke effect has fully faded, unload it.
                    if (effect->primColor.a <= 0.0f) {
                        effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                    }
                }
            } else if (effect->type == CLEAR_TAG_EFFECT_ISOLATED_SMOKE) {
                // Smooth scale the isolated smoke effects.
                Math_ApproachF(&effect->scale, effect->maxScale, 0.05f, 0.1f);
                if (effect->actionTimer > 10) {
                    Math_ApproachF(&effect->smokeScaleX, 3.0f, 0.1f, 0.01f);
                    Math_ApproachF(&effect->smokeScaleY, 3.0f, 0.1f, 0.02f);

                    // Fade the smoke effects.
                    Math_ApproachZeroF(&effect->primColor.a, 1.0f, 5.f);
                    if (effect->primColor.a <= 0.0f) {
                        effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                    }
                }
            } else if (effect->type == CLEAR_TAG_EFFECT_FLASH) {
                // Smooth scale the flash effects.
                Math_ApproachF(&effect->scale, effect->maxScale, 0.5f, 6.0f);

                // Fade the flash effects.
                Math_ApproachZeroF(&effect->primColor.a, 1.0f, 15.0f);
                if (effect->primColor.a <= 0.0f) {
                    effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                }
            } else if (effect->type == CLEAR_TAG_EFFECT_LIGHT_RAYS) {
                // Rotate the light ray effect.
                effect->rotationZ += Rand_ZeroFloat(KEG_PI / 2) + (KEG_PI / 2);

                // Fade the light ray effects.
                effect->lightRayAlpha -= effect->lightRayAlphaDecrementSpeed;
                if (effect->lightRayAlpha <= 0) {
                    effect->lightRayAlpha = 0;
                    effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                }
                effect->primColor.a = effect->lightRayAlpha;
                if (effect->primColor.a > 255.0f) {
                    effect->primColor.a = 255.0f;
                }

                // Smooth scale the light ray effects.
                Math_ApproachF(&effect->maxScale, effect->maxScaleTarget, 1.0f,
                               (effect->maxScaleTarget / 15.0f) * 4.0f);
            } else if (effect->type == CLEAR_TAG_EFFECT_SPLASH) {
                if (effect->effectsTimer == 0) {
                    effect->scale = 7.0f;

                    // Smooth scale the splash effects.
                    Math_ApproachF(&effect->maxScale, 500.0f, 1.0f, 50.0f);
                    Math_ApproachF(&effect->rotationX, 1.5f, 1.0f, 0.12f);
                    if (effect->actionTimer > 7) {
                        effect->type = CLEAR_TAG_EFFECT_AVAILABLE;
                    }
                } else {
                    effect->actionTimer = 0;
                }
            }

            if (effect->effectsTimer != 0) {
                effect->effectsTimer--;
            }
        }
    }
}

// MM EnClearTag_Update (params >= 0)
static void EnBomClearTag_Update(EnBom* this, PlayState* play) {
    struct EnBomComboClearTag* tag = this->comboClearTag;

    if (tag == NULL) {
        Actor_Kill(&this->actor);
        return;
    }
    if (tag->activeTimer != 0) {
        tag->activeTimer--;
    }
    if (tag->activeTimer != 0) {
        ClearTag_UpdateEffects(tag, play);
    } else {
        Actor_Kill(&this->actor);
    }
}

// MM EnClearTag_DrawEffects. Every model and texture is MM's (gameplay_keep and ovl_En_Clear_Tag, routed to MM's
// ResourceManager; the splash textures bound inside the "mm" bracket).
static void EnBomClearTag_Draw(EnBom* this, PlayState* play) {
    u8 isMaterialApplied = false;
    s16 i;
    s16 j;
    Vec3f vec;
    WaterBox* waterBox;
    f32 ySurface;
    MtxF mtxF;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    struct EnBomComboClearTag* tag = this->comboClearTag;
    KegClearTagEffect* effect;
    KegClearTagEffect* firstEffect;

    if (tag == NULL) {
        return;
    }
    effect = tag->effect;
    firstEffect = tag->effect;

    OPEN_DISPS(gfxCtx);

    gSPComboRMPush(POLY_OPA_DISP++, "mm");
    gSPComboRMPush(POLY_XLU_DISP++, "mm");

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    // Draw all Debris effects.
    for (i = 0; i < ARRAY_COUNT(tag->effect); i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_DEBRIS) {
            FrameInterpolation_RecordOpenChild(effect, effect->type);
            // Apply the debris effect material if it has not already been applied.
            if (!isMaterialApplied) {
                isMaterialApplied++;
                gSPDisplayList(POLY_OPA_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagDebrisEffectMaterialDL"));
            }

            // Draw the debris effect.
            Matrix_Translate(effect->pos.x, effect->pos.y, effect->pos.z, MTXMODE_NEW);
            Matrix_Scale(effect->scale, effect->scale, effect->scale, MTXMODE_APPLY);
            Matrix_RotateY(effect->rotationY, MTXMODE_APPLY);
            Matrix_RotateX(effect->rotationX, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagDebrisEffectDL"));
            FrameInterpolation_RecordCloseChild();
        }
    }

    // Draw all Shockwave effects.
    effect = firstEffect;
    if (this->actor.floorPoly != NULL) {
        for (i = 0; i < ARRAY_COUNT(tag->effect); i++, effect++) {
            if (effect->type == CLEAR_TAG_EFFECT_SHOCKWAVE) {
                FrameInterpolation_RecordOpenChild(effect, effect->type);
                // Draw the shockwave effect.
                gDPPipeSync(POLY_XLU_DISP++);
                gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 255, (s8)effect->primColor.a);
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, (s8)effect->primColor.a);
                Keg_PolyMatrix(this->actor.floorPoly, effect->pos.x, effect->pos.y, effect->pos.z, &mtxF);
                Matrix_Put(&mtxF);
                Matrix_Scale(effect->scale, 1.0f, effect->scale, MTXMODE_APPLY);
                gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPDisplayList(POLY_XLU_DISP++, MM_KEEP_DL("gEffShockwaveDL"));
                FrameInterpolation_RecordCloseChild();
            }
        }
    }

    // Draw all ground flash effects.
    effect = firstEffect;
    isMaterialApplied = false;
    if (this->actor.floorPoly != NULL) {
        for (i = 0; i < ARRAY_COUNT(tag->effect); i++, effect++) {
            if (effect->type == CLEAR_TAG_EFFECT_FLASH) {
                FrameInterpolation_RecordOpenChild(effect, effect->type);
                // Apply the flash ground effect material if it has not already been applied.
                if (!isMaterialApplied) {
                    gDPPipeSync(POLY_XLU_DISP++);
                    gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 200, 0);
                    isMaterialApplied++;
                }

                // Draw the ground flash effect.
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 200, (s8)(effect->primColor.a * 0.7f));
                Keg_PolyMatrix(this->actor.floorPoly, effect->pos.x, this->actor.floorHeight, effect->pos.z, &mtxF);
                Matrix_Put(&mtxF);
                Matrix_Scale(effect->scale * 3.0f, 1.0f, effect->scale * 3.0f, MTXMODE_APPLY);
                gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPDisplayList(POLY_XLU_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagFlashEffectGroundDL"));
                FrameInterpolation_RecordCloseChild();
            }
        }
    }

    // Draw all smoke effects.
    effect = firstEffect;
    isMaterialApplied = false;
    for (i = 0; i < ARRAY_COUNT(tag->effect); i++, effect++) {
        if ((effect->type == CLEAR_TAG_EFFECT_SMOKE) || (effect->type == CLEAR_TAG_EFFECT_ISOLATED_SMOKE)) {
            FrameInterpolation_RecordOpenChild(effect, effect->type);
            // Apply the smoke effect material if it has not already been applied.
            if (!isMaterialApplied) {
                gSPDisplayList(POLY_XLU_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagFireEffectMaterialDL"));
                isMaterialApplied++;
            }

            // Draw the smoke effect.
            gDPPipeSync(POLY_XLU_DISP++);
            gDPSetEnvColor(POLY_XLU_DISP++, (s8)effect->envColor.r, (s8)effect->envColor.g, (s8)effect->envColor.b,
                           128);
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, (s8)effect->primColor.r, (s8)effect->primColor.g,
                            (s8)effect->primColor.b, (s8)effect->primColor.a);
            gSPSegment(POLY_XLU_DISP++, 0x08,
                       (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, -effect->actionTimer * 5, 32, 64, 1, 0,
                                                     0, 32, 32, 0, -5, 0, 0));
            Matrix_Translate(effect->pos.x, effect->pos.y, effect->pos.z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(effect->smokeScaleX * effect->scale, effect->smokeScaleY * effect->scale, 1.0f, MTXMODE_APPLY);
            Matrix_Translate(0.0f, 20.0f, 0.0f, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagFireEffectDL"));
            FrameInterpolation_RecordCloseChild();
        }
    }

    // (MM's fire effects: never created.)

    // Draw all flash billboard effects.
    effect = firstEffect;
    isMaterialApplied = false;
    for (i = 0; i < ARRAY_COUNT(tag->effect); i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_FLASH) {
            FrameInterpolation_RecordOpenChild(effect, effect->type);
            // Apply the flash billboard effect material if it has not already been applied.
            if (!isMaterialApplied) {
                gDPPipeSync(POLY_XLU_DISP++);
                gDPSetEnvColor(POLY_XLU_DISP++, tag->flashEnvColor.r, tag->flashEnvColor.g, tag->flashEnvColor.b, 0);
                isMaterialApplied++;
            }

            // Draw the flash billboard effect.
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 200, (s8)effect->primColor.a);
            Matrix_Translate(effect->pos.x, effect->pos.y, effect->pos.z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(2.0f * effect->scale, 2.0f * effect->scale, 1.0f, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagFlashEffectDL"));
            FrameInterpolation_RecordCloseChild();
        }
    }

    // Draw all light ray effects.
    effect = firstEffect;
    isMaterialApplied = false;
    for (i = 0; i < ARRAY_COUNT(tag->effect); i++, effect++) {
        if (effect->type == CLEAR_TAG_EFFECT_LIGHT_RAYS) {
            FrameInterpolation_RecordOpenChild(effect, effect->type);
            // Apply the light ray effect material if it has not already been applied.
            if (!isMaterialApplied) {
                gDPPipeSync(POLY_XLU_DISP++);
                gDPSetEnvColor(POLY_XLU_DISP++, (u8)effect->envColor.r, (u8)effect->envColor.g, (u8)effect->envColor.b,
                               0);
                gSPDisplayList(POLY_XLU_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagLightRayEffectMaterialDL"));
                isMaterialApplied++;
            }

            // Draw the light ray effect.
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, (u8)effect->primColor.r, (u8)effect->primColor.g,
                            (u8)effect->primColor.b, (u8)effect->primColor.a);
            Matrix_Translate(effect->pos.x, effect->pos.y, effect->pos.z, MTXMODE_NEW);
            Matrix_RotateY(effect->rotationY, MTXMODE_APPLY);
            Matrix_RotateX(effect->rotationX, MTXMODE_APPLY);
            Matrix_RotateZ(effect->rotationZ, MTXMODE_APPLY);
            Matrix_Scale(effect->scale * 0.5f, effect->scale * 0.5f, effect->maxScale * effect->scale, MTXMODE_APPLY);
            Matrix_RotateX(KEG_PI / 2, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, MM_OVL_DL("ovl_En_Clear_Tag", "gClearTagLightRayEffectDL"));
            FrameInterpolation_RecordCloseChild();
        }
    }

    // Draw all splash effects.
    effect = firstEffect;
    for (i = 0; i < ARRAY_COUNT(tag->effect); i++, effect++) {
        if ((effect->type == CLEAR_TAG_EFFECT_SPLASH) && (effect->actionTimer < ARRAY_COUNT(sWaterSplashTextures)) &&
            (sWaterSplashTextures[effect->actionTimer] != NULL)) {
            gDPPipeSync(POLY_XLU_DISP++);
            gDPSetEnvColor(POLY_XLU_DISP++, 255, 255, 255, 200);
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 200);
            gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)sWaterSplashTextures[effect->actionTimer]);
            Gfx_SetupDL_61Xlu(gfxCtx);
            gSPClearGeometryMode(POLY_XLU_DISP++, G_CULL_BACK);
            isMaterialApplied++;

            // Apply material 16 times along a circle to give the appearance of a splash
            for (j = 0; j < 16; j++) {
                FrameInterpolation_RecordOpenChild(effect, effect->type + (j << 4));
                Matrix_RotateY(2.0f * (j * KEG_PI) * (1.0f / 16.0f), MTXMODE_NEW);
                Keg_MultVecZ(effect->maxScale, &vec);
                ySurface = effect->pos.y;
                if (WaterBox_GetSurface1(play, &play->colCtx, effect->pos.x + vec.x, effect->pos.z + vec.z, &ySurface,
                                         &waterBox)) {
                    if ((effect->pos.y - ySurface) < 200.0f) {
                        // Draw the splash effect.
                        Matrix_Translate(effect->pos.x + vec.x, ySurface, effect->pos.z + vec.z, MTXMODE_NEW);
                        Matrix_RotateY(2.0f * (j * KEG_PI) * (1.0f / 16.0f), MTXMODE_APPLY);
                        Matrix_RotateX(effect->rotationX, MTXMODE_APPLY);
                        Matrix_Scale(effect->scale, effect->scale, effect->scale, MTXMODE_APPLY);
                        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                        gSPDisplayList(POLY_XLU_DISP++, MM_KEEP_DL("gEffWaterSplashDL"));
                    }
                }
                FrameInterpolation_RecordCloseChild();
            }
        }
    }

    gSPComboRMPop(POLY_XLU_DISP++);
    gSPComboRMPop(POLY_OPA_DISP++);

    CLOSE_DISPS(gfxCtx);
}

// ---------------------------------------------------------------------------------------------------------------
// MM's Blast Mask: an ordinary OOT bomb that goes off at once, so Hyrule reacts to it as to any OOT bomb, with MM's
// blast sphere.

EnBom* EnBom_ComboSpawnBlastMask(PlayState* play, Vec3f* pos) {
    EnBom* bomb = (EnBom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, pos->x, pos->y, pos->z, 0, 0, 0, BOMB_BODY);

    if (bomb == NULL) {
        return NULL;
    }
    bomb->timer = 0;
    bomb->comboBlastMask = true;
    // MM EnBom_Init (z_en_bom.c:171-173) puts the blast sphere on the bomb. OOT's EnBom sets it only in its body draw
    // (Collider_UpdateSpheres), which a bomb with its fuse at 0 never reaches: it explodes on its first update, before
    // its first draw. Without this the blast sits at the world origin and hits nothing near Link.
    bomb->explosionCollider.elements[0].dim.worldSphere.center.x = bomb->actor.world.pos.x;
    bomb->explosionCollider.elements[0].dim.worldSphere.center.y = bomb->actor.world.pos.y;
    bomb->explosionCollider.elements[0].dim.worldSphere.center.z = bomb->actor.world.pos.z;
    // MM: the Blast Mask's bomb is an MM bomb (BOMB_EXPLOSIVE_TYPE_BOMB: DMG_EXPLOSIVES, 2).
    CROSS_MM_TAG_TOUCHER(&bomb->explosionColliderItems[0].info.toucher, CROSS_MM_TAG_EXPLOSIVES, 2);
    return bomb;
}

#define BLAST_MASK_EXPLOSION_RADIUS 100 // MM D_80872E8C[0]
#define BLAST_MASK_ENEMY_HITS_END 7     // MM EnBom_Explode: from this timer on the blast no longer hits AT_TYPE_ENEMY

// MM EnBom_Explode (z_en_bom.c:350-357): the blast is a radius-100 sphere from its first frame, and only its first two
// frames hit Link. Centred on his head, it reaches his raised shield whenever it reaches his body, so his damage check
// takes the shield's bounce (MM func_80834600). OOT's blast grows from radius 8 and hits him for nine frames.
void EnBom_ComboBlastMaskSphere(EnBom* this) {
    if (!this->comboBlastMask) {
        return;
    }
    this->explosionCollider.elements[0].dim.worldSphere.radius = BLAST_MASK_EXPLOSION_RADIUS;
    if (this->timer == BLAST_MASK_ENEMY_HITS_END) {
        this->explosionCollider.base.atFlags &= ~AT_TYPE_ENEMY;
    }
}

static EnBom* EnBom_ComboBlastMaskHitting(Collider* collider) {
    EnBom* bomb = (EnBom*)collider->ac;

    if (!(collider->acFlags & AC_HIT) || (bomb == NULL) || (bomb->actor.id != ACTOR_EN_BOM) || !bomb->comboBlastMask) {
        return NULL;
    }
    return bomb;
}

// MM keeps the shield up through its standing guard reaction (Player_Action_19), so the blast's second frame bounces
// off it again. OOT's (Player_Action_808435C4) lowers it, so that frame would reach his body: it is ignored instead.
s32 EnBom_ComboBlastMaskShielded(PlayState* play, Player* player) {
    EnBom* blast;

    if (player != GET_PLAYER(play)) {
        return false;
    }
    blast = EnBom_ComboBlastMaskHitting(&player->shieldQuad.base);
    if (blast != NULL) {
        blast->comboBlastGuarded = true;
        return false; // OOT's guard reaction to the bounce, as MM's
    }
    blast = EnBom_ComboBlastMaskHitting(&player->cylinder.base);
    return (blast != NULL) && blast->comboBlastGuarded;
}

// ---------------------------------------------------------------------------------------------------------------
// The hooks (z_en_bom.c).

s32 EnBom_ComboIsKeg(Actor* bomb) {
    if ((bomb == NULL) || (bomb->id != ACTOR_EN_BOM)) {
        return false;
    }
    if (bomb->init != NULL) { // not initialized yet: the spawn's rot.x says it (MM ENBOM_GET_1)
        return !ENBOM_COMBO_IS_CLEARTAG(bomb->params) && (bomb->shape.rot.x & ENBOM_COMBO_KEG);
    }
    return ((EnBom*)bomb)->comboIsKeg;
}

s32 EnBom_ComboInit(EnBom* this, PlayState* play) {
    if (ENBOM_COMBO_IS_CLEARTAG(this->actor.params)) {
        EnBomClearTag_Init(this, play);
        return true;
    }
    if (this->actor.shape.rot.x & ENBOM_COMBO_KEG) { // MM ENBOM_GET_1: BOMB_EXPLOSIVE_TYPE_POWDER_KEG
        EnBomKeg_Init(this, play);
        return true;
    }
    return false;
}

s32 EnBom_ComboDestroy(EnBom* this, PlayState* play) {
    if (this->comboIsKeg) {
        EnBomKeg_Destroy(this, play);
        return true;
    }
    if (ENBOM_COMBO_IS_CLEARTAG(this->actor.params)) {
        EnBomClearTag_Destroy(this, play);
        return true;
    }
    return false;
}

// Note: MM drives the keg's fuse sound through channel IO port 6 (Audio_PlaySfx_AtPosWithChannelIO: 0, 1, 2 as the
// flash speeds up). The native MM sfx player (audio_mm_sfx.c) has no per-source channel-IO entry point yet, so the fuse
// plays at its base setting; see EnBomKeg_Update.
s32 EnBom_ComboUpdate(EnBom* this, PlayState* play) {
    if (this->comboIsKeg) {
        EnBomKeg_Update(this, play);
        return true;
    }
    if (ENBOM_COMBO_IS_CLEARTAG(this->actor.params)) {
        EnBomClearTag_Update(this, play);
        return true;
    }
    return false;
}

s32 EnBom_ComboDraw(EnBom* this, PlayState* play) {
    if (this->comboIsKeg) {
        EnBomKeg_Draw(this, play);
        return true;
    }
    if (ENBOM_COMBO_IS_CLEARTAG(this->actor.params)) {
        EnBomClearTag_Draw(this, play);
        return true;
    }
    return false;
}

#endif
