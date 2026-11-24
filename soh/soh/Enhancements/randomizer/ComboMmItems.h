// soh/soh/Enhancements/randomizer/ComboMmItems.h — ComboShip: Majora's Mask items used in Hyrule. C API for the fenced
// seams in z_player.c and the OOT actors that react to MM items. See ComboMmItems.cpp.
#ifndef COMBO_MM_ITEMS_H
#define COMBO_MM_ITEMS_H

#ifdef __cplusplus
extern "C" {
#endif

struct Actor;
struct PlayState;

// ---------------------------------------------------------------------------------------------------------------
// MM bottle water: MM's ObjAqua (Spring Water / Hot Spring Water poured from a bottle), ported line for line as its own
// actor (soh/src/overlays/actors/ovl_Obj_Aqua/z_obj_aqua.c), registered in SoH's ActorDB under a dynamic id.

#define COMBO_AQUA_TYPE_COLD 0 // MM AQUA_TYPE_COLD: Spring Water
#define COMBO_AQUA_TYPE_HOT 1  // MM AQUA_TYPE_HOT: Hot Spring Water

// The water's AT damage flags in Hyrule. MM's 0xF7CFFFFF (with AT_TYPE_OTHER) only ever meant "touches the few
// AC_TYPE_OTHER colliders that ask who hit them" (MM ObjIcePoly, EnGo, ObjBean). OOT's AC_TYPE_OTHER colliders read
// those same bits as explosions/fire (bombable walls, bomb flowers, red ice...), so the water carries one bit
// (OOT DMG_SHIELD) that no OOT AC_TYPE_OTHER bumper accepts; only colliders added for MM's reactions take it.
#define COMBO_DMG_MM_AQUA (1 << 0x14)

// Spawns MM's ObjAqua exactly as MM's Player_Action_70 does on frame 76 of the pour (mm z_player.c:18486-18490):
// pass (sin(yaw) * 5 + leftHand.x, leftHand.y, cos(yaw) * 5 + leftHand.z) and the player's shape yaw; the actor is
// spawned with rot (0x4000, yaw, 0) and params AQUA_PARAMS(hot). hot: 0 = Spring Water, 1 = Hot Spring Water.
struct Actor* Combo_SpawnMmAqua(struct PlayState* play, float x, float y, float z, short yaw, int hot);

// The ObjAqua port's actor id (registers it on first use). -1 if ActorDB is not up.
short Combo_MmAquaActorId(void);

// True when `actor` is MM's ObjAqua of type `type` (COMBO_AQUA_TYPE_*): the check MM's reacting actors make on their
// collider's `ac` (MM z_obj_ice_poly.c:191-200: ac->id == ACTOR_OBJ_AQUA && ac->params == AQUA_TYPE_HOT).
int Combo_IsMmAqua(struct Actor* actor, int type);

// ---------------------------------------------------------------------------------------------------------------
// MM Bremen Mask: 1 while Link marches (MM PLAYER_STATE3_20000000). Implemented in z_player.c. OOT's cucco chicks
// (En_Nwc) and dogs (En_Dog) follow the march as MM's En_Nwc / En_Dg do.
int Combo_BremenMarching(void);

#ifdef __cplusplus
}
#endif

#endif
