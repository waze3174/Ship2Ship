// mm/2s2h/Rando/MiscBehavior/ComboAdultBody.h — ComboShip: adult Link in Termina (owner ruling 2026-11-12: age is
// kept across games). C API for the fenced seams in z_player.c / z_player_lib.c. See ComboAdultBody.cpp.
#ifndef COMBO_ADULT_BODY_H
#define COMBO_ADULT_BODY_H

#ifdef __cplusplus
extern "C" {
#endif

struct Actor;
struct PlayState;

// The player actor should be adult Link right now: human form, OOT says adult, OOT's adult skeleton is loadable.
int Combo_MM_AdultBodyActive(struct Actor* actor);

// Player_DrawImpl swap: OOT's adult skeleton limbs (raw, from OOT's resource manager) and their matrix count.
void** Combo_MM_AdultLimbs(int* dListCount);

// Wraps the caller's limb override: runs it, then turns the limb model it chose into the adult one ("@oot:"-routed).
void* Combo_MM_AdultWrapOverride(void* innerOverride);

// Face + tunic: binds OOT's adult eye/mouth textures on segments 8/9 and sets the tunic color. Appends to `gfx`.
void* Combo_MM_AdultDrawPrep(void* gfx, int mmEyeIndex, int mmMouthIndex);

// Left-hand post-limb: MM's Great Fairy's Sword blade in adult Link's closed fist (no-op otherwise).
void Combo_MM_AdultDrawHeld(struct PlayState* play);

#ifdef __cplusplus
}
#endif

#endif
