// soh/soh/Enhancements/randomizer/ComboPause.cpp — ComboShip: merged pause menus, OOT side (owner ask 2026-11-12:
// "I want the entire Masks panel of the MM pause menu to be in the OOT pause panels, I want both item panels from both
// games in each other's pause menus, I want every item equipable ... my dpad/cbutton equips line up in both games").
// Combo-owned file (soh GLOB_RECURSE). Mirror: mm/2s2h/Rando/MiscBehavior/ComboPause.cpp. Encoding and the export
// list: combo/menu/ComboPauseABI.h. Deviation record: docs/deviations/ui-menu.md "Merged pause menus".
//
// Two halves:
//  1. OOT as the HOST: the Termina and Masks faces of OOT's hexagonal pause prism (MM's live inventory, MM's icons /
//  name plates /
//     page tiles through a "mm" G_COMBO_RM bracket), foreign MM items on OOT's C buttons and D-pad, and what using
//     one does in Hyrule (transformation masks -> the form system in ComboForms.cpp, other MM masks -> worn with MM's
//     own model, everything else -> OOT's "can't use" error until it has a Hyrule implementation).
//  2. OOT as the OWNING game: exports that let MM's pause prism show OOT's item and equipment pages.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <libultraship/bridge/consolevariablebridge.h>

#include "ComboExport.h"
#include "ComboResolve.h"
#include "ComboPauseABI.h"
#include "ComboPauseHarness.h"
#include "ComboForms.h"
#include "ComboPause.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
void KaleidoScope_SetDefaultCursor(PlayState* play);
void KaleidoScope_ResetItemCycling();
const char* const* KaleidoScope_ComboSelectItemTexs(void); // z_kaleido_scope_PAL.c (OOT's ENG page tiles)
const char* const* KaleidoScope_ComboEquipmentTexs(void);  // z_kaleido_scope_PAL.c (OOT's ENG "Equipment" tiles)
const char* KaleidoScope_ComboItemNameTex(int item);       // z_kaleido_scope_PAL.c (OOT's ENG name plates)
extern void* gItemIcons[];
void Combo_RequestCrossSwitch(int mmEntrance); // OTRGlobals.cpp
int Combo_GamesSolo(void);                     // OTRGlobals.cpp
extern s16 gComboBlastMaskTimer;               // z_player_lib.c: MM blastMaskTimer for the Blast Mask in Hyrule
int Combo_SharedMmAmmo(int kind, int mmCount, int mmCap); // ComboSharedHealth.cpp: MM's bag with shared consumables
int Combo_SharedMmAmmoSpend(int kind, int change, int mmCount); // ComboSharedHealth.cpp: spend it, 0 = sharing off
}

namespace {

// ---- MM exports (resolved lazily: lib2ship is loaded before either game runs) ---------------------------------------
FnComboPauseGetItems sMmGetItems = nullptr;
FnComboPauseTex sMmIconTex = nullptr;
FnComboPauseTex sMmNameTex = nullptr;
FnComboPausePageTexs sMmPageTexs = nullptr;
FnComboPauseTex sMmMaskDL = nullptr;

bool MmResolved() {
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        sMmGetItems = (FnComboPauseGetItems)Combo_ResolveSym("2ship", "MM_ComboPause_GetItems");
        sMmIconTex = (FnComboPauseTex)Combo_ResolveSym("2ship", "MM_ComboPause_IconTex");
        sMmNameTex = (FnComboPauseTex)Combo_ResolveSym("2ship", "MM_ComboPause_NameTex");
        sMmPageTexs = (FnComboPausePageTexs)Combo_ResolveSym("2ship", "MM_ComboPause_PageTexs");
        sMmMaskDL = (FnComboPauseTex)Combo_ResolveSym("2ship", "MM_ComboPause_MaskDL");
    }
    return sMmGetItems && sMmIconTex && sMmNameTex && sMmPageTexs && sMmMaskDL;
}

// MM's resident save, read live (48 bytes; cheaper than any invalidation scheme).
uint8_t sMmItems[COMBO_PAUSE_MM_SLOTS];
int8_t sMmAmmo[COMBO_PAUSE_MM_SLOTS];
bool Snapshot() {
    if (!MmResolved())
        return false;
    if (Combo_GamesSolo()) { // a one-game seed never shows or uses the other game's items
        memset(sMmItems, 0xFF, sizeof(sMmItems));
        return false;
    }
    if (!sMmGetItems(sMmItems, sMmAmmo)) {
        memset(sMmItems, 0xFF, sizeof(sMmItems));
        return false;
    }
    return true;
}

// MM item ids this file names (mm/include/z64item.h; soh cannot include MM headers).
enum : uint8_t {
    MM_ITEM_OCARINA_OF_TIME = 0x00,
    MM_ITEM_BOW = 0x01,
    MM_ITEM_ARROW_FIRE = 0x02,
    MM_ITEM_ARROW_ICE = 0x03,
    MM_ITEM_ARROW_LIGHT = 0x04,
    MM_ITEM_OCARINA_FAIRY = 0x05,
    MM_ITEM_BOMB = 0x06,
    MM_ITEM_BOMBCHU = 0x07,
    MM_ITEM_DEKU_STICK = 0x08,
    MM_ITEM_DEKU_NUT = 0x09,
    MM_ITEM_MAGIC_BEANS = 0x0A,
    MM_ITEM_SLINGSHOT = 0x0B,
    MM_ITEM_LENS_OF_TRUTH = 0x0E,
    MM_ITEM_HOOKSHOT = 0x0F,
    MM_ITEM_LONGSHOT = 0x11,
    MM_ITEM_BOTTLE = 0x12,
    MM_ITEM_POTION_RED = 0x13,
    MM_ITEM_POTION_GREEN = 0x14,
    MM_ITEM_POTION_BLUE = 0x15,
    MM_ITEM_FAIRY = 0x16,
    MM_ITEM_MILK_BOTTLE = 0x18,
    MM_ITEM_MILK_HALF = 0x19,
    MM_ITEM_FISH = 0x1A,
    MM_ITEM_BUG = 0x1B,
    MM_ITEM_BLUE_FIRE = 0x1C,
    MM_ITEM_POE = 0x1D,
    MM_ITEM_BIG_POE = 0x1E,
    MM_ITEM_MASK_DEKU = 0x32,
    MM_ITEM_MASK_GORON = 0x33,
    MM_ITEM_MASK_ZORA = 0x34,
    MM_ITEM_MASK_FIERCE_DEITY = 0x35,
    MM_ITEM_MASK_TRUTH = 0x36,
    MM_ITEM_MASK_BUNNY = 0x39,
    MM_ITEM_MASK_KEATON = 0x3A,
    MM_ITEM_MASK_GIANT = 0x49,
};

// Shared items: the same item exists natively in Hyrule. Equipping the MM copy equips OOT's own (owner rule: "equipping
// a shared item should equip the host-native version") whenever OOT holds it; only an item OOT lacks goes on the
// button as a foreign id.
struct Twin {
    uint8_t mm;
    uint8_t oot;
};
const Twin kTwins[] = {
    { MM_ITEM_OCARINA_OF_TIME, ITEM_OCARINA_TIME },
    { MM_ITEM_OCARINA_FAIRY, ITEM_OCARINA_FAIRY },
    { MM_ITEM_BOW, ITEM_BOW },
    { MM_ITEM_ARROW_FIRE, ITEM_ARROW_FIRE },
    { MM_ITEM_ARROW_ICE, ITEM_ARROW_ICE },
    { MM_ITEM_ARROW_LIGHT, ITEM_ARROW_LIGHT },
    { MM_ITEM_BOMB, ITEM_BOMB },
    { MM_ITEM_BOMBCHU, ITEM_BOMBCHU },
    { MM_ITEM_DEKU_STICK, ITEM_STICK },
    { MM_ITEM_DEKU_NUT, ITEM_NUT },
    { MM_ITEM_MAGIC_BEANS, ITEM_BEAN },
    { MM_ITEM_SLINGSHOT, ITEM_SLINGSHOT },
    { MM_ITEM_LENS_OF_TRUTH, ITEM_LENS },
    { MM_ITEM_HOOKSHOT, ITEM_HOOKSHOT },
    { MM_ITEM_LONGSHOT, ITEM_LONGSHOT },
    { MM_ITEM_BOTTLE, ITEM_BOTTLE },
    { MM_ITEM_POTION_RED, ITEM_POTION_RED },
    { MM_ITEM_POTION_GREEN, ITEM_POTION_GREEN },
    { MM_ITEM_POTION_BLUE, ITEM_POTION_BLUE },
    { MM_ITEM_FAIRY, ITEM_FAIRY },
    { MM_ITEM_MILK_BOTTLE, ITEM_MILK_BOTTLE },
    { MM_ITEM_MILK_HALF, ITEM_MILK_HALF },
    { MM_ITEM_FISH, ITEM_FISH },
    { MM_ITEM_BUG, ITEM_BUG },
    { MM_ITEM_BLUE_FIRE, ITEM_BLUE_FIRE },
    { MM_ITEM_POE, ITEM_POE },
    { MM_ITEM_BIG_POE, ITEM_BIG_POE },
    { MM_ITEM_MASK_GORON, ITEM_MASK_GORON },
    { MM_ITEM_MASK_ZORA, ITEM_MASK_ZORA },
    { MM_ITEM_MASK_TRUTH, ITEM_MASK_TRUTH },
    { MM_ITEM_MASK_BUNNY, ITEM_MASK_BUNNY },
    { MM_ITEM_MASK_KEATON, ITEM_MASK_KEATON },
};

// The OOT slot holding a native twin of an MM item, or SLOT_NONE.
uint8_t NativeSlotFor(uint8_t mmItem, uint8_t* ootItemOut) {
    for (const Twin& t : kTwins) {
        if (t.mm != mmItem)
            continue;
        if (t.oot >= ITEM_BOTTLE && t.oot <= ITEM_POE) { // a bottle: whichever OOT bottle holds that content
            for (int s = SLOT_BOTTLE_1; s <= SLOT_BOTTLE_4; s++) {
                if (gSaveContext.inventory.items[s] == t.oot) {
                    *ootItemOut = t.oot;
                    return (uint8_t)s;
                }
            }
            return SLOT_NONE;
        }
        const uint8_t slot = gItemSlots[t.oot];
        const uint8_t held = gSaveContext.inventory.items[slot];
        // One slot, several tiers (ocarina, hookshot): OOT's own tier is the native version.
        const bool tiered = (t.oot == ITEM_OCARINA_TIME || t.oot == ITEM_OCARINA_FAIRY || t.oot == ITEM_HOOKSHOT ||
                             t.oot == ITEM_LONGSHOT);
        if (held == t.oot || (tiered && held != ITEM_NONE)) {
            *ootItemOut = held;
            return slot;
        }
        return SLOT_NONE;
    }
    return SLOT_NONE;
}

// The hexagonal prism (OOT's pause cube with six faces): face 0 Hyrule items, 1 Termina items, 2 Masks, 3 Map,
// 4 Quest, 5 Equipment. The three item faces share OOT's PAUSE_ITEM page state; the face says whose page it is.
int sFace = COMBO_PAUSE_FACE_HYRULE;
int sFaceFrom = COMBO_PAUSE_FACE_HYRULE; // a page turn in progress: from -> to
int sFaceTo = COMBO_PAUSE_FACE_HYRULE;
int sDrawTab = -1; // the item face being drawn right now (KaleidoScope_DrawPages draws all three), -1 = the active one
int CurTab() {
    if (sDrawTab >= 0)
        return sDrawTab;
    return sFace <= COMBO_PAUSE_FACE_MASKS ? sFace : COMBO_PAUSE_TAB_HYRULE;
}
int sWornMmMask = -1;   // MM item id of the MM mask Link wears in Hyrule (-1 = none)
int sWornForeignId = 0; // its foreign id (the button it was used from must still hold it)

int LiveMmItem(int foreignId) {
    const int s = ComboPause_OotDecode(foreignId);
    if (s < 0 || !Snapshot())
        return -1;
    return sMmItems[s] == 0xFF ? -1 : sMmItems[s];
}

// MM's bag for an MM ammo item, as Hyrule sees it (after a Snapshot, so MM's save is resident).
struct MmBag {
    int count = -1; // what a use spends: with shared consumables on, what MM will show on arrival, not only what MM's
                    // dormant save holds. -1 = not an ammo item MM holds
    int cap = 0;    // MM's bag
    int kind = -1;  // the shared consumable, -1 = not shared
    int own = -1;   // MM's save's own count
};

MmBag BagOf(int mmItem) {
    typedef int (*FnAmmoBag)(int, int*, int*);
    static FnAmmoBag sBag = (FnAmmoBag)Combo_ResolveSym("2ship", "MM_ComboAmmoBag");
    MmBag b;
    if (sBag != nullptr) {
        b.own = sBag(mmItem, &b.cap, &b.kind);
    }
    if (b.own >= 0) {
        b.count = Combo_SharedMmAmmo(b.kind, b.own, b.cap);
    }
    return b;
}

// BagOf's count; `cap` (may be null): MM's bag.
int MmAmmo(int mmItem, int* cap) {
    const MmBag b = BagOf(mmItem);
    if (cap != nullptr) {
        *cap = b.cap;
    }
    return b.count;
}

void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ComboPause] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

const char* TabName(int tab) {
    static const char* kNames[] = { "Hyrule", "Termina", "Masks" };
    return kNames[tab];
}

} // namespace

// =====================================================================================================================
// 1. OOT as the host.
// =====================================================================================================================

extern "C" int Combo_PauseTab(void) {
    return CurTab();
}

extern "C" int Combo_PauseIsForeign(int item) {
    return ComboPause_OotDecode(item) >= 0;
}

// ---- the prism ------------------------------------------------------------------------------------------------------
// Faces in page-turn order (R / stick right = +1): Hyrule items, Termina items, Masks, Map, Quest, Equipment. Each
// face's native page (PAUSE_ITEM for the three item faces) and which item page it shows.
extern "C" int Combo_PauseFacePage(int face) {
    static const int kPages[COMBO_PAUSE_FACE_COUNT] = { PAUSE_ITEM, PAUSE_ITEM,  PAUSE_ITEM,
                                                        PAUSE_MAP,  PAUSE_QUEST, PAUSE_EQUIP };
    return kPages[((face % COMBO_PAUSE_FACE_COUNT) + COMBO_PAUSE_FACE_COUNT) % COMBO_PAUSE_FACE_COUNT];
}

extern "C" int Combo_PauseFace(void) {
    return sFace;
}

extern "C" void Combo_PauseSetDrawTab(int tab) {
    sDrawTab = tab;
}

// KaleidoScope_SwitchPage: start a 60-degree page turn. dir +1 = right (R), -1 = left (Z). Returns the target page.
extern "C" int Combo_PausePrismTurn(int dir) {
    sFaceFrom = sFace;
    sFaceTo = (sFace + (dir > 0 ? 1 : COMBO_PAUSE_FACE_COUNT - 1)) % COMBO_PAUSE_FACE_COUNT;
    KaleidoScope_ResetItemCycling();
    return Combo_PauseFacePage(sFaceTo);
}

// The page turn is over: the target face is the active one. Returns its page.
extern "C" int Combo_PausePrismArrive(PlayState* play) {
    sFace = sFaceTo;
    sFaceFrom = sFace;
    static const char* const kFaceNames[] = { "Hyrule items", "Termina items", "Masks", "Map", "Quest", "Equipment" };
    Log("face -> %d (%s)", sFace, kFaceNames[sFace]);
    PauseContext* pauseCtx = &play->pauseCtx;
    if (sFace <= COMBO_PAUSE_FACE_MASKS) {
        const int slot = pauseCtx->cursorPoint[PAUSE_ITEM];
        const uint8_t item =
            (sFace == COMBO_PAUSE_FACE_HYRULE) ? gSaveContext.inventory.items[slot] : Combo_PauseSlotItem(slot);
        pauseCtx->cursorItem[PAUSE_ITEM] = (item == ITEM_NONE) ? PAUSE_ITEM_NONE : item;
    }
    return Combo_PauseFacePage(sFace);
}

// Keep the face in step with OOT's page when something else moved it (pause open, game over, debug).
extern "C" void Combo_PausePrismSync(int pageIndex) {
    if (Combo_PauseFacePage(sFace) != pageIndex) {
        sFace = pageIndex == PAUSE_MAP     ? COMBO_PAUSE_FACE_MAP
                : pageIndex == PAUSE_QUEST ? COMBO_PAUSE_FACE_QUEST
                : pageIndex == PAUSE_EQUIP ? COMBO_PAUSE_FACE_EQUIP
                                           : COMBO_PAUSE_FACE_HYRULE;
        sFaceFrom = sFaceTo = sFace;
    }
}

// The camera's heading in faces (fractional while turning). progress = 0..1 of a page turn / the opening spin.
extern "C" float Combo_PausePrismHeading(int turning, int opening, float progress) {
    if (turning) {
        float delta = (float)(sFaceTo - sFaceFrom);
        const float faces = (float)COMBO_PAUSE_FACE_COUNT;
        if (delta > faces / 2)
            delta -= faces;
        if (delta < -faces / 2)
            delta += faces;
        return (float)sFaceFrom + delta * progress;
    }
    if (opening)
        return (float)sFace - (1.0f - progress); // opens with one page turn onto the last face, as vanilla does
    return (float)sFace;
}

extern "C" const char* const* Combo_PausePageTexs(void) {
    if (CurTab() == COMBO_PAUSE_TAB_HYRULE || !MmResolved())
        return nullptr;
    return sMmPageTexs(CurTab() == COMBO_PAUSE_TAB_MASKS ? 1 : 0);
}

extern "C" unsigned char Combo_PauseSlotItem(int pageSlot) {
    if (CurTab() == COMBO_PAUSE_TAB_HYRULE || pageSlot < 0 || pageSlot >= 24 || !Snapshot())
        return ITEM_NONE;
    const int mmSlot = pageSlot + (CurTab() == COMBO_PAUSE_TAB_MASKS ? 24 : 0);
    return sMmItems[mmSlot] == 0xFF ? ITEM_NONE : (unsigned char)ComboPause_OotEncode(mmSlot);
}

extern "C" unsigned char Combo_PauseSlotItemAny(int mmSlot) {
    if (mmSlot < 0 || mmSlot >= COMBO_PAUSE_MM_SLOTS || !Snapshot())
        return ITEM_NONE;
    return sMmItems[mmSlot] == 0xFF ? ITEM_NONE : sMmItems[mmSlot];
}

extern "C" int Combo_PauseSlotAmmo(int pageSlot) {
    if (CurTab() != COMBO_PAUSE_TAB_TERMINA || pageSlot < 0 || pageSlot >= 24 || !Snapshot())
        return -1;
    return (sMmAmmo[pageSlot] < 0) ? -1 : MmAmmo(sMmItems[pageSlot], nullptr);
}

extern "C" const char* Combo_PauseIconTex(int item) {
    const int mm = LiveMmItem(item);
    return mm < 0 ? nullptr : sMmIconTex(mm);
}

extern "C" const char* Combo_PauseNameTex(int item) {
    const int mm = LiveMmItem(item);
    return mm < 0 ? nullptr : sMmNameTex(mm);
}

// Which item / slot equipping this foreign item puts on the button. 1 = OOT's own copy (shared item OOT holds).
extern "C" int Combo_PauseResolveEquip(int item, unsigned char* outItem, unsigned char* outSlot) {
    const int mm = LiveMmItem(item);
    uint8_t ootItem = ITEM_NONE;
    const uint8_t slot = (mm >= 0) ? NativeSlotFor((uint8_t)mm, &ootItem) : (uint8_t)SLOT_NONE;
    if (slot != SLOT_NONE) {
        *outItem = ootItem;
        *outSlot = slot;
        Log("equip MM item 0x%02X -> OOT's own item 0x%02X (slot %d)", mm, ootItem, slot);
        return 1;
    }
    // A foreign button's slot byte is the foreign id itself: unique per MM slot and never a real OOT slot (0-23), so
    // OOT's "same slot on another button -> swap" dedupe works for foreign items too and never matches an empty
    // button (SLOT_NONE).
    *outItem = (unsigned char)item;
    *outSlot = (unsigned char)item;
    Log("equip MM item 0x%02X (MM slot %d) as foreign id 0x%02X", mm, ComboPause_OotDecode(item), item);
    return 0;
}

extern "C" int Combo_PauseOnButton(int item) {
    const int buttons = CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) ? 8 : 4;
    for (int b = 1; b < buttons; b++)
        if (gSaveContext.equips.buttonItems[b] == item)
            return 1;
    return 0;
}

extern "C" int Combo_PauseForeignForm(int item) {
    if (!Combo_FormsAllowed())
        return -1;
    switch (LiveMmItem(item)) {
        case MM_ITEM_MASK_DEKU:
            return COMBO_FORM_DEKU;
        case MM_ITEM_MASK_GORON:
            return COMBO_FORM_GORON;
        case MM_ITEM_MASK_ZORA:
            return COMBO_FORM_ZORA;
        case MM_ITEM_MASK_FIERCE_DEITY:
            return COMBO_FORM_FIERCE_DEITY;
        default:
            return -1;
    }
}

// MM's Great Fairy's Sword (MM item 0x10) on an OOT button: Link wields it in Hyrule (z_player.c / z_player_lib.c).
extern "C" int Combo_PauseIsForeignGfs(int item) {
    return Combo_PauseIsForeign(item) && (LiveMmItem(item) == 0x10);
}

extern "C" int Combo_PauseIsForeignMask(int item) {
    const int mm = LiveMmItem(item);
    return mm >= MM_ITEM_MASK_DEKU && mm <= MM_ITEM_MASK_GIANT;
}

extern "C" void Combo_PauseToggleMmMask(int item) {
    const int mm = LiveMmItem(item);
    if (mm < 0 || mm == sWornMmMask) {
        Log("MM mask 0x%02X off", sWornMmMask);
        sWornMmMask = -1;
        sWornForeignId = 0;
        return;
    }
    sWornMmMask = mm;
    sWornForeignId = item;
    Log("wearing MM mask 0x%02X (MM's model on Link's head)", mm);
}

// ---- MM's Chateau Romani in Hyrule ----------------------------------------------------------------------------------
// MM: drinking it sets WEEKEVENTREG_DRANK_CHATEAU_ROMANI, and every magic drain (z_parameter.c Magic_Consume /
// Magic_Update) is skipped until the Song of Time resets the cycle. Hyrule has no three-day cycle: the owner's
// adaptation is "until next dawn" (OOT's day turning at 6:00). The magic meter's border flashes while it lasts, as MM's
// Magic_Update does. Not saved: reloading the file ends it.
namespace {
bool sChateau = false;
u16 sChateauLastTime = 0;
} // namespace

extern "C" void Combo_ChateauDrink(void) {
    sChateau = true;
    sChateauLastTime = gSaveContext.dayTime;
    Log("Chateau Romani: infinite magic until dawn");
}

extern "C" int Combo_ChateauActive(void) {
    if (sChateau) {
        const u16 dawn = 0x4000; // CLOCK_TIME(6, 0)
        const u16 now = gSaveContext.dayTime;
        if ((sChateauLastTime < dawn) && (now >= dawn) && (now - sChateauLastTime < 0x4000)) {
            sChateau = false; // a new day
            Log("Chateau Romani: dawn, its magic ends");
        }
        sChateauLastTime = now;
    }
    return sChateau;
}

extern "C" int Combo_MmMaskIs(int mmItem) {
    return (sWornMmMask == mmItem) && (Combo_OotForm() == COMBO_FORM_HUMAN);
}

// ---- MM's B-button masks in Hyrule ------------------------------------------------------------------------------
// MM Player_UpdateInterface (z_player.c ~11911) gives human Link's B the worn mask's action. MM
// Interface_SetBButtonPlayerDoAction (z_parameter.c ~5115) shows it, and Player_GetItemOnButton (z_player_lib.c ~851)
// runs it, only while B holds a sword or nothing (MM also: Deku Nuts). Otherwise B keeps its item (the bow on Epona).
namespace {
// MM Player_ProcessItemButtons: blastMaskTimer = 310 frames (15.5 s). 2S2H's slider is in seconds (x 20 frames), with
// 15.5 as its default and maximum.
constexpr int kMmBlastMaskCooldownFrames = 310;
constexpr float kMmBlastMaskCooldownMax = 15.5f;
constexpr int kMmFramesPerSecond = 20;
constexpr const char* kMmBlastMaskCooldownCVar = "gEnhancements.Masks.BlastMaskCooldown";

bool BHoldsSword() {
    const u8 b = gSaveContext.equips.buttonItems[0];
    return (b == ITEM_SWORD_KOKIRI) || (b == ITEM_SWORD_MASTER) || (b == ITEM_SWORD_BGS) || (b == ITEM_SWORD_KNIFE);
}
} // namespace

extern "C" const char* Combo_MaskBActionLabel(void) {
    if (!BHoldsSword() && (gSaveContext.equips.buttonItems[0] != ITEM_NONE)) {
        return nullptr;
    }
    if (Combo_MmMaskIs(COMBO_MM_ITEM_MASK_BLAST)) {
        return "__OTR__do_action_static/gDoActionExplodeENGTex"; // MM DO_ACTION_EXPLODE
    }
    if (Combo_MmMaskIs(COMBO_MM_ITEM_MASK_BREMEN)) {
        return "__OTR__do_action_static/gDoActionMarchENGTex"; // MM DO_ACTION_MARCH
    }
    if (Combo_MmMaskIs(COMBO_MM_ITEM_MASK_KAMARO)) {
        return "__OTR__do_action_static/gDoActionDanceENGTex"; // MM DO_ACTION_DANCE
    }
    return nullptr;
}

// MM Interface_UpdateButtonsPart2 (z_parameter.c ~3306): B ("Explode") is greyed out until blastMaskTimer runs out.
// OOT's buttonStatus[0] is a plain enabled / disabled flag only while B holds a sword (otherwise it stashes the B
// item), so the grey-out is limited to that.
extern "C" int Combo_BlastMaskCoolingDown(void) {
    return (gComboBlastMaskTimer != 0) && BHoldsSword() && Combo_MmMaskIs(COMBO_MM_ITEM_MASK_BLAST);
}

// 2S2H's "Blast Mask Cooldown" slider (mm/2s2h/Enhancements/Masks/BlastMaskCooldown.cpp, seconds) replaces MM's 310
// frames when it is set below its 15.5 s default; the CVar store is shared by both games.
extern "C" int Combo_BlastMaskCooldown(void) {
    const float seconds = CVarGetFloat(kMmBlastMaskCooldownCVar, kMmBlastMaskCooldownMax);
    return (seconds < kMmBlastMaskCooldownMax) ? (int)(seconds * kMmFramesPerSecond) : kMmBlastMaskCooldownFrames;
}

extern "C" int Combo_PauseLiveMmItem(int item) {
    return LiveMmItem(item);
}

// ---- MM items OOT's own code runs in Hyrule ----------------------------------------------------------------------
// MM's stick, nut, explosive, bow and bean code is OOT's, line for line (mm z_player.c: the take-out ammo checks in
// Player_UseItem, the break in func_8083FF30, the burn-out in func_808442D8, the nut throw in Player_Action_64, the
// bomb / bombchu take-out in Player_InitExplosiveIA, the arrow count in func_808305BC and the shot in func_80831194,
// the planting in Player_ActionHandler_13), so OOT's own runs them, on MM's bag (z_player.c Combo_PlayerAmmo), with
// OOT's models and actors. MM's ocarina and lens are OOT's too (no bag). MM's Hookshot flies as far as OOT's Longshot
// (mm z_arms_hook.c ArmsHook_Wait: 26 frames at 20 units, OOT's Longshot's), so it runs as the Longshot.
extern "C" int Combo_PauseForeignHostItem(int item) {
    switch (LiveMmItem(item)) {
        case MM_ITEM_DEKU_STICK:
            return ITEM_STICK;
        case MM_ITEM_DEKU_NUT:
            return ITEM_NUT;
        case MM_ITEM_BOMB:
            return ITEM_BOMB;
        case MM_ITEM_BOMBCHU:
            return ITEM_BOMBCHU;
        case MM_ITEM_BOW:
            return ITEM_BOW;
        case MM_ITEM_MAGIC_BEANS:
            return ITEM_BEAN;
        case MM_ITEM_OCARINA_OF_TIME:
            return ITEM_OCARINA_TIME;
        case MM_ITEM_OCARINA_FAIRY:
            return ITEM_OCARINA_FAIRY;
        case MM_ITEM_LENS_OF_TRUTH:
            return ITEM_LENS;
        case MM_ITEM_HOOKSHOT:
        case MM_ITEM_LONGSHOT:
            return ITEM_LONGSHOT;
        default:
            return ITEM_NONE;
    }
}

// OOT's age rule for the OOT item that runs an MM item (z_kaleido_scope.h CHECK_AGE_REQ_ITEM, so Timeless Equipment and
// Unified Inventory lift it as they do for OOT's own). OOT's pause greys its own items out by it; an MM item comes from
// the Termina face, which knows no ages, so Player_UseItem refuses it there instead.
extern "C" int Combo_PauseForeignAgeOk(int item) {
    constexpr u8 kAgeReqNone = 9; // z_kaleido_scope.h AGE_REQ_NONE
    const int oot = Combo_PauseForeignHostItem(item);
    if (oot == ITEM_NONE) {
        return 1;
    }
    const u8 req = gItemAgeReqs[oot];
    return GameInteractor_Should(VB_ITEM_MEETS_AGE_REQ, (req == kAgeReqNone) || (req == gSaveContext.linkAge), oot) ? 1
                                                                                                                    : 0;
}

// ---- MM bottles in Hyrule ---------------------------------------------------------------------------------------
// MM's own bottle contents with an OOT player action that runs MM's behaviour (MM's bottle code is OOT's, line for
// line: mm z_player.c Player_Action_67 drink / _69 fairy / _70 release vs OOT Player_Action_8084EAC0 / EED8 / EFC0):
// drink effects MM D_8085D790 = OOT D_808549FC (+ Chateau = heal|magic, Kotake's drink = MM's Poe IA).
extern "C" int Combo_PauseForeignBottleAction(int item) {
    switch (LiveMmItem(item)) {
        case 0x12: // MM_ITEM_BOTTLE (empty)
            return PLAYER_IA_BOTTLE;
        case 0x13:
            return PLAYER_IA_BOTTLE_POTION_RED;
        case 0x14:
            return PLAYER_IA_BOTTLE_POTION_GREEN;
        case 0x15:
            return PLAYER_IA_BOTTLE_POTION_BLUE;
        case 0x16:
            return PLAYER_IA_BOTTLE_FAIRY;
        case 0x18:
            return PLAYER_IA_BOTTLE_MILK_FULL;
        case 0x19:
            return PLAYER_IA_BOTTLE_MILK_HALF;
        case 0x1A:
            return PLAYER_IA_BOTTLE_FISH;
        case 0x1B:
            return PLAYER_IA_BOTTLE_BUG;
        case 0x1C: // MM's blue fire (MM maps it to its bug IA; in Hyrule OOT's blue fire action pours it)
            return PLAYER_IA_BOTTLE_FIRE;
        case 0x1D:
        case 0x27: // ITEM_OBABA_DRINK: MM's item table gives it PLAYER_IA_BOTTLE_POE (drinks like a Poe)
            return PLAYER_IA_BOTTLE_POE;
        case 0x25: // ITEM_CHATEAU: drunk like the blue potion (MM D_8085D790 CHATEAU = 1|2) + infinite magic
            return PLAYER_IA_BOTTLE_POTION_BLUE;
        default:
            return -1;
    }
}

extern "C" int Combo_PauseIsForeignBottle(int item) {
    const int mm = LiveMmItem(item);
    return (mm >= MM_ITEM_BOTTLE) && (mm <= 0x27); // MM ITEM_BOTTLE .. ITEM_OBABA_DRINK, show-only contents too
}

namespace {
typedef int (*FnSetSlot)(int, int);
FnSetSlot sMmSetSlot = nullptr;
} // namespace

// MM ammo of the MM item a foreign id names, as a use spends it, -1 = none.
extern "C" int Combo_PauseForeignAmmo(int foreignId) {
    return Combo_PauseForeignAmmoDigits(foreignId, nullptr);
}

extern "C" int Combo_PauseForeignAmmoDigits(int foreignId, int* full) {
    const int slot = ComboPause_OotDecode(foreignId);
    int cap = 0;
    const int ammo = ((slot >= 0) && Snapshot() && (sMmAmmo[slot] >= 0)) ? MmAmmo(sMmItems[slot], &cap) : -1;
    if (full != nullptr) {
        *full = (ammo >= 0) && (ammo >= cap);
    }
    return ammo;
}

// MM Inventory_ChangeAmmo for the MM item a foreign id names: on the shared count with sharing on (so the count drawn
// and spent, BagOf's, moves), otherwise on MM's resident save.
extern "C" int Combo_PauseForeignAmmoChange(int foreignId, int n) {
    typedef int (*FnChangeAmmo)(int, int);
    static FnChangeAmmo sChange = nullptr;
    const int mm = LiveMmItem(foreignId);
    if (mm < 0) {
        return 0;
    }
    const MmBag b = BagOf(mm);
    if (b.count >= 0) {
        const int bag = (b.cap > b.count) ? b.cap : b.count;
        int next = b.count + n;
        next = (next < 0) ? 0 : ((next > bag) ? bag : next);
        if (Combo_SharedMmAmmoSpend(b.kind, next - b.count, b.own)) {
            return 1;
        }
    }
    if (sChange == nullptr) {
        sChange = (FnChangeAmmo)Combo_ResolveSym("2ship", "MM_ComboItems_ChangeAmmo");
    }
    return (sChange != nullptr) ? sChange(mm, n) : 0;
}

// MM sPlayerBottleColors (mm z_player_lib.c), by MM item id.
extern "C" int Combo_PauseMmBottleColor(int foreignId, unsigned char* rgb) {
    struct C {
        uint8_t item, r, g, b;
    };
    static const C kColors[] = {
        { 0x12, 255, 255, 255 }, { 0x13, 255, 0, 0 },     { 0x14, 0, 200, 0 },     { 0x15, 0, 0, 255 },
        { 0x16, 80, 80, 255 },   { 0x17, 248, 200, 0 },   { 0x18, 255, 255, 255 }, { 0x19, 255, 255, 255 },
        { 0x1A, 80, 80, 255 },   { 0x1B, 0, 0, 255 },     { 0x1C, 0, 0, 255 },     { 0x1D, 255, 0, 255 },
        { 0x1E, 255, 0, 255 },   { 0x1F, 136, 192, 255 }, { 0x20, 136, 192, 255 }, { 0x21, 184, 232, 232 },
        { 0x22, 255, 180, 0 },   { 0x23, 131, 0, 174 },   { 0x24, 252, 238, 0 },   { 0x25, 255, 255, 255 },
        { 0x26, 64, 64, 32 },    { 0x27, 255, 0, 255 },
    };
    if (!Combo_PauseIsForeign(foreignId)) {
        return 0;
    }
    const int mm = LiveMmItem(foreignId);
    for (const C& c : kColors) {
        if (c.item == mm) {
            rgb[0] = c.r;
            rgb[1] = c.g;
            rgb[2] = c.b;
            return 1;
        }
    }
    return 0;
}

// The held foreign bottle's content changed (drunk / poured / caught): MM's own slot takes MM's item, so Termina
// sees the same bottle. `ootItem` is the OOT item OOT's player code would have written.
extern "C" int Combo_PauseSetForeignBottle(int foreignId, int ootItem) {
    const int slot = ComboPause_OotDecode(foreignId);
    const int cur = LiveMmItem(foreignId);
    if (slot < 0 || cur < 0) {
        return 0;
    }
    int mm;
    switch (ootItem) {
        case ITEM_BOTTLE:
            mm = (cur == 0x18) ? 0x19 : 0x12; // MM zp:18262: full milk leaves half milk, the rest an empty bottle
            break;
        case ITEM_MILK_HALF:
            mm = 0x19;
            break;
        case ITEM_FAIRY:
            mm = 0x16;
            break;
        case ITEM_FISH:
            mm = 0x1A;
            break;
        case ITEM_BUG:
            mm = 0x1B;
            break;
        case ITEM_BLUE_FIRE:
            mm = 0x1C;
            break;
        case ITEM_POE:
            mm = 0x1D;
            break;
        case ITEM_BIG_POE:
            mm = 0x1E;
            break;
        default:
            return 0;
    }
    if (sMmSetSlot == nullptr) {
        sMmSetSlot = (FnSetSlot)Combo_ResolveSym("2ship", "MM_ComboItems_SetSlot");
    }
    if (sMmSetSlot == nullptr || !sMmSetSlot(slot, mm)) {
        return 0;
    }
    Log("MM bottle slot %d: 0x%02X -> 0x%02X (OOT item 0x%02X)", slot, cur, mm, ootItem);
    return 1;
}

// Masks travel with Link across the doors (owner 2026-11-14: "if i don a mask in OOT i expect to be wearing it in MM").
extern "C" int Combo_PauseWornMmMaskId(void) {
    return sWornMmMask;
}

// Wear the MM mask `mm` from whichever button holds it (OOT's rule: a mask is worn only while it is on a button).
// Returns 1 when it is now worn.
extern "C" int Combo_PauseWearMmMaskFromButtons(int mm) {
    for (int b = 1; b < 8; b++) {
        const int id = gSaveContext.equips.buttonItems[b];
        if (Combo_PauseIsForeign(id) && (LiveMmItem(id) == mm)) {
            sWornMmMask = mm;
            sWornForeignId = id;
            Log("MM mask 0x%02X worn on arrival (button %d)", mm, b);
            return 1;
        }
    }
    return 0;
}

extern "C" void Combo_PauseClearMmMask(void) {
    sWornMmMask = -1;
    sWornForeignId = 0;
}

extern "C" const void* Combo_PauseWornMmMaskDL(void) {
    if (sWornMmMask < 0 || !MmResolved())
        return nullptr;
    // OOT's rule for its own masks: a mask comes off once it is on no button.
    if (!Combo_PauseOnButton(sWornForeignId) || LiveMmItem(sWornForeignId) != sWornMmMask) {
        Combo_PauseClearMmMask();
        return nullptr;
    }
    const char* dl = sMmMaskDL(sWornMmMask);
    if (dl == nullptr || strncmp(dl, "__OTR__", 7) != 0)
        return nullptr;
    return Combo_MmDL(dl + 7);
}

// MM's transformation mask model (MM_ComboTransformMaskDL), routed to MM's ResourceManager; NULL if unavailable.
extern "C" const void* Combo_TransformMaskDL(int mmForm, int face) {
    typedef const char* (*Fn)(int, int);
    static Fn sFn = (Fn)Combo_ResolveSym("2ship", "MM_ComboTransformMaskDL");
    const char* dl = (sFn != nullptr) ? sFn(mmForm, face) : nullptr;
    if ((dl == nullptr) || (strncmp(dl, "__OTR__", 7) != 0))
        return nullptr;
    return Combo_MmDL(dl + 7);
}

extern "C" int Combo_PauseWornMmMaskItem(void) {
    return sWornMmMask;
}

extern "C" void Combo_PauseLogUse(int item, const char* what) {
    Log("use foreign id 0x%02X (MM slot %d, MM item 0x%02X): %s", item, ComboPause_OotDecode(item), LiveMmItem(item),
        what);
}

// =====================================================================================================================
// 2. OOT as the owning game (consumed by mm/2s2h/Rando/MiscBehavior/ComboPause.cpp: MM's Hyrule tab).
// =====================================================================================================================

extern "C" COMBO_EXPORT int SOH_ComboPause_GetItems(uint8_t* items, int8_t* ammo) {
    static const uint8_t kAmmoItems[] = { ITEM_STICK,     ITEM_NUT,     ITEM_BOMB, ITEM_BOW,
                                          ITEM_SLINGSHOT, ITEM_BOMBCHU, ITEM_BEAN };
    for (int s = 0; s < COMBO_PAUSE_OOT_SLOTS; s++) {
        const uint8_t it = gSaveContext.inventory.items[s];
        items[s] = it;
        ammo[s] = -1;
        for (uint8_t a : kAmmoItems)
            if (it == a)
                ammo[s] = (int8_t)AMMO(it);
    }
    // fileNum 0xFF = no OOT save loaded (title / file select before any load).
    return gSaveContext.fileNum <= 2;
}

extern "C" COMBO_EXPORT const char* SOH_ComboPause_IconTex(int ootItem) {
    if (ootItem < 0 || ootItem > ITEM_ROCS_FEATHER)
        return nullptr;
    return (const char*)gItemIcons[ootItem];
}

extern "C" COMBO_EXPORT const char* SOH_ComboPause_NameTex(int ootItem) {
    if (ootItem < 0 || ootItem > ITEM_SWORD_KNIFE) // the item page and the equipment page's names
        return nullptr;
    return KaleidoScope_ComboItemNameTex(ootItem);
}

// page 0 = "Select Item", 1 = "Equipment".
extern "C" COMBO_EXPORT const char* const* SOH_ComboPause_PageTexs(int page) {
    return page == 1 ? KaleidoScope_ComboEquipmentTexs() : KaleidoScope_ComboSelectItemTexs();
}

// OOT's equipment page as OOT's KaleidoScope_DrawEquipment fills it: a 4x4 grid (row = sword / shield / tunic /
// boots; column 0 = that row's upgrade - bullet bag or quiver, bomb bag, strength, scale - columns 1-3 = the three
// pieces), ITEM_NONE where nothing is owned, and the column (1-3) each row has equipped (0 = none).
extern "C" COMBO_EXPORT int SOH_ComboPause_GetEquipGrid(uint8_t* items, uint8_t* equipped) {
    static const u8 kChildUpgrades[] = { UPG_BULLET_BAG, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };
    static const u8 kAdultUpgrades[] = { UPG_QUIVER, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };
    static const u8 kChildBases[] = { ITEM_BULLET_BAG_30, ITEM_BOMB_BAG_20, ITEM_BRACELET, ITEM_SCALE_SILVER };
    static const u8 kAdultBases[] = { ITEM_QUIVER_30, ITEM_BOMB_BAG_20, ITEM_BRACELET, ITEM_SCALE_SILVER };
    if (gSaveContext.fileNum > 2)
        return 0;
    const bool child = LINK_AGE_IN_YEARS == YEARS_CHILD;
    for (int row = 0; row < 4; row++) {
        int upgItem = ITEM_NONE;
        if (child || ((row == 0) && (CUR_UPG_VALUE(kAdultUpgrades[row]) == 0))) {
            const int v = CUR_UPG_VALUE(kChildUpgrades[row]);
            if (v != 0)
                upgItem = kChildBases[row] + v - 1;
        } else if (CUR_UPG_VALUE(kAdultUpgrades[row]) != 0) {
            upgItem = kAdultBases[row] + CUR_UPG_VALUE(kAdultUpgrades[row]) - 1;
        }
        items[row * 4] = (uint8_t)upgItem;
        for (int k = 0; k < 3; k++) {
            const int bit = row * 4 + k;
            int item = ITEM_NONE;
            if ((row == 0) && (k == 2) && (gSaveContext.bgsFlag != 0))
                item = ITEM_SWORD_BGS;
            else if ((row == 0) && (k == 2) && (gBitFlags[bit + 1] & gSaveContext.inventory.equipment))
                item = ITEM_SWORD_KNIFE;
            else if (gBitFlags[bit] & gSaveContext.inventory.equipment)
                item = ITEM_SWORD_KOKIRI + row * 3 + k;
            items[row * 4 + 1 + k] = (uint8_t)item;
        }
        equipped[row] = (uint8_t)CUR_EQUIP_VALUE(row);
    }
    return 1;
}

// =====================================================================================================================
// Debug (test harness only): COMBO_DEBUG_PAUSE=<script> drives the pause menu (combo/menu/ComboPauseHarness.h).
// =====================================================================================================================
namespace {
void DumpState() {
    PlayState* play = gPlayState;
    if (play == nullptr)
        return;
    const PauseContext* p = &play->pauseCtx;
    char btns[128];
    snprintf(btns, sizeof(btns), "B %02X C %02X/%02X/%02X D %02X/%02X/%02X/%02X", gSaveContext.equips.buttonItems[0],
             gSaveContext.equips.buttonItems[1], gSaveContext.equips.buttonItems[2], gSaveContext.equips.buttonItems[3],
             gSaveContext.equips.buttonItems[4], gSaveContext.equips.buttonItems[5], gSaveContext.equips.buttonItems[6],
             gSaveContext.equips.buttonItems[7]);
    Log("dump: pause state %d page %d tab %s cursor slot %d item %d special %d | %s | slots %02X/%02X/%02X "
        "%02X/%02X/%02X/%02X | form %d mmMask %d",
        p->state, p->pageIndex, TabName(CurTab()), p->cursorPoint[PAUSE_ITEM], p->cursorItem[PAUSE_ITEM],
        p->cursorSpecialPos, btns, gSaveContext.equips.cButtonSlots[0], gSaveContext.equips.cButtonSlots[1],
        gSaveContext.equips.cButtonSlots[2], gSaveContext.equips.cButtonSlots[3], gSaveContext.equips.cButtonSlots[4],
        gSaveContext.equips.cButtonSlots[5], gSaveContext.equips.cButtonSlots[6], Combo_OotForm(), sWornMmMask);
    {
        std::string row;
        for (int s = 0; s < 24; s++) {
            char b[8];
            snprintf(b, sizeof(b), "%02X ", gSaveContext.inventory.items[s]);
            row += b;
        }
        Log("dump: OOT items: %s", row.c_str());
    }
    if (CurTab() != COMBO_PAUSE_TAB_HYRULE) {
        std::string row;
        for (int s = 0; s < 24; s++) {
            char b[8];
            const unsigned char id = Combo_PauseSlotItem(s);
            snprintf(b, sizeof(b), "%02X ", id == ITEM_NONE ? 0xFF : LiveMmItem(id));
            row += b;
        }
        Log("dump: %s page MM items: %s", TabName(CurTab()), row.c_str());
    }
}

void RegisterComboPauseHarness() {
    static ComboPauseHarness::Script sScript(std::getenv("COMBO_DEBUG_PAUSE"));
    if (!sScript.Active())
        return;
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() {
        static int sReady = 0;
        PlayState* play = gPlayState;
        if (play == nullptr || gGameState != &play->state || gSaveContext.fileNum > 2 || GET_PLAYER(play) == nullptr)
            return;
        if (++sReady < 60)
            return; // let the scene settle
        static int sStall = 0;
        PauseContext* pc = &play->pauseCtx;
        if (sStall > 0 && pc->state == 6 && pc->unk_1E4 == 1 && pc->unk_1EA >= 32) {
            pc->unk_1EA -= 4; // the turn adds 4 back this frame: held at the halfway point
            sStall--;
        }
        const ComboPauseHarness::Frame f = sScript.Step();
        if (f.stall >= 0)
            sStall = f.stall;
        if (f.done)
            return;
        Input* in = &play->state.input[0];
        in->press.button |= f.press;
        in->cur.button |= f.press | f.hold;
        if (f.stickX || f.stickY) {
            in->rel.stick_x = f.stickX;
            in->rel.stick_y = f.stickY;
            in->cur.stick_x = f.stickX;
            in->cur.stick_y = f.stickY;
        }
        if (f.dpadOn)
            CVarSetInteger(CVAR_ENHANCEMENT("DpadEquips"), 1);
        if (f.faceCam) {
            Player* player = GET_PLAYER(play);
            player->actor.shape.rot.y += 0x8000;
            player->actor.world.rot.y = player->yaw = player->actor.shape.rot.y;
        }
        if (f.slot >= 0 && f.slot < 24) {
            PauseContext* p = &play->pauseCtx;
            p->cursorPoint[PAUSE_ITEM] = p->cursorSlot[PAUSE_ITEM] = f.slot;
            p->cursorX[PAUSE_ITEM] = f.slot % 6;
            p->cursorY[PAUSE_ITEM] = f.slot / 6;
            p->cursorSpecialPos = 0;
        }
        if (f.cross >= 0) {
            Log("debug: cross into MM at entrance 0x%X", f.cross);
            Combo_RequestCrossSwitch(f.cross);
        }
        if (f.give >= 0) {
            typedef void (*FnGive)(int);
            static FnGive sGive = (FnGive)Combo_ResolveSym("2ship", "MM_ComboPause_DebugGive");
            if (sGive != nullptr)
                sGive(f.give);
        }
        if (f.dump)
            DumpState();
    });
}
RegisterShipInitFunc sHarnessInit(RegisterComboPauseHarness, {});
} // namespace

// Debug (test harness only, COMBO_DEBUG_MM_PAUSE "give=XX"): put an OOT item into OOT's resident save at its home slot.
extern "C" COMBO_EXPORT void SOH_ComboPause_DebugGive(int ootItem) {
    if (ootItem < 0 || ootItem > ITEM_CLAIM_CHECK)
        return;
    gSaveContext.inventory.items[gItemSlots[ootItem]] = (u8)ootItem;
    fprintf(stderr, "[ComboPause] debug: OOT item 0x%02X -> OOT slot %d\n", ootItem, gItemSlots[ootItem]);
}
