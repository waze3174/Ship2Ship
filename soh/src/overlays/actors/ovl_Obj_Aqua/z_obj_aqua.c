/*
 * File: z_obj_aqua.c
 * ComboShip: Majora's Mask's ObjAqua in Hyrule: Spring Water / Hot Spring Water poured out of a bottle.
 *
 * A line-for-line port of mm/src/overlays/actors/ovl_Obj_Aqua/z_obj_aqua.c: the same falling drop (gravity -0.9,
 * terminal -4, velocity damped by 0.9 each frame, growing toward MM's scale), the same puddle when it lands (it lies
 * on the floor's slope, spreads out and fades, 4+1 splashes, MM's NA_SE_EV_BOTTLE_WATERING), the same splash into
 * deep water (NA_SE_EV_BOMB_DROP_WATER at the fixed spot), the same underwater cloud of bubbles, the same steam off hot
 * water, the same draw (MM's gameplay_keep gEffFire1DL, camera-facing, scrolling, the wobble underwater). Models and
 * sounds are MM's own: the display lists routed to MM's ResourceManager, the sounds MM's sfx played natively by OOT's
 * audio engine (ComboGuestAudio.cpp, audio_mm_sfx.c). Spawned by Combo_SpawnMmAqua (z_player.c: MM's
 * Player_Action_70 pour); registered as its own actor (ComboMmItems.cpp), as OOT has no ObjAqua.
 *
 * What is OOT's: the hit. MM's AT cylinder is kept (AT_TYPE_OTHER, MM's size), but its damage bits are
 * COMBO_DMG_MM_AQUA (ComboMmItems.h) — a bit only the colliders added for MM's reactions accept — so Hyrule's own
 * AC_TYPE_OTHER colliders (bombable walls, bomb flowers, red ice) don't read the water as a bomb or fire.
 *
 * Effects: OOT's EffectSsGSplash and EffectSsBubble are MM's (same code, same gameplay_keep art; MM's bubbles also
 * drift along water currents, OOT's don't). OOT's EffectSsIceSmoke is NOT MM's (it needs Freezard's object_fz and
 * draws gFreezardSteamStartDL), so MM's EffectSsIceSmoke is ported here: the water runs its steam puffs itself and,
 * when MM kills the water, lives on (no collider, no water, no shadow) until the last puff is gone.
 */

#include "z_obj_aqua.h"
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "soh/Enhancements/randomizer/ComboMmItems.h"
#include "soh/frame_interpolation.h"

#ifdef COMBO_BUILD

#define MM_DL(name) ((Gfx*)Combo_MmDL("objects/gameplay_keep/" name))

// MM sound ids (mm include/sfx.h)
#define MM_SE_EV_BOTTLE_WATERING 0x2920
#define MM_SE_EV_BOMB_DROP_WATER 0x2817

void func_80ACBC70(ObjAqua* this);
void func_80ACBDCC(ObjAqua* this);
void func_80ACBC8C(ObjAqua* this, PlayState* play);
void func_80ACBD48(ObjAqua* this, PlayState* play);
void func_80ACBDFC(ObjAqua* this, PlayState* play);

void func_80ACBD34(ObjAqua* this);

static ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_OTHER,
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { COMBO_DMG_MM_AQUA, 0x00, 0x00 }, // MM: 0xF7CFFFFF (see the file comment)
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 6, 10, 0, { 0, 0, 0 } },
};

static InitChainEntry sInitChain[] = {
    ICHAIN_VEC3S(shape.rot, 0, ICHAIN_CONTINUE),
    ICHAIN_VEC3S(world.rot, 0, ICHAIN_CONTINUE),
    ICHAIN_F32_DIV1000(gravity, -900, ICHAIN_CONTINUE),
    ICHAIN_F32_DIV1000(minVelocityY, -4000, ICHAIN_CONTINUE), // MM terminalVelocity
    ICHAIN_F32(uncullZoneForward, 4000, ICHAIN_CONTINUE),     // MM cullingVolumeDistance
    ICHAIN_F32(uncullZoneScale, 300, ICHAIN_CONTINUE),        // MM cullingVolumeScale
    ICHAIN_F32(uncullZoneDownward, 300, ICHAIN_STOP),         // MM cullingVolumeDownward
};

static Vec3f D_80ACC308 = { 1.0f / 1000.0f, 7.0f / 10000.0f, 1.0f / 1000.0f };
static Vec3f D_80ACC314 = { 8.6f / 1000.0f, 8.0f / 10000.0f, 8.6f / 1000.0f };
static Vec3f D_80ACC320 = { 1.0f / 100.0f, 2.6f / 1000.0f, 1.0f / 100.0f };

static Vec3f sZeroVec = { 0.0f, 0.0f, 0.0f }; // MM gZeroVec3f

// ---------------------------------------------------------------------------------------------------------------
// MM library code OOT lacks or builds differently, verbatim.

// mm z_lib.c Math_Vec3f_StepTo
static f32 MmMath_Vec3f_StepTo(Vec3f* start, Vec3f* target, f32 speed) {
    Vec3f diff;
    f32 f2;
    f32 f0;

    Math_Vec3f_Diff(target, start, &diff);
    f0 = Math3D_Vec3fMagnitude(&diff);
    if (speed < f0) {
        f2 = speed / f0;
        f0 -= speed;
        start->x += f2 * diff.x;
        start->y += f2 * diff.y;
        start->z += f2 * diff.z;
    } else {
        Math_Vec3f_Copy(start, target);
        f0 = 0.0f;
    }

    return f0;
}

// mm z_bgcheck.c func_800C0094 (OOT's func_80038A28 builds this matrix differently)
static void MmPolyMatrix(CollisionPoly* poly, f32 tx, f32 ty, f32 tz, MtxF* dest) {
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

// ---------------------------------------------------------------------------------------------------------------
// MM's EffectSsIceSmoke (mm/src/overlays/effects/ovl_Effect_Ss_Ice_Smoke/z_eff_ss_ice_smoke.c), run by the water.

static s32 ObjAqua_SmokeLive(ObjAqua* this) {
    s32 i;

    for (i = 0; i < OBJ_AQUA_SMOKE_MAX; i++) {
        if (this->smoke[i].life > -1) {
            return true;
        }
    }
    return false;
}

// EffectSsIceSmoke_Spawn + EffectSsIceSmoke_Init
static void ObjAqua_SpawnSmoke(ObjAqua* this, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale) {
    ObjAquaSmoke* s = NULL;
    s32 i;

    (void)accel; // MM passes gZeroVec3f
    for (i = 0; i < OBJ_AQUA_SMOKE_MAX; i++) {
        if (this->smoke[i].life < 0) {
            s = &this->smoke[i];
            break;
        }
    }
    if (s == NULL) {
        return; // MM: no free soft-sprite slot at priority 128
    }
    Math_Vec3f_Copy(&s->pos, pos);
    Math_Vec3f_Copy(&s->velocity, velocity);
    s->alpha = 0;
    s->scale = ABS(scale);
    if (scale < 0) {
        s->scrollX = 1;
        s->scrollY = 2;
    } else {
        s->scrollX = 3;
        s->scrollY = 15;
    }
    s->life = 50;
    s->epoch++;
    // The puffs rise well above the water: don't let the water's own culling hide them (MM's effects aren't culled
    // with an actor).
    this->actor.flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
}

// EffectSs_UpdateAll (life, delete) + EffectSs_Update (accel is zero: pos += velocity) + EffectSsIceSmoke_Update
static void ObjAqua_UpdateSmoke(ObjAqua* this) {
    s32 i;

    for (i = 0; i < OBJ_AQUA_SMOKE_MAX; i++) {
        ObjAquaSmoke* s = &this->smoke[i];

        if (s->life > -1) {
            s->life--;
        }
        if (s->life > -1) {
            s->pos.x += s->velocity.x;
            s->pos.y += s->velocity.y;
            s->pos.z += s->velocity.z;
            if (s->life < 10) {
                Math_StepToS(&s->alpha, 0, 10);
            } else if (s->alpha < 100) {
                s->alpha += 10;
            }
        }
    }
}

// EffectSsIceSmoke_Draw
static void ObjAqua_DrawSmoke(ObjAqua* this, PlayState* play) {
    s32 i;

    OPEN_DISPS(play->state.gfxCtx);

    for (i = 0; i < OBJ_AQUA_SMOKE_MAX; i++) {
        ObjAquaSmoke* s = &this->smoke[i];
        f32 scale;

        if (s->life < 0) {
            continue;
        }
        FrameInterpolation_RecordOpenChild(s, s->epoch);
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 195, 235, 235, s->alpha);
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, s->scrollX * s->life, s->scrollY * s->life, 0x20, 0x40, 1,
                                      0, 0, 0x20, 0x20, -s->scrollX, -s->scrollY, 0, 0));
        Matrix_Translate(s->pos.x, s->pos.y, s->pos.z, MTXMODE_NEW);
        Matrix_ReplaceRotation(&play->billboardMtxF);
        scale = s->scale * 0.0001f;
        Matrix_Scale(scale, scale, 1.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, MM_DL("gEffIceSmokeDL"));
        FrameInterpolation_RecordCloseChild();
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// MM's Actor_Kill(&this->actor) on the water: the water is gone, its steam stays until the last puff fades.
static void ObjAqua_Kill(ObjAqua* this) {
    if (ObjAqua_SmokeLive(this)) {
        this->waterGone = true;
        this->actor.shape.shadowDraw = NULL;
    } else {
        Actor_Kill(&this->actor);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Sound: MM's, at the water.

static void ObjAqua_PlaySfx(ObjAqua* this, u16 mmSfxId) { // MM Actor_PlaySfx
    if (this->sfxSlot == 0) {
        this->sfxSlot = Combo_GuestAllocSlot();
    }
    Combo_GuestPlaySfxAt(mmSfxId, this->sfxSlot, &this->actor.projectedPos.x);
}

// ---------------------------------------------------------------------------------------------------------------
// MM ObjAqua

void func_80ACB6A0(ObjAqua* this, PlayState* play) {
    s32 pad;
    Vec3f sp58;
    s32 angleOffset = 0;
    s32 i;

    sp58.y = this->actor.world.pos.y + this->actor.yDistToWater; // MM depthInWater
    for (i = 0; i < 4; i++) {
        sp58.x = this->actor.world.pos.x + Math_SinS((s32)(Rand_ZeroOne() * 7200.0f) + angleOffset) * 8.0f;
        sp58.z = this->actor.world.pos.z + Math_CosS((s32)(Rand_ZeroOne() * 7200.0f) + angleOffset) * 8.0f;
        EffectSsGSplash_Spawn(play, &sp58, NULL, NULL, 0, 120);
        angleOffset += 0x4000;
    }
    sp58.x = this->actor.world.pos.x;
    sp58.z = this->actor.world.pos.z;
    EffectSsGSplash_Spawn(play, &sp58, NULL, NULL, 0, 300);
}

void func_80ACB7F4(ObjAqua* this, PlayState* play) {
    s32 pad;
    Vec3f effectPos;
    s32 angleOffset = 0;
    s32 i;

    effectPos.y = this->actor.floorHeight;
    for (i = 0; i < 4; i++) {
        effectPos.x = this->actor.world.pos.x + Math_SinS((s32)(Rand_ZeroOne() * 7200.0f) + angleOffset) * 8.0f;
        effectPos.z = this->actor.world.pos.z + Math_CosS((s32)(Rand_ZeroOne() * 7200.0f) + angleOffset) * 8.0f;
        EffectSsGSplash_Spawn(play, &effectPos, NULL, NULL, 0, 120);
        angleOffset += 0x4000;
    }
    effectPos.x = this->actor.world.pos.x;
    effectPos.z = this->actor.world.pos.z;
    EffectSsGSplash_Spawn(play, &effectPos, NULL, NULL, 0, 300);
}

void func_80ACB940(ObjAqua* this, PlayState* play) {
    s32 pad;
    Vec3f effectPos;
    Vec3f effectVelocity;

    effectVelocity.x = Rand_ZeroOne() - 0.5f;
    effectVelocity.y = 2.0f;
    effectVelocity.z = Rand_ZeroOne() - 0.5f;
    effectPos.x = this->actor.world.pos.x + (effectVelocity.x * 40.0f);
    effectPos.y = this->actor.world.pos.y;
    effectPos.z = this->actor.world.pos.z + (effectVelocity.z * 40.0f);
    ObjAqua_SpawnSmoke(this, &effectPos, &effectVelocity, &sZeroVec, (s32)(Rand_ZeroOne() * 24.0f) + 70);
}

void func_80ACBA10(ObjAqua* this) {
    s32 pad;
    MtxF sp2C;

    if (this->actor.floorPoly == NULL) { // MM would read an unset matrix; on the ground there is always a floor
        return;
    }
    MmPolyMatrix(this->actor.floorPoly, this->actor.world.pos.x, this->actor.floorHeight, this->actor.world.pos.z,
                 &sp2C);
    Matrix_MtxFToYXZRotS(&sp2C, &this->actor.shape.rot, false); // MM Matrix_MtxFToYXZRot
}

s32 ObjAqua_IsUnderwater(ObjAqua* this, PlayState* play) {
    s32 pad;
    WaterBox* waterBox;
    f32 waterSurface;

    if (WaterBox_GetSurfaceImpl(play, &play->colCtx, this->actor.world.pos.x, this->actor.world.pos.z, &waterSurface,
                                &waterBox) &&
        (this->actor.world.pos.y < waterSurface)) {
        return true;
    }
    return false;
}

void ObjAqua_Init(Actor* thisx, PlayState* play) {
    ObjAqua* this = (ObjAqua*)thisx;
    s32 i;

    for (i = 0; i < OBJ_AQUA_SMOKE_MAX; i++) {
        this->smoke[i].life = -1;
    }

    Actor_ProcessInitChain(thisx, sInitChain);
    this->actor.scale.x = 0.0009f;
    this->actor.scale.y = 0.0005f;
    this->actor.scale.z = 0.0009f;
    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, thisx, &sCylinderInit);
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 60.0f);

    thisx->shape.shadowAlpha = 140;
    this->alpha = 255;
    if (ObjAqua_IsUnderwater(this, play)) {
        for (i = 0; i < 8; i++) {
            EffectSsBubble_Spawn(play, &thisx->world.pos, -4.0f, 4.0f, 4.0f, (Rand_ZeroOne() * 0.09f) + 0.03f);
        }
        func_80ACBDCC(this);
    } else {
        func_80ACBC70(this);
    }
}

void ObjAqua_Destroy(Actor* thisx, PlayState* play) {
    ObjAqua* this = (ObjAqua*)thisx;

    Collider_DestroyCylinder(play, &this->collider);
    if (this->sfxSlot != 0) {
        Combo_GuestStopSlot(this->sfxSlot); // MM Actor_Destroy stops the actor's sounds
    }
}

void func_80ACBC70(ObjAqua* this) {
    this->counter = 200;
    this->actionFunc = func_80ACBC8C;
}

void func_80ACBC8C(ObjAqua* this, PlayState* play) {
    if (this->actor.bgCheckFlags & (BGCHECKFLAG_GROUND | BGCHECKFLAG_WATER)) {
        if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
            func_80ACB7F4(this, play);
            func_80ACBA10(this);
            ObjAqua_PlaySfx(this, MM_SE_EV_BOTTLE_WATERING);
            func_80ACBD34(this);
        } else {
            func_80ACB6A0(this, play);
            // MM SoundSource_PlaySfxAtFixedWorldPos(play, &this->actor.world.pos, 0x28, NA_SE_EV_BOMB_DROP_WATER)
            Combo_GuestPlaySfxAtWorld(play, MM_SE_EV_BOMB_DROP_WATER, &this->actor.world.pos.x);
            ObjAqua_Kill(this);
        }
    } else if (this->counter <= 0) {
        ObjAqua_Kill(this);
    }
}

void func_80ACBD34(ObjAqua* this) {
    this->actionFunc = func_80ACBD48;
}

void func_80ACBD48(ObjAqua* this, PlayState* play) {
    if ((AQUA_GET_TYPE(&this->actor) == AQUA_TYPE_HOT) && (this->alpha > 90)) {
        func_80ACB940(this, play);
    }
    if (this->alpha > 5) {
        this->alpha -= 5;
    } else {
        this->alpha = 0;
    }
    if (this->actor.shape.shadowAlpha > 2) {
        this->actor.shape.shadowAlpha -= 2;
    } else {
        ObjAqua_Kill(this);
    }
}

void func_80ACBDCC(ObjAqua* this) {
    this->actor.gravity = -0.1f;
    this->counter = 40;
    this->alpha = 140;
    this->actionFunc = func_80ACBDFC;
}

void func_80ACBDFC(ObjAqua* this, PlayState* play) {
    s32 pad;
    f32 temp = this->counter;

    this->alpha = (s32)(temp * 3.25f) + 10;
    this->actor.shape.shadowAlpha = this->alpha;
    this->unk_198 += 1000;
    if (AQUA_GET_TYPE(&this->actor) == AQUA_TYPE_HOT) {
        f32 temp_f2 = this->actor.scale.x * 10000.0f;

        EffectSsBubble_Spawn(play, &this->actor.world.pos, temp_f2 * -0.5f, temp_f2, temp_f2,
                             (Rand_ZeroOne() * 0.1f) + 0.03f);
    }
    if (this->counter <= 0) {
        ObjAqua_Kill(this);
    }
}

void ObjAqua_Update(Actor* thisx, PlayState* play) {
    ObjAqua* this = (ObjAqua*)thisx;
    s32 pad;

    if (this->waterGone) { // only the steam is left
        ObjAqua_UpdateSmoke(this);
        if (!ObjAqua_SmokeLive(this)) {
            Actor_Kill(&this->actor);
        }
        return;
    }

    if (this->counter > 0) {
        this->counter--;
    }
    this->actionFunc(this, play);
    if ((this->actor.update != NULL) && !this->waterGone) {
        if (this->actionFunc == func_80ACBC8C) {
            MmMath_Vec3f_StepTo(&this->actor.scale, &D_80ACC308, 0.00006f);
        } else if (this->actionFunc == func_80ACBD48) {
            MmMath_Vec3f_StepTo(&this->actor.scale, &D_80ACC314, 0.00095f);
        } else {
            MmMath_Vec3f_StepTo(&this->actor.scale, &D_80ACC320, 0.0004f);
        }
        this->actor.velocity.y *= 0.9f;
        Actor_MoveXZGravity(&this->actor);                                     // MM Actor_MoveWithGravity
        Actor_UpdateBgCheckInfo(play, &this->actor, 12.0f, 4.0f, 0.0f, 1 | 4); // MM UPDBGCHECKINFO_FLAG_1 | _FLAG_4
        if (this->actionFunc != func_80ACBDFC) {
            Collider_UpdateCylinder(&this->actor, &this->collider);
            this->collider.dim.radius = this->actor.scale.x * 3000.0f;
            CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
        }
    }

    // MM: EffectSs_UpdateAll runs after the actors, the same frame a puff is spawned.
    ObjAqua_UpdateSmoke(this);
}

void ObjAqua_Draw(Actor* thisx, PlayState* play) {
    ObjAqua* this = (ObjAqua*)thisx;
    s32 framesTemp;
    s32 pad;
    s16 yaw = Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000;
    s32 actionFuncTemp = this->actionFunc == func_80ACBDFC;

    if (!this->waterGone) {
        OPEN_DISPS(play->state.gfxCtx);

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        framesTemp = ((play->gameplayFrames & 0x7FFFFFFF) * -0xA) & 0x1FF;
        if (actionFuncTemp) {
            framesTemp >>= 1;
        }
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0, framesTemp, 0x20, 0x80, 0, 0, 0,
                                      actionFuncTemp ? -5 : -0xA));
        gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 170, 255, 255, this->alpha);
        gDPSetEnvColor(POLY_XLU_DISP++, 0, 150, 255, 0);
        if (actionFuncTemp) {
            s16 rotation = Math_SinS(this->unk_198) * 8000.0f;

            Matrix_RotateZ(BINANG_TO_RAD(rotation), MTXMODE_APPLY); // MM Matrix_RotateZS
            Matrix_Scale(1.3f, 1.0f, 1.0f, MTXMODE_APPLY);
            Matrix_RotateZ(BINANG_TO_RAD((s16)(rotation * -1)), MTXMODE_APPLY);
            Matrix_Scale(10.0f / 13.0f, 1.0f, 1.0f, MTXMODE_APPLY);
        }

        Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_APPLY); // MM Matrix_RotateYS
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, MM_DL("gEffFire1DL"));

        CLOSE_DISPS(play->state.gfxCtx);
    }

    ObjAqua_DrawSmoke(this, play);
}

// ---------------------------------------------------------------------------------------------------------------
// ComboMmItems.h

Actor* Combo_SpawnMmAqua(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, s32 hot) {
    s16 id = Combo_MmAquaActorId();

    if ((play == NULL) || (id < 0)) {
        return NULL;
    }
    // MM Player_Action_70: Actor_Spawn(..., ACTOR_OBJ_AQUA, x, y, z, 0x4000, shape.rot.y, 0, AQUA_PARAMS(type))
    return Actor_Spawn(&play->actorCtx, play, id, x, y, z, 0x4000, yaw, 0,
                       AQUA_PARAMS(hot ? AQUA_TYPE_HOT : AQUA_TYPE_COLD));
}

int Combo_IsMmAqua(Actor* actor, int type) {
    s16 id;

    if (actor == NULL) {
        return false;
    }
    id = Combo_MmAquaActorId();
    return (id >= 0) && (actor->id == id) && (AQUA_GET_TYPE(actor) == type);
}

#endif // COMBO_BUILD
