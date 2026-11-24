/*
 * File: z_en_item00_combo_strayfairy.c
 * ComboShip: Majora's Mask's Stray Fairy in Hyrule. The cross-world randomizer places MM stray fairies at OOT checks;
 * a freestanding one, or one released from a pot / crate / beehive / grass / rock / tree, is presented the way MM
 * presents a stray fairy: as MM's EnElforg, free-floating.
 *
 * A line-for-line port of MM's free-floating stray fairy (mm/src/overlays/actors/ovl_En_Elforg/z_en_elforg.c:
 * EnElforg_Init + EnElforg_InitializeParams, EnElforg_FreeFloating, EnElforg_MoveToTarget,
 * EnElforg_ApproachTargetYPosition / SpeedXZ, EnElforg_SetupFairyCollected / EnElforg_FairyCollected /
 * EnElforg_CirclePlayer, EnElforg_SpawnSparkles, EnElforg_Update, EnElforg_OverrideLimbDraw, EnElforg_Draw) and of
 * the container release in MM's Item_DropCollectible (mm z_en_item00.c: the fairy appears 40 units above the broken
 * container with NA_SE_EV_BUTTERFRY_TO_FAIRY). Drift around home at speed 1; with the Great Fairy's Mask worn
 * (ComboShip: MM's mask worn from OOT's pause, Combo_PauseWornMmMaskItem) fly to Link's waist at speed 5, and chime
 * NA_SE_SY_FAIRY_MASK_SUCCESS once per fairy each time the mask goes on; collected on contact (MM's ranges), heal three
 * hearts (Health_ChangeBy 0x30), circle Link for 80 frames with NA_SE_PL_CHIBI_FAIRY_HEAL. Model, animation and the
 * per-area colour animation are MM's own (gStrayFairySkel / gStrayFairyFlyingAnim / gStrayFair*TexAnim, loaded from
 * MM's ResourceManager: combo/menu/ComboForeignAnim.h); sounds are MM's own, played natively (ComboGuestAudio.cpp).
 *
 * What is OOT's: the check. MM's EnElforg sets its scene flag and, in MM's randomizer, the flag hands the check to the
 * rando queue (VB_GIVE_ITEM_FROM_ELFORG is false there; mm/2s2h/Rando/ActorBehavior/EnElforg.cpp). Here the fairy is
 * the check's EnItem00 (ITEM00_SOH_DUMMY) running MM's code: on collection it sets the same flag the EnItem00 would
 * (randomizer inf / collectible flag), so OOT's randomizer queues the check and its give path delivers the fairy into
 * MM's save (OOT_DeliverForeign) exactly as before.
 *
 * The EnItem00 adopts this mode on its first update (EnItem00_Update -> EnItem00_ComboStrayFairyAdopt) and swaps its
 * update / draw / destroy for these; the MM actor's instance data lives in a small pool keyed by the actor.
 */

#include "global.h"
#include <string.h>
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "soh/Enhancements/randomizer/ComboPause.h"

#ifdef COMBO_BUILD

// MM sound ids (mm include/sfx.h); the continuous one without the 0x800 flag, as MM's Actor_PlaySfx_Flagged passes it.
#define MM_SE_PL_CHIBI_FAIRY_HEAL (0x8CF - 0x800)
#define MM_SE_SY_FAIRY_MASK_SUCCESS 0x4854
#define MM_SE_EV_BUTTERFRY_TO_FAIRY 0x28E7

// mm assets/objects/gameplay_keep/gameplay_keep.h StrayFairyLimb
#define MM_STRAY_FAIRY_LIMB_RIGHT_FACING_HEAD 1
#define MM_STRAY_FAIRY_LIMB_LEFT_FACING_HEAD 9
#define MM_STRAY_FAIRY_LIMB_MAX 10

#define STRAY_FAIRY_FLAG_GREAT_FAIRYS_MASK_EQUIPPED (1 << 3)
#define STRAY_FAIRY_AREA_CLOCK_TOWN 0
#define STRAY_FAIRY_AREA_MAX 5

#define COMBO_STRAY_FAIRY_POOL 16

struct ComboStrayFairy;
typedef void (*ComboStrayFairyActionFunc)(struct ComboStrayFairy*, EnItem00*, PlayState*);

// MM's EnElforg instance data (minus the collider/enemy members of the types not ported: see the deviation doc).
typedef struct ComboStrayFairy {
    EnItem00* item00; // owner; NULL = free slot
    SkelAnime skelAnime;
    Vec3s jointTable[MM_STRAY_FAIRY_LIMB_MAX];
    u16 strayFairyFlags;
    s16 direction; // negative when facing right, positive when facing left
    s16 area;
    s32 timer;
    s32 secondaryTimer;
    f32 targetSpeedXZ;
    ComboStrayFairyActionFunc actionFunc;
    s32 rc;      // the randomizer check this fairy is
    s32 sfxSlot; // actor-positioned MM sound slot
} ComboStrayFairy;

static ComboStrayFairy sComboStrayFairies[COMBO_STRAY_FAIRY_POOL];

// Set by a free-floating stray fairy (and by a container holding one) each update, read by the Great Fairy's Mask
// hair (z_player_lib.c) and cleared at the end of Player_Draw: MM's ACTORCTX_FLAG_3.
s32 gComboStrayFairyNear = false;

void EnItem00_Destroy(Actor* thisx, PlayState* play);

// ComboStrayFairy.cpp (randomizer side)
s32 Combo_StrayFairyCheckOfItem00(EnItem00* item00);
void Combo_StrayFairyCollect(EnItem00* item00);
// draw.cpp (MM resources through combo/menu/ComboForeignAnim.h)
int Randomizer_ComboStrayFairyArea(int32_t rc);
int Randomizer_ComboStrayFairyInitSkel(PlayState* play, int32_t rc, SkelAnime* skelAnime, Vec3s* jointTable,
                                       int32_t count);
int Randomizer_DrawComboStrayFairy(PlayState* play, int32_t rc, SkelAnime* skelAnime, int32_t hiddenLimb);

static void ComboElforg_FreeFloating(ComboStrayFairy* this, EnItem00* item, PlayState* play);
static void ComboElforg_FairyCollected(ComboStrayFairy* this, EnItem00* item, PlayState* play);

static s32 sDbg = -1;
#define DBG (sDbg < 0 ? (sDbg = Combo_DebugEnv("COMBO_DEBUG_STRAYFAIRY")) : sDbg)

void Combo_StrayFairyNearClear(void) {
    gComboStrayFairyNear = false;
}

// MM Player_GetMask(play) == PLAYER_MASK_GREAT_FAIRY. OOT's currentMask stays PLAYER_MASK_NONE for an MM mask; the
// worn MM mask is the pause menu's. Worn only on human Link (MM: a transformation mask IS the current mask).
static s32 ComboElforg_GreatFairysMaskWorn(void) {
    return (Combo_OotForm() == COMBO_FORM_HUMAN) && (Combo_PauseWornMmMaskItem() == COMBO_MM_ITEM_MASK_GREAT_FAIRY) &&
           (Combo_PauseWornMmMaskDL() != NULL);
}

static ComboStrayFairy* ComboElforg_Get(Actor* thisx) {
    s32 i;

    for (i = 0; i < COMBO_STRAY_FAIRY_POOL; i++) {
        if (sComboStrayFairies[i].item00 == (EnItem00*)thisx) {
            return &sComboStrayFairies[i];
        }
    }
    return NULL;
}

// EnElforg_InitializeParams
static void ComboElforg_InitializeParams(ComboStrayFairy* this, EnItem00* item) {
    item->actor.speedXZ = 1.0f;
    this->targetSpeedXZ = 1.0f;
    item->actor.velocity.y = 0.0f;
    item->actor.world.rot.y = Rand_CenteredFloat(0x10000);
    this->timer = 0;
    this->secondaryTimer = Rand_ZeroFloat(100.0f);
    item->actor.shape.yOffset = 0.0f;
    this->skelAnime.curFrame = (s32)Rand_ZeroFloat(5.0f);
}

// EnElforg_SpawnSparkles
static void ComboElforg_SpawnSparkles(ComboStrayFairy* this, EnItem00* item, PlayState* play, s32 life) {
    static Vec3f sVelocity = { 0.0f, -0.05f, 0.0f };
    static Vec3f sAccel = { 0.0f, -0.025f, 0.0f };
    static Color_RGBA8 sPrimColors[] = {
        { 255, 235, 220, 255 }, { 255, 220, 220, 255 }, { 220, 255, 220, 255 },
        { 220, 220, 255, 255 }, { 255, 255, 200, 255 },
    };
    static Color_RGBA8 sEnvColors[] = {
        { 255, 150, 0, 255 }, { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 0, 255 },
    };
    Vec3f pos;
    s32 index;

    pos.x = Rand_CenteredFloat(6.0f) + item->actor.world.pos.x;
    pos.y = (Rand_ZeroOne() * 6.0f) + item->actor.world.pos.y + (item->actor.shape.yOffset * item->actor.scale.y);
    pos.z = Rand_CenteredFloat(6.0f) + item->actor.world.pos.z;
    index = (this->area < STRAY_FAIRY_AREA_CLOCK_TOWN || this->area >= STRAY_FAIRY_AREA_MAX)
                ? STRAY_FAIRY_AREA_CLOCK_TOWN
                : this->area;
    EffectSsKiraKira_SpawnDispersed(play, &pos, &sVelocity, &sAccel, &sPrimColors[index], &sEnvColors[index], 1000,
                                    life);
}

// EnElforg_ApproachTargetYPosition
static void ComboElforg_ApproachTargetYPosition(EnItem00* item, Vec3f* targetPos) {
    f32 yDifference = targetPos->y - item->actor.world.pos.y;

    if (fabsf(yDifference) < item->actor.speedXZ) {
        item->actor.world.pos.y = targetPos->y;
    } else if (yDifference > 0.0f) {
        item->actor.world.pos.y += item->actor.speedXZ;
    } else {
        item->actor.world.pos.y -= item->actor.speedXZ;
    }
}

// EnElforg_ApproachTargetSpeedXZ
static void ComboElforg_ApproachTargetSpeedXZ(ComboStrayFairy* this, EnItem00* item) {
    if (item->actor.speedXZ > this->targetSpeedXZ) {
        item->actor.speedXZ *= 0.9f;
    } else if (item->actor.speedXZ < (this->targetSpeedXZ - 0.1f)) {
        item->actor.speedXZ += 0.1f;
    } else {
        item->actor.speedXZ = this->targetSpeedXZ;
    }
}

// EnElforg_MoveToTarget. MM's Math_Atan2S_XY is OOT's Math_Atan2S; MM's Actor_MoveWithGravity is OOT's
// Actor_MoveXZGravity.
static void ComboElforg_MoveToTarget(ComboStrayFairy* this, EnItem00* item, Vec3f* targetPos) {
    s16 targetAngle;

    item->actor.shape.yOffset += 100.0f * Math_SinS(this->timer * 0x200);
    ComboElforg_ApproachTargetYPosition(item, targetPos);
    targetAngle = Math_Atan2S(-(item->actor.world.pos.z - targetPos->z), -(item->actor.world.pos.x - targetPos->x));

    if (this->targetSpeedXZ > 2.0f) {
        Math_SmoothStepToS(&item->actor.world.rot.y, targetAngle, 2, 0x400, 0x100);
    } else {
        // If the speed is below a threshold, deliberately mess up the target
        // angle so the fairy "orbits" the target instead of reaching it.
        targetAngle += 0x2000;
        Math_SmoothStepToS(&item->actor.world.rot.y, targetAngle, 10, 0x200, 0x80);
    }

    ComboElforg_ApproachTargetSpeedXZ(this, item);
    Actor_MoveXZGravity(&item->actor);
}

// func_80ACCBB8: play->actorCtx.flags |= ACTORCTX_FLAG_3
static void ComboElforg_SetNearFlag(void) {
    gComboStrayFairyNear = true;
}

// EnElforg_CirclePlayer
static void ComboElforg_CirclePlayer(ComboStrayFairy* this, EnItem00* item, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 orbitRadius;

    if (Combo_OotForm() == COMBO_FORM_GORON) { // GET_PLAYER_FORM == PLAYER_FORM_GORON
        orbitRadius = 40.0f;
    } else {
        orbitRadius = 20.0f;
    }

    item->actor.world.pos.x = (Math_SinS(this->timer * 0x1000) * orbitRadius) + player->actor.world.pos.x;
    item->actor.world.pos.z = (Math_CosS(this->timer * 0x1000) * orbitRadius) + player->actor.world.pos.z;
    item->actor.world.pos.y = player->bodyPartsPos[PLAYER_BODYPART_WAIST].y;

    ComboElforg_SpawnSparkles(this, item, play, 16);
}

// EnElforg_FairyCollected
static void ComboElforg_FairyCollected(ComboStrayFairy* this, EnItem00* item, PlayState* play) {
    ComboElforg_CirclePlayer(this, item, play);

    if (this->timer > 80) {
        Actor_Kill(&item->actor);
        return;
    }

    // Actor_PlaySfx_Flagged(&this->actor, NA_SE_PL_CHIBI_FAIRY_HEAL - SFX_FLAG): replayed every frame at the actor
    Combo_GuestPlaySfxAt(MM_SE_PL_CHIBI_FAIRY_HEAL, this->sfxSlot, &item->actor.projectedPos.x);
}

// EnElforg_SetupFairyCollected
static void ComboElforg_SetupFairyCollected(ComboStrayFairy* this, EnItem00* item, PlayState* play) {
    Player* player = GET_PLAYER(play);

    item->actor.world.pos.x = player->actor.world.pos.x;
    item->actor.world.pos.y = player->bodyPartsPos[PLAYER_BODYPART_WAIST].y;
    item->actor.world.pos.z = player->actor.world.pos.z;
    this->actionFunc = ComboElforg_FairyCollected;
    this->timer = 0;
    this->secondaryTimer = 0;
    item->actor.shape.yOffset = 0.0f;
}

// EnElforg_FreeFloating
static void ComboElforg_FreeFloating(ComboStrayFairy* this, EnItem00* item, PlayState* play) {
    Vec3f pos;
    f32 scaledYDistance;
    Player* player = GET_PLAYER(play);
    s32 maskWorn = ComboElforg_GreatFairysMaskWorn();

    SkelAnime_Update(&this->skelAnime);

    if (maskWorn) {
        pos = player->bodyPartsPos[PLAYER_BODYPART_WAIST];
        this->targetSpeedXZ = 5.0f;
        ComboElforg_MoveToTarget(this, item, &pos);
    } else {
        this->targetSpeedXZ = 1.0f;
        ComboElforg_MoveToTarget(this, item, &item->actor.home.pos);
    }

    // MM's playerHeightRel is OOT's yDistToPlayer (both Actor_HeightDiff(actor, player)).
    scaledYDistance = item->actor.yDistToPlayer - (item->actor.shape.yOffset * item->actor.scale.y);

    if (!Player_InCsMode(play)) {
        if ((item->actor.xzDistToPlayer < 30.0f) && (scaledYDistance < 12.0f) && (scaledYDistance > -68.0f)) {
            ComboElforg_SetupFairyCollected(this, item, play);
            Health_ChangeBy(play, 0x30);
            // MM sets the fairy's scene flag here; in MM's randomizer that flag hands the check to the rando queue
            // (VB_GIVE_ITEM_FROM_ELFORG false: no textbox, no stray-fairy count). Same here: the check's own flag.
            Combo_StrayFairyCollect(item);
            if (DBG) {
                Combo_DebugLog("[ComboShip] stray fairy rc=%d collected (mask=%d) at (%.0f %.0f %.0f)\n", this->rc,
                               maskWorn, item->actor.world.pos.x, item->actor.world.pos.y, item->actor.world.pos.z);
            }
        }
        Actor_UpdateBgCheckInfo(play, &item->actor, 20.0f, 20.0f, 20.0f, 1 | 2 | 4);
        ComboElforg_SetNearFlag();

        if (maskWorn) {
            if (!(this->strayFairyFlags & STRAY_FAIRY_FLAG_GREAT_FAIRYS_MASK_EQUIPPED)) {
                Combo_GuestPlaySfx(MM_SE_SY_FAIRY_MASK_SUCCESS); // Audio_PlaySfx(NA_SE_SY_FAIRY_MASK_SUCCESS)
                if (DBG) {
                    Combo_DebugLog("[ComboShip] stray fairy rc=%d: Great Fairy's Mask on -> FAIRY_MASK_SUCCESS\n",
                                   this->rc);
                }
            }

            this->strayFairyFlags |= STRAY_FAIRY_FLAG_GREAT_FAIRYS_MASK_EQUIPPED;
        } else {
            this->strayFairyFlags &= ~STRAY_FAIRY_FLAG_GREAT_FAIRYS_MASK_EQUIPPED;
        }
    }
}

// EnElforg_Update
static void EnItem00_ComboStrayFairy_Update(Actor* thisx, PlayState* play) {
    EnItem00* item = (EnItem00*)thisx;
    ComboStrayFairy* this = ComboElforg_Get(thisx);

    if (this == NULL) {
        Actor_Kill(thisx);
        return;
    }

    this->actionFunc(this, item, play);

    if ((this->timer == 0) && (this->secondaryTimer > 0)) {
        this->secondaryTimer--;
    } else {
        this->timer++;
    }

    if (this->direction < 0) {
        this->direction++;
        if (this->direction == 0) {
            this->direction = Rand_ZeroFloat(20.0f) + 20.0f;
        }
    } else if (this->direction > 0) {
        this->direction--;
    } else {
        this->direction = -Rand_ZeroFloat(20.0f) - 20.0f;
    }

    if (DBG && ((play->gameplayFrames % 20) == 0)) {
        Combo_DebugLog("[ComboShip] stray fairy rc=%d %s pos (%.1f %.1f %.1f) home (%.1f %.1f %.1f) speed %.2f "
                       "target %.2f xzDist %.1f yRel %.1f near=%d\n",
                       this->rc, (this->actionFunc == ComboElforg_FreeFloating) ? "free" : "collected",
                       item->actor.world.pos.x, item->actor.world.pos.y, item->actor.world.pos.z,
                       item->actor.home.pos.x, item->actor.home.pos.y, item->actor.home.pos.z, item->actor.speedXZ,
                       this->targetSpeedXZ, item->actor.xzDistToPlayer, item->actor.yDistToPlayer,
                       gComboStrayFairyNear);
    }
}

// EnElforg_Draw (+ EnElforg_OverrideLimbDraw: one of the two heads hidden by facing)
static void EnItem00_ComboStrayFairy_Draw(Actor* thisx, PlayState* play) {
    ComboStrayFairy* this = ComboElforg_Get(thisx);
    s32 hiddenLimb;

    if (this == NULL) {
        return;
    }
    hiddenLimb = (this->direction < 0) ? MM_STRAY_FAIRY_LIMB_LEFT_FACING_HEAD : MM_STRAY_FAIRY_LIMB_RIGHT_FACING_HEAD;
    if (!Randomizer_DrawComboStrayFairy(play, this->rc, &this->skelAnime, hiddenLimb) && DBG) {
        Combo_DebugLog("[ComboShip] stray fairy rc=%d: MM model unavailable this frame\n", this->rc);
    }
}

static void EnItem00_ComboStrayFairy_Destroy(Actor* thisx, PlayState* play) {
    ComboStrayFairy* this = ComboElforg_Get(thisx);

    if (this != NULL) {
        Combo_GuestStopSlot(this->sfxSlot);
        this->item00 = NULL;
    }
    EnItem00_Destroy(thisx, play);
}

// Called from EnItem00_Update for an ITEM00_SOH_DUMMY randomizer pickup: when its check holds an MM stray fairy, the
// item becomes MM's free-floating stray fairy (EnElforg_Init, STRAY_FAIRY_TYPE_FREE_FLOATING / _COLLECTIBLE). Returns
// true when adopted (the fairy's first update already ran).
s32 EnItem00_ComboStrayFairyAdopt(EnItem00* item, PlayState* play) {
    ComboStrayFairy* this = NULL;
    s32 rc;
    s32 area;
    s32 released;
    Vec3f spawnPos;
    s32 i;

    rc = Combo_StrayFairyCheckOfItem00(item);
    if (rc < 0) {
        return false;
    }
    area = Randomizer_ComboStrayFairyArea(rc);
    if (area < 0) {
        return false;
    }
    for (i = 0; i < COMBO_STRAY_FAIRY_POOL; i++) {
        // Stale (an actor freed without its destroy, e.g. a reset): its memory is no longer running our update.
        if ((sComboStrayFairies[i].item00 == item) ||
            ((sComboStrayFairies[i].item00 != NULL) &&
             (sComboStrayFairies[i].item00->actor.update != EnItem00_ComboStrayFairy_Update))) {
            sComboStrayFairies[i].item00 = NULL;
        }
    }
    for (i = 0; i < COMBO_STRAY_FAIRY_POOL; i++) {
        if (sComboStrayFairies[i].item00 == NULL) {
            this = &sComboStrayFairies[i];
            break;
        }
    }
    if (this == NULL) {
        if (DBG) {
            Combo_DebugLog("[ComboShip] stray fairy rc=%d: pool full, left as an item\n", rc);
        }
        return false;
    }
    memset(this, 0, sizeof(*this));
    if (!Randomizer_ComboStrayFairyInitSkel(play, rc, &this->skelAnime, this->jointTable, MM_STRAY_FAIRY_LIMB_MAX)) {
        if (DBG) {
            Combo_DebugLog("[ComboShip] stray fairy rc=%d: MM skeleton not loadable, left as an item\n", rc);
        }
        return false;
    }
    this->item00 = item;
    this->rc = rc;
    this->area = area;
    this->sfxSlot = Combo_GuestAllocSlot();

    // A container's drop (pot, crate, beehive, grass, rock, tree...) spawned as ITEM00_SOH_DUMMY; a freestanding
    // item became one in its init. MM's Item_DropCollectible releases a container's fairy 40 units up, with a sound.
    released = ((item->ogParams & 0xFF) == ITEM00_SOH_DUMMY);
    spawnPos = item->actor.world.pos;
    if (released) {
        item->actor.world.pos.y += 40.0f;
        Combo_GuestPlaySfxAtWorld(play, MM_SE_EV_BUTTERFRY_TO_FAIRY, &spawnPos.x);
    }
    item->actor.home.pos = item->actor.world.pos;
    item->actor.prevPos = item->actor.world.pos;

    // EnElforg_Init
    Actor_SetScale(&item->actor, 0.01f);
    item->scale = 0.01f;
    this->strayFairyFlags = 0;
    this->direction = 0;
    this->skelAnime.playSpeed = 1.0f;
    ActorShape_Init(&item->actor.shape, 0.0f, NULL, 0.0f);
    item->actor.shape.shadowAlpha = 255;
    item->actor.flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED; // MM's EnElforg FLAGS
    item->actor.gravity = 0.0f;                              // an MM actor's default; the drop set the item's own
    item->actor.minVelocityY = -20.0f;
    item->actor.speedXZ = 0.0f;
    item->actor.shape.rot.x = item->actor.shape.rot.z = 0;
    ComboElforg_InitializeParams(this, item);
    this->actionFunc = ComboElforg_FreeFloating;
    item->actor.shape.rot.y = 0;

    item->actor.update = EnItem00_ComboStrayFairy_Update;
    item->actor.draw = EnItem00_ComboStrayFairy_Draw;
    item->actor.destroy = EnItem00_ComboStrayFairy_Destroy;

    if (DBG) {
        Combo_DebugLog("[ComboShip] stray fairy rc=%d area %d adopted (%s) home (%.1f %.1f %.1f)\n", rc, area,
                       released ? "released from a container" : "freestanding", item->actor.home.pos.x,
                       item->actor.home.pos.y, item->actor.home.pos.z);
    }
    EnItem00_ComboStrayFairy_Update(&item->actor, play);
    return true;
}

#endif
