/*
 * File: z_combo_magic_fire.c
 * Overlay: ovl_Combo_Magic_Fire (ComboShip)
 * Description: Ocarina of Time's Din's Fire, cast in Termina.
 *
 * A line-for-line port of OOT's ovl_Magic_Fire (soh/src/overlays/actors/ovl_Magic_Fire/z_magic_fire.c): 20 frames
 * after the cast NA_SE_PL_MAGIC_FIRE plays and the fire sphere grows out of Link (30 frames to scale 0.4, holds 25,
 * then bursts outward until it has faded), with OOT's orange screen tint (fade in 20, hold 45, fade out 5), OOT's
 * growing cylinder hit (radius scale*325, height scale*450, damage actionTimer+25 / actionTimer), OOT's sphere model
 * and fire texture (ovl_Magic_Fire sMaterialDL / sModelDL / sTex / sSphereVtx from OOT's ResourceManager, its vertex
 * alphas faded exactly as OOT does), killed by a played song.
 *
 * What is MM's: the hit. OOT's DMG_MAGIC_FIRE is credited as MM's fire-arrow bit (DMG_FIRE_ARROW), so Termina's
 * torches, frozen things and fire-weak enemies react to Din's Fire as they react to fire (owner rule: only damage /
 * puzzle credit maps to the host game).
 */

#include "z_combo_magic_fire.h"
#include "2s2h/Rando/MiscBehavior/ComboOotItems.h"

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_UPDATE_DURING_OCARINA)

// MM's gSPDisplayList looks a plain "__OTR__" path up in MM's archive when the command is recorded, and MM has no
// ovl_Magic_Fire: the "@oot:" route hands the lookup to OOT's ResourceManager at draw time.
#define OOT_DL(path) ((Gfx*)("__OTR__@oot:" path))

void ComboMagicFire_Init(Actor* thisx, PlayState* play);
void ComboMagicFire_Destroy(Actor* thisx, PlayState* play);
void ComboMagicFire_Update(Actor* thisx, PlayState* play);
void ComboMagicFire_Draw(Actor* thisx, PlayState* play);
void ComboMagicFire_UpdateBeforeCast(Actor* thisx, PlayState* play);

typedef enum {
    /* 0x00 */ DF_ACTION_INITIALIZE,
    /* 0x01 */ DF_ACTION_EXPAND_SLOWLY,
    /* 0x02 */ DF_ACTION_STOP_EXPANDING,
    /* 0x03 */ DF_ACTION_EXPAND_QUICKLY
} ComboMagicFireAction;

typedef enum {
    /* 0x00 */ DF_SCREEN_TINT_NONE,
    /* 0x01 */ DF_SCREEN_TINT_FADE_IN,
    /* 0x02 */ DF_SCREEN_TINT_MAINTAIN,
    /* 0x03 */ DF_SCREEN_TINT_FADE_OUT,
    /* 0x04 */ DF_SCREEN_TINT_FINISHED
} ComboMagicFireScreenTint;

ActorProfile Combo_Magic_Fire_Profile = {
    /**/ ACTOR_COMBO_MAGIC_FIRE,
    /**/ ACTORCAT_ITEMACTION,
    /**/ FLAGS,
    /**/ GAMEPLAY_KEEP,
    /**/ sizeof(ComboMagicFire),
    /**/ ComboMagicFire_Init,
    /**/ ComboMagicFire_Destroy,
    /**/ ComboMagicFire_Update,
    /**/ ComboMagicFire_Draw,
};

static ColliderCylinderInit sCylinderInit = {
    {
        COL_MATERIAL_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEM_MATERIAL_UNK0,
        { DMG_FIRE_ARROW, 0x00, 0x01 }, // OOT 0x00020000 (DMG_MAGIC_FIRE), credited as MM's fire
        { 0x00000000, 0x00, 0x00 },
        ATELEM_ON | ATELEM_SFX_NONE,
        ACELEM_NONE,
        OCELEM_NONE,
    },
    { 9, 9, 0, { 0, 0, 0 } },
};

static InitChainEntry sInitChain[] = {
    ICHAIN_VEC3F(scale, 0, ICHAIN_STOP),
};

static u8 sVertexIndices[] = {
    3,  4,  5,  6,  7,  8,  9,  10, 16, 17, 18, 19, 25, 26, 27, 32, 35, 36, 37, 38,
    39, 45, 46, 47, 52, 53, 54, 59, 60, 61, 67, 68, 69, 70, 71, 72, 0,  1,  11, 12,
    14, 20, 21, 23, 28, 30, 33, 34, 40, 41, 43, 48, 50, 55, 57, 62, 64, 65, 73, 74,
};

// OOT MSGMODE_OCARINA_CORRECT_PLAYBACK (0x0D) = MM MSGMODE_E (0x0E).
#define COMBO_SONG_PLAYED(play) \
    (((play)->msgCtx.msgMode == MSGMODE_E) || ((play)->msgCtx.msgMode == MSGMODE_SONG_PLAYED))

void ComboMagicFire_Init(Actor* thisx, PlayState* play) {
    ComboMagicFire* this = (ComboMagicFire*)thisx;

    Actor_ProcessInitChain(&this->actor, sInitChain);
    this->action = 0;
    this->screenTintBehaviour = 0;
    this->actionTimer = 0;
    this->alphaMultiplier = -3.0f;
    Actor_SetScale(&this->actor, 0.0f);
    Collider_InitCylinder(play, &this->collider);
    Collider_SetCylinder(play, &this->collider, &this->actor, &sCylinderInit);
    Collider_UpdateCylinder(&this->actor, &this->collider);
    this->actor.update = ComboMagicFire_UpdateBeforeCast;
    this->actionTimer = 20;
    this->actor.room = -1;
}

void ComboMagicFire_Destroy(Actor* thisx, PlayState* play) {
    Magic_Reset(play);
}

void ComboMagicFire_UpdateBeforeCast(Actor* thisx, PlayState* play) {
    ComboMagicFire* this = (ComboMagicFire*)thisx;
    Player* player = GET_PLAYER(play);

    if (COMBO_SONG_PLAYED(play)) {
        Actor_Kill(&this->actor);
        return;
    }
    if (this->actionTimer > 0) {
        this->actionTimer--;
    } else {
        this->actor.update = ComboMagicFire_Update;
        Player_PlaySfx(player, NA_SE_PL_MAGIC_FIRE);
        Combo_MmOotLog("Din's Fire: sphere out");
    }
    this->actor.world.pos = player->actor.world.pos;
}

void ComboMagicFire_Update(Actor* thisx, PlayState* play) {
    ComboMagicFire* this = (ComboMagicFire*)thisx;
    Player* player = GET_PLAYER(play);

    this->actor.world.pos = player->actor.world.pos;
    if (COMBO_SONG_PLAYED(play)) {
        Actor_Kill(&this->actor);
        return;
    }
    if (this->action == DF_ACTION_EXPAND_SLOWLY) {
        this->collider.elem.atDmgInfo.damage = this->actionTimer + 25;
    } else if (this->action == DF_ACTION_STOP_EXPANDING) {
        this->collider.elem.atDmgInfo.damage = this->actionTimer;
    }
    Collider_UpdateCylinder(&this->actor, &this->collider);
    this->collider.dim.radius = (this->actor.scale.x * 325.0f);
    this->collider.dim.height = (this->actor.scale.y * 450.0f);
    this->collider.dim.yShift = (this->actor.scale.y * -225.0f);
    CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);

    switch (this->action) {
        case DF_ACTION_INITIALIZE:
            this->actionTimer = 30;
            this->actor.scale.x = this->actor.scale.y = this->actor.scale.z = 0.0f;
            this->actor.world.rot.x = this->actor.world.rot.y = this->actor.world.rot.z = 0;
            this->actor.shape.rot.x = this->actor.shape.rot.y = this->actor.shape.rot.z = 0;
            this->alphaMultiplier = 0.0f;
            this->scalingSpeed = 0.08f;
            this->action++;
            break;
        case DF_ACTION_EXPAND_SLOWLY: // Fire sphere slowly expands out of player for 30 frames
            Math_StepToF(&this->alphaMultiplier, 1.0f, 1.0f / 30.0f);
            if (this->actionTimer > 0) {
                Math_SmoothStepToF(&this->actor.scale.x, 0.4f, this->scalingSpeed, 0.1f, 0.001f);
                this->actor.scale.y = this->actor.scale.z = this->actor.scale.x;
            } else {
                this->actionTimer = 25;
                this->action++;
            }
            break;
        case DF_ACTION_STOP_EXPANDING: // Sphere stops expanding and maintains size for 25 frames
            if (this->actionTimer <= 0) {
                this->actionTimer = 15;
                this->action++;
                this->scalingSpeed = 0.05f;
            }
            break;
        case DF_ACTION_EXPAND_QUICKLY: // Sphere beings to grow again and quickly expands out until killed
            this->alphaMultiplier -= 8.0f / 119.00001f;
            this->actor.scale.x += this->scalingSpeed;
            this->actor.scale.y += this->scalingSpeed;
            this->actor.scale.z += this->scalingSpeed;
            if (this->alphaMultiplier <= 0.0f) {
                this->action = 0;
                Actor_Kill(&this->actor);
            }
            break;
    }
    switch (this->screenTintBehaviour) {
        case DF_SCREEN_TINT_NONE:
            if (this->screenTintBehaviourTimer <= 0) {
                this->screenTintBehaviourTimer = 20;
                this->screenTintBehaviour = DF_SCREEN_TINT_FADE_IN;
            }
            break;
        case DF_SCREEN_TINT_FADE_IN:
            this->screenTintIntensity = 1.0f - (this->screenTintBehaviourTimer / 20.0f);
            if (this->screenTintBehaviourTimer <= 0) {
                this->screenTintBehaviourTimer = 45;
                this->screenTintBehaviour = DF_SCREEN_TINT_MAINTAIN;
            }
            break;
        case DF_SCREEN_TINT_MAINTAIN:
            if (this->screenTintBehaviourTimer <= 0) {
                this->screenTintBehaviourTimer = 5;
                this->screenTintBehaviour = DF_SCREEN_TINT_FADE_OUT;
            }
            break;
        case DF_SCREEN_TINT_FADE_OUT:
            this->screenTintIntensity = (this->screenTintBehaviourTimer / 5.0f);
            if (this->screenTintBehaviourTimer <= 0) {
                this->screenTintBehaviour = DF_SCREEN_TINT_FINISHED;
            }
            break;
    }
    if (this->actionTimer > 0) {
        this->actionTimer--;
    }
    if (this->screenTintBehaviourTimer > 0) {
        this->screenTintBehaviourTimer--;
    }
}

void ComboMagicFire_Draw(Actor* thisx, PlayState* play) {
    ComboMagicFire* this = (ComboMagicFire*)thisx;
    u32 gameplayFrames = play->gameplayFrames;
    s32 i;
    u8 alpha;

    if (this->action > 0) {
        OPEN_DISPS(play->state.gfxCtx);
        POLY_XLU_DISP = Gfx_SetupDL57(POLY_XLU_DISP);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, (u8)(s32)(60 * this->screenTintIntensity),
                        (u8)(s32)(20 * this->screenTintIntensity), (u8)(s32)(0 * this->screenTintIntensity),
                        (u8)(s32)(120 * this->screenTintIntensity));
        gDPSetAlphaDither(POLY_XLU_DISP++, G_AD_DISABLE);
        gDPSetColorDither(POLY_XLU_DISP++, G_CD_DISABLE);
        gDPFillRectangle(POLY_XLU_DISP++, 0, 0, 319, 239);
        Gfx_SetupDL25_Xlu(play->state.gfxCtx);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 200, 0, (u8)(this->alphaMultiplier * 255));
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, (u8)(this->alphaMultiplier * 255));
        Matrix_Scale(0.15f, 0.15f, 0.15f, MTXMODE_APPLY);
        MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
        gDPPipeSync(POLY_XLU_DISP++);
        gSPTexture(POLY_XLU_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetTextureLUT(POLY_XLU_DISP++, G_TT_NONE);
        // OOT's own fire texture and sphere. The texture is looked up at draw time, inside the "oot" bracket.
        gSPComboRMPush(POLY_XLU_DISP++, "oot");
        gDPLoadTextureBlock(POLY_XLU_DISP++, "__OTR__overlays/ovl_Magic_Fire/sTex", G_IM_FMT_I, G_IM_SIZ_8b, 64, 64, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 6, 6, 15, G_TX_NOLOD);
        gDPSetTile(POLY_XLU_DISP++, G_IM_FMT_I, G_IM_SIZ_8b, 8, 0, 1, 0, G_TX_NOMIRROR | G_TX_WRAP, 6, 14,
                   G_TX_NOMIRROR | G_TX_WRAP, 6, 14);
        gDPSetTileSize(POLY_XLU_DISP++, 1, 0, 0, 252, 252);
        gSPDisplayList(POLY_XLU_DISP++, OOT_DL("overlays/ovl_Magic_Fire/sMaterialDL"));
        gSPDisplayList(POLY_XLU_DISP++,
                       Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, (gameplayFrames * 2) % 512,
                                          511 - ((gameplayFrames * 5) % 512), 64, 64, 1, (gameplayFrames * 2) % 256,
                                          255 - ((gameplayFrames * 20) % 256), 32, 32, 2, -5, 2, -20));
        gSPDisplayList(POLY_XLU_DISP++, OOT_DL("overlays/ovl_Magic_Fire/sModelDL"));
        gSPComboRMPop(POLY_XLU_DISP++);
        CLOSE_DISPS(play->state.gfxCtx);

        {
            Vtx* vertices = (Vtx*)Combo_MmOotRawResource("overlays/ovl_Magic_Fire/sSphereVtx");

            if (vertices != NULL) {
                alpha = (s32)(this->alphaMultiplier * 255);
                for (i = 0; i < 36; i++) {
                    vertices[sVertexIndices[i]].n.a = alpha;
                }
                alpha = (s32)(this->alphaMultiplier * 76);
                for (i = 36; i < 60; i++) {
                    vertices[sVertexIndices[i]].n.a = alpha;
                }
            }
        }
    }
}
