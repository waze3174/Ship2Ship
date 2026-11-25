/*
 * File: z_combo_magic_dark.c
 * Overlay: ovl_Combo_Magic_Dark (ComboShip)
 * Description: Ocarina of Time's Nayru's Love, cast in Termina.
 *
 * A line-for-line port of OOT's ovl_Magic_Dark (soh/src/overlays/actors/ovl_Magic_Dark/z_magic_dark.c): the orb
 * gathering between Link's hands with the dimmed lighting (35 + 20 frames), then the spinning blue diamond around
 * Link for 1200 frames (flashing from 1100, fading from 1180) while Link cannot be hurt (invincibilityTimer -100 every
 * frame, OOT's protection), killed when a song is played, OOT's sounds (NA_SE_PL_MAGIC_SOUL_BALL / _NORMAL / _FLASH).
 * The diamond and the orb are OOT's models (ovl_Magic_Dark sDiamondMaterialDL / sDiamondModelDL, gameplay_keep
 * gEffFlash1DL), drawn from OOT's ResourceManager. The timer is MM's own save field nayrusLoveTimer (MM kept OOT's
 * "shield_magic_timer" as a remnant), so it carries across Termina's scene changes exactly as OOT's does across
 * Hyrule's (ComboOotItems.cpp re-casts it on arrival, OOT Player_Init).
 */

#include "z_combo_magic_dark.h"
#include "2s2h/Rando/MiscBehavior/ComboOotItems.h"
#include <libultraship/bridge/consolevariablebridge.h>

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_UPDATE_DURING_OCARINA)

// SoH's Infinite Nayru's Love (NayrusLove.cpp) holds the timer here, short of the flashing at 1100.
#define COMBO_INFINITE_NAYRU_TIMER 0x44B

#define OOT_DL(path) ((Gfx*)("__OTR__@oot:" path))

void ComboMagicDark_Init(Actor* thisx, PlayState* play);
void ComboMagicDark_Destroy(Actor* thisx, PlayState* play);
void ComboMagicDark_OrbUpdate(Actor* thisx, PlayState* play);
void ComboMagicDark_OrbDraw(Actor* thisx, PlayState* play);
void ComboMagicDark_DiamondUpdate(Actor* thisx, PlayState* play);
void ComboMagicDark_DiamondDraw(Actor* thisx, PlayState* play);

ActorProfile Combo_Magic_Dark_Profile = {
    /**/ ACTOR_COMBO_MAGIC_DARK,
    /**/ ACTORCAT_ITEMACTION,
    /**/ FLAGS,
    /**/ GAMEPLAY_KEEP,
    /**/ sizeof(ComboMagicDark),
    /**/ ComboMagicDark_Init,
    /**/ ComboMagicDark_Destroy,
    /**/ ComboMagicDark_OrbUpdate,
    /**/ ComboMagicDark_OrbDraw,
};

void ComboMagicDark_Init(Actor* thisx, PlayState* play) {
    ComboMagicDark* this = (ComboMagicDark*)thisx;
    Player* player = GET_PLAYER(play);

    // OOT: !LINK_IS_ADULT ? 0.4f : 0.6f
    this->scale = Combo_MM_AdultBodyActive(&player->actor) ? 0.6f : 0.4f;

    thisx->world.pos = player->actor.world.pos;
    Actor_SetScale(&this->actor, 0.0f);
    thisx->room = -1;

    if (gSaveContext.nayrusLoveTimer != 0) { // MM's remnant of OOT's timer (z64save.h)
        thisx->update = ComboMagicDark_DiamondUpdate;
        thisx->draw = ComboMagicDark_DiamondDraw;
        thisx->scale.x = thisx->scale.z = this->scale * 1.6f;
        thisx->scale.y = this->scale * 0.8f;
        this->timer = 0;
        this->primAlpha = 0;
    } else {
        this->timer = 0;
        gSaveContext.nayrusLoveTimer = 0;
        Combo_MmOotSpendNayrusLove(); // SoH's Nayru's Love Uses: a new cast, not one still running, costs a use
    }
}

void ComboMagicDark_Destroy(Actor* thisx, PlayState* play) {
    if (gSaveContext.nayrusLoveTimer == 0) {
        Magic_Reset(play);
    }
}

void ComboMagicDark_DiamondUpdate(Actor* thisx, PlayState* play) {
    ComboMagicDark* this = (ComboMagicDark*)thisx;
    u8 phi_a0;
    Player* player = GET_PLAYER(play);
    s16 nayrusLoveTimer;
    s32 msgMode = play->msgCtx.msgMode;

    if (CVarGetInteger(COMBO_SOH_CVAR_INFINITE_NAYRU, 0)) {
        gSaveContext.nayrusLoveTimer = COMBO_INFINITE_NAYRU_TIMER;
    }
    nayrusLoveTimer = gSaveContext.nayrusLoveTimer;

    // OOT MSGMODE_OCARINA_CORRECT_PLAYBACK (0x0D) = MM MSGMODE_E (0x0E); OOT MSGMODE_SONG_PLAYED = MM's.
    if ((msgMode == MSGMODE_E) || (msgMode == MSGMODE_SONG_PLAYED)) {
        Actor_Kill(thisx);
        return;
    }

    if (nayrusLoveTimer >= 1200) {
        player->invincibilityTimer = 0;
        gSaveContext.nayrusLoveTimer = 0;
        Actor_Kill(thisx);
        return;
    }

    player->invincibilityTimer = -100;
    thisx->scale.x = thisx->scale.z = this->scale;

    if (this->timer < 20) {
        thisx->scale.x = thisx->scale.z = (1.6f - (this->timer * 0.03f)) * this->scale;
        thisx->scale.y = ((this->timer * 0.01f) + 0.8f) * this->scale;
    } else {
        thisx->scale.x = thisx->scale.z = this->scale;
        thisx->scale.y = this->scale;
    }

    thisx->scale.x *= 1.3f;
    thisx->scale.z *= 1.3f;

    phi_a0 = (this->timer < 20) ? (this->timer * 12) : 255;

    if (nayrusLoveTimer >= 1180) {
        this->primAlpha = 15595 - (nayrusLoveTimer * 13);
        if (nayrusLoveTimer & 1) {
            this->primAlpha = this->primAlpha >> 1;
        }
    } else if (nayrusLoveTimer >= 1100) {
        this->primAlpha = (u8)(nayrusLoveTimer << 7) + 127;
    } else {
        this->primAlpha = 255;
    }

    if (this->primAlpha > phi_a0) {
        this->primAlpha = phi_a0;
    }

    thisx->world.rot.y += 0x3E8;
    thisx->shape.rot.y = thisx->world.rot.y + Camera_GetCamDirYaw(GET_ACTIVE_CAM(play));
    this->timer++;
    gSaveContext.nayrusLoveTimer = nayrusLoveTimer + 1;

    if (nayrusLoveTimer < 1100) {
        Actor_PlaySfx_Flagged(thisx, NA_SE_PL_MAGIC_SOUL_NORMAL - SFX_FLAG);
    } else {
        Actor_PlaySfx_Flagged(thisx, NA_SE_PL_MAGIC_SOUL_FLASH - SFX_FLAG);
    }
}

// OOT MagicDark_DimLighting
static void ComboMagicDark_DimLighting(PlayState* play, f32 intensity) {
    s32 i;
    f32 colorScale;
    f32 fogScale;

    if (play->roomCtx.curRoom.type != 5) { // OOT behaviorType1 != ROOM_BEHAVIOR_TYPE1_5 (MM Room.type)
        intensity = CLAMP_MIN(intensity, 0.0f);
        intensity = CLAMP_MAX(intensity, 1.0f);
        fogScale = intensity - 0.2f;

        if (intensity < 0.2f) {
            fogScale = 0.0f;
        }

        play->envCtx.adjLightSettings.fogNear = (850.0f - play->envCtx.lightSettings.fogNear) * fogScale;

        if (intensity == 0.0f) {
            for (i = 0; i < ARRAY_COUNT(play->envCtx.adjLightSettings.fogColor); i++) {
                play->envCtx.adjLightSettings.fogColor[i] = 0;
            }
        } else {
            colorScale = intensity * 5.0f;

            if (colorScale > 1.0f) {
                colorScale = 1.0f;
            }

            for (i = 0; i < ARRAY_COUNT(play->envCtx.adjLightSettings.fogColor); i++) {
                play->envCtx.adjLightSettings.fogColor[i] = -(s16)(play->envCtx.lightSettings.fogColor[i] * colorScale);
            }
        }
    }
}

void ComboMagicDark_OrbUpdate(Actor* thisx, PlayState* play) {
    ComboMagicDark* this = (ComboMagicDark*)thisx;
    Player* player = GET_PLAYER(play);

    Actor_PlaySfx_Flagged(&this->actor, NA_SE_PL_MAGIC_SOUL_BALL - SFX_FLAG);
    if (this->timer < 35) {
        ComboMagicDark_DimLighting(play, this->timer * (1 / 45.0f));
        Math_SmoothStepToF(&thisx->scale.x, this->scale * (1 / 12.000001f), 0.05f, 0.01f, 0.0001f);
        Actor_SetScale(&this->actor, thisx->scale.x);
    } else if (this->timer < 55) {
        Actor_SetScale(&this->actor, thisx->scale.x * 0.9f);
        Math_SmoothStepToF(&this->orbOffset.y, player->bodyPartsPos[0].y, 0.5f, 3.0f, 1.0f);
        if (this->timer > 48) {
            ComboMagicDark_DimLighting(play, (54 - this->timer) * 0.2f);
        }
    } else {
        thisx->update = ComboMagicDark_DiamondUpdate;
        thisx->draw = ComboMagicDark_DiamondDraw;
        thisx->scale.x = thisx->scale.z = this->scale * 1.6f;
        thisx->scale.y = this->scale * 0.8f;
        this->timer = 0;
        this->primAlpha = 0;
    }

    this->timer++;
}

void ComboMagicDark_DiamondDraw(Actor* thisx, PlayState* play) {
    ComboMagicDark* this = (ComboMagicDark*)thisx;
    u16 gameplayFrames = play->gameplayFrames;
    Player* player = GET_PLAYER(play);
    f32 heightDiff;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL25_Xlu(play->state.gfxCtx);

    this->actor.world.pos.x = player->bodyPartsPos[0].x;
    this->actor.world.pos.z = player->bodyPartsPos[0].z;
    heightDiff = player->bodyPartsPos[0].y - this->actor.world.pos.y;
    if (heightDiff < -2.0f) {
        this->actor.world.pos.y = player->bodyPartsPos[0].y + 2.0f;
    } else if (heightDiff > 2.0f) {
        this->actor.world.pos.y = player->bodyPartsPos[0].y - 2.0f;
    }
    Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
    Matrix_Scale(this->actor.scale.x, this->actor.scale.y, this->actor.scale.z, MTXMODE_APPLY);
    Matrix_RotateYS(this->actor.shape.rot.y, MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 170, 255, 255, (s32)(this->primAlpha * 0.6f) & 0xFF);
    gDPSetEnvColor(POLY_XLU_DISP++, 0, 100, 255, 128);
    gSPDisplayList(POLY_XLU_DISP++, OOT_DL("overlays/ovl_Magic_Dark/sDiamondMaterialDL"));
    gSPDisplayList(POLY_XLU_DISP++, Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, gameplayFrames * 2, gameplayFrames * -4,
                                                       32, 32, 1, 0, gameplayFrames * -16, 64, 32, 2, -4, 0, -16));
    gSPDisplayList(POLY_XLU_DISP++, OOT_DL("overlays/ovl_Magic_Dark/sDiamondModelDL"));

    CLOSE_DISPS(play->state.gfxCtx);
}

void ComboMagicDark_OrbDraw(Actor* thisx, PlayState* play) {
    ComboMagicDark* this = (ComboMagicDark*)thisx;
    Vec3f pos;
    Player* player = GET_PLAYER(play);
    f32 sp6C = play->state.frames & 0x1F;

    if (this->timer < 32) {
        // OOT bodyPartsPos[12] / [15]: the two hands.
        pos.x = (player->bodyPartsPos[12].x + player->bodyPartsPos[15].x) * 0.5f;
        pos.y = (player->bodyPartsPos[12].y + player->bodyPartsPos[15].y) * 0.5f;
        pos.z = (player->bodyPartsPos[12].z + player->bodyPartsPos[15].z) * 0.5f;
        if (this->timer > 20) {
            pos.y += (this->timer - 20) * 1.4f;
        }
        this->orbOffset = pos;
    } else if (this->timer < 130) {
        pos = this->orbOffset;
    } else {
        return;
    }

    pos.x -= (this->actor.scale.x * 300.0f * Math_SinS(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play))) *
              Math_CosS(Camera_GetCamDirPitch(GET_ACTIVE_CAM(play))));
    pos.y -= (this->actor.scale.x * 300.0f * Math_SinS(Camera_GetCamDirPitch(GET_ACTIVE_CAM(play))));
    pos.z -= (this->actor.scale.x * 300.0f * Math_CosS(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play))) *
              Math_CosS(Camera_GetCamDirPitch(GET_ACTIVE_CAM(play))));

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL25_Xlu(play->state.gfxCtx);

    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 170, 255, 255, 255);
    gDPSetEnvColor(POLY_XLU_DISP++, 0, 150, 255, 255);
    Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
    Matrix_Scale(this->actor.scale.x, this->actor.scale.y, this->actor.scale.z, MTXMODE_APPLY);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Push();
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
    Matrix_RotateZF(sp6C * (M_PIf / 32), MTXMODE_APPLY);
    gSPDisplayList(POLY_XLU_DISP++, OOT_DL("objects/gameplay_keep/gEffFlash1DL"));
    Matrix_Pop();
    Matrix_RotateZF(-sp6C * (M_PIf / 32), MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, OOT_DL("objects/gameplay_keep/gEffFlash1DL"));

    CLOSE_DISPS(play->state.gfxCtx);
}
