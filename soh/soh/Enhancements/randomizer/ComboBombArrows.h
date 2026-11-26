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

// The bow of a button's item: OOT's own (ITEM_BOW) or MM's, a foreign id that OOT's code runs as the bow. A shot spends
// the arrows and the bombs of the bow's game (MM's bag for MM's bow).
int Combo_BombArrowsIsBowItem(int item);

// ---- Which buttons fire bomb arrows (ship.bombArrowsEquipped, one bit per button) -----------------------------------
int Combo_BombArrowsFlag(int button);
void Combo_BombArrowsSetFlag(int button, int on);
// The option is on, the button holds a plain bow (OOT's or MM's) and its flag is set.
int Combo_BombArrowsButtonActive(int button);
// A shot from a button holding `bowItem` (OOT's bow or MM's foreign bow) needs and spends a bomb of the same game.
int Combo_BombArrowsButtonHasBombs(int button);
int Combo_BombArrowsBombAmmo(int bowItem);
void Combo_BombArrowsSpendBomb(int bowItem);
void Combo_BombArrowsSpendArrow(int bowItem);

// ---- Pause menu (item page) -----------------------------------------------------------------------------------------
// The bomb slot can swap to "bow + bomb badge": the option is on and both items are owned and usable by this Link.
int Combo_BombArrowsCanCycle(void);
int Combo_BombArrowsSlotShowsBow(void);
void Combo_BombArrowsToggleSlot(void);
// The item page draws the bow in the bomb slot while it shows bomb arrows: Begin puts the bow there (returns 1 if it
// did), End puts the bombs back. Between them the bomb slot reads as bombs to the queries above.
int Combo_BombArrowsBeginSlotDraw(void);
void Combo_BombArrowsEndSlotDraw(void);
// A C button / D-pad press on `cursorSlot`: with the bomb slot showing bomb arrows, the item to equip becomes the bow
// (slot SLOT_BOW). Returns whether this equip carries the bomb-arrow flag.
int Combo_BombArrowsPickEquip(unsigned short cursorSlot, unsigned short* item, unsigned short* slot);
// A new equip is starting (any page): forget the previous one's flag. Combo_BombArrowsEquipQueued then records the flag
// of an equip started from the item page; Combo_BombArrowsEquipPending reads it when the item lands on its button.
void Combo_BombArrowsEquipReset(void);
void Combo_BombArrowsEquipQueued(int bombArrow);
int Combo_BombArrowsEquipPending(void);
// The slot whose item the equipped-outline of `button` surrounds: the bomb slot for bomb arrows while it shows them.
int Combo_BombArrowsOutlineSlot(int button, int slot);
// Whether the outline of `button` is drawn at all (a bomb-arrow button's is hidden while the bomb slot shows bombs).
int Combo_BombArrowsOutlineShown(int button);

// ---- Termina item page (MM's page on OOT's pause menu), for MM's own bow and bombs
// -------------------------------------- The bomb slot of MM's page can swap to "bow + bomb badge" as OOT's does: the
// option is on, MM holds both, and MM's bow goes on a button as MM's own (OOT holds none; with an OOT bow, OOT's own
// page does this).
int Combo_BombArrowsMmCanCycle(void);
int Combo_BombArrowsMmSlotShowsBow(void);
void Combo_BombArrowsMmToggleSlot(void);
int Combo_BombArrowsMmIsBombSlot(int pageSlot);
// What the page slot holds: MM's bow in the bomb slot while it shows bomb arrows, else `item` itself.
unsigned char Combo_BombArrowsMmSlotItem(int pageSlot, unsigned char item);
// The ammo digits of the page slot: the lower of MM's arrows and bombs for the bomb slot showing bomb arrows.
int Combo_BombArrowsMmSlotAmmo(int pageSlot, int ammo);
// The item whose slot the equipped-outline of a foreign button surrounds on MM's page: the bomb slot for bomb arrows
// while it shows them, none while it shows bombs.
unsigned char Combo_BombArrowsMmOutlineItem(int button, unsigned char item);

// ---- Counts ---------------------------------------------------------------------------------------------------------
// The count shown with bomb arrows: a shot spends one of each, so the lower of the two. `hudFallback` (HUD only): with
// no bombs the arrow count is shown instead. `full`: the lower of the two bags is full.
void Combo_BombArrowsCount(int hudFallback, short* ammo, int* full);
// The HUD's button (0 = B): returns 1 and fills `ammo` / `full` when it is a bomb-arrow button.
int Combo_BombArrowsHudAmmo(int hudButton, short* ammo, int* full);
// The same for a button holding MM's bow (MM's bags): 1 when it is a bomb-arrow button.
int Combo_BombArrowsMmHudAmmo(int hudButton, int* ammo, int* full);

#ifdef __cplusplus
}
#endif

#endif
