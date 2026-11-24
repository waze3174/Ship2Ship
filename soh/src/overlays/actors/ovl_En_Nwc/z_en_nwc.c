/*
 * File: z_en_nwc.c
 * Overlay: ovl_En_Nwc
 * Description: Cluster of cucco chicks. Unfinished.
 */

#include "z_en_nwc.h"
#include "objects/object_nwc/object_nwc.h"
#include "soh/frame_interpolation.h"
#ifdef COMBO_BUILD
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "soh/Enhancements/randomizer/ComboMmItems.h"
#endif

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED)

void EnNwc_Init(Actor* thisx, PlayState* play);
void EnNwc_Destroy(Actor* thisx, PlayState* play);
void EnNwc_Update(Actor* thisx, PlayState* play);
void EnNwc_Draw(Actor* thisx, PlayState* play);

void EnNwc_SetUpdate(EnNwc* this, EnNwcUpdateFunc updateFunc);
void EnNwc_ChickNoop(EnNwcChick* chick, EnNwc* this, PlayState* play);
void EnNwc_ChickBgCheck(EnNwcChick* chick, PlayState* play);
void EnNwc_ChickFall(EnNwcChick* chick, EnNwc* this, PlayState* play);
void EnNwc_UpdateChicks(EnNwc* this, PlayState* play);
void EnNwc_DrawChicks(EnNwc* this, PlayState* play);
void EnNwc_Idle(EnNwc* this, PlayState* play);

#define CHICK_BG_FLOOR (1 << 0)
#define CHICK_BG_WALL (1 << 1)

typedef enum {
    /* 0 */ CHICK_NONE,
    /* 1 */ CHICK_NORMAL
} ChickTypes;

const ActorInit En_Nwc_InitVars = {
    ACTOR_EN_NWC,
    ACTORCAT_PROP,
    FLAGS,
    OBJECT_NWC,
    sizeof(EnNwc),
    (ActorFunc)EnNwc_Init,
    (ActorFunc)EnNwc_Destroy,
    (ActorFunc)EnNwc_Update,
    (ActorFunc)EnNwc_Draw,
    NULL,
};

static ColliderJntSphElementInit sJntSphElementInit = {
    {
        ELEMTYPE_UNK1,
        { 0x00000000, 0x00, 0x00 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_NONE,
    },
    { 0, { { 0, 0, 0 }, 10 }, 100 },
};

static ColliderJntSphInitType1 sJntSphInit = {
    {
        COLTYPE_HIT3,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        COLSHAPE_JNTSPH,
    },
    16,
    NULL,
};

void EnNwc_SetUpdate(EnNwc* this, EnNwcUpdateFunc updateFunc) {
    this->updateFunc = updateFunc;
}

void EnNwc_ChickNoop(EnNwcChick* chick, EnNwc* this, PlayState* play) {
}

void EnNwc_ChickBgCheck(EnNwcChick* chick, PlayState* play) {
    CollisionPoly* outPoly;
    s32 bgId;
    Vec3f outPos;
    f32 dy;

    chick->bgFlags &= ~CHICK_BG_WALL & ~CHICK_BG_FLOOR;
    outPos.x = chick->pos.x;
    outPos.y = chick->pos.y;
    outPos.z = chick->pos.z;
    if (BgCheck_EntitySphVsWall1(&play->colCtx, &outPos, &chick->pos, &chick->lastPos, 10.0f, &chick->floorPoly,
                                 20.0f)) {
        chick->bgFlags |= CHICK_BG_WALL;
    }
    //! @bug The use of outPos here is totally wrong. Even if it didn't get overwritten
    //       by the wall check, it should add an offset to the y-value so the raycast
    //       doesn't go through the floor and cause the chicks to ignore all floors.
    chick->floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &outPoly, &bgId, &outPos);
    dy = chick->floorY - chick->pos.y;
    if ((0.0f <= dy) && (dy < 40.0f)) {
        chick->pos.y = chick->floorY;
        chick->bgFlags |= CHICK_BG_FLOOR;
    }
}

void EnNwc_ChickFall(EnNwcChick* chick, EnNwc* this, PlayState* play) {
    chick->velY -= 0.1f;
    if (chick->velY < -10.0f) {
        chick->velY = -10.0f;
    }
    chick->pos.y += chick->velY;
    EnNwc_ChickBgCheck(chick, play);
    if (chick) {} // Needed for matching. Possibly from remnant of unfinished code?
}

#ifdef COMBO_BUILD
// ComboShip: MM's Bremen Mask (MM En_Nwc, mm/src/overlays/actors/ovl_En_Nwc/z_en_nwc.c). While Link marches, a chick
// within 100 units joins the line behind him (a chime per chick), hops along the line at MM's speed 2 with MM's
// wobble, faces the chick ahead when it catches up, and scatters (MM NWC_STATE_RUNNING) when the march ends, then
// goes back to being OOT's chick. MM's chicks march behind Grog's trail (MM EnHs nwcPos); the cluster keeps it here.
//
// Not ported: MM's chicks growing into cuccos once ten are in line (MM En_Nwc:303-311). It is the Cucco Shack's
// payoff (Grog's count of grown cuccos ends the march) and needs MM's per-chick cucco skeleton from object_niw; OOT's
// chick cluster draws bare display lists and has no skeleton or object_niw to grow into.

#define COMBO_NWC_STATE_FOLLOWING 3 // MM NWC_STATE_FOLLOWING
#define COMBO_NWC_STATE_RUNNING 4   // MM NWC_STATE_RUNNING

// MM sound ids (mm include/sfx.h): OOT has no chick song or join chime.
#define MM_SE_EV_CHICK_SONG 0x2994
#define MM_SE_SY_CHICK_JOIN_CHIME 0x4852

// ComboShip: MM's Bremen Mask (MM EnHs_UpdateChickPos, mm z_en_hs.c:105)
static void EnNwc_ComboUpdateChickPos(Vec3f* dst, Vec3f src, f32 offset) {
    Vec3f diff;
    f32 distance;

    Math_Vec3f_Diff(&src, dst, &diff);

    distance = SQXZ(diff); // gets un-squared after we check if we are too close

    if (SQ(offset) > distance) {
        return;
    }

    distance = sqrtf(distance);
    diff.x *= (distance - offset) / distance;
    diff.z *= (distance - offset) / distance;

    dst->x += diff.x;
    dst->z += diff.z;
}

// ComboShip: MM's Bremen Mask (MM EnHs func_80952C50 seeding the trail at Link, func_80952F00 pulling it along
// every frame, mm z_en_hs.c:58, 125). Grog updates before the chicks (NPCs before props), so this runs first.
static void EnNwc_ComboUpdateTrail(EnNwc* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 offset;
    s32 i;

    if (player == NULL) {
        return;
    }
    if (!this->comboTrailReady) {
        for (i = 0; i < ARRAY_COUNT(this->comboTrail); i++) {
            Math_Vec3f_Copy(&this->comboTrail[i], &player->actor.world.pos);
        }
        this->comboCount = 0;
        this->comboTrailReady = true;
    }

    if (this->comboCount >= 20) { // current chick count >= 10
        offset = 15.0f;
    } else {
        offset = 10.0f;
    }

    EnNwc_ComboUpdateChickPos(&this->comboTrail[0], player->actor.world.pos, offset);

    for (i = 1; i < ARRAY_COUNT(this->comboTrail); i++) {
        EnNwc_ComboUpdateChickPos(&this->comboTrail[i], this->comboTrail[i - 1], offset);
    }
}

// ComboShip: MM's Bremen Mask (MM EnNwc_ChangeState, En_Nwc:169).
static void EnNwc_ComboChangeState(EnNwcChick* chick, s16 newState) {
    chick->comboSpeed = 0.0f;
    switch (newState) {
        case COMBO_NWC_STATE_FOLLOWING:
            chick->comboRandomRot = (s16)(s32)Rand_CenteredFloat(0x2710);
            break;

        case COMBO_NWC_STATE_RUNNING:
            chick->comboYaw = chick->comboOrder * 0x3000 & 0xFFFF;
            chick->rot.y = chick->comboYaw;
            chick->comboStateTimer = Rand_ZeroFloat(40.0f) + 120.0f;
            break;

        default:
            break;
    }
    chick->comboState = newState;
}

// ComboShip: MM's Bremen Mask (MM EnNwc_IsFound En_Nwc:155, the march check at :162, + EnNwc_CheckFound En_Nwc:223):
// a chick within 100 units of the marching player joins the line. MM's chicks look while standing still
// (NWC_STATE_CHECK_BREMAN); OOT's chicks always stand still. MM has ten chicks and Grog's trail holds ten; OOT's
// cluster has sixteen, so the line is full at ten (an 11th would index past the trail).
static void EnNwc_ComboCheckFound(EnNwcChick* chick, EnNwc* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 dx;
    f32 dz;
    u8 currentChickCount;

    if (!Combo_BremenMarching() || (this->comboCount >= 20)) {
        return;
    }
    dx = chick->pos.x - player->actor.world.pos.x;
    dz = chick->pos.z - player->actor.world.pos.z;
    if (sqrtf(SQ(dx) + SQ(dz)) >= 100.0f) { // MM xzDistToPlayer < 100
        return;
    }

    currentChickCount = this->comboCount / 2;
    if (currentChickCount > 9) {
        currentChickCount = 9;
    }

    // save our current chick order
    chick->comboOrder = this->comboCount + 1;

    // if < 10 chicks, increment the chick counter
    if (this->comboCount < 20) {
        this->comboCount += 2;
    }

    chick->comboStateTimer = 0;
    EnNwc_ComboChangeState(chick, COMBO_NWC_STATE_FOLLOWING);
    // MM Audio_PlaySfx_AtPosWithAllChannelsIO(&gSfxDefaultPos, NA_SE_SY_CHICK_JOIN_CHIME, currentChickCount): the IO
    // value raises the chime's pitch per chick; the guest sfx player takes no channel IO, so every chime is the first.
    (void)currentChickCount;
    Combo_GuestPlaySfx(MM_SE_SY_CHICK_JOIN_CHIME);
}

// ComboShip: MM's Bremen Mask (MM EnNwc_Update's Actor_MoveWithGravity + Actor_UpdateBgCheckInfo(10, 10, 10, wall |
// floor), with MM's chick gravity -1 / terminal -9 from EnNwc_Init). OOT's chick only ever falls (gravity 0.1).
static void EnNwc_ComboChickMove(EnNwcChick* chick, PlayState* play) {
    CollisionPoly* wallPoly;
    CollisionPoly* floorPoly;
    Vec3f result;
    Vec3f probe;
    s32 bgId;
    f32 floorY;

    chick->velY += -1.0f;
    if (chick->velY < -9.0f) {
        chick->velY = -9.0f;
    }
    chick->pos.x += Math_SinS(chick->comboYaw) * chick->comboSpeed;
    chick->pos.y += chick->velY;
    chick->pos.z += Math_CosS(chick->comboYaw) * chick->comboSpeed;

    chick->bgFlags &= ~CHICK_BG_WALL & ~CHICK_BG_FLOOR;
    if (BgCheck_EntitySphVsWall1(&play->colCtx, &result, &chick->pos, &chick->lastPos, 10.0f, &wallPoly, 10.0f)) {
        Math_Vec3f_Copy(&chick->pos, &result);
        chick->bgFlags |= CHICK_BG_WALL;
    }

    probe.x = chick->pos.x;
    probe.y = chick->lastPos.y + 50.0f; // as OOT/MM's actor floor check, from above the last position
    probe.z = chick->pos.z;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &floorPoly, &bgId, &probe);
    chick->comboGrounded = false;
    if (floorY > BGCHECK_Y_MIN) {
        chick->floorY = floorY;
        chick->floorPoly = floorPoly;
        if (chick->pos.y <= floorY) {
            chick->pos.y = floorY;
            if (chick->velY < 0.0f) {
                chick->velY = 0.0f;
            }
            chick->bgFlags |= CHICK_BG_FLOOR;
            chick->comboGrounded = true;
        }
    }
}

// ComboShip: MM's Bremen Mask (MM EnNwc_Follow, En_Nwc:271, the not-grown-up branches).
static void EnNwc_ComboFollow(EnNwcChick* chick, EnNwc* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Vec3f* chickCoords = this->comboTrail;
    Vec3f targetVector;
    s16 newRotY;

    chick->comboStateTimer++;
    if (((chick->comboStateTimer & 3) == 3) && ((chick->comboStateTimer & 0x14) != 0)) {
        chick->velY = 2.0f; // hop up and down
    }
    if ((chick->comboStateTimer & 0x1B) == 24) {
        Combo_GuestPlaySfxAtWorld(play, MM_SE_EV_CHICK_SONG, &chick->pos.x);
    }

    Math_Vec3f_Diff(&chickCoords[chick->comboOrder], &chick->pos, &targetVector);
    if (SQXZ(targetVector) < SQ(5.0f)) { // too close to keep moving, stop
        chick->comboSpeed = 0.0f;

        // first nwc in the line follows player, the rest follow the previous one
        if (chick->comboOrder == 1) {
            newRotY = Math_Vec3f_Yaw(&chick->pos, &player->actor.world.pos); // MM yawTowardsPlayer
        } else {
            newRotY = Math_Vec3f_Yaw(&chick->pos, &chickCoords[chick->comboOrder - 2]);
        }

    } else { // not too close: keep moving
        chick->comboRandomRot += (s16)(s32)Rand_CenteredFloat(0x5DC);
        if (chick->comboRandomRot > 0x1388) {
            chick->comboRandomRot = 0x1388;
        } else if (chick->comboRandomRot < -0x1388) {
            chick->comboRandomRot = -0x1388;
        }
        chick->comboSpeed = 2.0f;
        newRotY = Math_Vec3f_Yaw(&chick->pos, &chickCoords[chick->comboOrder]) + chick->comboRandomRot;
    }

    chick->comboYaw = newRotY;
    Math_SmoothStepToS(&chick->rot.y, chick->comboYaw, 2, 0xBB8, 0xC8);

    // MM EnNwc_PlayerReleasedBremanMarch (En_Nwc:135, the march check at :144). Its first clause (all ten grown into cuccos) never holds here.
    if (!Combo_BremenMarching()) {
        this->comboCount = 0; // reset chick follow count

        EnNwc_ComboChangeState(chick, COMBO_NWC_STATE_RUNNING);
    }
}

// ComboShip: MM's Bremen Mask (MM EnNwc_RunAway, En_Nwc:400). When it ends, MM's chick goes back to wandering
// (EnNwc_ToggleState); OOT's goes back to being OOT's chick.
static void EnNwc_ComboRunAway(EnNwcChick* chick, EnNwc* this, PlayState* play) {
    if (DECR(chick->comboStateTimer) == 0) {
        chick->comboState = 0;
        chick->comboSpeed = 0.0f;
        return;
    }

    if ((chick->comboStateTimer & 3) == 3) {
        chick->velY = 2.0f; // hop up and down
    }
    if ((chick->comboStateTimer & 0xB) == 8) {
        Combo_GuestPlaySfxAtWorld(play, MM_SE_EV_CHICK_SONG, &chick->pos.x);
    }

    chick->comboSpeed = 2.0f;
    if (chick->bgFlags & CHICK_BG_WALL) {
        chick->comboState = 0;
        chick->comboSpeed = 0.0f;
    }
}

// ComboShip: MM's Bremen Mask. One chick's frame in MM's order (EnNwc_Update: move, bg check, then the action).
// Returns true when MM's march states ran it (OOT's chick action is skipped).
static s32 EnNwc_ComboChickUpdate(EnNwcChick* chick, EnNwc* this, PlayState* play) {
    if (chick->type == CHICK_NONE) {
        return false;
    }
    if (chick->comboState == 0) {
        EnNwc_ComboCheckFound(chick, this, play);
        if (chick->comboState == 0) {
            return false;
        }
    }
    EnNwc_ComboChickMove(chick, play);
    if (chick->comboState == COMBO_NWC_STATE_FOLLOWING) {
        EnNwc_ComboFollow(chick, this, play);
    } else if (chick->comboState == COMBO_NWC_STATE_RUNNING) {
        EnNwc_ComboRunAway(chick, this, play);
    }
    return true;
}
#endif

void EnNwc_UpdateChicks(EnNwc* this, PlayState* play) {
    static EnNwcChickFunc chickActionFuncs[] = { EnNwc_ChickNoop, EnNwc_ChickFall };
    EnNwcChick* chick = this->chicks;
    ColliderJntSphElement* element = this->collider.elements;
    Vec3f prevChickPos;
    s32 i;
    f32 test;

#ifdef COMBO_BUILD
    EnNwc_ComboUpdateTrail(this, play); // ComboShip: MM's Bremen Mask (MM EnHs func_80952F00)
#endif

    prevChickPos.y = 99999.9f;
    for (i = 0; i < this->count; i++, prevChickPos = chick->pos, chick++, element++) {
        Math_Vec3f_Copy(&chick->lastPos, &chick->pos);

#ifdef COMBO_BUILD
        // ComboShip: MM's Bremen Mask (MM En_Nwc:144, 162): marching chicks run MM's states instead
        if (!EnNwc_ComboChickUpdate(chick, this, play))
#endif
        chickActionFuncs[chick->type](chick, this, play);

        element->dim.worldSphere.center.x = chick->pos.x;
        element->dim.worldSphere.center.y = chick->pos.y;
        element->dim.worldSphere.center.z = chick->pos.z;

        test = chick->pos.y - prevChickPos.y;
        if (fabsf(test) < 10.0f) {
            f32 dx = chick->pos.x - prevChickPos.x;
            f32 dz = chick->pos.z - prevChickPos.z;

            test = SQ(dx) + SQ(dz);
            if (test < SQ(10.0f)) {
                if (test != 0.0f) {
                    chick->pos.x += dx / sqrtf(test);
                    chick->pos.z += dz / sqrtf(test);
                } else {
                    chick->pos.x += 1.0f;
                    chick->pos.z += 1.0f;
                }
            }
        }
    }
}

void EnNwc_DrawChicks(EnNwc* this, PlayState* play) {
    s32 i;
    Gfx* dList1;
    Gfx* dList2;
    Gfx* dList3;
    MtxF floorMat;
    EnNwcChick* chick;

    OPEN_DISPS(play->state.gfxCtx);
    func_80093C80(play);

    dList1 = POLY_XLU_DISP;
    dList2 = dList1 + 3 * this->count + 1;
    dList3 = dList2 + 2 * this->count + 1;

    gSPDisplayList(dList1++, gCuccoChickSetupBodyDL);
    gSPDisplayList(dList2++, gCuccoChickSetupEyeDL);
    gSPDisplayList(dList3++, gCuccoChickSetupBeakDL);

    chick = this->chicks;
    for (i = 0; i < this->count; i++, chick++) {
        if (chick->type != CHICK_NONE) {
            Mtx* mtx;

            FrameInterpolation_RecordOpenChild(chick, chick->epoch);
            Matrix_SetTranslateRotateYXZ(chick->pos.x, chick->pos.y + chick->height, chick->pos.z, &chick->rot);
            Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);
            mtx = MATRIX_NEWMTX(play->state.gfxCtx);
            gDPSetEnvColor(dList1++, 0, 100, 255, 255);
            gSPMatrix(dList1++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(dList1++, gCuccoChickBodyDL);
            gSPMatrix(dList2++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(dList2++, gCuccoChickEyesDL);
            gSPMatrix(dList3++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(dList3++, gCuccoChickBeakDL);
            FrameInterpolation_RecordCloseChild();
        }
    }

    chick = this->chicks;
    POLY_XLU_DISP = dList3;
    Gfx_SetupDL_44Xlu(play->state.gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, gCuccoChickSetupShadowDL);

    for (i = 0; i < this->count; i++, chick++) {
        if ((chick->type != CHICK_NONE) && (chick->floorPoly != NULL)) {
            FrameInterpolation_RecordOpenChild(chick, chick->epoch * 25);
            func_80038A28(chick->floorPoly, chick->pos.x, chick->floorY, chick->pos.z, &floorMat);
            Matrix_Put(&floorMat);
            Matrix_RotateY(chick->rot.y * (M_PI / 0x8000), MTXMODE_APPLY);
            Matrix_Scale(1.0f, 1.0f, 1.0f, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gCuccoChickShadowDL);
            FrameInterpolation_RecordCloseChild();
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void EnNwc_Init(Actor* thisx, PlayState* play) {
    s32 pad;
    EnNwc* this = (EnNwc*)thisx;
    ColliderJntSphElementInit elementInits[16];
    ColliderJntSphElementInit* element;
    EnNwcChick* chick;
    s32 i;

    element = sJntSphInit.elements = elementInits;
    for (i = 0; i < 16; i++, element++) {
        *element = sJntSphElementInit;
    }

    Collider_InitJntSph(play, &this->collider);
    Collider_SetJntSphAllocType1(play, &this->collider, &this->actor, &sJntSphInit);
    this->count = 16;
    chick = this->chicks;
    for (i = 0; i < this->count; i++, chick++) {
        chick->epoch++;
        chick->type = CHICK_NORMAL;
        chick->pos.x = thisx->world.pos.x + ((Rand_ZeroOne() * 100.0f) - 50.0f);
        chick->pos.y = thisx->world.pos.y + 20.0f;
        chick->pos.z = thisx->world.pos.z + ((Rand_ZeroOne() * 100.0f) - 50.0f);
        chick->height = 5;
    }
    EnNwc_SetUpdate(this, EnNwc_Idle);
}

void EnNwc_Destroy(Actor* thisx, PlayState* play) {
    s32 pad;
    EnNwc* this = (EnNwc*)thisx;

    Collider_FreeJntSph(play, &this->collider);
}

void EnNwc_Idle(EnNwc* this, PlayState* play) {
    EnNwc_UpdateChicks(this, play);
}

void EnNwc_Update(Actor* thisx, PlayState* play) {
    s32 pad;
    EnNwc* this = (EnNwc*)thisx;

    this->updateFunc(this, play);
    CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider.base);
}

void EnNwc_Draw(Actor* thisx, PlayState* play) {
    s32 pad;
    EnNwc* this = (EnNwc*)thisx;

    EnNwc_DrawChicks(this, play);
}
