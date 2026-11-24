// soh/soh/Enhancements/randomizer/ComboPause.h — ComboShip: merged pause menus, OOT side. C API for the fenced
// seams in the kaleido overlay, z_parameter.c, z_player.c and z_player_lib.c. See ComboPause.cpp and
// combo/menu/ComboPauseABI.h (foreign-id encoding).
#ifndef COMBO_PAUSE_H
#define COMBO_PAUSE_H

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Player;

enum ComboPauseTab {
    COMBO_PAUSE_TAB_HYRULE = 0, // OOT's own item page
    COMBO_PAUSE_TAB_TERMINA,    // MM's item page (MM slots 0-23)
    COMBO_PAUSE_TAB_MASKS,      // MM's mask page (MM slots 24-47)
    COMBO_PAUSE_TAB_COUNT,
};

// The hexagonal prism: OOT's pause cube with six faces (page-turn order, R = +1).
enum ComboPauseFace {
    COMBO_PAUSE_FACE_HYRULE = 0, // OOT's item page
    COMBO_PAUSE_FACE_TERMINA,    // MM's item page
    COMBO_PAUSE_FACE_MASKS,      // MM's mask page
    COMBO_PAUSE_FACE_MAP,
    COMBO_PAUSE_FACE_QUEST,
    COMBO_PAUSE_FACE_EQUIP,
    COMBO_PAUSE_FACE_COUNT,
};
int Combo_PauseFace(void);
int Combo_PauseFacePage(int face);
void Combo_PauseSetDrawTab(int tab); // which item page the next item-face draw shows (-1 = the active face's)
int Combo_PausePrismTurn(int dir);   // +1 right / -1 left; returns the target page
int Combo_PausePrismArrive(struct PlayState* play);
void Combo_PausePrismSync(int pageIndex);
float Combo_PausePrismHeading(int turning, int opening, float progress);

// The item page the active (or currently drawn) item face shows.
int Combo_PauseTab(void);
const char* const* Combo_PausePageTexs(void); // MM's page tiles for a Termina / Masks face (NULL on Hyrule)

// Foreign items (ids: ComboPauseABI.h). All reads are live from MM's resident save.
int Combo_PauseIsForeign(int item);
unsigned char Combo_PauseSlotItem(int pageSlot);  // foreign id of a foreign-tab slot, ITEM_NONE when empty
unsigned char Combo_PauseSlotItemAny(int mmSlot); // MM's item in that MM slot (0-47), ITEM_NONE when empty
int Combo_PauseSlotAmmo(int pageSlot);            // MM ammo digits for that slot, -1 = none
// The ammo digits for a foreign (MM) item on an OOT button: MM's count as a use spends it (with shared consumables,
// what MM will show), -1 = none. `full` (may be NULL): MM's bag is full (the count is drawn green).
int Combo_PauseForeignAmmoDigits(int foreignId, int* full);
const char* Combo_PauseIconTex(int item); // MM icon (draw inside a "mm" bracket); NULL = unavailable
const char* Combo_PauseNameTex(int item); // MM name plate (inside a "mm" bracket)
int Combo_PauseResolveEquip(int item, unsigned char* outItem, unsigned char* outSlot);
int Combo_PauseOnButton(int item); // 1 = that foreign item sits on a C button / D-pad direction

// Using a foreign item (Player_UseItem). Masks: transformation -> form, others -> worn with MM's model.
int Combo_PauseForeignForm(int item); // COMBO_FORM_* for a transformation mask, -1 otherwise
int Combo_PauseIsForeignMask(int item);
int Combo_PauseIsForeignGfs(int item); // MM Great Fairy's Sword on an OOT button
void Combo_PauseToggleMmMask(int item);
void Combo_PauseClearMmMask(void);
const void* Combo_PauseWornMmMaskDL(void);               // routed "@mm:" DL of the worn MM mask, NULL = none
int Combo_PauseWornMmMaskItem(void);                     // MM item id of the worn MM mask, -1 = none
const void* Combo_TransformMaskDL(int mmForm, int face); // MM's transformation mask model (face: fused variant)
void Combo_PauseLogUse(int item, const char* what);

// MM items in Hyrule (docs/deviations/ui-menu.md "MM items in Hyrule"). MM item ids (mm/include/z64item.h).
enum ComboMmItem {
    COMBO_MM_ITEM_POWDER_KEG = 0x0C,
    COMBO_MM_ITEM_PICTOGRAPH_BOX = 0x0D,
    COMBO_MM_ITEM_SWORD_GREAT_FAIRY = 0x10,
    COMBO_MM_ITEM_MASK_TRUTH = 0x36,
    COMBO_MM_ITEM_MASK_KAFEIS_MASK = 0x37,
    COMBO_MM_ITEM_MASK_ALL_NIGHT = 0x38,
    COMBO_MM_ITEM_MASK_BUNNY = 0x39,
    COMBO_MM_ITEM_MASK_KEATON = 0x3A,
    COMBO_MM_ITEM_MASK_GARO = 0x3B,
    COMBO_MM_ITEM_MASK_ROMANI = 0x3C,
    COMBO_MM_ITEM_MASK_CIRCUS_LEADER = 0x3D,
    COMBO_MM_ITEM_MASK_POSTMAN = 0x3E,
    COMBO_MM_ITEM_MASK_COUPLE = 0x3F,
    COMBO_MM_ITEM_MASK_GREAT_FAIRY = 0x40,
    COMBO_MM_ITEM_MASK_GIBDO = 0x41,
    COMBO_MM_ITEM_MASK_DON_GERO = 0x42,
    COMBO_MM_ITEM_MASK_KAMARO = 0x43,
    COMBO_MM_ITEM_MASK_CAPTAIN = 0x44,
    COMBO_MM_ITEM_MASK_STONE = 0x45,
    COMBO_MM_ITEM_MASK_BREMEN = 0x46,
    COMBO_MM_ITEM_MASK_BLAST = 0x47,
    COMBO_MM_ITEM_MASK_SCENTS = 0x48,
    COMBO_MM_ITEM_MASK_GIANT = 0x49,
};
// 1 when human Link wears that MM mask in Hyrule (MM's Player_GetMask(play) == PLAYER_MASK_*). OOT actors whose MM
// counterparts check a mask call this at the same decision (e.g. the Stone Mask: COMBO_MM_STONE_MASK()).
int Combo_MmMaskIs(int mmItem);
#define COMBO_MM_STONE_MASK() Combo_MmMaskIs(COMBO_MM_ITEM_MASK_STONE)
// MM's B-action masks (Blast, Bremen, Kamaro's): while one is worn and B holds a sword or nothing, B runs the mask's
// action and shows MM's label for it ("Explode", "March", "Dance"). Returns that label's MM texture path, drawn through
// a "mm" bracket; NULL = B is B.
const char* Combo_MaskBActionLabel(void);
// 1 while the Blast Mask's cooldown greys out B (only with a sword on B).
int Combo_BlastMaskCoolingDown(void);
// Frames for a new Blast Mask cooldown: MM's 310, or 2S2H's "Blast Mask Cooldown" when it is set lower.
int Combo_BlastMaskCooldown(void);
// MM's Deku Stick / Deku Nut: ITEM_STICK / ITEM_NUT, whose OOT code runs it on MM's bag; ITEM_NONE for anything else.
int Combo_PauseForeignOotAmmoItem(int item);
int Combo_PauseLiveMmItem(int item);                         // the MM item id a foreign id names right now, -1 = none
int Combo_PauseForeignBottleAction(int item);                // OOT PLAYER_IA_BOTTLE_* running that MM bottle, -1 = none
int Combo_PauseSetForeignBottle(int foreignId, int ootItem); // write the new content into MM's slot; 1 = done
int Combo_PauseMmBottleColor(int foreignId, unsigned char* rgb); // MM's bottle content colour; 1 = an MM bottle
void Combo_ChateauDrink(void); // MM Chateau Romani drunk in Hyrule: infinite magic until next dawn
int Combo_ChateauActive(void); // 1 while it lasts (magic drains skip, meter border flashes)

#ifdef __cplusplus
}
#endif

#endif
