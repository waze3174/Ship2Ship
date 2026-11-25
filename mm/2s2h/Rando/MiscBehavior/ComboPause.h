// mm/2s2h/Rando/MiscBehavior/ComboPause.h — ComboShip: merged pause menus, MM as the host. C API for the fenced
// seams in MM's kaleido overlay, z_parameter.c and z_player.c. See ComboPause.cpp and combo/menu/ComboPauseABI.h.
#ifndef COMBO_PAUSE_MM_H
#define COMBO_PAUSE_MM_H

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// Which page an "item" face of the prism shows.
enum ComboMmPauseTab {
    COMBO_MM_PAUSE_TAB_TERMINA = 0,  // MM's own item page
    COMBO_MM_PAUSE_TAB_HYRULE,       // OOT's item page (OOT slots 0-23)
    COMBO_MM_PAUSE_TAB_HYRULE_EQUIP, // OOT's equipment page
    COMBO_MM_PAUSE_TAB_COUNT,
};

// The hexagonal prism: MM's pause cube with six faces (page-turn order, R = +1).
enum ComboMmPauseFace {
    COMBO_MM_PAUSE_FACE_TERMINA = 0, // MM's item page
    COMBO_MM_PAUSE_FACE_HYRULE,      // OOT's item page
    COMBO_MM_PAUSE_FACE_MAP,
    COMBO_MM_PAUSE_FACE_QUEST,
    COMBO_MM_PAUSE_FACE_MASKS,
    COMBO_MM_PAUSE_FACE_EQUIP, // OOT's equipment page
    COMBO_MM_PAUSE_FACE_COUNT,
};
// The equipment face's cursor "items" for the name panel (u16 cursorItem, never a real item id).
#define COMBO_MM_PAUSE_EQUIP_CURSOR_BASE 0x200

int Combo_MmPauseTab(void);
int Combo_MmPauseFace(void);
int Combo_MmPauseFacePage(int face);
int Combo_MmPauseFaceTab(int face);
void Combo_MmPauseSetDrawTab(int tab);
int Combo_MmPausePrismTurn(int dir);
int Combo_MmPausePrismArrive(struct PlayState* play);
void Combo_MmPausePrismSync(int pageIndex);
float Combo_MmPausePrismHeading(int turning, int opening, float progress);
const char* const* Combo_MmPausePageTexs(void); // OOT's page tiles on a Hyrule face (NULL on Termina)

// The Hyrule equipment face.
int Combo_MmPauseEquipItem(int gridSlot); // OOT item in the 4x4 grid (row*4 + col), 0xFF = empty
int Combo_MmPauseEquipEquipped(int row);  // the column (1-3) that row has equipped, 0 = none
const char* Combo_MmPauseOotIconTex(int ootItem);
void Combo_MmPauseEquipUse(int gridSlot);
int Combo_MmPauseIsForeignName(int item);
// SoH's "Assignable Tunics and Boots": OOT's tunics and boots on Termina's C buttons and D-pad.
int Combo_MmPauseGearOnButtons(void);       // the setting is on
int Combo_MmPauseEquipGearId(int gridSlot); // that tunic / boots as a button's foreign id, 0xFF = can't go on one

int Combo_MmPauseIsForeign(int item);
unsigned char Combo_MmPauseSlotItem(int ootSlot); // foreign id of a Hyrule-tab slot, ITEM_NONE when empty
int Combo_MmPauseSlotAmmo(int ootSlot);           // OOT's ammo digits for that slot, -1 = none
const char* Combo_MmPauseIconTex(int item);       // OOT icon (inside an "oot" bracket)
const char* Combo_MmPauseNameTex(int item);       // OOT name plate (inside an "oot" bracket)
int Combo_MmPauseResolveEquip(int item, unsigned char* outItem, unsigned char* outSlot);
// The MM item whose own code runs a foreign OOT item in Termina on OOT's bag (OOT's Deku Sticks, Deku Nuts, Bombs,
// Bombchus, Bow and Magic Beans when MM has none): the button keeps the foreign id, so that code knows to check and
// spend OOT's count. OOT's bottles (but Blue Fire and Ruto's Letter) are MM's same content, on OOT's slot. 0xFF = none.
int Combo_MmPauseForeignHostItem(int item);
// OOT's bottles in Termina. heldItemButton: Player heldItemButton.
int Combo_MmPauseRunsAs(int item);   // the item itself, or for an OOT item Combo_MmPauseForeignHostItem
int Combo_MmPauseIsBottle(int item); // MM's bottle or an OOT bottle (MM's bottle button rules apply)
// 0 = the bottle on that button is OOT's and OOT's save has no such content (Termina's spring water, Zora Egg...).
int Combo_MmPauseBottleCanHold(int heldItemButton, int mmItem);
// An OOT bottle on that button takes MM content mmItem into OOT's slot; 0 = not OOT's (MM's own code writes MM's).
int Combo_MmPauseSetForeignBottle(struct PlayState* play, int heldItemButton, int mmItem);
int Combo_MmPauseUseItem(int item);      // the MM item a foreign item runs as in Termina, 0xFF = none (can't use)
int Combo_MmPauseForeignSlot(int item);  // OOT slot behind a foreign id, -1 = not foreign
int Combo_MmPauseOotItemAt(int ootSlot); // OOT's item in that OOT slot (live, 24-29 = gear), 0xFF = empty

#ifdef __cplusplus
}
#endif

#endif
