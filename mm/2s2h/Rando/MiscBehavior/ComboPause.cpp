// mm/2s2h/Rando/MiscBehavior/ComboPause.cpp — ComboShip: merged pause menus, MM side (owner ask 2026-11-12: "both
// item panels from both games in each other's pause menus ... every item equipable ... my dpad/cbutton equips line
// up in both games"). Combo-owned file (2ship GLOB_RECURSE). Encoding + export list: combo/menu/ComboPauseABI.h.
//
// Two halves:
//  1. MM as the OWNING game: exports that let OOT's pause menu show MM's live inventory with MM's own icons, name
//     plates, page tiles and worn-mask models (the Termina and Masks faces of OOT's pause prism).
//  2. MM as the HOST: the Hyrule items / Hyrule equipment faces of MM's hexagonal pause prism (OOT's pages), foreign
//     OOT items on MM's C buttons and D-pad, and what using one does in Termina.
#ifdef COMBO_BUILD

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>

#include "ComboExport.h"
#include "ComboResolve.h"
#include "ComboPauseABI.h"
#include "2s2h/BenGui/HudEditor.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h_assets.h"
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"
extern PlayState* gPlayState;
extern u8 gAmmoItems[];
extern const char* gAmmoDigitTextures[10]; // z_parameter.c: the HUD's ammo digits
extern s16 sDpadItemAmmoX[];               // z_parameter.c: where the D-pad ammo digits go
extern s16 sDpadItemAmmoY[];
extern TexturePtr sItemPageBgTextures[];
extern TexturePtr sMaskPageBgTextures[];
extern const char* D_801C0B20[];
void Kaleido_LoadItemNameStatic(void** segment, u32 texIndex);
}

// ===================================================================================================================
// 1. MM as the owning game (consumed by soh/soh/Enhancements/randomizer/ComboPause.cpp).
// ===================================================================================================================

// The resident MM save's whole items[] (item page + mask page) and the ammo digits MM's own page would draw for each
// slot (-1 = no digits). 0 = no MM save is resident (nothing loaded for this slot yet).
extern "C" COMBO_EXPORT int MM_ComboPause_GetItems(uint8_t* items, int8_t* ammo) {
    const Inventory& inv = gSaveContext.save.saveInfo.inventory;
    for (int s = 0; s < COMBO_PAUSE_MM_SLOTS; s++) {
        items[s] = inv.items[s];
        ammo[s] = -1;
        if (s < ITEM_NUM_SLOTS && gAmmoItems[s] != ITEM_NONE && inv.items[s] != ITEM_NONE) {
            ammo[s] = (inv.items[s] == ITEM_PICTOGRAPH_BOX) ? (CHECK_QUEST_ITEM(QUEST_PICTOGRAPH) ? 1 : 0)
                                                            : inv.ammo[SLOT(inv.items[s])];
        }
    }
    // A never-initialised save context reads all zero: item 0 (Ocarina) in every slot is not a real inventory.
    return gSaveContext.save.saveInfo.playerData.newf[0] == 'Z' ||
           (inv.items[1] != 0 || inv.items[24] != 0 || inv.items[0] == ITEM_NONE);
}

extern "C" COMBO_EXPORT const char* MM_ComboPause_IconTex(int mmItem) {
    if (mmItem < 0 || mmItem >= ITEM_MAP_POINT_GREAT_BAY) // gItemIcons covers the real items only
        return nullptr;
    return (const char*)gItemIcons[mmItem];
}

extern "C" COMBO_EXPORT const char* MM_ComboPause_NameTex(int mmItem) {
    void* seg = nullptr;
    if (mmItem < 0 || mmItem >= ITEM_MAP_POINT_GREAT_BAY)
        return nullptr;
    Kaleido_LoadItemNameStatic(&seg, (u32)mmItem); // MM's own table (bounds-checked; gEmptyTexture past the end)
    return (const char*)seg;
}

extern "C" COMBO_EXPORT const char* const* MM_ComboPause_PageTexs(int maskPage) {
    return (const char* const*)(maskPage ? sMaskPageBgTextures : sItemPageBgTextures);
}

// The DL MM's z_player_lib.c draws on Link's head for a worn mask (D_801C0B20, indexed PLAYER_MASK_* - 1).
extern "C" COMBO_EXPORT const char* MM_ComboPause_MaskDL(int mmItem) {
    if (mmItem < ITEM_MASK_DEKU || mmItem > ITEM_MASK_GIANT)
        return nullptr;
    // ITEM_MASK_TRUTH..GIANT run in PLAYER_MASK_TRUTH..GIANT order; the four transformation masks come after.
    static const s32 kTransform[4] = { PLAYER_MASK_DEKU, PLAYER_MASK_GORON, PLAYER_MASK_ZORA,
                                       PLAYER_MASK_FIERCE_DEITY };
    const s32 mask = (mmItem <= ITEM_MASK_FIERCE_DEITY) ? kTransform[mmItem - ITEM_MASK_DEKU]
                                                        : PLAYER_MASK_TRUTH + (mmItem - ITEM_MASK_TRUTH);
    return D_801C0B20[mask - 1];
}

// A transformation mask's model for the transformation in Hyrule (mm z_player_lib.c D_801C0B20): mmForm is MM's
// PLAYER_FORM_* (FD 0, Goron 1, Zora 2, Deku 3); face = the mask fused to Link's face that MM draws from cl_setmask
// frame 51 (maskMinusOne + 4).
extern "C" COMBO_EXPORT const char* MM_ComboTransformMaskDL(int mmForm, int face) {
    if ((mmForm < PLAYER_FORM_FIERCE_DEITY) || (mmForm > PLAYER_FORM_DEKU))
        return nullptr;
    return D_801C0B20[PLAYER_MASK_FIERCE_DEITY - 1 + mmForm + (face ? 4 : 0)];
}

// Debug (test harness only, COMBO_DEBUG_PAUSE "give=XX"): put an MM item into MM's resident save at its home slot.
extern "C" COMBO_EXPORT void MM_ComboPause_DebugGive(int mmItem) {
    if (mmItem < 0 || mmItem > ITEM_MASK_GIANT)
        return;
    gSaveContext.save.saveInfo.inventory.items[SLOT(mmItem)] = (u8)mmItem;
    if ((SLOT(mmItem) < ITEM_NUM_SLOTS) && (gAmmoItems[SLOT(mmItem)] != ITEM_NONE) &&
        (gSaveContext.save.saveInfo.inventory.ammo[SLOT(mmItem)] == 0)) {
        gSaveContext.save.saveInfo.inventory.ammo[SLOT(mmItem)] = 1; // test data: one to use (e.g. a Powder Keg)
    }
    fprintf(stderr, "[ComboPause] debug: MM item 0x%02X -> MM slot %d\n", mmItem, SLOT(mmItem));
}

// ===================================================================================================================
// 2. MM as the host: the Hyrule tab, foreign OOT items on MM's buttons, and using them in Termina.
// ===================================================================================================================
#include "ComboPause.h"
#include "ComboPauseHarness.h"
#include "ComboOotItems.h"

extern "C" {
extern u8 gPlayerFormItemRestrictions[PLAYER_FORM_MAX][114];
void Combo_RequestCrossSwitch(int ootEntrance); // BenPort.cpp
int Combo_GamesSolo(void);                      // BenPort.cpp
}

namespace {

FnComboPauseGetItems sOotGetItems = nullptr;
FnComboPauseTex sOotIconTex = nullptr;
FnComboPauseTex sOotNameTex = nullptr;
FnComboPausePageTexs sOotPageTexs = nullptr;

bool OotResolved() {
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        sOotGetItems = (FnComboPauseGetItems)Combo_ResolveSym("soh", "SOH_ComboPause_GetItems");
        sOotIconTex = (FnComboPauseTex)Combo_ResolveSym("soh", "SOH_ComboPause_IconTex");
        sOotNameTex = (FnComboPauseTex)Combo_ResolveSym("soh", "SOH_ComboPause_NameTex");
        sOotPageTexs = (FnComboPausePageTexs)Combo_ResolveSym("soh", "SOH_ComboPause_PageTexs");
    }
    return sOotGetItems && sOotIconTex && sOotNameTex && sOotPageTexs;
}

// OOT's resident save, read live.
uint8_t sOotItems[COMBO_PAUSE_OOT_SLOTS];
int8_t sOotAmmo[COMBO_PAUSE_OOT_SLOTS];
bool Snapshot() {
    if (!OotResolved())
        return false;
    if (Combo_GamesSolo()) { // a one-game seed never shows or uses the other game's items
        memset(sOotItems, 0xFF, sizeof(sOotItems));
        return false;
    }
    if (!sOotGetItems(sOotItems, sOotAmmo)) {
        memset(sOotItems, 0xFF, sizeof(sOotItems));
        return false;
    }
    return true;
}

// OOT item ids this file names (soh/include/z64item.h; 2ship cannot include OOT headers).
enum : uint8_t {
    OOT_ITEM_STICK = 0x00,
    OOT_ITEM_NUT = 0x01,
    OOT_ITEM_BOMB = 0x02,
    OOT_ITEM_BOW = 0x03,
    OOT_ITEM_ARROW_FIRE = 0x04,
    OOT_ITEM_SLINGSHOT = 0x06,
    OOT_ITEM_OCARINA_FAIRY = 0x07,
    OOT_ITEM_OCARINA_TIME = 0x08,
    OOT_ITEM_BOMBCHU = 0x09,
    OOT_ITEM_HOOKSHOT = 0x0A,
    OOT_ITEM_LONGSHOT = 0x0B,
    OOT_ITEM_ARROW_ICE = 0x0C,
    OOT_ITEM_LENS = 0x0F,
    OOT_ITEM_BEAN = 0x10,
    OOT_ITEM_ARROW_LIGHT = 0x12,
    OOT_ITEM_BOTTLE = 0x14,
    OOT_ITEM_POTION_RED = 0x15,
    OOT_ITEM_POTION_GREEN = 0x16,
    OOT_ITEM_POTION_BLUE = 0x17,
    OOT_ITEM_FAIRY = 0x18,
    OOT_ITEM_FISH = 0x19,
    OOT_ITEM_MILK_BOTTLE = 0x1A,
    OOT_ITEM_BLUE_FIRE = 0x1C,
    OOT_ITEM_BUG = 0x1D,
    OOT_ITEM_BIG_POE = 0x1E,
    OOT_ITEM_MILK_HALF = 0x1F,
    OOT_ITEM_POE = 0x20,
    OOT_ITEM_MASK_KEATON = 0x24,
    OOT_ITEM_MASK_BUNNY = 0x27,
    OOT_ITEM_MASK_GORON = 0x28,
    OOT_ITEM_MASK_ZORA = 0x29,
    OOT_ITEM_MASK_TRUTH = 0x2B,
};

// Shared items (the same item exists natively in Termina): equipping OOT's copy equips MM's own whenever MM holds it.
struct Twin {
    uint8_t oot;
    uint8_t mm;
};
const Twin kTwins[] = {
    { OOT_ITEM_STICK, ITEM_DEKU_STICK },
    { OOT_ITEM_NUT, ITEM_DEKU_NUT },
    { OOT_ITEM_BOMB, ITEM_BOMB },
    { OOT_ITEM_BOW, ITEM_BOW },
    { OOT_ITEM_ARROW_FIRE, ITEM_ARROW_FIRE },
    { OOT_ITEM_ARROW_ICE, ITEM_ARROW_ICE },
    { OOT_ITEM_ARROW_LIGHT, ITEM_ARROW_LIGHT },
    // OOT items in Termina: no slingshot twin (MM has none; SLOT(ITEM_SLINGSHOT) is the Room Key slot). OOT's own
    // Slingshot runs in Termina (ComboOotItems.cpp).
    { OOT_ITEM_OCARINA_FAIRY, ITEM_OCARINA_OF_TIME },
    { OOT_ITEM_OCARINA_TIME, ITEM_OCARINA_OF_TIME },
    { OOT_ITEM_BOMBCHU, ITEM_BOMBCHU },
    // OOT items in Termina: no hookshot twins. OOT's Hookshot (13 frames) and Longshot (26) run as themselves
    // (ComboOotItems.cpp); MM's own Hookshot (26) stays Termina's item on the Termina face.
    { OOT_ITEM_LENS, ITEM_LENS_OF_TRUTH },
    { OOT_ITEM_BEAN, ITEM_MAGIC_BEANS },
    { OOT_ITEM_BOTTLE, ITEM_BOTTLE },
    { OOT_ITEM_POTION_RED, ITEM_POTION_RED },
    { OOT_ITEM_POTION_GREEN, ITEM_POTION_GREEN },
    { OOT_ITEM_POTION_BLUE, ITEM_POTION_BLUE },
    { OOT_ITEM_FAIRY, ITEM_FAIRY },
    { OOT_ITEM_FISH, ITEM_FISH },
    { OOT_ITEM_MILK_BOTTLE, ITEM_MILK_BOTTLE },
    { OOT_ITEM_MILK_HALF, ITEM_MILK_HALF },
    { OOT_ITEM_BLUE_FIRE, ITEM_BLUE_FIRE },
    { OOT_ITEM_BUG, ITEM_BUG },
    { OOT_ITEM_BIG_POE, ITEM_BIG_POE },
    { OOT_ITEM_POE, ITEM_POE },
    { OOT_ITEM_MASK_KEATON, ITEM_MASK_KEATON },
    { OOT_ITEM_MASK_BUNNY, ITEM_MASK_BUNNY },
    { OOT_ITEM_MASK_GORON, ITEM_MASK_GORON },
    { OOT_ITEM_MASK_ZORA, ITEM_MASK_ZORA },
    { OOT_ITEM_MASK_TRUTH, ITEM_MASK_TRUTH },
};

uint8_t TwinOf(uint8_t ootItem) {
    for (const Twin& t : kTwins)
        if (t.oot == ootItem)
            return t.mm;
    return ITEM_NONE;
}

// The MM slot holding a shared item's native copy, or SLOT_NONE.
uint8_t NativeSlotFor(uint8_t ootItem, uint8_t* mmItemOut) {
    const uint8_t mm = TwinOf(ootItem);
    if (mm == ITEM_NONE)
        return SLOT_NONE;
    const Inventory& inv = gSaveContext.save.saveInfo.inventory;
    if (mm >= ITEM_BOTTLE && mm <= ITEM_OBABA_DRINK) {
        for (int s = SLOT_BOTTLE_1; s <= SLOT_BOTTLE_6; s++) {
            if (inv.items[s] == mm) {
                *mmItemOut = mm;
                return (uint8_t)s;
            }
        }
        return SLOT_NONE;
    }
    const uint8_t slot = SLOT(mm);
    const uint8_t held = inv.items[slot];
    const bool tiered = (mm == ITEM_OCARINA_OF_TIME || mm == ITEM_HOOKSHOT);
    if (held == mm || (tiered && held != ITEM_NONE)) {
        *mmItemOut = held;
        return slot;
    }
    return SLOT_NONE;
}

int LiveOotItem(int foreignId) {
    const int s = ComboPause_MmDecode(foreignId);
    if (s < 0 || !Snapshot())
        return -1;
    return sOotItems[s] == 0xFF ? -1 : sOotItems[s];
}

// The hexagonal prism (MM's pause cube with six faces): 0 Termina items, 1 Hyrule items, 2 Map, 3 Quest, 4 Masks,
// 5 Hyrule equipment. The three "item" faces share MM's PAUSE_ITEM page state; the face says whose page it is.
int sFace = COMBO_MM_PAUSE_FACE_TERMINA;
int sFaceFrom = COMBO_MM_PAUSE_FACE_TERMINA;
int sFaceTo = COMBO_MM_PAUSE_FACE_TERMINA;
int sDrawTab = -1;
int CurTab() {
    if (sDrawTab >= 0)
        return sDrawTab;
    return sFace == COMBO_MM_PAUSE_FACE_HYRULE  ? COMBO_MM_PAUSE_TAB_HYRULE
           : sFace == COMBO_MM_PAUSE_FACE_EQUIP ? COMBO_MM_PAUSE_TAB_HYRULE_EQUIP
                                                : COMBO_MM_PAUSE_TAB_TERMINA;
}
const char* const kFaceNames[] = { "Termina items", "Hyrule items", "Map", "Quest", "Masks", "Hyrule equipment" };

// OOT's equipment page (the Hyrule equipment face), read live.
typedef int (*FnEquipGrid)(uint8_t* items, uint8_t* equipped);
uint8_t sOotEquip[16];
uint8_t sOotEquipped[4];
bool EquipSnapshot() {
    static FnEquipGrid sGrid = (FnEquipGrid)Combo_ResolveSym("soh", "SOH_ComboPause_GetEquipGrid");
    if (sGrid == nullptr || Combo_GamesSolo() || !sGrid(sOotEquip, sOotEquipped)) {
        memset(sOotEquip, 0xFF, sizeof(sOotEquip));
        memset(sOotEquipped, 0, sizeof(sOotEquipped));
        return false;
    }
    return true;
}
bool sErrorPlayed = false;

void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ComboPause/MM] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

} // namespace

extern "C" int Combo_MmPauseTab(void) {
    return CurTab();
}

extern "C" int Combo_MmPauseIsForeign(int item) {
    return ComboPause_MmDecode(item) >= 0;
}

// The name panel's "named item": a foreign id, or an equipment-face cursor item (0x200 + grid slot).
extern "C" int Combo_MmPauseIsForeignName(int item) {
    return Combo_MmPauseIsForeign(item) ||
           (item >= COMBO_MM_PAUSE_EQUIP_CURSOR_BASE && item < COMBO_MM_PAUSE_EQUIP_CURSOR_BASE + 16);
}

// ---- the prism ------------------------------------------------------------------------------------------------------
extern "C" int Combo_MmPauseFacePage(int face) {
    static const int kPages[COMBO_MM_PAUSE_FACE_COUNT] = { PAUSE_ITEM,  PAUSE_ITEM, PAUSE_MAP,
                                                           PAUSE_QUEST, PAUSE_MASK, PAUSE_ITEM };
    return kPages[((face % COMBO_MM_PAUSE_FACE_COUNT) + COMBO_MM_PAUSE_FACE_COUNT) % COMBO_MM_PAUSE_FACE_COUNT];
}

extern "C" int Combo_MmPauseFace(void) {
    return sFace;
}

extern "C" void Combo_MmPauseSetDrawTab(int tab) {
    sDrawTab = tab;
}

extern "C" int Combo_MmPauseFaceTab(int face) {
    return face == COMBO_MM_PAUSE_FACE_HYRULE  ? COMBO_MM_PAUSE_TAB_HYRULE
           : face == COMBO_MM_PAUSE_FACE_EQUIP ? COMBO_MM_PAUSE_TAB_HYRULE_EQUIP
                                               : COMBO_MM_PAUSE_TAB_TERMINA;
}

extern "C" int Combo_MmPausePrismTurn(int dir) {
    sFaceFrom = sFace;
    sFaceTo = (sFace + (dir > 0 ? 1 : COMBO_MM_PAUSE_FACE_COUNT - 1)) % COMBO_MM_PAUSE_FACE_COUNT;
    return Combo_MmPauseFacePage(sFaceTo);
}

extern "C" int Combo_MmPausePrismArrive(PlayState* play) {
    sFace = sFaceTo;
    sFaceFrom = sFace;
    Log("face -> %d (%s)", sFace, kFaceNames[sFace]);
    PauseContext* pauseCtx = &play->pauseCtx;
    if (Combo_MmPauseFacePage(sFace) == PAUSE_ITEM) {
        // A different item face: its own cursor rules take over from the page-edge position.
        pauseCtx->cursorItem[PAUSE_ITEM] = PAUSE_ITEM_NONE;
    }
    return Combo_MmPauseFacePage(sFace);
}

extern "C" void Combo_MmPausePrismSync(int pageIndex) {
    if (Combo_MmPauseFacePage(sFace) != pageIndex) {
        sFace = pageIndex == PAUSE_MAP     ? COMBO_MM_PAUSE_FACE_MAP
                : pageIndex == PAUSE_QUEST ? COMBO_MM_PAUSE_FACE_QUEST
                : pageIndex == PAUSE_MASK  ? COMBO_MM_PAUSE_FACE_MASKS
                                           : COMBO_MM_PAUSE_FACE_TERMINA;
        sFaceFrom = sFaceTo = sFace;
    }
}

// Heading in faces. MM opens by scrolling LEFT onto the last page (it starts one page to the right).
extern "C" float Combo_MmPausePrismHeading(int turning, int opening, float progress) {
    if (turning) {
        float delta = (float)(sFaceTo - sFaceFrom);
        if (delta > COMBO_MM_PAUSE_FACE_COUNT / 2)
            delta -= COMBO_MM_PAUSE_FACE_COUNT;
        if (delta < -COMBO_MM_PAUSE_FACE_COUNT / 2)
            delta += COMBO_MM_PAUSE_FACE_COUNT;
        return (float)sFaceFrom + delta * progress;
    }
    if (opening)
        return (float)sFace + (1.0f - progress);
    return (float)sFace;
}

// ---- the Hyrule equipment face (OOT's equipment page)
// ----------------------------------------------------------------
extern "C" int Combo_MmPauseEquipItem(int gridSlot) {
    if (gridSlot < 0 || gridSlot >= 16 || !EquipSnapshot())
        return ITEM_NONE;
    return sOotEquip[gridSlot];
}
extern "C" int Combo_MmPauseEquipEquipped(int row) {
    if (row < 0 || row >= 4 || !EquipSnapshot())
        return 0;
    return sOotEquipped[row];
}
extern "C" const char* Combo_MmPauseOotIconTex(int ootItem) {
    return (ootItem < 0 || ootItem == 0xFF || !OotResolved()) ? nullptr : sOotIconTex(ootItem);
}
extern "C" int Combo_MmOotLinkAdult(void); // ComboOotItems.cpp: Link's age in Termina
extern "C" void Combo_MmPauseEquipUse(int gridSlot) {
    // OOT's equipment page: row = sword / shield / tunic / boots, column 1-3 = the piece (its equip value). Tunics and
    // boots are OOT's own in Termina too (the boots' physics in MM's Player read OOT's save); swords and shields are
    // MM's there.
    typedef int (*FnSetEquip)(int, int, int);
    static FnSetEquip sSet = (FnSetEquip)Combo_ResolveSym("soh", "SOH_ComboTermina_SetEquipValue");
    const int row = gridSlot / 4;
    const int col = gridSlot % 4;
    if ((sSet != nullptr) && ((row == 2) || (row == 3)) && (col >= 1) && sSet(row, col, Combo_MmOotLinkAdult())) {
        Audio_PlaySfx(NA_SE_SY_DECIDE);
        Log("Hyrule equipment: %s %d equipped from Termina", (row == 2) ? "tunic" : "boots", col);
        return;
    }
    Audio_PlaySfx(NA_SE_SY_ERROR);
    Log("Hyrule equipment slot %d (OOT item 0x%02X): refused (not owned / wrong age / MM's own in Termina)", gridSlot,
        Combo_MmPauseEquipItem(gridSlot));
}

extern "C" const char* const* Combo_MmPausePageTexs(void) {
    if (CurTab() == COMBO_MM_PAUSE_TAB_TERMINA || !OotResolved())
        return nullptr;
    return sOotPageTexs(CurTab() == COMBO_MM_PAUSE_TAB_HYRULE_EQUIP ? 1 : 0);
}

extern "C" unsigned char Combo_MmPauseSlotItem(int ootSlot) {
    if (CurTab() != COMBO_MM_PAUSE_TAB_HYRULE || ootSlot < 0 || ootSlot >= COMBO_PAUSE_OOT_SLOTS || !Snapshot())
        return ITEM_NONE;
    return sOotItems[ootSlot] == 0xFF ? ITEM_NONE : (unsigned char)ComboPause_MmEncode(ootSlot);
}

extern "C" int Combo_MmPauseSlotAmmo(int ootSlot) {
    if (CurTab() != COMBO_MM_PAUSE_TAB_HYRULE || ootSlot < 0 || ootSlot >= COMBO_PAUSE_OOT_SLOTS || !Snapshot())
        return -1;
    // The count a use spends (with shared consumables, what Hyrule will show), not only what OOT's dormant save holds.
    return (sOotAmmo[ootSlot] < 0) ? -1 : Combo_MmOotAmmo(sOotItems[ootSlot]);
}

extern "C" const char* Combo_MmPauseIconTex(int item) {
    const int oot = LiveOotItem(item);
    return oot < 0 ? nullptr : sOotIconTex(oot);
}

extern "C" const char* Combo_MmPauseNameTex(int item) {
    if (item >= COMBO_MM_PAUSE_EQUIP_CURSOR_BASE && item < COMBO_MM_PAUSE_EQUIP_CURSOR_BASE + 16) {
        const int eq = Combo_MmPauseEquipItem(item - COMBO_MM_PAUSE_EQUIP_CURSOR_BASE);
        return (eq == ITEM_NONE || !OotResolved()) ? nullptr : sOotNameTex(eq);
    }
    const int oot = LiveOotItem(item);
    return oot < 0 ? nullptr : sOotNameTex(oot);
}

extern "C" int Combo_MmPauseResolveEquip(int item, unsigned char* outItem, unsigned char* outSlot) {
    const int oot = LiveOotItem(item);
    uint8_t mmItem = ITEM_NONE;
    const uint8_t slot = (oot >= 0) ? NativeSlotFor((uint8_t)oot, &mmItem) : (uint8_t)SLOT_NONE;
    if (slot != SLOT_NONE) {
        *outItem = mmItem;
        *outSlot = slot;
        Log("equip OOT item 0x%02X -> MM's own item 0x%02X (slot %d)", oot, mmItem, slot);
        return 1;
    }
    // A foreign button's slot byte is the foreign id itself (unique, never a real MM slot): MM's "same slot on another
    // button -> swap" rule works for foreign items too and never matches an empty button.
    *outItem = (unsigned char)item;
    *outSlot = (unsigned char)item;
    Log("equip OOT item 0x%02X (OOT slot %d) as foreign id 0x%02X", oot, ComboPause_MmDecode(item), item);
    return 0;
}

// What a foreign item runs as when used in Termina: OOT's masks are MM's own masks (Keaton, Bunny Hood, Mask of
// Truth; Goron and Zora transform). Everything else has no Termina version yet (ITEM_NONE).
extern "C" int Combo_MmPauseUseItem(int item) {
    const int oot = LiveOotItem(item);
    switch (oot) {
        case OOT_ITEM_MASK_KEATON:
        case OOT_ITEM_MASK_BUNNY:
        case OOT_ITEM_MASK_TRUTH:
        case OOT_ITEM_MASK_GORON:
        case OOT_ITEM_MASK_ZORA:
            return TwinOf((uint8_t)oot);
        default:
            return ITEM_NONE;
    }
}

// The OOT slot behind a foreign id (ComboEquipShare.cpp), -1 = not foreign.
extern "C" int Combo_MmPauseForeignSlot(int item) {
    return ComboPause_MmDecode(item);
}
// OOT's item in an OOT slot (live), ITEM_NONE when empty / no OOT save resident.
extern "C" int Combo_MmPauseOotItemAt(int ootSlot) {
    if (ootSlot < 0 || ootSlot >= COMBO_PAUSE_OOT_SLOTS || !Snapshot())
        return ITEM_NONE;
    return sOotItems[ootSlot];
}

// Outside the anonymous namespace: OPEN_DISPS block-declares the FrameInterpolation hooks, which must keep C linkage.
static void ComboPause_DrawEquipAnimIcon(const char* tex) {
    OPEN_DISPS(gPlayState->state.gfxCtx);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, gPlayState->pauseCtx.equipAnimAlpha);
    gSPVertex(OVERLAY_DISP++, (uintptr_t)&gPlayState->pauseCtx.cursorVtx[16], 4, 0);
    gSPComboRMPush(OVERLAY_DISP++, "oot");
    gDPLoadTextureBlock(OVERLAY_DISP++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0, G_TX_NOMIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPComboRMPop(OVERLAY_DISP++);
    gSP1Quadrangle(OVERLAY_DISP++, 0, 2, 3, 1, 0);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

// A foreign item's ammo digits on a C button / D-pad direction, drawn as MM's Interface_DrawAmmoCount /
// Interface_Dpad_DrawAmmoCount draw MM's own (same places, colours and HUD editor element); those know MM's items only.
static void ComboPause_DrawForeignAmmo(s16 button, s16 alpha, bool isDpad, int ammo, bool full) {
    static const s16 kCButtonX[] = { 162, 228, 250, 272 }; // Interface_DrawAmmoCount sAmmoDigitsXPositions
    static const s16 kCButtonY[] = { 35, 35, 51, 35 };
    const s16 x = isDpad ? sDpadItemAmmoX[button] : kCButtonX[button];
    const s16 y = isDpad ? sDpadItemAmmoY[button] : kCButtonY[button];
    const HudEditorElementID element = isDpad ? HUD_EDITOR_ELEMENT_D_PAD : (HudEditorElementID)button;
    OPEN_DISPS(gPlayState->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    if (!isDpad) {
        GameInteractor_Should(VB_SET_BUTTON_ENV_COLOR, false);
    }
    if (full) {
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 120, 255, 0, alpha);
    }
    if (ammo == 0) {
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 100, 100, 100, alpha);
    }
    const int tens = ammo / 10;
    if (tens != 0) {
        HudEditor_SetActiveElement(element);
        OVERLAY_DISP =
            Gfx_DrawTexRectIA8(OVERLAY_DISP, (TexturePtr)gAmmoDigitTextures[tens], 8, 8, x, y, 8, 8, 1 << 10, 1 << 10);
    }
    HudEditor_SetActiveElement(element);
    OVERLAY_DISP = Gfx_DrawTexRectIA8(OVERLAY_DISP, (TexturePtr)gAmmoDigitTextures[ammo % 10], 8, 8, x + 6, y, 8, 8,
                                      1 << 10, 1 << 10);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

namespace {

void RegisterComboPauseHost() {
    // HUD: a foreign item's ammo count (OOT's Slingshot shows the seeds a shot spends).
    COND_VB_SHOULD(VB_DRAW_HUD_AMMO_COUNT, true, {
        const s16 button = (s16)va_arg(args, int);
        const s16 alpha = (s16)va_arg(args, int);
        const bool isDpad = va_arg(args, int) != 0;
        int full = 0;
        const int ammo = (gPlayState == nullptr)
                             ? -1
                             : Combo_MmForeignAmmo(
                                   isDpad ? DPAD_GET_CUR_FORM_BTN_ITEM(button) : GET_CUR_FORM_BTN_ITEM(button), &full);
        if (ammo >= 0) {
            ComboPause_DrawForeignAmmo(button, alpha, isDpad, ammo, full != 0);
            *should = false;
        }
    });
    // HUD: a foreign item's icon slot holds OOT's icon path (Interface_DrawItemIconTexture brackets it to OOT's RM).
    COND_VB_SHOULD(VB_INTERFACE_LOAD_ITEM_ICON, true, {
        u8 btn = (u8)va_arg(args, int);
        if (gPlayState != nullptr && Combo_MmPauseIsForeign(GET_CUR_FORM_BTN_ITEM(btn))) {
            const char* tex = Combo_MmPauseIconTex(GET_CUR_FORM_BTN_ITEM(btn));
            gPlayState->interfaceCtx.iconItemSegment[btn] = tex ? (char*)tex : (char*)gEmptyTexture;
            *should = false;
        }
    });
    COND_VB_SHOULD(VB_INTERFACE_LOAD_DPAD_ITEM_ICON, true, {
        u8 btn = (u8)va_arg(args, int);
        if (gPlayState != nullptr && Combo_MmPauseIsForeign(DPAD_GET_CUR_FORM_BTN_ITEM(btn))) {
            const char* tex = Combo_MmPauseIconTex(DPAD_GET_CUR_FORM_BTN_ITEM(btn));
            gPlayState->interfaceCtx.iconItemSegment[DPAD_BUTTON(btn) + EQUIP_SLOT_MAX] =
                tex ? (char*)tex : (char*)gEmptyTexture;
            *should = false;
        }
    });
    // Pause: the icon flying to its button is OOT's.
    COND_VB_SHOULD(VB_KALEIDO_DRAW_EQUIP_ANIM_ICON, true, {
        ItemId* drawItem = va_arg(args, ItemId*);
        if (Combo_MmPauseIsForeign(*drawItem)) {
            const char* tex = Combo_MmPauseIconTex(*drawItem);
            if (tex != nullptr) {
                ComboPause_DrawEquipAnimIcon(tex);
            }
            *should = false;
        }
    });
    // Forms: a foreign item follows MM's form rule for its Termina twin; one without a twin is Link's only.
    COND_VB_SHOULD(VB_ITEM_BE_RESTRICTED, true, {
        ItemId* itemId = va_arg(args, ItemId*);
        if (Combo_MmPauseIsForeign(*itemId)) {
            const int twin = Combo_MmPauseUseItem(*itemId);
            // An OOT-only item (OOT items in Termina, ComboOotItems.cpp) is Link's only and follows OOT's age rule.
            *should = (twin != ITEM_NONE) ? !gPlayerFormItemRestrictions[GET_PLAYER_FORM][twin]
                                          : ((GET_PLAYER_FORM != PLAYER_FORM_HUMAN) || !Combo_MmOotItemAgeOk(*itemId));
        }
    });
    // Using one: OOT's masks run as MM's; the rest play MM's "can't use" once per press.
    COND_VB_SHOULD(VB_GET_ITEM_ON_BUTTON, true, {
        (void)va_arg(args, int); // slot (the item itself is the VB's default "should")
        ItemId* item = va_arg(args, ItemId*);
        if (!Combo_MmPauseIsForeign(*item))
            return;
        const int foreign = *item;
        // OOT items in Termina: an OOT item with a Termina port stays itself (Player_ItemToItemAction runs OOT's
        // item action for it), when Link is human and OOT's age rule allows it.
        if ((Combo_MmOotItemAction(foreign) != PLAYER_IA_NONE) && (GET_PLAYER_FORM == PLAYER_FORM_HUMAN) &&
            Combo_MmOotItemAgeOk(foreign)) {
            return;
        }
        const int use = Combo_MmPauseUseItem(foreign);
        Input* input = CONTROLLER1(&gPlayState->state);
        const bool pressed = CHECK_BTN_ANY(input->press.button, BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT | BTN_DRIGHT |
                                                                    BTN_DLEFT | BTN_DDOWN | BTN_DUP);
        if (use != ITEM_NONE) {
            if (pressed)
                Log("use foreign id 0x%02X (OOT item 0x%02X) -> MM item 0x%02X", foreign, LiveOotItem(foreign), use);
            *item = (ItemId)use;
            return;
        }
        if (pressed && !sErrorPlayed) {
            sErrorPlayed = true;
            Audio_PlaySfx(NA_SE_SY_ERROR);
            Log("use foreign id 0x%02X (OOT item 0x%02X): no Termina implementation yet -> error sfx", foreign,
                LiveOotItem(foreign));
        }
        *item = ITEM_NONE;
    });
    COND_HOOK(OnGameStateUpdate, true, []() {
        if (gPlayState == nullptr)
            return;
        Input* input = CONTROLLER1(&gPlayState->state);
        if (!CHECK_BTN_ANY(input->cur.button,
                           BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT | BTN_DRIGHT | BTN_DLEFT | BTN_DDOWN | BTN_DUP)) {
            sErrorPlayed = false;
        }
    });
}
RegisterShipInitFunc sHostInit(RegisterComboPauseHost, {});

// Debug (test harness only): COMBO_DEBUG_MM_PAUSE=<script> drives MM's pause menu (combo/menu/ComboPauseHarness.h).
void DumpState() {
    PlayState* play = gPlayState;
    if (play == nullptr)
        return;
    const PauseContext* p = &play->pauseCtx;
    Log("dump: pause state %d page %d tab %s cursor slot %d item %d special %d | C %02X/%02X/%02X "
        "D-up/down/left/right %02X/%02X/%02X/%02X | slots %02X/%02X/%02X %02X/%02X/%02X/%02X | form %d mask %d",
        p->state, p->pageIndex, kFaceNames[sFace], p->cursorPoint[PAUSE_ITEM], p->cursorItem[PAUSE_ITEM],
        p->cursorSpecialPos, BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT), BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN),
        BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT), DPAD_BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_D_UP),
        DPAD_BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_D_DOWN), DPAD_BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_D_LEFT),
        DPAD_BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_D_RIGHT), C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT),
        C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN), C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT), DPAD_SLOT_EQUIP(0, EQUIP_SLOT_D_UP),
        DPAD_SLOT_EQUIP(0, EQUIP_SLOT_D_DOWN), DPAD_SLOT_EQUIP(0, EQUIP_SLOT_D_LEFT),
        DPAD_SLOT_EQUIP(0, EQUIP_SLOT_D_RIGHT), GET_PLAYER_FORM, GET_PLAYER(play) ? GET_PLAYER(play)->currentMask : -1);
    std::string row;
    for (int s = 0; s < 24; s++) {
        char b[8];
        snprintf(b, sizeof(b), "%02X ", gSaveContext.save.saveInfo.inventory.items[s]);
        row += b;
    }
    Log("dump: MM items: %s", row.c_str());
    if (Snapshot()) {
        row.clear();
        for (int s = 0; s < 24; s++) {
            char b[8];
            snprintf(b, sizeof(b), "%02X ", sOotItems[s]);
            row += b;
        }
        Log("dump: OOT items (Hyrule tab): %s", row.c_str());
    }
}

void RegisterComboPauseHarness() {
    static ComboPauseHarness::Script sScript(std::getenv("COMBO_DEBUG_MM_PAUSE"));
    if (!sScript.Active())
        return;
    COND_HOOK(OnGameStateMainStart, true, []() {
        static int sReady = 0;
        PlayState* play = gPlayState;
        if (play == nullptr || GET_PLAYER(play) == nullptr)
            return;
        if (++sReady < 60)
            return;
        static int sStall = 0;
        PauseContext* pc = &play->pauseCtx;
        if (sStall > 0 && pc->state == PAUSE_STATE_MAIN && pc->mainState == PAUSE_MAIN_STATE_SWITCHING_PAGE &&
            pc->switchPageTimer >= 32) {
            pc->switchPageTimer -= 8; // the turn adds 8 back this frame: held at the halfway point
            sStall--;
        }
        const ComboPauseHarness::Frame f = sScript.Step();
        if (f.stall >= 0)
            sStall = f.stall;
        if (f.done)
            return;
        Input* in = CONTROLLER1(&play->state);
        in->press.button |= f.press;
        in->cur.button |= f.press | f.hold;
        if (f.stickX || f.stickY) {
            in->rel.stick_x = f.stickX;
            in->rel.stick_y = f.stickY;
            in->cur.stick_x = f.stickX;
            in->cur.stick_y = f.stickY;
        }
        if (f.dpadOn)
            CVarSetInteger("gEnhancements.Dpad.DpadEquips", 1);
        if (f.give >= 0) {
            typedef void (*FnGive)(int);
            static FnGive sGive = (FnGive)Combo_ResolveSym("soh", "SOH_ComboPause_DebugGive");
            if (sGive != nullptr)
                sGive(f.give);
        }
        if (f.slot >= 0 && f.slot < 24) {
            PauseContext* p = &play->pauseCtx;
            p->cursorPoint[PAUSE_ITEM] = p->cursorSlot[PAUSE_ITEM] = f.slot;
            p->cursorXIndex[PAUSE_ITEM] = f.slot % 6;
            p->cursorYIndex[PAUSE_ITEM] = f.slot / 6;
            p->cursorSpecialPos = 0;
        }
        if (f.cross >= 0) {
            Log("debug: cross back to Hyrule at entrance 0x%X", f.cross);
            Combo_RequestCrossSwitch(f.cross | 0x10000);
        }
        if (f.dump)
            DumpState();
    });
}
RegisterShipInitFunc sHarnessInit(RegisterComboPauseHarness, {});

} // namespace

#endif // COMBO_BUILD
