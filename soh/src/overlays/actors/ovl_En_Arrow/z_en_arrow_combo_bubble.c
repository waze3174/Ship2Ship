/*
 * File: z_en_arrow_combo_bubble.c
 * ComboShip: Majora's Mask's Deku Bubble in Hyrule, as an OOT EnArrow (params ARROW_COMBO_DEKU_BUBBLE).
 *
 * A line-for-line port of MM's bubble paths in mm/src/overlays/actors/ovl_En_Arrow/z_en_arrow.c (EnArrow_Init,
 * func_8088A514 / func_8088A594 / func_8088ACE0 / func_8088AA98, the bubble branch of EnArrow_Draw) and of MM's
 * popped-bubble splat effect (mm/src/overlays/effects/ovl_Effect_Ss_Sbn/z_eff_ss_sbn.c): the same growth while
 * Deku Link blows it, the same flight (shrinking, wobbling, slowing), the same refraction draw (the framebuffer slice
 * MM's gameplay_keep_DL_06F380 samples), the same bounce off reflective hits, the same pop and wall splat. Models,
 * textures and sounds are MM's own: display lists routed to MM's ResourceManager ("__OTR__@mm:"), sounds MM's own
 * sfx played natively by OOT's audio engine (ComboGuestAudio.cpp, audio_mm_sfx.c).
 *
 * What is OOT's: the hit. The bubble's collider carries OOT's slingshot damage (DMG_SLINGSHOT) so everything in
 * Hyrule that reacts to a slingshot seed (eye switches, enemies, Gold Skulltulas...) reacts to a Deku bubble.
 *
 * The splat is MM's EffectSsSbn. OOT's soft-sprite table has no slot for it, so the popped bubble's actor lives on
 * for the effect's 93 frames as the splat (collider off) with the effect's own init/update/draw.
 */

#include "z_en_arrow.h"
#include "soh/framebuffer_effects.h"
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "rando/CrossDamageTags.h"

#ifdef COMBO_BUILD

// MM sound ids (mm include/sfx.h); continuous ones without the 0x800 flag, as MM's Actor_PlaySfx_Flagged passes them.
#define MM_SE_PL_DEKUNUTS_FIRE 0x8E0
#define MM_SE_PL_DEKUNUTS_BUBLE_BREATH_LOOP (0x9A1 - 0x800)
#define MM_SE_IT_DEKUNUTS_BUBLE_VANISH 0x1854
#define MM_SE_IT_DEKUNUTS_BUBLE_SHOT_LEVEL_LOOP (0x185A - 0x800)

#define MM_DL(name) ((Gfx*)Combo_MmDL("objects/gameplay_keep/" name))
#define MM_TEX(name) ("__OTR__objects/gameplay_keep/" name) // resolved inside an "mm" RM bracket / routed DL

#define BUBBLE_GROW_SCALE 16.0f // MM: the bubble's full size while blown

static s32 EnArrow_ComboBubble_Fly(EnArrow* this, PlayState* play);

// MM's sQuadInit in OOT's layout: player AT, nearest touch, no touch sfx (the bubble's sounds are its own).
static ColliderQuadInit sComboBubbleColliderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_QUAD,
    },
    {
        ELEMTYPE_UNK2,
        { DMG_SLINGSHOT, 0x00, 0x02 },
        { 0xF7CFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

// ---------------------------------------------------------------------------------------------------------------
// MM math the splat needs, verbatim (OOT's func_80038A28 builds this matrix differently; MM's is used for a 1:1 look).

static void MmPolyMatrix(CollisionPoly* poly, f32 tx, f32 ty, f32 tz, MtxF* dest) { // mm z_bgcheck.c func_800C0094
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

static void MmSkinMatrix_MulYRotation(MtxF* mf, s16 a) { // mm z_skin_matrix.c SkinMatrix_MulYRotation
    f32 sinA;
    f32 cosA;
    f32 rx;
    f32 rz;

    if (a != 0) {
        sinA = Math_SinS(a);
        cosA = Math_CosS(a);
        rx = mf->xx;
        rz = mf->xz;
        mf->xx = rx * cosA - rz * sinA;
        mf->xz = rx * sinA + rz * cosA;
        rx = mf->yx;
        rz = mf->yz;
        mf->yx = rx * cosA - rz * sinA;
        mf->yz = rx * sinA + rz * cosA;
        rx = mf->zx;
        rz = mf->zz;
        mf->zx = rx * cosA - rz * sinA;
        mf->zz = rx * sinA + rz * cosA;
        rx = mf->wx;
        rz = mf->wz;
        mf->wx = rx * cosA - rz * sinA;
        mf->wz = rx * sinA + rz * cosA;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Sound: MM's, at the bubble.

static void Bubble_Sfx(EnArrow* this, u16 mmSfxId) {
    if (this->comboSfxSlot == 0) {
        this->comboSfxSlot = Combo_GuestAllocSlot();
    }
    Combo_GuestPlaySfxAt(mmSfxId, this->comboSfxSlot, &this->actor.projectedPos.x);
}

// ---------------------------------------------------------------------------------------------------------------
// The popped-bubble splat (MM EffectSsSbn), run by the popped bubble's actor.

static const char* sSbnSlidingTextures[] = {
    MM_TEX("gEffPoppedDekuBubbleSliding1Tex"),  MM_TEX("gEffPoppedDekuBubbleSliding2Tex"),
    MM_TEX("gEffPoppedDekuBubbleSliding3Tex"),  MM_TEX("gEffPoppedDekuBubbleSliding4Tex"),
    MM_TEX("gEffPoppedDekuBubbleSliding5Tex"),  MM_TEX("gEffPoppedDekuBubbleSliding6Tex"),
    MM_TEX("gEffPoppedDekuBubbleSliding7Tex"),  MM_TEX("gEffPoppedDekuBubbleSliding8Tex"),
    MM_TEX("gEffPoppedDekuBubbleSliding9Tex"),  MM_TEX("gEffPoppedDekuBubbleSliding10Tex"),
    MM_TEX("gEffPoppedDekuBubbleSliding11Tex"), MM_TEX("gEffPoppedDekuBubbleSliding12Tex"),
};

static const char* sSbnTextures[] = {
    MM_TEX("gEffPoppedDekuBubble1Tex"), MM_TEX("gEffPoppedDekuBubble2Tex"), MM_TEX("gEffPoppedDekuBubble3Tex"),
    MM_TEX("gEffPoppedDekuBubble4Tex"), MM_TEX("gEffPoppedDekuBubble5Tex"),
};

static void EnArrow_ComboBubble_Splat(EnArrow* this, PlayState* play);
static void EnArrow_ComboBubble_DrawSplat(EnArrow* this, PlayState* play);

// EffectSsSbn_Init: pos, the wall polygon, the bubble's size.
static void Bubble_StartSplat(EnArrow* this, PlayState* play, CollisionPoly* colPoly, f32 bubbleScale) {
    ComboBubbleSplat* s = &this->comboSplat;
    MtxF mtx;
    Vec3f normal;
    Vec3f colPolyVec;
    Vec3f bubbleVec;
    f32 angle;
    f32 opposite;

    s->pos = this->actor.world.pos;
    s->colPoly = colPoly;
    s->scroll = 0;
    s->scrollStep = 7;
    s->texIndex = 0;
    s->alpha = 200;
    s->reg4 = 250;
    s->reg4Step = 30;
    s->scale = Rand_CenteredFloat(100.0f) + (bubbleScale * 120.0f);
    if (s->scale < 600) {
        s->scale = 600;
    } else if (s->scale > 1500) {
        s->scale = 1500;
    }
    s->reg8 = 1000;
    s->rotAngle = 0;
    s->life = 93;
    s->sliding = true;

    if (colPoly != NULL) {
        MmPolyMatrix(colPoly, s->pos.x, s->pos.y, s->pos.z, &mtx);
        normal.x = COLPOLY_GET_NORMAL(colPoly->normal.x);
        normal.y = COLPOLY_GET_NORMAL(colPoly->normal.y);
        normal.z = COLPOLY_GET_NORMAL(colPoly->normal.z);
        if ((normal.y > 0.95f) || (normal.y < -0.95f)) {
            s->sliding = false;
        } else {
            if (normal.y > 0.05f) {
                colPolyVec.x = normal.x * 10.0f;
                colPolyVec.y = (-(SQ(normal.x) + SQ(normal.z)) * 10.0f) / normal.y;
                colPolyVec.z = normal.z * 10.0f;
            } else if (normal.y < -0.05f) {
                colPolyVec.x = -normal.x * 10.0f;
                colPolyVec.y = (-(SQ(normal.x) + SQ(normal.z)) * 10.0f) / -normal.y;
                colPolyVec.z = -normal.z * 10.0f;
            } else {
                colPolyVec.x = 0.0f;
                colPolyVec.y = -10.0f;
                colPolyVec.z = 0.0f;
            }
            bubbleVec.x = -mtx.mf[2][0] * 10.0f;
            bubbleVec.y = -mtx.mf[2][1] * 10.0f;
            bubbleVec.z = -mtx.mf[2][2] * 10.0f;
            Math3D_CosOut(&colPolyVec, &bubbleVec, &angle);

            opposite = (SQ(angle) >= 1.0f) ? 0.0f : sqrtf(1.0f - SQ(angle));
            if (((mtx.mf[0][0] * colPolyVec.x) + (mtx.mf[0][1] * colPolyVec.y) + (mtx.mf[0][2] * colPolyVec.z)) <
                0.0f) {
                s->rotAngle = Math_Atan2S(angle, opposite); // MM Math_Atan2S_XY(x, y) == OOT Math_Atan2S(x, y)
            } else {
                s->rotAngle = -Math_Atan2S(angle, opposite);
            }
        }
    }

    // The bubble is gone; what remains is the splat: no hit, no flight, its own draw.
    Collider_ResetQuadAT(play, &this->collider.base);
    this->collider.base.atFlags &= ~AT_ON;
    this->actor.speedXZ = 0.0f;
    this->actor.velocity.y = 0.0f;
    this->actor.gravity = 0.0f;
    this->actionFunc = EnArrow_ComboBubble_Splat;
}

// EffectSsSbn_Update + the effect system's life countdown.
static void EnArrow_ComboBubble_Splat(EnArrow* this, PlayState* play) {
    ComboBubbleSplat* s = &this->comboSplat;

    if (s->life < 10) {
        s->alpha -= 20;
    } else if (s->life >= 45) {
        if (s->life < 85) {
            s->reg8 += 12;
            s->reg4 -= s->reg4Step;
        } else {
            s->reg8 = 1000;
        }
    }
    if (--s->life <= 0) {
        Actor_Kill(&this->actor);
    }
}

// EffectSsSbn_DrawSliding / EffectSsSbn_Draw.
static void EnArrow_ComboBubble_DrawSplat(EnArrow* this, PlayState* play) {
    ComboBubbleSplat* s = &this->comboSplat;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    MtxF mtx;
    f32 scale = s->scale * 0.001f;

    if (s->colPoly == NULL) {
        return; // MM's effect draws from the polygon; a pop in open air leaves nothing on a wall
    }
    MmPolyMatrix(s->colPoly, s->pos.x, s->pos.y, s->pos.z, &mtx);
    MmSkinMatrix_MulYRotation(&mtx, s->rotAngle);
    Matrix_Mult(&mtx, MTXMODE_NEW);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    Matrix_Scale(0.05f, 0.05f, 0.05f, MTXMODE_APPLY);

    OPEN_DISPS(gfxCtx);

    gSPComboRMPush(POLY_XLU_DISP++, "mm");
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Gfx_SetupDL_25Xlu(gfxCtx);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_FOG_SHADE_A, G_RM_ZB_XLU_DECAL2);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 255, s->alpha);

    if (s->sliding) {
        if (s->texIndex < ARRAY_COUNT(sSbnSlidingTextures)) {
            gSPSegment(POLY_XLU_DISP++, 0x08, sSbnSlidingTextures[s->texIndex]);
            s->texIndex++;
            s->scroll += s->scrollStep;
        } else {
            gSPSegment(POLY_XLU_DISP++, 0x08, MM_TEX("gEffPoppedDekuBubbleSliding12Tex"));
            if ((s->scrollStep >= 2) && ((play->state.frames % 2) == 0)) {
                s->scrollStep--;
            }
            s->scroll += s->scrollStep;
        }
        gSPSegment(POLY_XLU_DISP++, 0x09,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0, s->scroll, 0x20, 0x20, 0, 0, 0,
                                      s->scrollStep));
        gSPDisplayList(POLY_XLU_DISP++, MM_DL("gEffPoppedDekuBubbleSlidingDL"));
    } else {
        if (s->texIndex < ARRAY_COUNT(sSbnTextures)) {
            gSPSegment(POLY_XLU_DISP++, 0x08, sSbnTextures[s->texIndex]);
            s->texIndex++;
        } else {
            gSPSegment(POLY_XLU_DISP++, 0x08, MM_TEX("gEffPoppedDekuBubble5Tex"));
        }
        gSPDisplayList(POLY_XLU_DISP++, MM_DL("gEffPoppedDekuBubbleDL"));
    }
    gSPComboRMPop(POLY_XLU_DISP++);

    CLOSE_DISPS(gfxCtx);
}

// ---------------------------------------------------------------------------------------------------------------
// The bubble (MM EnArrow, ARROW_TYPE_DEKU_BUBBLE).

static void Bubble_SetSpeeds(EnArrow* this) { // MM func_8088A514
    f32 temp_f0 = 16.0f - this->comboBubbleScale;

    Actor_SetProjectileSpeed(&this->actor, CLAMP(temp_f0, 1.0f, 80.0f));
}

// MM func_8088AA98: hitting water on the way.
static void Bubble_CheckWater(EnArrow* this, PlayState* play) {
    WaterBox* waterBox;
    f32 sp50 = this->actor.world.pos.y;
    Vec3f sp44;
    f32 temp_f0;

    if (WaterBox_GetSurface1(play, &play->colCtx, this->actor.world.pos.x, this->actor.world.pos.z, &sp50,
                             &waterBox) &&
        (this->actor.world.pos.y < sp50) && !(this->actor.bgCheckFlags & BGCHECKFLAG_WATER)) {
        this->actor.bgCheckFlags |= BGCHECKFLAG_WATER;

        Math_Vec3f_Diff(&this->actor.world.pos, &this->actor.home.pos, &sp44);

        if (sp44.y != 0.0f) {
            temp_f0 = sqrtf(SQ(sp44.x) + SQ(sp44.z));
            if (temp_f0 != 0.0f) {
                temp_f0 = (((sp50 - this->actor.home.pos.y) / sp44.y) * temp_f0) / temp_f0;
            }
            sp44.x = this->actor.home.pos.x + (sp44.x * temp_f0);
            sp44.y = sp50;
            sp44.z = this->actor.home.pos.z + (sp44.z * temp_f0);
            EffectSsGSplash_Spawn(play, &sp44, NULL, NULL, 0, 300);
        }

        Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIVE_INTO_WATER_L); // same id in both games

        EffectSsGRipple_Spawn(play, &sp44, 100, 500, 0);
        EffectSsGRipple_Spawn(play, &sp44, 100, 500, 4);
        EffectSsGRipple_Spawn(play, &sp44, 100, 500, 8);
    }
}

// MM func_8088ACE0, bubble paths only.
static void EnArrow_ComboBubble_FlyAction(EnArrow* this, PlayState* play) {
    (void)EnArrow_ComboBubble_Fly(this, play);
}

static s32 EnArrow_ComboBubble_Fly(EnArrow* this, PlayState* play) {
    s32 bgId;
    Vec3f sp9C;
    s32 phi_a2 = 0;
    f32 sp78;
    f32 sp74;
    f32 temp_f12_2;

    if ((DECR(this->timer) == 0) || (this->touchedPoly != 0) ||
        (phi_a2 = (this->collider.base.atFlags & AT_HIT) != 0)) {
        if (phi_a2 && (this->collider.info.atHitInfo != NULL) &&
            (this->collider.info.atHitInfo->elemType != ELEMTYPE_UNK4) &&
            (this->collider.base.atFlags & AT_BOUNCED)) {
            if ((this->collider.base.at != NULL) && (this->collider.base.at->id != ACTOR_OBJ_SYOKUDAI)) {
                // A reflective hit (a shield, metal): the bubble bounces off instead of popping.
                Math_Vec3f_Copy(&this->actor.world.pos, &this->actor.prevPos);
                this->actor.world.rot.y += (s16)(0x8000 + (s16)Rand_CenteredFloat(0x1F40));
                this->actor.velocity.y = -this->actor.velocity.y;
                this->comboBubbleState = -1; // no more hits (the draw resets the AT), as MM
                return false;
            }
        }

        if ((this->touchedPoly != 0) || phi_a2) {
            EffectSsStone1_Spawn(play, &this->actor.world.pos, 0);
        }

        Combo_GuestPlaySfxAtWorld(play, MM_SE_IT_DEKUNUTS_BUBLE_VANISH, &this->actor.world.pos.x);
        if (Combo_DebugEnv("COMBO_DEBUG_FORM_B")) {
            Combo_DebugLog("[ComboShip] deku bubble popped: wall %d hit %d size %.2f\n", this->touchedPoly, phi_a2,
                           this->comboBubbleScale);
        }
        if (this->comboSfxSlot != 0) {
            Combo_GuestStopSlot(this->comboSfxSlot); // the flight hum ends with the bubble
        }

        if ((this->touchedPoly != 0) && (this->actor.wallBgId == BGCHECK_SCENE)) {
            Bubble_StartSplat(this, play, this->actor.wallPoly, this->comboBubbleScale);
            return true;
        }
        Actor_Kill(&this->actor);
        return true;
    }

    Bubble_CheckWater(this, play);

    if (this->comboBubbleState == 0) {
        sp78 = sqrtf(SQ(this->actor.speedXZ) + SQ(this->actor.velocity.y));
        sp74 = Math_SinS(this->actor.world.rot.y) * this->actor.speedXZ;
        temp_f12_2 = Math_CosS(this->actor.world.rot.y) * this->actor.speedXZ;

        this->actor.prevPos.x = this->actor.world.pos.x - (sp74 * (10.0f / sp78));
        this->actor.prevPos.y = this->actor.world.pos.y - (this->actor.velocity.y * (10.0f / sp78));
        this->actor.prevPos.z = this->actor.world.pos.z - (temp_f12_2 * (10.0f / sp78));

        this->comboBubbleState = 1;
    }

    if (Math_StepToF(&this->comboBubbleScale, 1.0f, 0.4f)) {
        this->timer = 0;
    } else {
        this->comboBubbleWobX += (s16)(this->comboBubbleScale * (500.0f + Rand_ZeroFloat(1400.0f)));
        this->actor.world.rot.x += (s16)(500.0f * Math_SinS(this->comboBubbleWobX));
        this->actor.shape.rot.x = this->actor.world.rot.x;

        this->comboBubbleWobY += (s16)(this->comboBubbleScale * (500.0f + Rand_ZeroFloat(1400.0f)));
        this->actor.world.rot.y += (s16)(500.0f * Math_SinS(this->comboBubbleWobY));

        this->actor.shape.rot.y = this->actor.world.rot.y;

        Bubble_SetSpeeds(this);
    }
    Bubble_Sfx(this, MM_SE_IT_DEKUNUTS_BUBLE_SHOT_LEVEL_LOOP);

    Math_Vec3f_Copy(&this->unk_210, &this->actor.world.pos);

    if (this->actor.speedXZ == 0.0f) {
        this->actor.velocity.y -= 1.0f;
        if (this->actor.velocity.y < this->actor.minVelocityY) {
            this->actor.velocity.y = this->actor.minVelocityY;
        }
        Actor_UpdatePos(&this->actor);
    } else {
        Actor_MoveXZGravity(&this->actor);
    }

    if ((this->touchedPoly = BgCheck_ProjectileLineTest(&play->colCtx, &this->actor.prevPos, &this->actor.world.pos,
                                                        &sp9C, &this->actor.wallPoly, true, true, true, true,
                                                        &bgId))) {
        Math_Vec3f_Copy(&this->actor.world.pos, &sp9C);
        this->actor.wallBgId = bgId;
    }
    return false;
}

// MM func_8088A594, bubble paths: grows while Deku Link blows it, leaves when he lets go.
static void EnArrow_ComboBubble_Blow(EnArrow* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (this->actor.parent != NULL) {
        if (Math_SmoothStepToF(&this->comboBubbleScale, BUBBLE_GROW_SCALE, 0.07f, 1.8f, 0.0f) > 0.5f) {
            Bubble_Sfx(this, MM_SE_PL_DEKUNUTS_BUBLE_BREATH_LOOP);
            return;
        }

        this->comboBubbleTimer++;
        if (this->comboBubbleTimer > 20) {
            this->actionFunc = EnArrow_ComboBubble_FlyAction;
            Magic_Reset(play);
        }
    } else {
        if (player->unk_A73 == 0) { // MM unk_D57: put away without firing
            Magic_Reset(play);
            Actor_Kill(&this->actor);
            return;
        }

        Combo_GuestPlaySfx(MM_SE_PL_DEKUNUTS_FIRE);
        if (Combo_DebugEnv("COMBO_DEBUG_FORM_B")) {
            Combo_DebugLog("[ComboShip] deku bubble fired: size %.2f at (%.0f %.0f %.0f)\n", this->comboBubbleScale,
                           this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z);
        }

        this->actionFunc = EnArrow_ComboBubble_FlyAction;
        Math_Vec3f_Copy(&this->unk_210, &this->actor.world.pos);

        this->comboBubbleScale = CLAMP_MIN(this->comboBubbleScale, 3.5f);
        Bubble_SetSpeeds(this);
        this->timer = 99;
        Magic_Reset(play);
    }
}

void EnArrow_ComboBubbleInit(EnArrow* this, PlayState* play) {
    this->actor.minVelocityY = -150.0f; // MM sInitChain terminalVelocity
    Collider_InitQuad(play, &this->collider);
    Collider_SetQuad(play, &this->collider, &this->actor, &sComboBubbleColliderInit);
    // The bubble's hit counts as a slingshot seed in Hyrule (eye switches, enemies), MM's damage 2.
    this->collider.info.toucher.dmgFlags = DMG_SLINGSHOT;
    this->collider.info.toucher.damage = 2;
    // MM: DMG_DEKU_BUBBLE, 2 — enemies react as their MM damage table says (CrossDamageMap.h).
    CROSS_MM_TAG_TOUCHER(&this->collider.info.toucher, CROSS_MM_TAG_DEKU_BUBBLE, 2);
    Actor_SetScale(&this->actor, 1.0f);
    this->comboBubbleScale = 0.0f;
    this->comboBubbleTimer = 0;
    this->comboBubbleState = 0;
    this->comboBubbleWobX = 0;
    this->comboBubbleWobY = 0;
    this->comboSfxSlot = 0;
    this->hitActor = NULL;
    this->actionFunc = EnArrow_ComboBubble_Blow;
}

void EnArrow_ComboBubbleDestroy(EnArrow* this, PlayState* play) {
    Collider_DestroyQuad(play, &this->collider);
    if (this->comboSfxSlot != 0) {
        Combo_GuestStopSlot(this->comboSfxSlot);
    }
}

void EnArrow_ComboBubbleUpdate(EnArrow* this, PlayState* play) {
    // MM EnArrow_Update: a bubble keeps updating through blocking cutscene modes.
    this->actionFunc(this, play);
    Math_Vec3f_Copy(&this->actor.home.pos, &this->actor.prevPos);
}

// MM func_8088B88C: the hit quad follows the bubble (the arrow-trail part never applies to a bubble).
static void Bubble_UpdateHitQuad(EnArrow* this, PlayState* play) {
    // MM D_8088C2EC[1]: unk_00 (not yet active) and unk_18 (alternating each frame once active).
    static Vec3f sTipNoWeapon[2] = { { 20.0f, -20.0f, 20.0f }, { 20.0f, -20.0f, 20.0f } };
    static Vec3f sQuad[2][2] = {
        { { 20.0f, -20.0f, 20.0f }, { -20.0f, 20.0f, 20.0f } },
        { { -20.0f, -20.0f, 20.0f }, { 20.0f, 20.0f, 20.0f } },
    };
    Vec3f* sp4C;
    Vec3f sp40;
    Vec3f sp34;

    if (this->actionFunc == EnArrow_ComboBubble_FlyAction) {
        if (!this->weaponInfo.active) {
            sp4C = sTipNoWeapon;
        } else {
            sp4C = sQuad[play->gameplayFrames % 2];
        }
        Matrix_MultVec3f(&sp4C[0], &sp40);
        Matrix_MultVec3f(&sp4C[1], &sp34);
        func_80090480(play, &this->collider, &this->weaponInfo, &sp40, &sp34);
    }
}

// MM EnArrow_Draw, ARROW_TYPE_DEKU_BUBBLE branch.
void EnArrow_ComboBubbleDraw(EnArrow* this, PlayState* play) {
    if (this->actionFunc == EnArrow_ComboBubble_Splat) {
        EnArrow_ComboBubble_DrawSplat(this, play);
        return;
    }
    {
        s32 spA4 = 255 - (s32)(this->comboBubbleScale * 4.0f);
        f32 spA0 = (this->actor.speedXZ * 0.1f) + 1.0f;
        f32 sp9C = (1.0f / spA0);

        OPEN_DISPS(play->state.gfxCtx);

        if (this->comboBubbleState >= 0) {
            Bubble_UpdateHitQuad(this, play);
        } else {
            Collider_ResetQuadAT(play, &this->collider.base);
        }

        sp9C *= 0.002f;
        spA0 *= 0.002f;

        Matrix_Scale(this->comboBubbleScale * sp9C, this->comboBubbleScale * sp9C, this->comboBubbleScale * spA0,
                     MTXMODE_APPLY);
        Matrix_Translate(0.0f, 0.0f, 460.0f, MTXMODE_APPLY);

        if (this->actor.speedXZ == 0.0f) {
            func_8002ED80(&this->actor, play, 0); // MM func_800B8118 (xlu hilite)

            // As 2S2H does: capture the framebuffer so gameplay_keep_DL_06F380 can sample it from segment 0x0F.
            Gfx* gfx = POLY_XLU_DISP;
            FB_WriteFramebufferSliceToCPU(&gfx, play->state.gfxCtx->curFrameBuffer, true);
            POLY_XLU_DISP = gfx;
            gSPInvalidateTexCache(POLY_XLU_DISP++,
                                  (uintptr_t)(play->state.gfxCtx->curFrameBuffer) + ((104 * 320 + 144) * 2));

            gSPDisplayList(POLY_XLU_DISP++, MM_DL("gameplay_keep_DL_06F380"));
            gDPSetRenderMode(POLY_XLU_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);
            gDPSetCombineLERP(POLY_XLU_DISP++, TEXEL1, 0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC,
                              TEXEL0, COMBINED, 0, ENVIRONMENT, 0, COMBINED, 0, ENVIRONMENT, 0);
            gDPSetEnvColor(POLY_XLU_DISP++, 230, 225, 150, spA4);

            Matrix_ReplaceRotation(&gMtxFClear); // MM gIdentityMtxF

            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPMatrix(POLY_XLU_DISP++, (0x01000000 | 1), G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW); // billboard, seg 1 (2S2H D_01000000_TO_SEGMENTED: low bit = segmented for LUS)
            gSPDisplayList(POLY_XLU_DISP++, MM_DL("gameplay_keep_DL_06F9F0"));
        } else {
            func_8002EBCC(&this->actor, play, 0); // MM func_800B8050 (opa hilite)

            Gfx* gfx = POLY_OPA_DISP;
            FB_WriteFramebufferSliceToCPU(&gfx, play->state.gfxCtx->curFrameBuffer, true);
            POLY_OPA_DISP = gfx;
            gSPInvalidateTexCache(POLY_OPA_DISP++,
                                  (uintptr_t)(play->state.gfxCtx->curFrameBuffer) + ((104 * 320 + 144) * 2));

            gSPDisplayList(POLY_OPA_DISP++, MM_DL("gameplay_keep_DL_06F380"));
            gDPSetCombineLERP(POLY_OPA_DISP++, TEXEL1, 0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC,
                              TEXEL0, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0x7F, 230, 225, 150, 255);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, MM_DL("gameplay_keep_DL_06FAE0"));
        }

        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// Deku Link's head squash and tilt while he blows (MM z_player_lib.c head limb, PLAYER_FORM_DEKU): the bubble's
// size drives both. Returns the head scale in `scale` and the extra roll; 0 = no bubble being blown.
s32 EnArrow_ComboBubbleHeadPose(Actor* heldActor, Vec3f* scale, s16* rotZ, s16* upperRotX) {
    EnArrow* bubble = (EnArrow*)heldActor;

    if ((heldActor == NULL) || (heldActor->id != ACTOR_EN_ARROW) || (heldActor->params != ARROW_COMBO_DEKU_BUBBLE)) {
        return false;
    }
    scale->x = 1.0f - (bubble->comboBubbleScale * 0.03f);
    scale->y = 1.0f - (bubble->comboBubbleScale * 0.01f);
    scale->z = 1.0f - (bubble->comboBubbleScale * 0.04f);
    *rotZ = (s16)(bubble->comboBubbleScale * 320.0f);
    *upperRotX = (s16)(bubble->comboBubbleScale * -470.0f);
    return true;
}

#endif // COMBO_BUILD
