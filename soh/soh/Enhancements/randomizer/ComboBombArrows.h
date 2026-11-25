// soh/soh/Enhancements/randomizer/ComboBombArrows.h — ComboShip: 2S2H's Bomb Arrows on OOT's bow in Hyrule. C API for
// the fenced seams in the kaleido overlay, z_parameter.c, z_player.c and ovl_En_Arrow. See ComboBombArrows.cpp.
//
// A "button" below is an index into equips.cButtonSlots: 0..2 = C-Left, C-Down, C-Right, 3..6 = D-Up, D-Down, D-Left,
// D-Right (equips.buttonItems[button + 1]). The HUD's button numbers (0 = B) are one higher.
#ifndef COMBO_BOMB_ARROWS_H
#define COMBO_BOMB_ARROWS_H

#ifdef __cplusplus
extern "C" {
#endif

// 2S2H's switch (gEnhancements.Equipment.BombArrows, one shared CVar store) in a both-games seed.
int Combo_BombArrowsEnabled(void);

// ---- Which buttons fire bomb arrows (ship.bombArrowsEquipped, one bit per button) -----------------------------------
int Combo_BombArrowsFlag(int button);
void Combo_BombArrowsSetFlag(int button, int on);
// The option is on, the button holds OOT's plain bow and its flag is set.
int Combo_BombArrowsButtonActive(int button);

#ifdef __cplusplus
}
#endif

#endif
