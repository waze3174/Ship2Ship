/*
 * File: z_en_boom_combo_oot.c
 * ComboShip: Ocarina of Time's Boomerang in Termina, as an MM EnBoom (params COMBO_OOT_BOOMERANG).
 *
 * A line-for-line port of OOT's EnBoom (soh/src/overlays/actors/ovl_En_Boom/z_en_boom.c, "Thrown Boomerang"): OOT's
 * speed 12, OOT's homing on a lock-on target (scaled yaw/pitch steps, drops a target behind it), OOT's return timer
 * (20 frames, set by the thrower), OOT's bounce off walls (reverse pitch, turn around, head home) with the metal
 * shield sparks, OOT's item fetch (Recovery hearts / rupees / any EnItem00 and Gold Skulltula tokens ride the
 * boomerang home and drop at Link's feet), OOT's catch radius 40 around Link's focus, OOT's blur trail colours and
 * OOT's model (gameplay_keep gBoomerangRefDL, routed to OOT's ResourceManager) spinning 12000 per frame.
 *
 * What is MM's: the hit. The boomerang carries OOT's damage bit 0x10, which is MM's Zora-boomerang bit, so every
 * Termina enemy and switch a boomerang can affect reacts to it the way it reacts to the Zora's fins (owner rule: only
 * damage / puzzle credit maps to the host game).
 */

#include "z_en_boom.h"

#ifdef COMBO_BUILD

#define OOT_BOOMERANG_DL ((Gfx*)"__OTR__@oot:objects/gameplay_keep/gBoomerangRefDL")

// OOT's sQuadInit, in MM's layout.
static ColliderQuadInit sComboOotQuadInit = {
    {
        COL_MATERIAL_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_QUAD,
    },
    {
        ELEM_MATERIAL_UNK2,
        { 0x00000010, 0x00, 0x01 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        ATELEM_ON | ATELEM_NEAREST | ATELEM_SFX_NORMAL,
        ACELEM_NONE,
        OCELEM_NONE,
    },
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

static InitChainEntry sComboOotInitChain[] = {
    ICHAIN_S8(attentionRangeType, ATTENTION_RANGE_5, ICHAIN_CONTINUE),
    ICHAIN_VEC3S(shape.rot, 0, ICHAIN_STOP),
};

static void EnBoom_ComboOotFly(EnBoom* this, PlayState* play);

void EnBoom_ComboOotInit(EnBoom* this, PlayState* play) {
    EffectBlureInit1 blure;
    s32 i;

    this->actor.room = -1;
    Actor_ProcessInitChain(&this->actor, sComboOotInitChain);

    for (i = 0; i < 3; i++) {
        static const u8 kRgb[3] = { 255, 255, 100 };
        blure.p1StartColor[i] = blure.p2StartColor[i] = blure.p1EndColor[i] = blure.p2EndColor[i] = kRgb[i];
    }
    blure.p1StartColor[3] = 255;
    blure.p2StartColor[3] = 64;
    blure.p1EndColor[3] = 0;
    blure.p2EndColor[3] = 0;
    blure.elemDuration = 8;
    blure.unkFlag = 0;
    blure.calcMode = 0;
    Effect_Add(play, &this->effectIndex, EFFECT_BLURE1, 0, 0, &blure);

    Collider_InitQuad(play, &this->collider);
    Collider_SetQuad(play, &this->collider, &this->actor, &sComboOotQuadInit);

    this->actionFunc = EnBoom_ComboOotFly;
}

void EnBoom_ComboOotDestroy(EnBoom* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    Effect_Destroy(play, this->effectIndex);
    Collider_DestroyQuad(play, &this->collider);
    if ((player != NULL) && (player->zoraBoomerangActor == &this->actor)) {
        player->zoraBoomerangActor = NULL;
    }
}

// OOT EnBoom_Fly. moveTo = OOT moveTo, unk_1C8 = OOT grabbed, unk_1CC = OOT returnTimer.
static void EnBoom_ComboOotFly(EnBoom* this, PlayState* play) {
    Actor* target;
    Player* player = GET_PLAYER(play);
    s32 collided;
    s16 yawTarget;
    s16 yawDiff;
    s16 pitchTarget;
    s16 pitchDiff;
    f32 distXYZScale;
    f32 distFromLink;
    s32 hitDynaID;
    Vec3f hitPoint;

    target = this->moveTo;

    if (target != NULL) {
        yawTarget = Actor_WorldYawTowardPoint(&this->actor, &target->focus.pos);
        yawDiff = this->actor.world.rot.y - yawTarget;

        pitchTarget = Actor_WorldPitchTowardPoint(&this->actor, &target->focus.pos);
        pitchDiff = this->actor.world.rot.x - pitchTarget;

        distXYZScale = (200.0f - Math_Vec3f_DistXYZ(&this->actor.world.pos, &target->focus.pos)) * 0.005f;
        if (distXYZScale < 0.12f) {
            distXYZScale = 0.12f;
        }

        if ((target != &player->actor) && ((target->update == NULL) || (ABS(yawDiff) > 0x4000))) {
            this->moveTo = NULL;
        } else {
            Math_ScaledStepToS(&this->actor.world.rot.y, yawTarget, (s16)(ABS(yawDiff) * distXYZScale));
            Math_ScaledStepToS(&this->actor.world.rot.x, pitchTarget, (s16)(ABS(pitchDiff) * distXYZScale));
        }
    }

    Actor_SetSpeeds(&this->actor, 12.0f); // OOT Actor_SetProjectileSpeed
    Actor_MoveWithGravity(&this->actor);  // OOT Actor_MoveXZGravity
    Actor_PlaySfx_Flagged(&this->actor, NA_SE_IT_BOOMERANG_FLY - SFX_FLAG);

    collided = !!(this->collider.base.atFlags & AT_HIT);
    if (collided) {
        if ((this->collider.base.at->id == ACTOR_EN_ITEM00) || (this->collider.base.at->id == ACTOR_EN_SI)) {
            this->unk_1C8 = this->collider.base.at;
            if (this->collider.base.at->id == ACTOR_EN_SI) {
                this->collider.base.at->flags |= ACTOR_FLAG_HOOKSHOT_ATTACHED;
            }
        }
    }

    if (DECR(this->unk_1CC) == 0) {
        distFromLink = Math_Vec3f_DistXYZ(&this->actor.world.pos, &player->actor.focus.pos);
        this->moveTo = &player->actor;

        if (distFromLink < 40.0f) {
            target = this->unk_1C8;
            if (target != NULL) {
                Math_Vec3f_Copy(&target->world.pos, &player->actor.world.pos);

                if (target->id == ACTOR_EN_ITEM00) {
                    target->gravity = -0.9f;
                    target->bgCheckFlags &= ~(BGCHECKFLAG_GROUND | BGCHECKFLAG_GROUND_TOUCH);
                } else {
                    target->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
                }
            }
            player->stateFlags1 &= ~PLAYER_STATE1_ZORA_BOOMERANG_THROWN;
            Actor_Kill(&this->actor);
        }
    } else {
        collided = !!(this->collider.base.atFlags & AT_HIT);
        if (collided) {
            Math_Vec3f_Copy(&this->actor.world.pos, &this->actor.prevPos);
        } else {
            collided = BgCheck_EntityLineTest1(&play->colCtx, &this->actor.prevPos, &this->actor.world.pos, &hitPoint,
                                               &this->actor.wallPoly, true, true, true, true, &hitDynaID);

            if (collided) {
                // OOT func_8002F9EC (= MM func_800B90AC): a surface that takes the hit (e.g. a crystal switch face)
                // does not bounce the boomerang.
                if (func_800B90AC(play, &this->actor, this->actor.wallPoly, hitDynaID, &hitPoint)) {
                    collided = false;
                } else {
                    CollisionCheck_SpawnShieldParticlesMetal(play, &hitPoint);
                }
            }
        }

        if (collided) {
            this->actor.world.rot.x = -this->actor.world.rot.x;
            this->actor.world.rot.y += 0x8000;
            this->moveTo = &player->actor;
            this->unk_1CC = 0;
        }
    }

    target = this->unk_1C8;
    if (target != NULL) {
        if (target->update == NULL) {
            this->unk_1C8 = NULL;
        } else {
            Math_Vec3f_Copy(&target->world.pos, &this->actor.world.pos);
        }
    }
}

void EnBoom_ComboOotUpdate(EnBoom* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    // OOT: frozen while Link is in a cutscene (PLAYER_STATE1_IN_CUTSCENE = MM PLAYER_STATE1_20000000).
    if (!(player->stateFlags1 & PLAYER_STATE1_20000000)) {
        this->actionFunc(this, play);
        Actor_SetFocus(&this->actor, 0.0f);
        this->unk_1CD++; // OOT activeTimer
    }
}

void EnBoom_ComboOotDraw(EnBoom* this, PlayState* play) {
    static Vec3f sMultVec1 = { -960.0f, 0.0f, 0.0f };
    static Vec3f sMultVec2 = { 960.0f, 0.0f, 0.0f };
    Vec3f vec1;
    Vec3f vec2;

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_RotateYS(this->actor.world.rot.y, MTXMODE_APPLY);
    Matrix_RotateZS(0x1F40, MTXMODE_APPLY);
    Matrix_RotateXS(this->actor.world.rot.x, MTXMODE_APPLY);
    Matrix_MultVec3f(&sMultVec1, &vec1);
    Matrix_MultVec3f(&sMultVec2, &vec2);

    if (func_80126440(play, &this->collider, &this->weaponInfo, &vec1, &vec2)) {
        EffectBlure_AddVertex(Effect_GetByIndex(this->effectIndex), &vec1, &vec2);
    }

    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    Matrix_RotateYS((s16)(this->unk_1CD * 12000), MTXMODE_APPLY);

    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, OOT_BOOMERANG_DL);

    CLOSE_DISPS(play->state.gfxCtx);
}

#endif
