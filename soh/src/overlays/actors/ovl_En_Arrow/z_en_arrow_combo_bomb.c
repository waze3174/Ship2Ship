/*
 * File: z_en_arrow_combo_bomb.c
 * ComboShip: 2S2H's Bomb Arrows on OOT's bow in Hyrule, as an OOT EnArrow with a bomb on its tip.
 *
 * Ported from 2S2H's mm/2s2h/Enhancements/Equipment/BombArrows.cpp (OnBombArrowInit, OnBombArrowUpdate,
 * OnBombArrowDraw, OnPlayerReleaseHeldActor, BombArrowDetonate, TryConsumeBombAmmo, TryConsumeArrowOnHeldExpiry),
 * reading 2S2H's own CVar through ComboBombArrows.cpp. The pause menu, the flag and the HUD are there and in
 * z_kaleido_item.c / z_parameter.c.
 *
 * A shot from a bomb-arrow button spends one arrow, as every shot does, and one bomb at the release, from the bags of
 * the bow's game: OOT's own for OOT's bow, MM's for MM's bow in Hyrule (the shared-consumables tick, if on, carries the
 * counts to the other game). The arrow burns a 70-frame fuse from the nock, flashing as a bomb does. It goes off where
 * it hits an enemy or a wall, or in Link's hand when the fuse runs out while he still holds it (spending the arrow he
 * never shot), as a plain OOT bomb with its fuse at 0. An arrow that flies out without hitting anything is gone with
 * its bomb.
 *
 * The bomb on the tip is OOT's own bomb model (the cap and the body, flashing the way EnBom flashes them).
 */

#include "z_en_arrow.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "soh/Enhancements/randomizer/ComboBombArrows.h"

#ifdef COMBO_BUILD

#define BOMB_ARROW_FUSE_FRAMES 70             // 2S2H BombArrowData.timer, as EnBom's own fuse
#define BOMB_ARROW_FLASH_SPEED 7              // EnBom.flashSpeedScale at the start
#define BOMB_ARROW_FLASH_MAX 140.0f           // EnBom's flash peak
#define BOMB_ARROW_FLASH_SLOW_FRAMES_1 40     // EnBom: the flash doubles in speed with this many frames left,
#define BOMB_ARROW_FLASH_SLOW_FRAMES_2 20     // ...again here
#define BOMB_ARROW_FLASH_SLOW_FRAMES_3 3      // ...and here
#define BOMB_ARROW_FLASH_ALWAYS_BELOW 100     // EnBom: no flashing before the fuse is this low
#define BOMB_ARROW_TIP_OFFSET 1000.0f         // 2S2H: the bomb sits this far (model units) ahead of the arrow
#define BOMB_ARROW_SCALE 0.5f                 // the bomb's size against an OOT bomb's
#define BOMB_ARROW_FUSE_HEIGHT 10.0f          // where the sparks start, above the arrow (EnBom: 10, then 17)
#define BOMB_ARROW_BODY_ROT_Z 0x4000          // EnBom_Draw: the body model is turned a quarter after the cap
#define BOMB_ARROW_FUSE_SPARK_PERIOD_FRAMES 2 // EnBom: a spark every other frame
#define BOMB_ARROW_DUST_SCALE 50              // EnBom's fuse dust
#define BOMB_ARROW_DUST_SCALE_STEP 5

// MM's gating of what a flagged button fires (BombArrows.cpp OnBombArrowInit): plain arrows, not magic arrows or seeds.
static s32 EnArrow_ComboBombArrowType(s32 arrowType) {
    return (arrowType == ARROW_NORMAL) || (arrowType == ARROW_NORMAL_HORSE);
}

// The shot's bomb: 2S2H TryConsumeBombAmmo. One per shot; with none left the arrow flies as any arrow.
static void EnArrow_ComboBombArrowSpendBomb(EnArrow* this) {
    if (this->comboBombSpent || (Combo_BombArrowsBombAmmo(this->comboBombBowItem) <= 0)) {
        return;
    }
    Combo_BombArrowsSpendBomb(this->comboBombBowItem);
    this->comboBombSpent = true;
}

// OOT's bomb with its fuse at 0 (EnBom_ComboSpawnBlastMask's way): it goes off on its first update, before its first
// draw, so the blast sphere is put on the bomb by hand.
static void EnArrow_ComboBombArrowDetonate(EnArrow* this, PlayState* play) {
    EnBom* bomb;

    EnArrow_ComboBombArrowSpendBomb(this);
    if (this->comboBombSpent) {
        bomb = (EnBom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, this->actor.world.pos.x,
                                   this->actor.world.pos.y, this->actor.world.pos.z, 0, 0, 0, BOMB_BODY);
        if (bomb != NULL) {
            bomb->timer = 0;
            bomb->explosionCollider.elements[0].dim.worldSphere.center.x = bomb->actor.world.pos.x;
            bomb->explosionCollider.elements[0].dim.worldSphere.center.y = bomb->actor.world.pos.y;
            bomb->explosionCollider.elements[0].dim.worldSphere.center.z = bomb->actor.world.pos.z;
        }
    }
    Actor_Kill(&this->actor);
}

static void EnArrow_ComboBombArrowFuseEffects(EnArrow* this, PlayState* play) {
    static Vec3f velocity = { 0.0f, 0.0f, 0.0f };
    static Vec3f sparkAccel = { 0.0f, 0.0f, 0.0f };
    static Vec3f dustAccel = { 0.0f, 0.2f, 0.0f };
    static Color_RGBA8 dustColor = { 255, 255, 255, 255 };
    Vec3f fusePos = this->actor.world.pos;

    fusePos.y += BOMB_ARROW_FUSE_HEIGHT;
    if ((play->gameplayFrames % BOMB_ARROW_FUSE_SPARK_PERIOD_FRAMES) == 0) {
        EffectSsGSpk_SpawnFuse(play, &this->actor, &fusePos, &velocity, &sparkAccel);
    }
    Audio_PlayActorSound2(&this->actor, NA_SE_IT_BOMB_IGNIT - SFX_FLAG);
    func_8002829C(play, &fusePos, &velocity, &dustAccel, &dustColor, &dustColor, BOMB_ARROW_DUST_SCALE,
                  BOMB_ARROW_DUST_SCALE_STEP);
}

// EnBom_Update: the flash speeds up as the fuse runs down.
static void EnArrow_ComboBombArrowFlash(EnArrow* this) {
    if ((this->comboBombFuse == BOMB_ARROW_FLASH_SLOW_FRAMES_1) ||
        (this->comboBombFuse == BOMB_ARROW_FLASH_SLOW_FRAMES_2) ||
        (this->comboBombFuse == BOMB_ARROW_FLASH_SLOW_FRAMES_3)) {
        this->comboBombFlashSpeed >>= 1;
    }
    if ((this->comboBombFuse < BOMB_ARROW_FLASH_ALWAYS_BELOW) &&
        ((this->comboBombFuse & (this->comboBombFlashSpeed + 1)) != 0)) {
        Math_SmoothStepToF(&this->comboBombFlash, BOMB_ARROW_FLASH_MAX, 1.0f,
                           BOMB_ARROW_FLASH_MAX / this->comboBombFlashSpeed, 0.0f);
    } else {
        Math_SmoothStepToF(&this->comboBombFlash, 0.0f, 1.0f, BOMB_ARROW_FLASH_MAX / this->comboBombFlashSpeed, 0.0f);
    }
}

void EnArrow_ComboBombArrowTag(EnArrow* this, PlayState* play, s32 heldItemButton, s32 arrowType) {
    const s32 hudButtonOfFirstC = 1; // heldItemButton counts the HUD's buttons, 0 = B

    if ((heldItemButton < hudButtonOfFirstC) || !EnArrow_ComboBombArrowType(arrowType) ||
        !Combo_BombArrowsButtonActive(heldItemButton - hudButtonOfFirstC)) {
        return;
    }
    const s32 bowItem = gSaveContext.equips.buttonItems[heldItemButton];

    // The two minigames count their own arrows and have no use for a bomb.
    if ((Combo_BombArrowsBombAmmo(bowItem) <= 0) || (gSaveContext.minigameState == 1) ||
        (play->shootingGalleryStatus != 0)) {
        return;
    }
    this->comboBombArrow = true;
    this->comboBombBowItem = bowItem;
    this->comboBombFuse = BOMB_ARROW_FUSE_FRAMES;
    this->comboBombFlashSpeed = BOMB_ARROW_FLASH_SPEED;
}

void EnArrow_ComboBombArrowRelease(Actor* heldActor) {
    EnArrow* this = (EnArrow*)heldActor;

    if ((heldActor->id == ACTOR_EN_ARROW) && this->comboBombArrow) {
        EnArrow_ComboBombArrowSpendBomb(this);
    }
}

s32 EnArrow_ComboBombArrowUpdate(EnArrow* this, PlayState* play) {
    if (Player_InBlockingCsMode(play, GET_PLAYER(play))) {
        return false;
    }

    if (this->comboBombFuse > 0) {
        this->comboBombFuse--;
    }
    if (this->comboBombFuse <= 0) {
        if (this->actor.parent != NULL) {
            // the arrow Link never shot (2S2H TryConsumeArrowOnHeldExpiry)
            Combo_BombArrowsSpendArrow(this->comboBombBowItem);
        }
        EnArrow_ComboBombArrowDetonate(this, play);
        return true;
    }

    EnArrow_ComboBombArrowFlash(this);
    EnArrow_ComboBombArrowFuseEffects(this, play);

    // It goes off at a hit only once it has left Link's hand.
    if ((this->actor.parent == NULL) && ((this->collider.base.atFlags & AT_HIT) || this->touchedPoly)) {
        EnArrow_ComboBombArrowDetonate(this, play);
        return true;
    }
    return false;
}

// The bomb at the arrow's tip. Called with the arrow's own matrix on top (Actor_Draw's).
void EnArrow_ComboBombArrowDraw(EnArrow* this, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Translate(0.0f, 0.0f, BOMB_ARROW_TIP_OFFSET, MTXMODE_APPLY);
    Matrix_Scale(BOMB_ARROW_SCALE, BOMB_ARROW_SCALE, BOMB_ARROW_SCALE, MTXMODE_APPLY);
    func_8002EBCC(&this->actor, play, 0);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, gBombCapDL);
    Matrix_RotateZYX(BOMB_ARROW_BODY_ROT_Z, 0, 0, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, (s16)this->comboBombFlash, 0, 40, 255);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, (s16)this->comboBombFlash, 0, 40, 255);
    gSPDisplayList(POLY_OPA_DISP++, gBombBodyDL);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

#endif // COMBO_BUILD
