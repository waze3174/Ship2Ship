// soh/soh/Enhancements/randomizer/ComboLostWoodsLink.h — ComboShip: the Lost Woods link, OOT side. C API for the
// fenced seam in z_player.c. See ComboLostWoodsLink.cpp and combo/rando/ComboLostWoodsLink.h.
#ifndef COMBO_LOST_WOODS_LINK_H
#define COMBO_LOST_WOODS_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// 1 when the Market's Mask Shop is OoT's own shop, not the way into Termina: the link is on, and the corridor end is
// the only way between the games.
int Combo_OOT_LostWoodsLinkKeepsMaskShop(void);

// Called by Player_HandleExitsAndVoids for the exit `poly` (a CollisionPoly) the player stands on, before the entrance
// override. Returns 1 when it is the link's wrong turn: the switch to Termina is requested and the caller must not run
// the override (the exit's own destination, Kokiri Forest, never reaches the entrance tracker or the cross-game
// redirect).
int Combo_OOT_LostWoodsLinkExit(struct PlayState* play, const void* poly, unsigned int bgId);

#ifdef __cplusplus
}
#endif

#endif // COMBO_LOST_WOODS_LINK_H
