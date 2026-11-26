/*
 * File: z_combo_magic_wind.c
 * Overlay: ovl_Combo_Magic_Wind (ComboShip)
 * Description: Ocarina of Time's Farore's Wind, cast in Termina.
 *
 * A line-for-line port of OOT's ovl_Magic_Wind (soh/src/overlays/actors/ovl_Magic_Wind/z_magic_wind.c): params 0 =
 * the cast (29 frames, then NA_SE_PL_MAGIC_WIND_NORMAL and the green cylinders grow over 60 animation frames, hold 50,
 * fade over 30), params 1 = the arrival at the warp point (the cylinders shrink from frame 60 to 0 with
 * NA_SE_PL_MAGIC_WIND_WARP). OOT's curve skeleton and animation (ovl_Magic_Wind sSkel / sAnim), OOT's two scrolling
 * cylinder models and OOT's vertex-alpha fade (sCylinderVtx), all from OOT's ResourceManager; killed by a played song.
 * The warp point itself (set / return / dispel) is OOT's player code, ported in z_player_combo_oot.inc.
 */

#include "z_combo_magic_wind.h"
#include "2s2h/BenPort.h"
#include "2s2h/Rando/MiscBehavior/ComboOotItems.h"

#include <stdio.h>

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_UPDATE_DURING_OCARINA)

void ComboMagicWind_Init(Actor* thisx, PlayState* play);
void ComboMagicWind_Destroy(Actor* thisx, PlayState* play);
void ComboMagicWind_Update(Actor* thisx, PlayState* play);
void ComboMagicWind_Draw(Actor* thisx, PlayState* play);

void ComboMagicWind_Shrink(ComboMagicWind* this, PlayState* play);
void ComboMagicWind_WaitForTimer(ComboMagicWind* this, PlayState* play);
void ComboMagicWind_FadeOut(ComboMagicWind* this, PlayState* play);
void ComboMagicWind_WaitAtFullSize(ComboMagicWind* this, PlayState* play);
void ComboMagicWind_Grow(ComboMagicWind* this, PlayState* play);

ActorProfile Combo_Magic_Wind_Profile = {
    /**/ ACTOR_COMBO_MAGIC_WIND,
    /**/ ACTORCAT_ITEMACTION,
    /**/ FLAGS,
    /**/ GAMEPLAY_KEEP,
    /**/ sizeof(ComboMagicWind),
    /**/ ComboMagicWind_Init,
    /**/ ComboMagicWind_Destroy,
    /**/ ComboMagicWind_Update,
    /**/ ComboMagicWind_Draw,
};

static u8 sAlphaUpdVals[] = {
    0x00, 0x03, 0x04, 0x07, 0x09, 0x0A, 0x0D, 0x0F, 0x11, 0x12, 0x15, 0x16, 0x19, 0x1B, 0x1C, 0x1F, 0x21, 0x23,
};

#define OOT_SKEL() ((CurveSkeletonHeader*)Combo_MmOotRawResource("overlays/ovl_Magic_Wind/sSkel"))
#define OOT_ANIM() ((CurveAnimationHeader*)Combo_MmOotRawResource("overlays/ovl_Magic_Wind/sAnim"))

// OOT's skeleton: a root and the two cylinders.
#define ROUTED_SKEL_LIMB_MAX 3
#define ROUTED_DL_PATH_MAX 128
#define OTR_PATH_PREFIX_LEN 7 // "__OTR__"

static SkelCurveLimb sRoutedLimbs[ROUTED_SKEL_LIMB_MAX];
static SkelCurveLimb* sRoutedLimbTable[ROUTED_SKEL_LIMB_MAX];
static char sRoutedDLPaths[ROUTED_SKEL_LIMB_MAX][ARRAY_COUNT(sRoutedLimbs[0].dList)][ROUTED_DL_PATH_MAX];
static CurveSkeletonHeader sRoutedSkel;

// A copy of OOT's curve skeleton whose limb models carry the "@oot:" route. OOT's skeleton names them as plain
// "__OTR__" paths, which MM's gSPDisplayList looks up in MM's archive when recorded, and MM has no ovl_Magic_Wind.
// Built once; NULL when OOT's skeleton is missing or not the shape above.
static CurveSkeletonHeader* ComboMagicWind_RoutedSkel(CurveSkeletonHeader* ootSkel) {
    s32 i;
    s32 j;

    if (sRoutedSkel.limbs != NULL) {
        return &sRoutedSkel;
    }
    if ((ootSkel == NULL) || (ootSkel->limbs == NULL) || (ootSkel->limbCount > ROUTED_SKEL_LIMB_MAX)) {
        return NULL;
    }
    for (i = 0; i < ootSkel->limbCount; i++) {
        SkelCurveLimb* limb = ootSkel->limbs[i];

        if (limb == NULL) {
            return NULL;
        }
        sRoutedLimbs[i] = *limb;
        for (j = 0; j < ARRAY_COUNT(limb->dList); j++) {
            char* path = (char*)limb->dList[j];

            if ((path == NULL) || !ResourceMgr_OTRSigCheck(path)) {
                continue; // no model, or raw OOT data: the draw's "oot" bracket covers it
            }
            if (snprintf(sRoutedDLPaths[i][j], ROUTED_DL_PATH_MAX, "__OTR__@oot:%s", path + OTR_PATH_PREFIX_LEN) >=
                ROUTED_DL_PATH_MAX) {
                return NULL;
            }
            sRoutedLimbs[i].dList[j] = (Gfx*)sRoutedDLPaths[i][j];
        }
        sRoutedLimbTable[i] = &sRoutedLimbs[i];
    }
    sRoutedSkel.limbCount = ootSkel->limbCount;
    sRoutedSkel.limbs = sRoutedLimbTable;
    return &sRoutedSkel;
}

static void ComboMagicWind_SetupAction(ComboMagicWind* this, ComboMagicWindFunc actionFunc) {
    this->actionFunc = actionFunc;
}

void ComboMagicWind_Init(Actor* thisx, PlayState* play) {
    ComboMagicWind* this = (ComboMagicWind*)thisx;
    Player* player = GET_PLAYER(play);
    CurveSkeletonHeader* skel = ComboMagicWind_RoutedSkel(OOT_SKEL());
    CurveAnimationHeader* anim = OOT_ANIM();

    if ((skel == NULL) || (anim == NULL) || !SkelCurve_Init(play, &this->skelCurve, skel, anim)) {
        Combo_MmOotLog("Farore's Wind: OOT's cylinders unavailable");
        Actor_Kill(&this->actor);
        return;
    }
    this->actor.room = -1;
    switch (this->actor.params) {
        case 0:
            SkelCurve_SetAnim(&this->skelCurve, anim, 0.0f, 60.0f, 0.0f, 1.0f);
            this->timer = 29;
            ComboMagicWind_SetupAction(this, ComboMagicWind_WaitForTimer);
            break;
        case 1:
            SkelCurve_SetAnim(&this->skelCurve, anim, 60.0f, 0.0f, 60.0f, -1.0f);
            ComboMagicWind_SetupAction(this, ComboMagicWind_Shrink);
            Player_PlaySfx(player, NA_SE_PL_MAGIC_WIND_WARP);
            break;
    }
}

void ComboMagicWind_Destroy(Actor* thisx, PlayState* play) {
    ComboMagicWind* this = (ComboMagicWind*)thisx;

    if (this->skelCurve.jointTable != NULL) {
        SkelCurve_Destroy(play, &this->skelCurve);
    }
    Magic_Reset(play);
}

static void ComboMagicWind_UpdateAlpha(f32 alpha) {
    s32 i;
    Vtx* vtx = (Vtx*)Combo_MmOotRawResource("overlays/ovl_Magic_Wind/sCylinderVtx");

    if (vtx == NULL) {
        return;
    }
    for (i = 0; i < ARRAY_COUNT(sAlphaUpdVals); i++) {
        vtx[sAlphaUpdVals[i]].n.a = alpha * 255.0f;
    }
}

void ComboMagicWind_WaitForTimer(ComboMagicWind* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (this->timer > 0) {
        this->timer--;
        return;
    }

    Player_PlaySfx(player, NA_SE_PL_MAGIC_WIND_NORMAL);
    ComboMagicWind_UpdateAlpha(1.0f);
    ComboMagicWind_SetupAction(this, ComboMagicWind_Grow);
    SkelCurve_Update(play, &this->skelCurve);
}

void ComboMagicWind_Grow(ComboMagicWind* this, PlayState* play) {
    if (SkelCurve_Update(play, &this->skelCurve)) {
        ComboMagicWind_SetupAction(this, ComboMagicWind_WaitAtFullSize);
        this->timer = 50;
    }
}

void ComboMagicWind_WaitAtFullSize(ComboMagicWind* this, PlayState* play) {
    if (this->timer > 0) {
        this->timer--;
    } else {
        ComboMagicWind_SetupAction(this, ComboMagicWind_FadeOut);
        this->timer = 30;
    }
}

void ComboMagicWind_FadeOut(ComboMagicWind* this, PlayState* play) {
    if (this->timer > 0) {
        ComboMagicWind_UpdateAlpha((f32)this->timer * (1.0f / 30.0f));
        this->timer--;
    } else {
        Actor_Kill(&this->actor);
    }
}

void ComboMagicWind_Shrink(ComboMagicWind* this, PlayState* play) {
    if (SkelCurve_Update(play, &this->skelCurve)) {
        Actor_Kill(&this->actor);
    }
}

void ComboMagicWind_Update(Actor* thisx, PlayState* play) {
    ComboMagicWind* this = (ComboMagicWind*)thisx;

    // OOT MSGMODE_OCARINA_CORRECT_PLAYBACK (0x0D) = MM MSGMODE_E (0x0E).
    if ((play->msgCtx.msgMode == MSGMODE_E) || (play->msgCtx.msgMode == MSGMODE_SONG_PLAYED)) {
        Actor_Kill(thisx);
        return;
    }
    this->actionFunc(this, play);
}

static s32 ComboMagicWind_OverrideLimbDraw(PlayState* play, SkelCurve* skelCurve, s32 limbIndex, Actor* thisx) {
    OPEN_DISPS(play->state.gfxCtx);

    if (limbIndex == 1) {
        gSPSegment(POLY_XLU_DISP++, 8,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, (play->state.frames * 9) & 0xFF,
                                      0xFF - ((play->state.frames * 0xF) & 0xFF), 0x40, 0x40, 1,
                                      (play->state.frames * 0xF) & 0xFF, 0xFF - ((play->state.frames * 0x1E) & 0xFF),
                                      0x40, 0x40, 9, -0xF, 0xF, -0x1E));
    } else if (limbIndex == 2) {
        gSPSegment(POLY_XLU_DISP++, 9,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, (play->state.frames * 3) & 0xFF,
                                      0xFF - ((play->state.frames * 5) & 0xFF), 0x40, 0x40, 1,
                                      (play->state.frames * 6) & 0xFF, 0xFF - ((play->state.frames * 0xA) & 0xFF), 0x40,
                                      0x40, 3, -5, 6, -0xA));
    }

    CLOSE_DISPS(play->state.gfxCtx);

    return true;
}

void ComboMagicWind_Draw(Actor* thisx, PlayState* play) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    ComboMagicWind* this = (ComboMagicWind*)thisx;

    OPEN_DISPS(gfxCtx);

    if (this->actionFunc != ComboMagicWind_WaitForTimer) {
        POLY_XLU_DISP = Gfx_SetupDL(POLY_XLU_DISP, SETUPDL_25);
        // The limb models are routed (ComboMagicWind_RoutedSkel); the bracket covers raw OOT data in the skeleton.
        gSPComboRMPush(POLY_OPA_DISP++, "oot");
        gSPComboRMPush(POLY_XLU_DISP++, "oot");
        SkelCurve_Draw(thisx, play, &this->skelCurve, ComboMagicWind_OverrideLimbDraw, NULL, 1, thisx);
        gSPComboRMPop(POLY_XLU_DISP++);
        gSPComboRMPop(POLY_OPA_DISP++);
    }

    CLOSE_DISPS(gfxCtx);
}
