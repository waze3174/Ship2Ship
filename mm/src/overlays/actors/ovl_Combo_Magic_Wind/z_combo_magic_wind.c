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
#include "2s2h/Rando/MiscBehavior/ComboOotItems.h"

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

static void ComboMagicWind_SetupAction(ComboMagicWind* this, ComboMagicWindFunc actionFunc) {
    this->actionFunc = actionFunc;
}

void ComboMagicWind_Init(Actor* thisx, PlayState* play) {
    ComboMagicWind* this = (ComboMagicWind*)thisx;
    Player* player = GET_PLAYER(play);
    CurveSkeletonHeader* skel = OOT_SKEL();
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
        // OOT's curve skeleton's limb models are OOT paths: resolve them against OOT's ResourceManager.
        gSPComboRMPush(POLY_OPA_DISP++, "oot");
        gSPComboRMPush(POLY_XLU_DISP++, "oot");
        SkelCurve_Draw(thisx, play, &this->skelCurve, ComboMagicWind_OverrideLimbDraw, NULL, 1, thisx);
        gSPComboRMPop(POLY_XLU_DISP++);
        gSPComboRMPop(POLY_OPA_DISP++);
    }

    CLOSE_DISPS(gfxCtx);
}
