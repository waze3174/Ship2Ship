// mm/2s2h/Rando/MiscBehavior/ComboOotItems.h — ComboShip: OOT items used in Termina (owner ask 2026-11-13: "every item
// has to work from both games in both games ... correct models, correct sounds, animations, all of it"). C API for the
// fenced seams in z_player.c / z_player_lib.c and the combo actor ports. See ComboOotItems.cpp.
#ifndef COMBO_OOT_ITEMS_H
#define COMBO_OOT_ITEMS_H

#ifdef __cplusplus
extern "C" {
#endif

struct Player;
struct PlayState;

// OOT item ids MM-side code names (soh/include/z64item.h; MM cannot include OOT headers).
#define COMBO_OOT_ITEM_SLINGSHOT 0x06
#define COMBO_OOT_ITEM_HOOKSHOT 0x0A
#define COMBO_OOT_ITEM_LONGSHOT 0x0B
#define COMBO_OOT_ITEM_BOOMERANG 0x0E
#define COMBO_OOT_ITEM_HAMMER 0x11
#define COMBO_OOT_ITEM_DINS_FIRE 0x05
#define COMBO_OOT_ITEM_FARORES_WIND 0x0D
#define COMBO_OOT_ITEM_NAYRUS_LOVE 0x13
#define COMBO_OOT_ITEM_MASK_SKULL 0x25
#define COMBO_OOT_ITEM_MASK_SPOOKY 0x26
#define COMBO_OOT_ITEM_MASK_GERUDO 0x2A
#define COMBO_OOT_ITEM_ROCS_FEATHER 0x9D // SoH's randomizer item, in OOT's Nayru's Love slot

// The item action an OOT item (foreign id on an MM button) runs in Termina: one of PLAYER_IA_COMBO_OOT_* for an OOT
// item with a Termina port, PLAYER_IA_NONE otherwise (masks with a Termina twin go through Combo_MmPauseUseItem).
int Combo_MmOotItemAction(int foreignItem);

// The live OOT item behind a foreign id (-1 = none / not foreign).
int Combo_MmOotItemOf(int foreignItem);

// OOT's own age rule for that item (gItemAgeReqs + SoH's Timeless Equipment), for Link's age in Termina.
int Combo_MmOotItemAgeOk(int foreignItem);

// OOT's ammo for an ammo item (Deku Seeds etc.) as a use in Termina spends it (-1 = not an ammo item OOT holds), and
// OOT's Inventory_ChangeAmmo on it. With shared consumables on, that is the shared count, not OOT's dormant save.
int Combo_MmOotAmmo(int ootItem);
void Combo_MmOotChangeAmmo(int ootItem, int delta);

// The ammo digits for a foreign (OOT) item on an MM button: that count, -1 = none. `full` (may be NULL): OOT's bag is
// full (the count is drawn green).
int Combo_MmForeignAmmo(int foreignItem, int* full);

// Link's hand model while holding an OOT item: the OOT model (routed "@oot:") for the left / right hand limb, or NULL
// to keep MM's choice. `limbIndex` is PLAYER_LIMB_*; called from the gameplay limb override after MM picked a model.
const void* Combo_MmOotHeldDL(struct Player* player, int limbIndex);

// An OOT resource's raw data (path without "__OTR__"), from OOT's resident ResourceManager; NULL if missing.
void* Combo_MmOotRawResource(const char* path);

// Farore's Wind in Termina: OOT's "warp / dispel / exit" choice box, and the arrival flag Player_Init reads
// (set: 1 / 0; query: -1 returns the flag).
void Combo_MmOotFaroresWindMessage(struct PlayState* play);
int Combo_MmOotFaroresWindArrivalPending(int set);

// OOT's masks with no Termina twin (Skull / Spooky / Gerudo): toggle (1 = it was one), the worn one's OOT model,
// clear (an MM mask or a transformation takes it off).
int Combo_MmOotToggleMask(int foreignItem);
int Combo_MmOotWornMask(void);
const void* Combo_MmOotWornMaskDL(void);
void Combo_MmOotClearMask(void);

// Debug log line ("[ComboOot] ...") for item use evidence in harness runs.
void Combo_MmOotLog(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
