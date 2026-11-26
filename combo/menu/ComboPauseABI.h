/* combo/menu/ComboPauseABI.h — ComboShip: merged pause menus (owner ask 2026-11-12: "both item panels from both
 * games in each other's pause menus, every item equipable, D-pad/C-button equips line up in both games").
 *
 * Each game's pause cube is a hexagonal prism (docs/deviations/ui-menu.md "Merged pause menus: hexagonal prisms"):
 *   OOT: Hyrule items -> Termina items (MM's item page, MM slots 0-23) -> Masks (MM's mask page, MM slots 24-47)
 *        -> Map -> Quest -> Equipment
 *   MM:  Termina items -> Hyrule items (OOT's item page, OOT slots 0-23) -> Map -> Quest -> Masks
 *        -> Hyrule equipment (OOT's equipment page)
 * A foreign face shows the OTHER game's live inventory, read from its dormant save in this process through the
 * exports below (C ABI, POD only, resolved with Combo_ResolveSym). Icons, names and the page background are the
 * owning game's own resources, drawn inside a G_COMBO_RM_PUSH/POP bracket so they resolve against its RM.
 *
 * FOREIGN ITEM IDS. A foreign item put on a C button / D-pad direction is stored in the host's own button-item
 * byte as a reserved id that can never collide with a native one. It names the owning game's INVENTORY SLOT (not its
 * item id): the slot is re-read live from that game's save, so a bottle or trade slot always shows what it holds.
 *   OOT host (SoH ItemID: 0x00-0x9D real items, 0xBF-0xC1 kaleido magic-arrow anim ids, 0xFC ITEM_LAST_USED,
 *             0xFE ITEM_NONE_FE, 0xFF ITEM_NONE; the HUD only draws ids < 0xF0):
 *             MM slot s (0..47) -> 0x9E + s, skipping 0xBF..0xC1  => 0x9E..0xBE, 0xC2..0xD0.
 *   MM host  (2Ship ItemId: 0x00-0xB4 real items, 0xB5-0xB7 kaleido magic-arrow anim ids, 0xB8-0xCC GI-only
 *             specials, 0xF0-0xF2 mask actions, 0xFC-0xFF last-used/none):
 *             OOT slot s (0..23) -> 0xD0 + s                     => 0xD0..0xE7.
 *             OOT's tunics and boots (SoH's "Assignable Tunics and Boots") have no item slot, so they go on as the
 *             button slots OOT itself stores for them, SLOT_TUNIC_KOKIRI 24 .. SLOT_BOOTS_HOVER 29 => 0xE8..0xED.
 * The host's cButtonSlots / slot byte for a foreign button is the foreign id itself (unique, never a real host slot),
 * so the host's "same slot on another button -> swap" rule works for it. Host code that indexes by that byte (the
 * pause pages' equipped boxes, bottle writes) must check it is a real slot first. */
#ifndef COMBO_PAUSE_ABI_H
#define COMBO_PAUSE_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMBO_PAUSE_OOT_FOREIGN_FIRST 0x9E
#define COMBO_PAUSE_OOT_ARROW_ANIM_FIRST 0xBF /* 0xBF..0xC1: OOT kaleido magic-arrow equip anim, never stored */
#define COMBO_PAUSE_OOT_ARROW_ANIM_LAST 0xC1
#define COMBO_PAUSE_OOT_FOREIGN_LAST 0xD0
#define COMBO_PAUSE_MM_FOREIGN_FIRST 0xD0
#define COMBO_PAUSE_MM_FOREIGN_LAST 0xED

#define COMBO_PAUSE_MM_SLOTS 48      /* MM items[]: 0-23 item page, 24-47 mask page */
#define COMBO_PAUSE_OOT_SLOTS 24     /* OOT items[]: the item page */
#define COMBO_PAUSE_OOT_GEAR_SLOTS 6 /* OOT button slots 24-29: three tunics, three boots (MM host only) */

/* OOT host: MM slot (0..47) <-> foreign id. */
static inline int ComboPause_OotEncode(int mmSlot) {
    int id = COMBO_PAUSE_OOT_FOREIGN_FIRST + mmSlot;
    if (id >= COMBO_PAUSE_OOT_ARROW_ANIM_FIRST)
        id += (COMBO_PAUSE_OOT_ARROW_ANIM_LAST - COMBO_PAUSE_OOT_ARROW_ANIM_FIRST + 1);
    return id;
}
static inline int ComboPause_OotDecode(int id) { /* -1 = native / none */
    if (id >= COMBO_PAUSE_OOT_FOREIGN_FIRST && id < COMBO_PAUSE_OOT_ARROW_ANIM_FIRST)
        return id - COMBO_PAUSE_OOT_FOREIGN_FIRST;
    if (id > COMBO_PAUSE_OOT_ARROW_ANIM_LAST && id <= COMBO_PAUSE_OOT_FOREIGN_LAST)
        return id - COMBO_PAUSE_OOT_FOREIGN_FIRST -
               (COMBO_PAUSE_OOT_ARROW_ANIM_LAST - COMBO_PAUSE_OOT_ARROW_ANIM_FIRST + 1);
    return -1;
}

/* MM host: OOT slot (0..23 items, 24..29 tunics and boots) <-> foreign id. */
static inline int ComboPause_MmEncode(int ootSlot) {
    return COMBO_PAUSE_MM_FOREIGN_FIRST + ootSlot;
}
static inline int ComboPause_MmDecode(int id) { /* -1 = native / none */
    return (id >= COMBO_PAUSE_MM_FOREIGN_FIRST && id <= COMBO_PAUSE_MM_FOREIGN_LAST) ? id - COMBO_PAUSE_MM_FOREIGN_FIRST
                                                                                     : -1;
}
static inline int ComboPause_MmIsGear(int id) { /* one of OOT's tunics or boots */
    return ComboPause_MmDecode(id) >= COMBO_PAUSE_OOT_SLOTS;
}

/* ---- exports (owning game -> host) ------------------------------------------------------------------------------
 * MM (lib2ship):
 *   int         MM_ComboPause_GetItems(uint8_t items[48], int8_t ammo[48]);   1 = a save is resident
 *   const char* MM_ComboPause_IconTex(int mmItem);                           32x32 RGBA32 (gItemIcons)
 *   const char* MM_ComboPause_NameTex(int mmItem);                           128x16 IA4 (item_name_static, ENG)
 *   const char* const* MM_ComboPause_PageTexs(int maskPage);                 15 IA8 80x32 page tiles
 *   const char* MM_ComboPause_MaskDL(int mmItem);                            the worn-mask DL (z_player_lib)
 * OOT (libsoh):
 *   int         SOH_ComboPause_GetItems(uint8_t items[24], int8_t ammo[24]);
 *   const char* SOH_ComboPause_IconTex(int ootItem);
 *   const char* SOH_ComboPause_NameTex(int ootItem);
 *   const char* const* SOH_ComboPause_PageTexs(void);                        OOT's "Select Item" tiles (ENG)
 */
typedef int (*FnComboPauseGetItems)(uint8_t* items, int8_t* ammo);
typedef const char* (*FnComboPauseTex)(int item);
typedef const char* const* (*FnComboPausePageTexs)(int page);

#ifdef __cplusplus
}
#endif

#endif /* COMBO_PAUSE_ABI_H */
