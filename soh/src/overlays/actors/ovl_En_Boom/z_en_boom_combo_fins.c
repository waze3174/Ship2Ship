/*
 * File: z_en_boom_combo_fins.c
 * ComboShip: Majora's Mask's Zora boomerang fins in Hyrule, as an OOT EnBoom (params COMBO_ZORA_FIN_LEFT/_RIGHT).
 *
 * A line-for-line port of MM's EnBoom (mm/src/overlays/actors/ovl_En_Boom/z_en_boom.c, "Zora boomerangs"): the
 * Zora throws BOTH fins (left and right, linked as parent/child), each curving out on its own side for 8 frames
 * before homing (MM unk_1CE / unk_1CF), MM's speed 12, MM's bounce off walls, MM's catch of items and tokens, MM's
 * blur trail colors, MM's fin models (gameplay_keep_DL_06FE20 left / _06FF68 right, routed to MM's
 * ResourceManager) spinning opposite ways. The player catches each fin (MM's per-fin catch), and is done when the
 * second one is back.
 *
 * What is OOT's: the hit. The fins carry OOT's boomerang damage (DMG_BOOMERANG, the same bit MM's fins use), so every
 * Hyrule switch, enemy and item a boomerang reacts to reacts to the Zora's fins.
 */

#include "z_en_boom.h"
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "rando/CrossDamageTags.h"

#ifdef COMBO_BUILD

#define MM_DL(name) ((Gfx*)Combo_MmDL("objects/gameplay_keep/" name))

s32 gComboZoraFinCaught = false; // z_player.c: a fin came back this frame (MM PLAYER_STATE3_ZORA_BOOMERANG_CAUGHT)

static ColliderQuadInit sComboFinQuadInit = { // MM sQuadInit in OOT's layout
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
        { DMG_BOOMERANG, 0x00, 0x02 },
        { 0xF7CFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

static void Fin_Fly(EnBoom* this, PlayState* play);

// MM func_808A24DC: bubbles under water, a splash where the fin crosses the surface.
static void Fin_Water(EnBoom* this, PlayState* play) {
    WaterBox* waterBox;
    f32 sp50 = this->actor.world.pos.y;
    u16 sp4E = this->actor.bgCheckFlags & BGCHECKFLAG_WATER;

    if (WaterBox_GetSurface1(play, &play->colCtx, this->actor.world.pos.x, this->actor.world.pos.z, &sp50,
                             &waterBox) &&
        (this->actor.world.pos.y < sp50)) {
        Vec3f sp40;

        this->actor.bgCheckFlags |= BGCHECKFLAG_WATER;
        sp40.x = this->actor.world.pos.x;
        sp40.y = this->actor.world.pos.y - 20.0f;
        sp40.z = this->actor.world.pos.z;
        EffectSsBubble_Spawn(play, &sp40, 20.0f, 10.0f, 20.0f, 0.13f);
    } else {
        this->actor.bgCheckFlags &= ~BGCHECKFLAG_WATER;
    }

    if ((this->actor.bgCheckFlags & BGCHECKFLAG_WATER_TOUCH) &&
        ((this->actor.bgCheckFlags & BGCHECKFLAG_WATER) != sp4E)) {
        Vec3f sp34;

        Math_Vec3f_Diff(&this->actor.world.pos, &this->actor.prevPos, &sp34);
        if (sp34.y != 0.0f) {
            f32 temp_fv0 = sqrtf(SQ(sp34.x) + SQ(sp34.z));

            if (temp_fv0 != 0.0f) {
                temp_fv0 = (((sp50 - this->actor.prevPos.y) / sp34.y) * temp_fv0) / temp_fv0;
            }
            sp34.x = this->actor.prevPos.x + (sp34.x * temp_fv0);
            sp34.y = sp50;
            sp34.z = this->actor.prevPos.z + (sp34.z * temp_fv0);
            EffectSsGSplash_Spawn(play, &sp34, NULL, NULL, 0, 300);
        }

        Audio_PlayActorSound2(&this->actor, NA_SE_EV_DIVE_INTO_WATER_L);

        EffectSsGRipple_Spawn(play, &sp34, 100, 500, 0);
        EffectSsGRipple_Spawn(play, &sp34, 100, 500, 4);
        EffectSsGRipple_Spawn(play, &sp34, 100, 500, 8);
    }

    this->actor.bgCheckFlags |= BGCHECKFLAG_WATER_TOUCH;
}

void EnBoom_ComboFinInit(EnBoom* this, PlayState* play) {
    EffectBlureInit1 blure;
    s32 i;

    this->actor.room = -1;
    this->actor.targetMode = 5;
    this->actor.shape.rot.x = this->actor.shape.rot.y = this->actor.shape.rot.z = 0;

    // MM: trail 255,255,100 fading 255 -> 64 -> 0.
    for (i = 0; i < 3; i++) {
        blure.p1StartColor[i] = blure.p2StartColor[i] = blure.p1EndColor[i] = blure.p2EndColor[i] =
            (i < 2) ? 255 : 100;
    }
    blure.p1StartColor[3] = 255;
    blure.p2StartColor[3] = 64;
    blure.p1EndColor[3] = 0;
    blure.p2EndColor[3] = 0;
    blure.elemDuration = 8;
    blure.unkFlag = 0;
    blure.calcMode = 0;
    blure.trailType = TRAIL_TYPE_BOOMERANG;
    Effect_Add(play, &this->effectIndex, EFFECT_BLURE1, 0, 0, &blure);

    Collider_InitQuad(play, &this->collider);
    Collider_SetQuad(play, &this->collider, &this->actor, &sComboFinQuadInit);
    // MM: the fins are DMG_ZORA_BOOMERANG, 2 — each enemy reacts as its MM damage table says (CrossDamageMap.h).
    CROSS_MM_TAG_TOUCHER(&this->collider.info.toucher, CROSS_MM_TAG_ZORA_BOOMERANG, 2);
    this->comboCurve = 0;
    this->comboHome = 0;
    this->actionFunc = Fin_Fly;
}

// MM EnBoom_Destroy: hand the player's fin pointer to the other fin; the throw is over when both are back.
void EnBoom_ComboFinDestroy(EnBoom* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* other;

    if (player != NULL) {
        Effect_Delete(play, this->effectIndex);
        Collider_DestroyQuad(play, &this->collider);

        other = this->actor.child;
        if (other != NULL) {
            other->parent = NULL;
            player->boomerangActor = other;
        } else {
            other = this->actor.parent;
            if (other != NULL) {
                other->child = NULL;
            } else {
                player->boomerangActor = NULL;
                player->stateFlags1 &= ~PLAYER_STATE1_BOOMERANG_THROWN;
            }
        }
        gComboZoraFinCaught = true;
        if (Combo_DebugEnv("COMBO_DEBUG_FORM_B")) {
            Combo_DebugLog("[ComboShip] zora fin %d caught\n", this->actor.params - COMBO_ZORA_FIN_LEFT);
        }
    }
}

// MM func_808A2918.
static void Fin_Fly(EnBoom* this, PlayState* play) {
    Actor* targetActor;
    Player* player = GET_PLAYER(play);
    s32 sp74;
    s16 sp72;
    s16 sp70;
    s16 sp6E;
    s16 sp6C;
    f32 sp64;
    f32 distXYZ;

    targetActor = this->moveTo;

    if (targetActor != NULL) {
        sp72 = Actor_WorldYawTowardPoint(&this->actor, &targetActor->focus.pos);
        sp70 = this->actor.world.rot.y - sp72;
        sp6E = Actor_WorldPitchTowardPoint(&this->actor, &targetActor->focus.pos);
        sp6C = this->actor.world.rot.x - sp6E;

        sp64 = (200.0f - Math_Vec3f_DistXYZ(&this->actor.world.pos, &targetActor->focus.pos)) * 0.005f;
        if (sp64 < 0.12f) {
            sp64 = 0.12f;
        }

        if ((&player->actor != targetActor) &&
            ((targetActor->update == NULL) || ((this->comboHome < 0) && (ABS(sp70) > 0x4000)))) {
            this->moveTo = NULL;
        } else {
            if (this->comboCurve != 0) {
                this->comboCurve--;
                this->actor.world.rot.y += (this->actor.params == COMBO_ZORA_FIN_LEFT) ? 0x190 : -0x190;
            } else {
                if (this->comboHome > 0) {
                    this->comboHome--;
                } else {
                    this->comboHome = -1;
                }
                Math_ScaledStepToS(&this->actor.world.rot.y, sp72,
                                   (this->comboHome > 0) ? 0x3E8 : (s16)(ABS(sp70) * sp64));
            }
            Math_ScaledStepToS(&this->actor.world.rot.x, sp6E, ABS(sp6C) * sp64);
        }
    }

    Actor_SetProjectileSpeed(&this->actor, 12.0f);
    Actor_MoveXZGravity(&this->actor);
    Fin_Water(this, play);
    Actor_PlaySfx_Flagged(&this->actor, NA_SE_IT_BOOMERANG_FLY - SFX_FLAG); // same sound in both games

    if ((this->collider.base.atFlags & AT_HIT) && (this->collider.base.at != NULL) &&
        (((this->collider.base.at->id == ACTOR_EN_ITEM00) &&
          (this->collider.base.at->params != ITEM00_HEART_CONTAINER) &&
          (this->collider.base.at->params != ITEM00_HEART_PIECE)) ||
         (this->collider.base.at->id == ACTOR_EN_SI))) {
        this->grabbed = this->collider.base.at;
        if (this->collider.base.at->id == ACTOR_EN_SI) {
            this->collider.base.at->flags |= ACTOR_FLAG_HOOKSHOT_ATTACHED;
        }
    }

    if (DECR(this->returnTimer) != 0) {
        s32 bgId;
        Vec3f sp50;
        DynaPolyActor* hitActor;

        sp74 = BgCheck_EntityLineTest1(&play->colCtx, &this->actor.prevPos, &this->actor.world.pos, &sp50,
                                       &this->actor.wallPoly, true, true, true, true, &bgId);
        if (sp74) {
            // MM func_800B90AC; OOT's boomerang also flies through Jabu-Jabu's membranes (BgBdanObjects param 0).
            if ((func_8002F9EC(play, &this->actor, this->actor.wallPoly, bgId, &sp50) != 0) ||
                ((bgId != BGCHECK_SCENE) && ((hitActor = DynaPoly_GetActor(&play->colCtx, bgId)) != NULL) &&
                 (hitActor->actor.id == ACTOR_BG_BDAN_OBJECTS) && (hitActor->actor.params == 0))) {
                sp74 = false;
            } else {
                CollisionCheck_SpawnShieldParticlesMetal(play, &sp50);
            }
        }

        if (Combo_DebugEnv("COMBO_DEBUG_FINS")) {
            Combo_DebugLog("[ComboShip] fin %d t %d pos %.0f %.0f %.0f prev %.0f %.0f %.0f rot %d %d wall %d bg %d\n",
                           this->actor.params - COMBO_ZORA_FIN_LEFT, this->returnTimer, this->actor.world.pos.x,
                           this->actor.world.pos.y, this->actor.world.pos.z, this->actor.prevPos.x,
                           this->actor.prevPos.y, this->actor.prevPos.z, this->actor.world.rot.x,
                           this->actor.world.rot.y, sp74, bgId);
        }
        if (sp74) {
            this->actor.world.rot.x = -this->actor.world.rot.x;
            this->actor.world.rot.y += 0x8000;
            this->moveTo = &player->actor;
            this->returnTimer = 0;
        }
    }

    if (this->returnTimer <= 16) {
        distXYZ = Math_Vec3f_DistXYZ(&this->actor.world.pos, &player->actor.focus.pos);

        if (&player->actor != this->moveTo) {
            if (this->moveTo == NULL) {
                this->comboCurve = 8;
            }
            this->moveTo = &player->actor;
        }

        if (distXYZ < 40.0f) {
            targetActor = this->grabbed;
            if (targetActor != NULL) {
                Math_Vec3f_Copy(&targetActor->world.pos, &player->actor.world.pos);
                if (targetActor->id == ACTOR_EN_ITEM00) {
                    targetActor->gravity = -0.9f;
                    targetActor->bgCheckFlags &= ~(BGCHECKFLAG_GROUND | BGCHECKFLAG_GROUND_TOUCH);
                } else {
                    targetActor->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
                }
            }
            Actor_Kill(&this->actor);
        }
    }

    targetActor = this->grabbed;
    if (targetActor != NULL) {
        if (targetActor->update == NULL) {
            this->grabbed = NULL;
        } else {
            Math_Vec3f_Copy(&targetActor->world.pos, &this->actor.world.pos);
        }
    }
}

// MM EnBoom_Update.
void EnBoom_ComboFinUpdate(EnBoom* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* actor;

    if (!(player->stateFlags1 & PLAYER_STATE1_IN_CUTSCENE)) {
        this->actionFunc(this, play);

        if (((actor = this->actor.child) != NULL) || ((actor = this->actor.parent) != NULL)) {
            this->actor.focus.pos.x = (this->actor.world.pos.x + actor->world.pos.x) * 0.5f;
            this->actor.focus.pos.y = (this->actor.world.pos.y + actor->world.pos.y) * 0.5f;
            this->actor.focus.pos.z = (this->actor.world.pos.z + actor->world.pos.z) * 0.5f;
        } else {
            Actor_SetFocus(&this->actor, 0.0f);
        }

        if (this->actor.params != COMBO_ZORA_FIN_LEFT) {
            this->activeTimer--;
        } else {
            this->activeTimer++;
        }
    }
}

// MM EnBoom_Draw.
void EnBoom_ComboFinDraw(EnBoom* this, PlayState* play) {
    static Vec3f sTip = { -960.0f, 0.0f, 0.0f };
    static Vec3f sBase = { 960.0f, 0.0f, 0.0f };
    const s32 left = (this->actor.params == COMBO_ZORA_FIN_LEFT);
    Vec3f sp4C;
    Vec3f sp40;

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_RotateY(BINANG_TO_RAD(this->actor.world.rot.y), MTXMODE_APPLY);
    Matrix_RotateZ(BINANG_TO_RAD(left ? -0x1F40 : 0x1F40), MTXMODE_APPLY);
    Matrix_RotateX(BINANG_TO_RAD(this->actor.world.rot.x), MTXMODE_APPLY);
    Matrix_MultVec3f(&sTip, &sp4C);
    Matrix_MultVec3f(&sBase, &sp40);

    if (func_80090480(play, &this->collider, &this->boomerangInfo, &sp4C, &sp40)) {
        EffectBlure_AddVertex(Effect_GetByIndex(this->effectIndex), &sp4C, &sp40);
    }

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_RotateY(BINANG_TO_RAD((s16)(this->activeTimer * 0x2EE0)), MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, left ? MM_DL("gameplay_keep_DL_06FE20") : MM_DL("gameplay_keep_DL_06FF68"));

    CLOSE_DISPS(play->state.gfxCtx);
}

#endif // COMBO_BUILD
