#ifndef Z_EN_BOM_COMBO_H
#define Z_EN_BOM_COMBO_H

// ComboShip: Majora's Mask's Powder Keg in Hyrule, as an OOT EnBom (z_en_bom_combo_keg.c).
//
// Spawn contract (MM Player_InitExplosiveIA / the keg branch of MM EnBom):
//   gComboPowderKegTimer = 200;
//   Actor_SpawnAsChild(&play->actorCtx, &player->actor, play, ACTOR_EN_BOM, x, y, z,
//                      ENBOM_COMBO_KEG /* rot.x: MM BOMB_EXPLOSIVE_TYPE_POWDER_KEG */, player->actor.shape.rot.y,
//                      0 /* rot.z: MM BOMB_TYPE_BODY */, BOMB_BODY);
// EnBom_Init reads the keg bit from shape.rot.x (MM ENBOM_GET_1). While Link holds the keg its fuse is written back to
// gComboPowderKegTimer each frame (MM gSaveContext.powderKegTimer); it is cleared when the keg is put down, explodes
// or sinks.

#ifdef COMBO_BUILD

#include "z_en_bom.h"

#define ENBOM_COMBO_KEG 1 // shape.rot.x bit (MM BOMB_EXPLOSIVE_TYPE_POWDER_KEG)

// The explosion effects (MM EnClearTag) live on a second EnBom with these params; `type` is MM's ClearTagType.
#define ENBOM_COMBO_CLEARTAG_PARAMS(type) (0x4000 | (type))
#define ENBOM_COMBO_IS_CLEARTAG(params) (((params) & 0xFF00) == 0x4000)

extern s16 gComboPowderKegTimer; // MM gSaveContext.powderKegTimer (frames; 0 = none)

// True for a Powder Keg (also before its Init ran). Only Gorons may lift one (that rule lives in the player code).
s32 EnBom_ComboIsKeg(Actor* bomb);

// MM's Blast Mask (MM Player_ProcessItemButtons, ITEM_F0): an MM bomb at `pos` (Link's head) with its fuse at 0, so it
// explodes on its first update. NULL when it couldn't be spawned.
EnBom* EnBom_ComboSpawnBlastMask(PlayState* play, Vec3f* pos);

// For Link's damage check (z_player.c): true when this frame's hit on `player` is to be ignored because his raised
// shield takes the Blast Mask's blast, as in MM.
s32 EnBom_ComboBlastMaskShielded(PlayState* play, Player* player);

// Hooks from z_en_bom.c. Each returns true when it handled the actor (vanilla code is skipped).
s32 EnBom_ComboInit(EnBom* this, PlayState* play);
s32 EnBom_ComboDestroy(EnBom* this, PlayState* play);
s32 EnBom_ComboUpdate(EnBom* this, PlayState* play);
s32 EnBom_ComboDraw(EnBom* this, PlayState* play);

// MM resource raw data (Vtx etc.) from MM's ResourceManager (ComboMmRawRes.cpp); NULL when unavailable.
void* Combo_MmRawResource(const char* otrPath);

#endif

#endif
