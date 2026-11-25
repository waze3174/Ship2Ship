/*
 * z_player_combo_oot.inc — ComboShip: OOT items in Termina, the player side. Included at the end of MM's z_player.c
 * (COMBO_BUILD only) so these ports call MM's own static player helpers.
 *
 * Each block is a port of OOT's item action (soh/src/overlays/actors/ovl_player_actor/z_player.c) onto MM's player:
 * OOT's function chain, OOT's animations (OOT's archive, "@oot:" routed), OOT's frame timings and sounds. Where OOT's
 * player and MM's player share a helper (MM's player is OOT's, extended), the MM name of the same function is used;
 * the OOT name is given next to each.
 *
 * Field / helper names, OOT -> MM:
 *   upperSkelAnime -> skelAnimeUpper, unk_834 -> unk_ACC, unk_6AD -> unk_AA5, unk_870 -> unk_B40, unk_A73 -> unk_D57,
 *   boomerangActor -> zoraBoomerangActor, PLAYER_STATE1_(USING_)BOOMERANG(_THROWN) -> ..._ZORA_BOOMERANG...,
 *   func_80834758 (shield) -> func_80830B88, func_80834E44 -> func_80830F9C, func_80834EB8 -> func_80831010,
 *   sUseHeldItem -> sPlayerUseHeldItem, sHeldItemButtonIsHeldDown -> sPlayerHeldItemButtonIsHeldDown,
 *   func_808334E4 / func_80833528 (boomerang waits) -> func_8082EF54 / func_8082EF9C.
 */

#define COMBO_OOT_ANIM(name) ((PlayerAnimationHeader*)"__OTR__@oot:objects/gameplay_keep/" name)

// =====================================================================================================================
// Boomerang (OOT PLAYER_IA_BOOMERANG): func_80835800 / func_80835884 / func_808358F0 / func_808359FC / func_80835B60 /
// func_80835C08, Player_InitBoomerangIA; the flying boomerang is OOT's EnBoom (ovl_En_Boom/z_en_boom_combo_oot.c).
// =====================================================================================================================
s32 ComboOot_Boomerang_Ready(Player* this, PlayState* play);
s32 ComboOot_Boomerang_Aim(Player* this, PlayState* play);
s32 ComboOot_Boomerang_Throw(Player* this, PlayState* play);
s32 ComboOot_Boomerang_Out(Player* this, PlayState* play);
s32 ComboOot_Boomerang_Catch(Player* this, PlayState* play);

// OOT Player_InitBoomerangIA
void ComboOot_InitBoomerangIA(PlayState* play, Player* this) {
    this->stateFlags1 |= PLAYER_STATE1_USING_ZORA_BOOMERANG;
}

// OOT func_80834D2C, boomerang branch
static s32 ComboOot_Boomerang_StartReady(Player* this, PlayState* play) {
    Player_SetUpperAction(play, this, ComboOot_Boomerang_Ready);
    this->unk_ACC = 10;
    PlayerAnimation_PlayOnce(play, &this->skelAnimeUpper, COMBO_OOT_ANIM("gPlayerAnim_link_boom_throw_wait2waitR"));

    if (this->stateFlags1 & PLAYER_STATE1_800000) { // OOT PLAYER_STATE1_ON_HORSE
        Player_Anim_PlayLoop(play, this, &gPlayerAnim_link_uma_anim_walk);
    } else if ((this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && !Player_UpdateHostileLockOn(this)) {
        Player_Anim_PlayLoop(play, this, D_8085BE84[PLAYER_ANIMGROUP_wait][this->modelAnimType]);
    }
    return true;
}

// OOT func_80834F2C (boomerang): start aiming on the item button.
static s32 ComboOot_Boomerang_TryReady(Player* this, PlayState* play) {
    if ((this->doorType == PLAYER_DOORTYPE_NONE) && !(this->stateFlags1 & PLAYER_STATE1_ZORA_BOOMERANG_THROWN)) {
        if (sPlayerUseHeldItem || func_80830F9C(play)) {
            if (ComboOot_Boomerang_StartReady(this, play)) {
                return func_80831010(this, play);
            }
        }
    }
    return false;
}

// OOT func_80835800
s32 ComboOot_UpperAction_Boomerang(Player* this, PlayState* play) {
    if (func_80830B88(play, this)) {
        return true;
    }

    if (this->stateFlags1 & PLAYER_STATE1_ZORA_BOOMERANG_THROWN) {
        Player_SetUpperAction(play, this, ComboOot_Boomerang_Out);
    } else if (ComboOot_Boomerang_TryReady(this, play)) {
        return true;
    }

    return false;
}

// OOT func_80835884
s32 ComboOot_Boomerang_Ready(Player* this, PlayState* play) {
    if (PlayerAnimation_Update(play, &this->skelAnimeUpper)) {
        Player_SetUpperAction(play, this, ComboOot_Boomerang_Aim);
        PlayerAnimation_PlayLoop(play, &this->skelAnimeUpper, COMBO_OOT_ANIM("gPlayerAnim_link_boom_throw_waitR"));
    }

    func_80831010(this, play);

    return true;
}

// OOT func_808358F0
s32 ComboOot_Boomerang_Aim(Player* this, PlayState* play) {
    PlayerAnimationHeader* animSeg = this->skelAnime.animation;

    if (BEN_ANIM_EQUAL(func_8082EF54(this), animSeg) || BEN_ANIM_EQUAL(func_8082EF9C(this), animSeg) ||
        BEN_ANIM_EQUAL(D_8085BE84[PLAYER_ANIMGROUP_side_walkR][this->modelAnimType], animSeg) ||
        BEN_ANIM_EQUAL(D_8085BE84[PLAYER_ANIMGROUP_side_walkL][this->modelAnimType], animSeg)) {
        AnimTaskQueue_AddCopy(play, this->skelAnime.limbCount, this->skelAnimeUpper.jointTable,
                              this->skelAnime.jointTable);
    } else {
        PlayerAnimation_Update(play, &this->skelAnimeUpper);
    }

    func_80831010(this, play);

    if (!sPlayerHeldItemButtonIsHeldDown) {
        Player_SetUpperAction(play, this, ComboOot_Boomerang_Throw);
        PlayerAnimation_PlayOnce(play, &this->skelAnimeUpper,
                                 (this->unk_B40 < 0.5f) ? COMBO_OOT_ANIM("gPlayerAnim_link_boom_throwR")
                                                        : COMBO_OOT_ANIM("gPlayerAnim_link_boom_throwL"));
    }

    return true;
}

// OOT func_808359FC: the boomerang leaves the hand on frame 6.
s32 ComboOot_Boomerang_Throw(Player* this, PlayState* play) {
    if (PlayerAnimation_Update(play, &this->skelAnimeUpper)) {
        Player_SetUpperAction(play, this, ComboOot_Boomerang_Out);
        this->unk_ACC = 0;
    } else if (PlayerAnimation_OnFrame(&this->skelAnimeUpper, 6.0f)) {
        f32 posX = (Math_SinS(this->actor.shape.rot.y) * 10.0f) + this->actor.world.pos.x;
        f32 posZ = (Math_CosS(this->actor.shape.rot.y) * 10.0f) + this->actor.world.pos.z;
        s32 yaw = (this->focusActor != NULL) ? this->actor.shape.rot.y + 14000 : this->actor.shape.rot.y;
        EnBoom* boomerang =
            (EnBoom*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOOM, posX, this->actor.world.pos.y + 30.0f, posZ,
                                 this->actor.focus.rot.x, yaw, 0, COMBO_OOT_BOOMERANG);

        this->zoraBoomerangActor = &boomerang->actor;

        if (boomerang != NULL) {
            boomerang->moveTo = this->focusActor;
            boomerang->unk_1CC = 20; // OOT returnTimer
            this->stateFlags1 |= PLAYER_STATE1_ZORA_BOOMERANG_THROWN;

            if (!Player_CheckHostileLockOn(this)) {
                Player_SetParallel(this);
            }

            this->unk_D57 = 4;
            Player_PlaySfx(this, NA_SE_IT_BOOMERANG_THROW);
            Player_AnimSfx_PlayVoice(this, NA_SE_VO_LI_SWORD_N);
            Combo_MmOotLog("boomerang thrown (OOT EnBoom) yaw %d pitch %d target %p", yaw, this->actor.focus.rot.x,
                           (void*)this->focusActor);
        }
    }

    return true;
}

// OOT func_80835B60: wait for the boomerang, then catch it.
s32 ComboOot_Boomerang_Out(Player* this, PlayState* play) {
    if (func_80830B88(play, this)) {
        return true;
    }

    if (!(this->stateFlags1 & PLAYER_STATE1_ZORA_BOOMERANG_THROWN)) {
        Player_SetUpperAction(play, this, ComboOot_Boomerang_Catch);
        PlayerAnimation_PlayOnce(play, &this->skelAnimeUpper, COMBO_OOT_ANIM("gPlayerAnim_link_boom_catch"));
        Player_PlaySfx(this, NA_SE_PL_CATCH_BOOMERANG);
        Player_AnimSfx_PlayVoice(this, NA_SE_VO_LI_SWORD_N);
        Combo_MmOotLog("boomerang caught");
        return true;
    }

    if (sPlayerUseHeldItem && CVarGetInteger(COMBO_SOH_CVAR_FAST_BOOMERANG, 0)) {
        EnBoom_ComboOotRequestQuickRecall();
    }

    return false;
}

// OOT func_80835C08
s32 ComboOot_Boomerang_Catch(Player* this, PlayState* play) {
    if (!ComboOot_UpperAction_Boomerang(this, play) && PlayerAnimation_Update(play, &this->skelAnimeUpper)) {
        Player_SetUpperAction(play, this, ComboOot_UpperAction_Boomerang);
    }

    return true;
}

// =====================================================================================================================
// Megaton Hammer (OOT PLAYER_IA_HAMMER, OOT melee weapon 5). The swing is OOT's melee path, which MM's player keeps
// (MM's Goron punch grew out of it): OOT's stick-direction choice (func_80837818: forward / side, no combo, no spin),
// OOT's hammer animations and hit windows (sMeleeWeaponAnimInfo HAMMER_FORWARD 3..10, HAMMER_SIDE 2..11), OOT's
// wall/floor hit (func_80842DF4: hit-stop, quake 27767/7/20, rumble, NA_SE_IT_HAMMER_HIT, the "hammer quake" every
// nearby actor feels, the rebound), OOT's frame-7 / frame-2 ground shockwave (Player_Action_808502D0), no lunge.
// MM ids for OOT's two swings: the hammer's vertical swing runs as MM's two-handed vertical slash
// (PLAYER_MWA_FORWARD_SLASH_2H) and its horizontal swing as the two-handed horizontal slash (PLAYER_MWA_RIGHT_SLASH_2H),
// with OOT's hammer animations and hit windows in place of the sword's.
// =====================================================================================================================
#define COMBO_OOT_MWA_HAMMER_FORWARD PLAYER_MWA_FORWARD_SLASH_2H
#define COMBO_OOT_MWA_HAMMER_SIDE PLAYER_MWA_RIGHT_SLASH_2H

// OOT sMeleeWeaponAnimInfo[PLAYER_MWA_HAMMER_FORWARD / _SIDE]
static AttackAnimInfo sComboOotHammerAnimInfo[2] = {
    { COMBO_OOT_ANIM("gPlayerAnim_link_hammer_hit"), COMBO_OOT_ANIM("gPlayerAnim_link_hammer_hit_end"),
      COMBO_OOT_ANIM("gPlayerAnim_link_hammer_hit_endR"), 3, 10 },
    { COMBO_OOT_ANIM("gPlayerAnim_link_hammer_side_hit"), COMBO_OOT_ANIM("gPlayerAnim_link_hammer_side_hit_end"),
      COMBO_OOT_ANIM("gPlayerAnim_link_hammer_side_hit_endR"), 2, 11 },
};

struct AttackAnimInfo* ComboOot_MeleeInfo(Player* this, s32 mwa) {
    if (this->heldItemAction == PLAYER_IA_COMBO_OOT_HAMMER) {
        if (mwa == COMBO_OOT_MWA_HAMMER_FORWARD) {
            return &sComboOotHammerAnimInfo[0];
        }
        if (mwa == COMBO_OOT_MWA_HAMMER_SIDE) {
            return &sComboOotHammerAnimInfo[1];
        }
    }
    return &sMeleeAttackAnimInfo[mwa];
}

// OOT func_80837818, hammer branch (D_80854484).
s32 ComboOot_HammerSelectMwa(Player* this, s32* outMwa) {
    static const s8 sHammerMwa[] = {
        COMBO_OOT_MWA_HAMMER_FORWARD, // PLAYER_STICK_DIR_FORWARD
        COMBO_OOT_MWA_HAMMER_SIDE,    // PLAYER_STICK_DIR_LEFT
        COMBO_OOT_MWA_HAMMER_FORWARD, // PLAYER_STICK_DIR_BACKWARD
        COMBO_OOT_MWA_HAMMER_SIDE,    // PLAYER_STICK_DIR_RIGHT
    };
    s32 controlStickDirection;

    if (this->heldItemAction != PLAYER_IA_COMBO_OOT_HAMMER) {
        return false;
    }
    controlStickDirection = this->controlStickDirections[this->controlStickDataIndex];
    if (controlStickDirection <= PLAYER_STICK_DIR_NONE) {
        controlStickDirection = PLAYER_STICK_DIR_FORWARD;
    }
    *outMwa = sHammerMwa[controlStickDirection];
    this->unk_ADD = 0; // OOT unk_845 = 0: the hammer never builds a combo
    Combo_MmOotLog("hammer swing %s", (*outMwa == COMBO_OOT_MWA_HAMMER_FORWARD) ? "forward" : "side");
    return true;
}

// OOT func_80842A28: the hammer's impact.
static void ComboOot_HammerImpact(PlayState* play, Player* this) {
    Player_RequestQuakeAndRumble(play, this, NA_SE_IT_HAMMER_HIT); // quake 27767/7/20 + rumble 255/20/150
    // OOT play->actorCtx.unk_02 = 4 ("hammer quake": stuns / flips the actors near Link). MM's equivalent is the
    // player impact; the hammer's is credited as the Goron ground pound (owner rule: credit maps to the host game).
    Actor_SetPlayerImpact(play, PLAYER_IMPACT_GORON_GROUND_POUND, 4, 100.0f, &this->actor.world.pos);
}

// OOT func_80842DF4, the hammer branch: a swing that meets a wall or the floor.
void ComboOot_HammerHitSurface(PlayState* play, Player* this) {
    func_8082DF2C(play);               // OOT func_80832630 (hit-stop)
    ComboOot_HammerImpact(play, this); // OOT func_80842A28
    func_808400CC(play, this);         // OOT func_80842D20 (rebound)
    Combo_MmOotLog("hammer hit a surface: quake + rebound");
}

// OOT Player_Action_808502D0, hammer block: the forward swing (frame 7) and the jump attack's landing (frame 2) send
// the white shockwave along the ground in front of Link, when the ground is within 40 units of his feet.
void ComboOot_HammerShockwave(Player* this, PlayState* play) {
    static Vec3f sShockwaveOffset = { 0.0f, 40.0f, 45.0f }; // OOT D_80854A40
    static Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
    Vec3f shockwavePos;
    f32 dy;

    if ((this->meleeWeaponAnimation != COMBO_OOT_MWA_HAMMER_FORWARD) &&
        (this->meleeWeaponAnimation != PLAYER_MWA_JUMPSLASH_FINISH)) {
        return;
    }
    shockwavePos.y = func_80835D2C(play, this, &sShockwaveOffset, &shockwavePos); // OOT func_8083973C
    dy = this->actor.world.pos.y - shockwavePos.y;

    Math_ScaledStepToS(&this->actor.focus.rot.x, Math_Atan2S_XY(45.0f, dy), 800);
    func_80832754(this, true); // OOT func_80836AB8

    if ((((this->meleeWeaponAnimation == COMBO_OOT_MWA_HAMMER_FORWARD) &&
          PlayerAnimation_OnFrame(&this->skelAnime, 7.0f)) ||
         ((this->meleeWeaponAnimation == PLAYER_MWA_JUMPSLASH_FINISH) &&
          PlayerAnimation_OnFrame(&this->skelAnime, 2.0f))) &&
        (dy > -40.0f) && (dy < 40.0f)) {
        ComboOot_HammerImpact(play, this);
        EffectSsBlast_SpawnWhiteShockwave(play, &shockwavePos, &zeroVec, &zeroVec);
        Combo_MmOotLog("hammer shockwave (%s) at %.0f %.0f %.0f",
                       (this->meleeWeaponAnimation == PLAYER_MWA_JUMPSLASH_FINISH) ? "jump attack" : "forward swing",
                       shockwavePos.x, shockwavePos.y, shockwavePos.z);
    }
}

// OOT func_8083485C: with the hammer in hand the upper body only listens for the shield.
s32 ComboOot_UpperAction_Hammer(Player* this, PlayState* play) {
    if (func_80830B88(play, this)) {
        return true;
    }
    return false;
}

// =====================================================================================================================
// Slingshot (OOT PLAYER_IA_SLINGSHOT). OOT's bow and slingshot share one upper-body chain, and MM's player kept that
// chain (MM's bow / hookshot / Deku-nut aim is OOT's bow / hookshot / slingshot aim): OOT's slingshot runs on it with
// OOT's slingshot id (unk_B28 2 = OOT unk_860 2), OOT's draw sound NA_SE_IT_SLING_DRAW, OOT's seed (MM's EnArrow kept
// OOT's ARROW_SEED as ARROW_TYPE_SLINGSHOT: speed 80, 15 frames, the cyan sparkle, NA_SE_IT_SLING_SHOT; MM credits it
// as Deku-nut damage), OOT's ammo (OOT's bullet bag through SOH_ComboTermina_*), OOT's models.
// =====================================================================================================================

// OOT Player_InitBowOrSlingshotIA (slingshot)
void ComboOot_InitSlingshotIA(PlayState* play, Player* this) {
    this->stateFlags1 |= PLAYER_STATE1_8; // OOT PLAYER_STATE1_ITEM_IN_HAND
    this->unk_B28 = -2;
    this->unk_ACC = 0;
}

// =====================================================================================================================
// Magic spells (OOT PLAYER_IA_FARORES_WIND / _NAYRUS_LOVE / _DINS_FIRE = OOT magic spells 3 / 4 / 5): OOT's cast
// (Player_UseItem's spell branch, Player_ActionHandler_13's spell branch, func_8083AF44, Player_Action_808507F4, OOT's
// cast animations and voice/sound cues, OOT's costs on Termina's magic meter) spawning OOT's spell actors, ported as
// Combo_Magic_Wind / _Dark / _Fire. Farore's Wind's warp point: ComboOot_FaroresWind* below.
// =====================================================================================================================
static u8 sComboOotSpellCosts[] = { 12, 24, 24, 12, 24, 12 }; // OOT sMagicSpellCosts

static PlayerAnimationHeader* sComboOotSpellAnims1[] = {
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_kaze1"),
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_honoo1"),
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_tamashii1"),
};
static PlayerAnimationHeader* sComboOotSpellAnims2[] = {
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_kaze2"),
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_honoo2"),
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_tamashii2"),
};
static PlayerAnimationHeader* sComboOotSpellAnims3[] = {
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_kaze3"),
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_honoo3"),
    COMBO_OOT_ANIM("gPlayerAnim_link_magic_tamashii3"),
};
static u8 sComboOotSpellHold[] = { 70, 10, 10 }; // OOT D_80854A7C

#define COMBO_OOT_SPELL_ANIM_SPEED 0.83f
#define COMBO_OOT_FAST_FARORES_ANIM_SPEED_FACTOR 2
#define COMBO_OOT_FAST_FARORES_HOLD 10

// OOT D_80854A80 / D_80854A8C (OOT ANIMSFX_TYPE_RUNNING / _WALKING = MM ANIMSFX_TYPE_6 / _8)
static AnimSfxEntry sComboOotSpellSfxStart[] = {
    ANIMSFX(ANIMSFX_TYPE_GENERAL, 20, NA_SE_PL_SKIP, CONTINUE),
    ANIMSFX(ANIMSFX_TYPE_VOICE, 20, NA_SE_VO_LI_SWORD_N, CONTINUE),
    ANIMSFX(ANIMSFX_TYPE_6, 26, 0, STOP),
};
static AnimSfxEntry sComboOotSpellSfx[][2] = {
    {
        ANIMSFX(ANIMSFX_TYPE_8, 20, 0, CONTINUE),
        ANIMSFX(ANIMSFX_TYPE_VOICE, 30, NA_SE_VO_LI_MAGIC_FROL, STOP),
    },
    {
        ANIMSFX(ANIMSFX_TYPE_8, 20, 0, CONTINUE),
        ANIMSFX(ANIMSFX_TYPE_VOICE, 44, NA_SE_VO_LI_MAGIC_NALE, STOP),
    },
    {
        ANIMSFX(ANIMSFX_TYPE_VOICE, 20, NA_SE_VO_LI_MAGIC_ATTACK, CONTINUE),
        ANIMSFX(ANIMSFX_TYPE_GENERAL, 20, NA_SE_IT_SWORD_SWING_HARD, STOP),
    },
};

// OOT Player_ActionToMagicSpell: 3 Farore's Wind, 4 Nayru's Love, 5 Din's Fire; -1 = not a spell.
static s32 ComboOot_SpellOf(s32 itemAction) {
    switch (itemAction) {
        case PLAYER_IA_COMBO_OOT_FARORES_WIND:
            return 3;
        case PLAYER_IA_COMBO_OOT_NAYRUS_LOVE:
            return 4;
        case PLAYER_IA_COMBO_OOT_DINS_FIRE:
            return 5;
        default:
            return -1;
    }
}

// OOT Player_SpawnMagicSpell
static Actor* ComboOot_SpawnMagicSpell(PlayState* play, Player* this, s32 spell) {
    static s16 sSpellActorIds[] = { ACTOR_COMBO_MAGIC_WIND, ACTOR_COMBO_MAGIC_DARK, ACTOR_COMBO_MAGIC_FIRE };

    return Actor_Spawn(&play->actorCtx, play, sSpellActorIds[spell], this->actor.world.pos.x, this->actor.world.pos.y,
                       this->actor.world.pos.z, 0, 0, 0, 0);
}

void ComboOot_Action_Spell(Player* this, PlayState* play);
void ComboOot_Action_FaroresWindPrompt(Player* this, PlayState* play);

// ---- Farore's Wind's warp point (OOT respawn[RESPAWN_MODE_TOP] + gSaveContext.fw). Termina keeps its own point in
// MM's save (shipSaveInfo.comboFaroresWind), set and used exactly as OOT sets and uses Hyrule's.
static s32 ComboOot_FaroresWindSet(void) {
    return gSaveContext.save.shipSaveInfo.comboFaroresWind.data > 0;
}

// OOT Player_Action_808507F4, actionVar2 reaching 0: Play_SetupRespawnPoint(play, RESPAWN_MODE_TOP, 0x6FF) + fw.
static void ComboOot_FaroresWindSetPoint(PlayState* play, Player* this) {
    RespawnData* fw = &gSaveContext.save.shipSaveInfo.comboFaroresWind;

    fw->data = 1;
    fw->pos = this->actor.world.pos;
    fw->yaw = this->actor.shape.rot.y;
    fw->playerParams = 0x6FF;
    fw->entrance = gSaveContext.save.entrance;
    fw->roomIndex = play->roomCtx.curRoom.num;
    fw->tempSwitchFlags = play->actorCtx.sceneFlags.switches[2];
    fw->unk_18 = play->actorCtx.sceneFlags.collectible[1];
    fw->tempCollectFlags = play->actorCtx.sceneFlags.collectible[2];
    Combo_MmOotLog("Farore's Wind: warp point set (entrance 0x%04X room %d at %.0f %.0f %.0f)", fw->entrance,
                   fw->roomIndex, fw->pos.x, fw->pos.y, fw->pos.z);
}

// OOT Player_ActionHandler_13: a set warp point asks first (OOT Player_Action_8085063C).
static void ComboOot_FaroresWindPrompt(PlayState* play, Player* this) {
    Player_SetAction(play, this, ComboOot_Action_FaroresWindPrompt, 1);
    this->stateFlags1 |= PLAYER_STATE1_10000000 | PLAYER_STATE1_20000000;
    Player_Anim_PlayOnce(play, this, Player_GetIdleAnim(this));
}

// OOT Player_UseItem, the magic-spell branch: a returned true means the item was handled (cast queued or refused).
s32 ComboOot_UseSpell(PlayState* play, Player* this, s32 itemAction) {
    s32 spell = ComboOot_SpellOf(itemAction);

    if (spell < 0) {
        return false;
    }
    if (((spell == 3) && ComboOot_FaroresWindSet()) ||
        ((gSaveContext.magicCapacity != 0) && (gSaveContext.magicState == MAGIC_STATE_IDLE) &&
         (gSaveContext.save.saveInfo.playerData.magic >= sComboOotSpellCosts[spell]))) {
        this->itemAction = itemAction;
        this->unk_AA5 = PLAYER_UNKAA5_5; // OOT unk_6AD = 4 ("use the item in place")
    } else {
        Audio_PlaySfx(NA_SE_SY_ERROR);
        Combo_MmOotLog("spell %d refused (magic %d, state %d)", spell, gSaveContext.save.saveInfo.playerData.magic,
                       gSaveContext.magicState);
    }
    return true;
}

// SoH's Fast Farore's Wind (gEnhancements.FastFarores): the cast animations run at double speed, the hold before the
// ending animation is 10 frames, and the cast doesn't wait for the magic meter to settle.
static s32 ComboOot_IsFastFaroresWind(Player* this) {
    return CVarGetInteger(COMBO_SOH_CVAR_FAST_FARORES, 0) && (this->itemAction == PLAYER_IA_COMBO_OOT_FARORES_WIND);
}

static f32 ComboOot_SpellAnimSpeed(Player* this) {
    f32 speedFactor = ComboOot_IsFastFaroresWind(this) ? COMBO_OOT_FAST_FARORES_ANIM_SPEED_FACTOR : 1;

    return COMBO_OOT_SPELL_ANIM_SPEED * speedFactor;
}

static s32 ComboOot_SpellHold(Player* this) {
    return ComboOot_IsFastFaroresWind(this) ? COMBO_OOT_FAST_FARORES_HOLD : sComboOotSpellHold[this->av1.actionVar1];
}

// OOT func_8083AF44
static void ComboOot_StartCast(PlayState* play, Player* this, s32 spell) {
    Player_SetAction_PreserveItemAction(play, this, ComboOot_Action_Spell, 0);
    this->av1.actionVar1 = spell - 3;
    Magic_Consume(play, sComboOotSpellCosts[spell], MAGIC_CONSUME_WAIT_PREVIEW);
    PlayerAnimation_PlayOnceSetSpeed(play, &this->skelAnime, COMBO_OOT_ANIM("gPlayerAnim_link_magic_tame"),
                                     ComboOot_SpellAnimSpeed(this));
    Combo_MmOotLog("cast %s (cost %d, magic %d)",
                   (spell == 3) ? "Farore's Wind" : (spell == 4) ? "Nayru's Love" : "Din's Fire",
                   sComboOotSpellCosts[spell], gSaveContext.save.saveInfo.playerData.magic);
}

// OOT Player_ActionHandler_13, the magic-spell branch (unk_6AD 4 = MM unk_AA5 5).
s32 ComboOot_ActionHandlerSpell(Player* this, PlayState* play) {
    s32 spell = ComboOot_SpellOf(this->itemAction);

    if (spell < 0) {
        return false;
    }
    if ((spell != 3) || !ComboOot_FaroresWindSet()) {
        ComboOot_StartCast(play, this, spell);
    } else {
        ComboOot_FaroresWindPrompt(play, this);
    }
    func_8082DAD4(this); // OOT func_80832224
    return true;
}

// OOT Player_Action_808507F4
void ComboOot_Action_Spell(Player* this, PlayState* play) {
    if (PlayerAnimation_Update(play, &this->skelAnime)) {
        if (this->av1.actionVar1 < 0) {
            if ((this->itemAction == PLAYER_IA_COMBO_OOT_NAYRUS_LOVE) || ComboOot_IsFastFaroresWind(this) ||
                (gSaveContext.magicState == MAGIC_STATE_IDLE)) {
                this->stateFlags1 &= ~(PLAYER_STATE1_10000000 | PLAYER_STATE1_20000000);
                func_80836988(this, play);                                 // OOT func_80839FFC
                Camera_SetFinishedFlag(Play_GetCamera(play, CAM_ID_MAIN)); // OOT func_8005B1A4
            }
        } else {
            if (this->av2.actionVar2 == 0) {
                PlayerAnimation_PlayOnceSetSpeed(play, &this->skelAnime, sComboOotSpellAnims1[this->av1.actionVar1],
                                                 ComboOot_SpellAnimSpeed(this));

                if (ComboOot_SpawnMagicSpell(play, this, this->av1.actionVar1) != NULL) {
                    this->stateFlags1 |= PLAYER_STATE1_10000000 | PLAYER_STATE1_20000000;
                    if ((this->av1.actionVar1 != 0) || !ComboOot_FaroresWindSet()) {
                        gSaveContext.magicState = MAGIC_STATE_CONSUME_SETUP;
                    }
                } else {
                    Magic_Reset(play);
                }
            } else {
                PlayerAnimation_PlayLoopSetSpeed(play, &this->skelAnime, sComboOotSpellAnims2[this->av1.actionVar1],
                                                 ComboOot_SpellAnimSpeed(this));

                if (this->av1.actionVar1 == 0) {
                    this->av2.actionVar2 = -10;
                }
            }

            this->av2.actionVar2++;
        }
    } else {
        if (this->av2.actionVar2 < 0) {
            this->av2.actionVar2++;

            if (this->av2.actionVar2 == 0) {
                ComboOot_FaroresWindSetPoint(play, this);
                this->av2.actionVar2 = 2;
            }
        } else if (this->av1.actionVar1 >= 0) {
            if (this->av2.actionVar2 == 0) {
                Player_PlayAnimSfx(this, sComboOotSpellSfxStart);
            } else if (this->av2.actionVar2 == 1) {
                Player_PlayAnimSfx(this, sComboOotSpellSfx[this->av1.actionVar1]);
                if ((this->av1.actionVar1 == 2) && PlayerAnimation_OnFrame(&this->skelAnime, 30.0f)) {
                    this->stateFlags1 &= ~(PLAYER_STATE1_10000000 | PLAYER_STATE1_20000000);
                }
            } else if (ComboOot_SpellHold(this) < this->av2.actionVar2++) {
                PlayerAnimation_PlayOnceSetSpeed(play, &this->skelAnime, sComboOotSpellAnims3[this->av1.actionVar1],
                                                 ComboOot_SpellAnimSpeed(this));
                this->yaw = this->actor.shape.rot.y;
                this->av1.actionVar1 = -1;
            }
        }
    }

    Player_DecelerateToZero(this);
}

// OOT Player_Action_8085063C: "warp / dispel / exit" (OOT message 0x3B, here the same choices in Termina's message
// box), then OOT's warp (respawnFlag 3, fade to white, fast) or OOT's dispel (NA_SE_PL_MAGIC_WIND_VANISH at the point).
void ComboOot_Action_FaroresWindPrompt(Player* this, PlayState* play) {
    PlayerAnimation_Update(play, &this->skelAnime);

    if (this->av2.actionVar2 == 0) {
        Combo_MmOotFaroresWindMessage(play);
        this->av2.actionVar2 = 1;
        return;
    }

    if (Message_GetState(&play->msgCtx) == TEXT_STATE_CLOSING) {
        RespawnData* fw = &gSaveContext.save.shipSaveInfo.comboFaroresWind;

        if (play->msgCtx.choiceIndex == 0) { // Returns to FW
            gSaveContext.respawn[RESPAWN_MODE_TOP] = *fw;
            gSaveContext.respawnFlag = 3;
            Combo_MmOotFaroresWindArrivalPending(1);
            play->transitionTrigger = TRANS_TRIGGER_START;
            play->nextEntrance = fw->entrance;
            play->transitionType = TRANS_TYPE_FADE_WHITE_FAST;
            Combo_MmOotLog("Farore's Wind: warp to entrance 0x%04X", fw->entrance);
            return;
        }

        if (play->msgCtx.choiceIndex == 1) { // Unsets FW
            fw->data = -fw->data;
            Audio_PlaySfx_AtPos(&fw->pos, NA_SE_PL_MAGIC_WIND_VANISH);
            Combo_MmOotLog("Farore's Wind: warp point dispelled");
        }

        this->stateFlags1 &= ~(PLAYER_STATE1_10000000 | PLAYER_STATE1_20000000);
        func_80836988(this, play);
        Camera_SetFinishedFlag(Play_GetCamera(play, CAM_ID_MAIN));
    }
}

// OOT Player_StartMode_FaroresWind + Player_Action_8085076C: Link appears at the point 20 frames after arrival with
// NA_SE_PL_MAGIC_WIND_WARP, 60 units up, and drops.
void ComboOot_Action_FaroresWindArrive(Player* this, PlayState* play) {
    RespawnData* fw = &gSaveContext.save.shipSaveInfo.comboFaroresWind;

    if (this->av2.actionVar2 > 20) {
        this->actor.draw = Player_Draw;
        this->actor.world.pos.y += 60.0f;
        this->stateFlags1 &= ~PLAYER_STATE1_20000000;
        func_80833AA0(this, play); // OOT func_80837B9C: falling
        return;
    }

    if (this->av2.actionVar2++ == 20) {
        fw->data++;
        Audio_PlaySfx_AtPos(&fw->pos, NA_SE_PL_MAGIC_WIND_WARP);
    }
}

void ComboOot_StartMode_FaroresWind(PlayState* play, Player* this) {
    this->actor.draw = NULL;
    Player_SetAction(play, this, ComboOot_Action_FaroresWindArrive, 0);
    this->stateFlags1 |= PLAYER_STATE1_20000000;
    Combo_MmOotLog("Farore's Wind: arrived at the warp point");
}

// =====================================================================================================================
// Roc's Feather (SoH's randomizer item: soh/soh/Enhancements/randomizer/RocsFeather.cpp; the same port as Varuuna/
// ComboShip PR #246's CrossRocsFeather.cpp, 3f9e2d16c): one jump per time in the air, back once Link has stood on the
// ground for 3 frames. SoH's jump animation comes from OOT's archive.
// =====================================================================================================================
#define COMBO_ROCS_MAX_USES 1

static u8 sComboRocsUseCount = 0;
static u8 sComboRocsGroundTimer = 0;

// SoH's OnPlayerUpdate hook.
void ComboOot_UpdateRocsFeather(Player* this) {
    if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
        if (sComboRocsGroundTimer <= 3) {
            sComboRocsGroundTimer++;
        }
    } else {
        sComboRocsGroundTimer = 0;
    }
    if (sComboRocsGroundTimer >= 3) {
        sComboRocsUseCount = 0;
    }
}

// SoH's VB_CHANGE_HELD_ITEM_AND_USE_ITEM hook. Two differences between OOT's func_80838940 and MM's twin
// func_80834D50 are undone here: OOT always plays the floor-jump sound (MM only along with a voice, and SoH passes
// none), and OOT keeps the fall's start height, so a jump in mid-air doesn't cancel fall damage.
void ComboOot_UseRocsFeather(PlayState* play, Player* this) {
    f32 effectsScale;
    Vec3f effectsPos;
    s16 fallStartHeight = this->fallStartHeight;

    if (sComboRocsUseCount >= COMBO_ROCS_MAX_USES) {
        return;
    }
    sComboRocsUseCount++;

    func_80834D50(play, this, COMBO_OOT_ANIM("gPlayerAnim_link_rocs_feather_jump"), 5.8f, NA_SE_NONE);
    Player_AnimSfx_PlayFloorJump(this);
    this->fallStartHeight = fallStartHeight;

    // SoH: stops a landing animation morph.
    this->av2.actionVar2 = 1;

    this->speedXZ = 5.0f;
    this->actor.world.rot.y = this->yaw = this->actor.shape.rot.y;

    if (Combo_MM_AdultBodyActive(&this->actor)) {
        this->actor.velocity.y = 7.5f;
        effectsScale = 1.5f;
    } else {
        this->actor.velocity.y = 7.0f;
        effectsScale = 1.0f;
    }

    effectsPos = this->actor.home.pos;
    effectsPos.y += 3;
    EffectSsGRipple_Spawn(play, &effectsPos, (s16)(200 * effectsScale), (s16)(300 * effectsScale), 1);
    EffectSsGSplash_Spawn(play, &effectsPos, NULL, NULL, 0, (s16)(150 * effectsScale));

    // SoH: a sidehop or backflip's hop no longer blocks grabbing a ledge.
    this->stateFlags2 &= ~PLAYER_STATE2_80000;

    Player_PlaySfx(this, NA_SE_PL_SKIP);
}

// OOT Player_Init: Nayru's Love still running carries into the new scene (MAGIC_STATE_METER_FLASH_1 + the diamond).
void ComboOot_PlayerInit(PlayState* play, Player* this) {
    if (gSaveContext.nayrusLoveTimer != 0) {
        gSaveContext.magicState = MAGIC_STATE_METER_FLASH_1;
        ComboOot_SpawnMagicSpell(play, this, 1);
    }
}

// Debug (test harness only): COMBO_DEBUG_OOTITEMS=1 logs Link's state whenever it changes while an OOT item is in
// hand or in use: held / current item action, the body and upper-body animation (path) and frame, melee state.
void ComboOot_DebugTrace(Player* this, PlayState* play) {
    static s32 sOn = -1;
    static const void* sLastAnim = NULL;
    static const void* sLastUpper = NULL;
    static s32 sLastMelee = -1;
    static s32 sLastHeld = -1;

    if (sOn < 0) {
        sOn = (getenv("COMBO_DEBUG_OOTITEMS") != NULL);
    }
    if (!sOn || (this->actor.id != ACTOR_PLAYER)) {
        return;
    }
    if ((this->heldItemAction < PLAYER_IA_COMBO_OOT_MIN) && (this->itemAction < PLAYER_IA_COMBO_OOT_MIN) &&
        (sLastHeld < PLAYER_IA_COMBO_OOT_MIN)) {
        return;
    }
    if ((this->skelAnime.animation != sLastAnim) || (this->skelAnimeUpper.animation != sLastUpper) ||
        (this->meleeWeaponState != sLastMelee) || (this->heldItemAction != sLastHeld)) {
        sLastAnim = this->skelAnime.animation;
        sLastUpper = this->skelAnimeUpper.animation;
        sLastMelee = this->meleeWeaponState;
        sLastHeld = this->heldItemAction;
        Combo_MmOotLog("trace: held %d item %d melee %d mwa %d anim %s @%.0f upper %s @%.0f flags1 %08X",
                       this->heldItemAction, this->itemAction, this->meleeWeaponState, this->meleeWeaponAnimation,
                       (sLastAnim != NULL) ? (const char*)sLastAnim : "-", this->skelAnime.curFrame,
                       (sLastUpper != NULL) ? (const char*)sLastUpper : "-", this->skelAnimeUpper.curFrame,
                       this->stateFlags1);
    }
}
