// soh/soh/Enhancements/randomizer/ComboCrater.c — ComboShip: MM's EnTest (mm/src/overlays/actors/ovl_En_Test), the
// crater a Goron's punch leaves in a wall and his pound leaves in the floor, line for line. MM spawns it as an actor;
// here it is a small pool Player updates and draws (OOT's actor table has no free slot for it, and OOT's ACTOR_EN_TEST
// is the Stalfos). Its keyframe skeleton, animation, animated materials and debris are MM's gameplay_keep data, loaded
// by MM's own resource manager and drawn under MM's resource scope.
#include "global.h"
#include "ComboCrater.h"
#include "ComboKeyframe.h"
#include "ComboForms.h"

void* Combo_MmRawResource(const char* otrPath); // ComboForms.cpp (MM's resource manager)

#define COMBO_CRATER_MAX 4
#define COMBO_CRATER_DEBRIS 20

#define kCraterSkel "__OTR__objects/gameplay_keep/gGameplayKeepKFSkel_6EB70"
#define kCraterAnim "__OTR__objects/gameplay_keep/gGameplayKeepKFAnim_6BB0C"
#define kCraterMatDirt "__OTR__objects/gameplay_keep/gameplay_keep_Matanimheader_06B6A0"
#define kCraterMatSnow "__OTR__objects/gameplay_keep/gameplay_keep_Matanimheader_06B730"
#define kCraterDebrisDL "__OTR__@mm:objects/gameplay_keep/gameplay_keep_DL_06AB30"

typedef struct {
    s32 active;   // unk_00
    s32 timer;    // unk_04
    Vec3f pos;    // unk_08
    Vec3f vel;    // unk_14
    Vec3f accel;  // unk_20
    f32 scale;    // unk_2C
    Vec3s rot;    // unk_30
    Vec3s rotVel; // unk_36
} ComboCraterDebris; // MM EnTestStruct

typedef struct {
    s32 active;
    Vec3f pos;      // actor.world.pos
    Vec3s rot;      // actor.shape.rot
    f32 scale;      // actor.scale
    s16 params;     // actor.params
    ComboKFSkelAnimeFlex kfSkelAnime;
    s32 debrisThrown; // unk_174 & 1
    Vec3s jointTable[12];
    Vec3s morphTable[12];
    u8 curFrame;    // unk_208
    u8 fade;        // unk_209
    u8 snow;        // surfaceMaterial == SURFACE_MATERIAL_SNOW
    ComboCraterDebris debris[COMBO_CRATER_DEBRIS];
} ComboCrater; // MM EnTest

static ComboCrater sCraters[COMBO_CRATER_MAX];

// MM func_80862B70
static void Combo_Crater_InitDebris(ComboCraterDebris* debris) {
    s32 i;

    for (i = 0; i < COMBO_CRATER_DEBRIS; i++) {
        ComboCraterDebris* ptr = &debris[i];

        ptr->active = false;
        ptr->timer = 0;
        ptr->pos.x = ptr->pos.y = ptr->pos.z = 0.0f;
        ptr->vel.x = ptr->vel.y = ptr->vel.z = 0.0f;
        ptr->accel.x = ptr->accel.y = ptr->accel.z = 0.0f;
        ptr->scale = 0.001f;
        ptr->rot.x = ptr->rot.y = ptr->rot.z = 0;
        ptr->rotVel.x = ptr->rotVel.y = ptr->rotVel.z = 0;
    }
}

// MM func_80862CBC: throw one chunk
static void Combo_Crater_ThrowDebris(ComboCraterDebris* debris, Vec3f* pos) {
    s32 i;

    for (i = 0; i < COMBO_CRATER_DEBRIS; i++) {
        ComboCraterDebris* ptr = &debris[i];

        if (!ptr->active) {
            s16 angle = Rand_ZeroOne() * 0xFFFF;
            f32 spread;

            ptr->active = true;
            ptr->pos.x = (Math_CosS(angle) * 20.0f) + pos->x;
            ptr->pos.y = pos->y;
            ptr->pos.z = (Math_SinS(angle) * 20.0f) + pos->z;
            spread = Rand_ZeroOne();
            ptr->vel.x = Math_CosS(angle) * 13.0f * spread;
            ptr->vel.y = (Rand_ZeroOne() * 5.0f) + 8.0f;
            ptr->vel.z = (Math_SinS(angle) * 13.0f) * spread;
            ptr->accel.x = 0.0f;
            ptr->accel.z = 0.0f;
            ptr->accel.y = -2.0f;
            ptr->scale = (Rand_ZeroOne() * 0.0004f) + 0.0004f;
            ptr->rot.x = Rand_ZeroOne() * 0x7FFE;
            ptr->rot.y = Rand_ZeroOne() * 0x7FFE;
            ptr->rot.z = Rand_ZeroOne() * 0x7FFE;
            ptr->rotVel.x = Rand_ZeroOne() * 0x2000;
            ptr->rotVel.y = Rand_ZeroOne() * 0x2000;
            ptr->rotVel.z = Rand_ZeroOne() * 0x2000;
            ptr->timer = 10;
            break;
        }
    }
}

// MM func_80862EDC
static void Combo_Crater_UpdateDebris(ComboCraterDebris* debris) {
    s32 i;

    for (i = 0; i < COMBO_CRATER_DEBRIS; i++) {
        ComboCraterDebris* ptr = &debris[i];

        if (ptr->active) {
            ptr->timer--;
            if (ptr->timer < 0) {
                ptr->active = false;
            }
            ptr->pos.x += ptr->vel.x;
            ptr->pos.y += ptr->vel.y;
            ptr->pos.z += ptr->vel.z;
            ptr->vel.x += ptr->accel.x;
            ptr->vel.y += ptr->accel.y;
            ptr->vel.z += ptr->accel.z;
            ptr->rot.x += ptr->rotVel.x;
            ptr->rot.y += ptr->rotVel.y;
            ptr->rot.z += ptr->rotVel.z;
        }
    }
}

// MM func_80863048
static void Combo_Crater_DrawDebris(PlayState* play, ComboCraterDebris* debris) {
    s32 i;

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0x80, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
    for (i = 0; i < COMBO_CRATER_DEBRIS; i++) {
        ComboCraterDebris* ptr = &debris[i];

        if (!ptr->active) {
            continue;
        }
        Matrix_Translate(ptr->pos.x, ptr->pos.y, ptr->pos.z, MTXMODE_NEW);
        Matrix_RotateZYX(ptr->rot.x, ptr->rot.y, ptr->rot.z, MTXMODE_APPLY);
        Matrix_Scale(ptr->scale, ptr->scale, ptr->scale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)kCraterDebrisDL);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// MM EnTest_Init
void Combo_Crater_Spawn(PlayState* play, Vec3f* pos, Vec3s* rot, s16 params) {
    ComboKeyFrameFlexSkeleton* skel = (ComboKeyFrameFlexSkeleton*)Combo_MmRawResource(kCraterSkel);
    ComboKeyFrameAnimation* anim = (ComboKeyFrameAnimation*)Combo_MmRawResource(kCraterAnim);
    ComboCrater* this = NULL;
    s32 i;

    if ((skel == NULL) || (anim == NULL)) {
        return;
    }
    for (i = 0; i < COMBO_CRATER_MAX; i++) {
        if (!sCraters[i].active) {
            this = &sCraters[i];
            break;
        }
    }
    if (this == NULL) {
        this = &sCraters[0]; // the oldest makes room (MM: another actor)
        for (i = 1; i < COMBO_CRATER_MAX; i++) {
            if (sCraters[i].fade > this->fade) {
                this = &sCraters[i];
            }
        }
    }

    this->fade = 0;
    this->debrisThrown = 0;
    this->params = params;
    this->pos = *pos;
    this->rot = *rot;
    this->scale = 0.01f; // Actor_Init's default
    if (params > 0) {
        this->scale = params / 100000.0f;
        this->snow = false; // SURFACE_MATERIAL_DIRT
    } else {
        CollisionPoly* floorPoly = NULL;
        s32 bgId;
        f32 floorHeight;
        MtxF mf;

        this->pos.y += 10.0f;
        floorHeight = BgCheck_EntityRaycastFloor3(&play->colCtx, &floorPoly, &bgId, &this->pos);
        if ((floorPoly == NULL) || (floorHeight == BGCHECK_Y_MIN)) {
            return; // MM: Actor_Kill
        }
        this->pos.y = floorHeight;
        func_80038A28(floorPoly, this->pos.x, floorHeight, this->pos.z, &mf); // MM func_800C0094
        Matrix_MtxFToYXZRotS(&mf, &this->rot, true);
        // MM SURFACE_MATERIAL_SNOW. Hyrule has no snow surface; its ice is the one that crunches like it.
        this->snow = (SurfaceType_GetSfx(&play->colCtx, floorPoly, bgId) == (NA_SE_PL_WALK_ICE - NA_SE_PL_WALK_GROUND));
    }
    ComboKeyframe_InitFlex(&this->kfSkelAnime, skel, anim, this->jointTable, this->morphTable, NULL);
    ComboKeyframe_FlexPlayOnce(&this->kfSkelAnime, anim);
    this->kfSkelAnime.frameCtrl.curTime = 9.0f;
    Combo_Crater_InitDebris(this->debris);
    this->active = true;
}

// MM EnTest_Update
void Combo_Crater_Update(void) {
    s32 c;
    s32 i;

    for (c = 0; c < COMBO_CRATER_MAX; c++) {
        ComboCrater* this = &sCraters[c];

        if (!this->active) {
            continue;
        }
        this->curFrame = this->kfSkelAnime.frameCtrl.curTime;
        if (ComboKeyframe_UpdateFlex(&this->kfSkelAnime) && (this->params != -1)) {
            this->fade++;
            if (this->fade > 20) {
                this->active = false; // MM: Actor_Kill
                continue;
            }
        }
        if ((this->params != -1) && (this->curFrame >= 2)) {
            if (!(this->debrisThrown & 1)) {
                this->debrisThrown |= 1;
                for (i = 0; i < COMBO_CRATER_DEBRIS; i++) {
                    Combo_Crater_ThrowDebris(this->debris, &this->pos);
                }
            }
        }
        Combo_Crater_UpdateDebris(this->debris);
    }
}

// MM EnTest_OverrideLimbDraw
static s32 Combo_Crater_OverrideLimbDraw(PlayState* play, ComboKFSkelAnimeFlex* kfSkelAnime, s32 limbIndex,
                                         Gfx** dList, u8* flags, void* arg, Vec3f* scale, Vec3s* rot, Vec3f* pos) {
    ComboCrater* this = (ComboCrater*)arg;

    OPEN_DISPS(play->state.gfxCtx);
    if ((this->params == -1) && ((limbIndex == 1) || (limbIndex == 2))) {
        *dList = NULL;
    }
    if ((this->params == -2) && (limbIndex == 3)) {
        *dList = NULL;
    }
    if (limbIndex == 3) {
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 0, 0, 0, ((20 - this->fade) * 255.0f) / 20.0f);
        gDPSetEnvColor(POLY_XLU_DISP++, 0, 0, 0, ((20 - this->fade) * 255.0f) / 20.0f);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    return true;
}

// MM EnTest_Draw (with Actor_Draw's model matrix: world pos, shape rot, scale)
void Combo_Crater_Draw(PlayState* play) {
    s32 c;

    // The pool draws from inside Player_Draw, where the matrix on top is Link's own model matrix (Actor_Draw set it):
    // the crater and its debris (MTXMODE_NEW) must not leave theirs behind, or Link's skeleton is drawn at the last
    // chunk, shrunk out of sight (owner 2026-11-14: "during goron pound, the player model can dissapear for a
    // second"). MM draws EnTest as its own actor, so it never had to.
    Matrix_Push();
    for (c = 0; c < COMBO_CRATER_MAX; c++) {
        ComboCrater* this = &sCraters[c];
        s32 step;
        Mtx* mtxStack;

        if (!this->active) {
            continue;
        }
        step = this->curFrame - 1;
        if (step >= 29) {
            step = 29;
        }

        OPEN_DISPS(play->state.gfxCtx);
        gSPComboRMPush(POLY_OPA_DISP++, "mm");
        gSPComboRMPush(POLY_XLU_DISP++, "mm");

        Matrix_SetTranslateRotateYXZ(this->pos.x, this->pos.y, this->pos.z, &this->rot);
        Matrix_Scale(this->scale, this->scale, this->scale, MTXMODE_APPLY);

        Combo_MmAnimatedMatStep(play, this->snow ? kCraterMatSnow : kCraterMatDirt, 1.0f, 3, (u32)MAX(step, 0));

        mtxStack = Graph_Alloc(play->state.gfxCtx, this->kfSkelAnime.skeleton->dListCount * sizeof(Mtx));
        if (mtxStack != NULL) {
            Gfx_SetupDL_25Xlu(play->state.gfxCtx);
            ComboKeyframe_DrawFlex(play, &this->kfSkelAnime, mtxStack, Combo_Crater_OverrideLimbDraw, NULL, this);
            Combo_Crater_DrawDebris(play, this->debris);
        }

        gSPComboRMPop(POLY_XLU_DISP++);
        gSPComboRMPop(POLY_OPA_DISP++);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    Matrix_Pop();
}

void Combo_Crater_Clear(void) {
    s32 c;

    for (c = 0; c < COMBO_CRATER_MAX; c++) {
        sCraters[c].active = false;
    }
}
