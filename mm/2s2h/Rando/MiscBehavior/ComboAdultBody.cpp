// mm/2s2h/Rando/MiscBehavior/ComboAdultBody.cpp — ComboShip: adult Link in Termina. Combo-owned file (2ship
// GLOB_RECURSE).
//
// Owner ruling 2026-11-12: Link keeps his age across games ("if you came into termina as an adult, you're an adult").
// MM has no adult Link, but the two engines share Link's whole rig: the same 22-limb player skeleton layout, the same
// animation format, and MM's own human animations already play on an adult-proportioned body (Fierce Deity is OOT's
// adult Link scaled 1.5x). So adult Link in MM = MM's HUMAN form in every way that matters for play (items, masks,
// animations, controls) with:
//   - OOT's adult skeleton drawn instead of the child one (loaded from OOT's resident ResourceManager), every limb
//     model submitted as an "__OTR__@oot:" routed display list so it resolves against OOT's archive;
//   - hand / sheath / waist models MM picks for human Link remapped to OOT's adult equivalents (sword in hand, shield,
//     bow, hookshot, ocarina, bottle, sheath + shield on the back);
//   - OOT's adult eye/mouth textures and OOT's tunic color;
//   - adult body dimensions (ceiling, ledges, water depth, wall radius, root motion), set in z_player.c.
// The age and tunic come from OOT's own save, pushed by the launcher before every switch into MM.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include <libultraship/libultraship.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceManagerScope.h>

#include "ComboExport.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "ComboAdultBody.h"
#include "ComboGfsGrip.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
}

extern "C" int Combo_MmOotTunicValue(void); // ComboOotItems.cpp: 1 Kokiri, 2 Goron, 3 Zora, 0 = no OOT save

namespace {

int sLinkAdult = 0; // launcher push: OOT's Link is adult
int sTunic = 0;     // OOT's equipped tunic (0 Kokiri, 1 Goron, 2 Zora)

constexpr const char* kOotObj = "__OTR__objects/object_link_boy/";
constexpr const char* kRoutedObj = "__OTR__@oot:objects/object_link_boy/";
constexpr const char* kMmHumanObj = "__OTR__objects/object_link_child/";

std::shared_ptr<Ship::IResource> sSkelRes;
FlexSkeletonHeader* sSkel = nullptr;
bool sSkelTried = false;

void ClearCache() {
    sSkelRes.reset();
    sSkel = nullptr;
    sSkelTried = false;
}

FlexSkeletonHeader* AdultSkeleton() {
    static bool sHooked = (Ship::CrossRMRegistry::RegisterTeardownListener(&ClearCache), true);
    (void)sHooked;
    if (sSkelTried)
        return sSkel;
    sSkelTried = true;
    auto rm = Ship::CrossRMRegistry::Get("oot");
    if (rm == nullptr)
        return nullptr;
    try {
        // The Skeleton factory nested-loads its limbs through the ACTIVE resource manager: scope it to OOT's.
        Ship::ResourceManagerScope scope(rm);
        sSkelRes = rm->LoadResource("__OTR__objects/object_link_boy/gLinkAdultSkel");
    } catch (...) { sSkelRes.reset(); }
    FlexSkeletonHeader* skel = sSkelRes ? (FlexSkeletonHeader*)sSkelRes->GetRawPointer() : nullptr;
    if (skel == nullptr || skel->sh.limbCount + 1 != PLAYER_LIMB_MAX) {
        fprintf(stderr, "[ComboAdultBody] OOT adult skeleton unavailable (%s) — Link stays child-sized in MM\n",
                skel ? "limb count mismatch" : "not loaded");
        sSkelRes.reset();
        return nullptr;
    }
    sSkel = skel;
    return sSkel;
}

// "__OTR__objects/object_link_boy/X" (or a bare OOT asset name) -> stable "__OTR__@oot:objects/object_link_boy/X".
const char* Routed(const char* name) {
    static std::unordered_map<std::string, std::string> sInterned;
    std::string key = name;
    if (key.rfind(kOotObj, 0) == 0)
        key = key.substr(strlen(kOotObj));
    auto it = sInterned.find(key);
    if (it == sInterned.end())
        it = sInterned.emplace(key, std::string(kRoutedObj) + key).first;
    return it->second.c_str();
}

struct AdultPair {
    const char* nearDl;
    const char* farDl;
};

// MM human limb models -> OOT adult equivalents.
const std::unordered_map<std::string, AdultPair>& HumanToAdult() {
    static const std::unordered_map<std::string, AdultPair> k = {
        { "gLinkHumanLeftHandOpenDL", { "gLinkAdultLeftHandNearDL", "gLinkAdultLeftHandFarDL" } },
        { "gLinkHumanLeftHandClosedDL", { "gLinkAdultLeftHandClosedNearDL", "gLinkAdultLeftHandClosedFarDL" } },
        { "gLinkHumanLeftHandHoldingKokiriSwordDL",
          { "gLinkAdultLeftHandHoldingMasterSwordNearDL", "gLinkAdultLeftHandHoldingMasterSwordFarDL" } },
        { "gLinkHumanLeftHandHoldingRazorSwordDL",
          { "gLinkAdultLeftHandHoldingMasterSwordNearDL", "gLinkAdultLeftHandHoldingMasterSwordFarDL" } },
        { "gLinkHumanLeftHandHoldingGildedSwordDL",
          { "gLinkAdultLeftHandHoldingMasterSwordNearDL", "gLinkAdultLeftHandHoldingMasterSwordFarDL" } },
        // MM's Great Fairy's Sword is NOT OOT's Biggoron Sword: the adult closed fist here, MM's own blade drawn in it
        // after the hand (Combo_MM_AdultDrawHeld, from Player_PostLimbDrawGameplay).
        { "gLinkHumanLeftHandHoldingGreatFairysSwordDL",
          { "gLinkAdultLeftHandClosedNearDL", "gLinkAdultLeftHandClosedFarDL" } },
        { "gLinkHumanLeftHandHoldBottleDL", { "gLinkAdultHandHoldingBottleDL", "gLinkAdultHandHoldingBottleDL" } },
        { "gLinkHumanRightHandOpenDL", { "gLinkAdultRightHandNearDL", "gLinkAdultRightHandFarDL" } },
        { "gLinkHumanRightHandClosedDL", { "gLinkAdultRightHandClosedNearDL", "gLinkAdultRightHandClosedFarDL" } },
        { "gLinkHumanRightHandHoldingHerosShieldDL",
          { "gLinkAdultRightHandHoldingHylianShieldNearDL", "gLinkAdultRightHandHoldingHylianShieldFarDL" } },
        { "gLinkHumanRightHandHoldingMirrorShieldDL",
          { "gLinkAdultRightHandHoldingMirrorShieldNearDL", "gLinkAdultRightHandHoldingMirrorShieldFarDL" } },
        { "gLinkHumanRightHandHoldingBowDL",
          { "gLinkAdultRightHandHoldingBowNearDL", "gLinkAdultRightHandHoldingBowFarDL" } },
        { "gLinkHumanRightHandHoldingHookshotDL",
          { "gLinkAdultRightHandHoldingHookshotNearDL", "gLinkAdultRightHandHoldingHookshotFarDL" } },
        { "gLinkHumanRightHandHoldingOcarinaDL",
          { "gLinkAdultRightHandHoldingOotNearDL", "gLinkAdultRightHandHoldingOotFarDL" } },
        { "gLinkHumanWaistDL", { "gLinkAdultWaistNearDL", "gLinkAdultWaistFarDL" } },
    };
    return k;
}

// The sheath limb shows sword + sheath + shield on the back as ONE model in OOT (MM draws the back shield separately;
// that draw is skipped for adult in z_player_lib.c). MM's sheath model types: 12/14 = sword in the sheath,
// 14/15 = shield on the back.
const char* AdultSheath(Player* player, int lod) {
    const bool sword = GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) != EQUIP_VALUE_SWORD_NONE;
    const bool sheathed = sword && ((player->sheathType == PLAYER_MODELTYPE_SHEATH_12) ||
                                    (player->sheathType == PLAYER_MODELTYPE_SHEATH_14));
    const bool back =
        (player->currentShield != PLAYER_SHIELD_NONE) &&
        ((player->sheathType == PLAYER_MODELTYPE_SHEATH_14) || (player->sheathType == PLAYER_MODELTYPE_SHEATH_15));
    const bool mirror = player->currentShield == PLAYER_SHIELD_MIRROR_SHIELD;
    const char* base;
    if (back && sheathed)
        base = mirror ? "gLinkAdultMirrorShieldSwordAndSheath" : "gLinkAdultHylianShieldSwordAndSheath";
    else if (back)
        base = mirror ? "gLinkAdultMirrorShieldAndSheath" : "gLinkAdultHylianShieldAndSheath";
    else if (sheathed)
        base = "gLinkAdultMasterSwordAndSheath";
    else
        base = "gLinkAdultSheath";
    return Routed((std::string(base) + (lod == 0 ? "NearDL" : "FarDL")).c_str());
}

// OOT's adult first-person arms (soh z_player_lib.c sFirstPerson* tables, adult column).
const char* AdultFirstPerson(Player* player, s32 limbIndex) {
    switch (limbIndex) {
        case PLAYER_LIMB_LEFT_FOREARM:
            return Routed("gLinkAdultRightArmOutNearDL");
        case PLAYER_LIMB_LEFT_HAND:
            return Routed("gLinkAdultRightHandOutNearDL");
        case PLAYER_LIMB_RIGHT_SHOULDER:
            return Routed("gLinkAdultRightShoulderNearDL");
        case PLAYER_LIMB_RIGHT_FOREARM:
            return Routed("gLinkAdultLeftArmOutNearDL");
        case PLAYER_LIMB_RIGHT_HAND:
            return Player_IsHoldingHookshot(player) ? Routed("gLinkAdultRightHandHoldingHookshotFarDL")
                                                    : Routed("gLinkAdultRightHandHoldingBowFirstPersonDL");
        default:
            return nullptr;
    }
}

// The OOT skeleton's own model for this limb, routed (null = the limb has none).
const char* AdultDefault(s32 limbIndex, int lod) {
    if (sSkel == nullptr || limbIndex < 1 || limbIndex >= PLAYER_LIMB_MAX)
        return nullptr;
    LodLimb* limb = (LodLimb*)((void**)sSkel->sh.segment)[limbIndex - 1];
    const char* dl = limb ? (const char*)limb->dLists[lod] : nullptr;
    return (dl != nullptr && strncmp(dl, kOotObj, strlen(kOotObj)) == 0) ? Routed(dl) : nullptr;
}

OverrideLimbDrawFlex sInner = nullptr;
bool sAdultGfsPending = false;

s32 AdultOverride(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, Actor* actor) {
    Player* player = (Player*)actor;
    const int lod = 0; // Player_DrawImpl forces LOD 0 for the adult body (the routed models carry both)
    const s32 ret = sInner != nullptr ? sInner(play, limbIndex, dList, pos, rot, actor) : 0;

    if (sInner == Player_OverrideLimbDrawGameplayFirstPerson) {
        // First person: OOT's adult arms in front of the camera, nothing else.
        *dList = (player->unk_AA5 == PLAYER_UNKAA5_3) ? (Gfx*)AdultFirstPerson(player, limbIndex) : nullptr;
        return ret;
    }
    if (*dList == nullptr)
        return ret; // the override hid this limb (or it has no model)
    if (limbIndex == PLAYER_LIMB_SHEATH) {
        *dList = (Gfx*)AdultSheath(player, lod);
        return ret;
    }
    const char* s = (const char*)*dList;
    if (strncmp(s, "__OTR__@oot:", 12) == 0) {
        return ret; // already OOT's model (an OOT item in hand, ComboOotItems.cpp)
    }
    if (strncmp(s, kOotObj, strlen(kOotObj)) == 0) {
        *dList = (Gfx*)Routed(s); // the adult skeleton's own limb model
    } else if (strncmp(s, kMmHumanObj, strlen(kMmHumanObj)) == 0) {
        if (strcmp(s + strlen(kMmHumanObj), "gLinkHumanLeftHandHoldingGreatFairysSwordDL") == 0) {
            sAdultGfsPending = true; // draw MM's GFS blade in the adult fist once the hand is drawn
        }
        auto it = HumanToAdult().find(s + strlen(kMmHumanObj));
        const char* adult = it != HumanToAdult().end() ? Routed(lod == 0 ? it->second.nearDl : it->second.farDl)
                                                       : AdultDefault(limbIndex, lod);
        *dList = (Gfx*)adult;
    } else {
        // Not an asset path (a runtime-built model, e.g. a cosmetic split): fall back to the adult limb so no child
        // part ever shows on the adult body.
        *dList = (Gfx*)AdultDefault(limbIndex, lod);
    }
    return ret;
}

// OOT eye order: open, half, closed, left, right, wide, down, wincing. MM: open, half, closed, right, left, up,
// down, wincing.
const char* const kAdultEyes[PLAYER_EYES_MAX] = {
    "__OTR__objects/object_link_boy/gLinkAdultEyesOpenTex",
    "__OTR__objects/object_link_boy/gLinkAdultEyesHalfTex",
    "__OTR__objects/object_link_boy/gLinkAdultEyesClosedfTex",
    "__OTR__objects/object_link_boy/gLinkAdultEyesRollRightTex",
    "__OTR__objects/object_link_boy/gLinkAdultEyesRollLeftTex",
    "__OTR__objects/object_link_boy/gLinkAdultEyesShockTex",
    "__OTR__objects/object_link_boy/gLinkAdultEyesUnk1Tex",
    "__OTR__objects/object_link_boy/gLinkAdultEyesUnk2Tex",
};
const char* const kAdultMouths[PLAYER_MOUTH_MAX] = {
    "__OTR__objects/object_link_boy/gLinkAdultMouth1Tex",
    "__OTR__objects/object_link_boy/gLinkAdultMouth2Tex",
    "__OTR__objects/object_link_boy/gLinkAdultMouth3Tex",
    "__OTR__objects/object_link_boy/gLinkAdultMouth4Tex",
};
const Color_RGB8 kTunicColors[3] = { { 30, 105, 27 }, { 100, 20, 0 }, { 0, 60, 100 } };

// The player's tunic color choice: OOT's cosmetics editor for the tunic OOT has equipped (its body is OOT's), then
// MM's "Human Tunic" cosmetic for the green tunic (the color picked for Link in Termina), else the stock color.
Color_RGB8 AdultTunicColor() {
    // OOT's save, live: a tunic put on in Termina (its pause menu, a tunic button) shows at once. The launcher push
    // (sTunic) covers the moment before OOT's save is readable.
    const int live = Combo_MmOotTunicValue() - 1;
    const int tunic = (live >= 0) ? live : sTunic;
    const int t = (tunic >= 0 && tunic < 3) ? tunic : 0;
    static const char* const kOotCosmetic[3] = { "gCosmetics.Link.KokiriTunic", "gCosmetics.Link.GoronTunic",
                                                 "gCosmetics.Link.ZoraTunic" };
    const std::string oot = kOotCosmetic[t];
    if (CVarGetInteger((oot + ".Changed").c_str(), 0)) {
        // CVarGetColor reads the value whether it was stored as RGB (SoH's tunic picker) or RGBA.
        const Color_RGBA8 d = { kTunicColors[t].r, kTunicColors[t].g, kTunicColors[t].b, 255 };
        const Color_RGBA8 v = CVarGetColor((oot + ".Value").c_str(), d);
        return { v.r, v.g, v.b };
    }
    if (t == 0 && CVarGetInteger("gCosmetic.Player.HumanTunic.Changed", 0)) {
        const Color_RGBA8 m = CVarGetColor("gCosmetic.Player.HumanTunic.Color", { 30, 105, 27, 255 });
        return { m.r, m.g, m.b };
    }
    return kTunicColors[t];
}

} // namespace

extern "C" int Combo_MM_LinkIsAdult(void) {
    return sLinkAdult;
}

extern "C" int Combo_MM_AdultBodyActive(Actor* actor) {
    if (actor == nullptr || actor->id != ACTOR_PLAYER)
        return 0;
    const bool wanted = sLinkAdult != 0 || CVarGetInteger("gCombo.Debug.AdultInMM", 0) != 0;
    if (!wanted || ((Player*)actor)->transformation != PLAYER_FORM_HUMAN || CVarGetInteger("gModes.PlayAsKafei", 0))
        return 0;
    return AdultSkeleton() != nullptr;
}

extern "C" void** Combo_MM_AdultLimbs(int* dListCount) {
    FlexSkeletonHeader* skel = AdultSkeleton();
    if (skel == nullptr)
        return nullptr;
    if (dListCount)
        *dListCount = skel->dListCount;
    return (void**)skel->sh.segment;
}

extern "C" void* Combo_MM_AdultWrapOverride(void* innerOverride) {
    sInner = (OverrideLimbDrawFlex)innerOverride;
    return (void*)&AdultOverride;
}

extern "C" void* Combo_MM_AdultDrawPrep(void* gfxIn, int mmEyeIndex, int mmMouthIndex) {
    Gfx* gfx = (Gfx*)gfxIn;
    const int eye = (mmEyeIndex >= 0 && mmEyeIndex < PLAYER_EYES_MAX) ? mmEyeIndex : 0;
    const int mouth = (mmMouthIndex >= 0 && mmMouthIndex < PLAYER_MOUTH_MAX) ? mmMouthIndex : 0;
    {
        // gSPSegment resolves a texture path at record time, against the active resource manager: OOT's here.
        Ship::ResourceManagerScope scope(Ship::CrossRMRegistry::Get("oot"));
        gSPSegment(gfx++, 0x08, (uintptr_t)kAdultEyes[eye]);
        gSPSegment(gfx++, 0x09, (uintptr_t)kAdultMouths[mouth]);
    }
    const Color_RGB8 c = AdultTunicColor();
    static int sLastColor = -1;
    const int packed = (c.r << 16) | (c.g << 8) | c.b;
    if (packed != sLastColor) {
        sLastColor = packed;
        fprintf(stderr, "[ComboAdultBody] tunic color %d,%d,%d\n", c.r, c.g, c.b);
    }
    gDPSetEnvColor(gfx++, c.r, c.g, c.b, 0);
    return gfx;
}

// Owner ask 2026-11-12 (transformations work anywhere): the form travels with Link. Combo form ids (soh ComboForms.h):
// 0 human, 1 Goron, 2 Zora, 3 Deku, 4 Fierce Deity. Applied when MM loads its save for the switch (OnSaveLoad, before
// Link spawns).
static int sPendingComboForm = -1;

extern "C" COMBO_EXPORT void MM_SetComboForm(int comboForm) {
    sPendingComboForm = comboForm;
}

extern "C" COMBO_EXPORT int MM_GetComboForm(void) {
    switch (gSaveContext.save.playerForm) { // combo form ids (soh ComboForms.h)
        case PLAYER_FORM_GORON:
            return 1;
        case PLAYER_FORM_ZORA:
            return 2;
        case PLAYER_FORM_DEKU:
            return 3;
        case PLAYER_FORM_FIERCE_DEITY:
            return 4;
        default:
            return 0;
    }
}

static void ComboApplyPendingForm() {
    if (sPendingComboForm < 0)
        return;
    static const u8 kMmForm[] = { PLAYER_FORM_HUMAN, PLAYER_FORM_GORON, PLAYER_FORM_ZORA, PLAYER_FORM_DEKU,
                                  PLAYER_FORM_FIERCE_DEITY };
    gSaveContext.save.playerForm =
        (sPendingComboForm >= 0 && sPendingComboForm < 5) ? kMmForm[sPendingComboForm] : PLAYER_FORM_HUMAN;
    sPendingComboForm = -1;
}

extern "C" void Combo_RequestCrossSwitch(int ootEntrance); // BenPort.cpp
extern "C" void Combo_MmOotClearMask(void);                // ComboOotItems.cpp

static void ComboApplyPendingMask(); // (below)
static void RegisterComboFormCarry() {
    // MM's save keeps its own mask (equippedMask) but never an OOT mask worn in Termina, so a loaded save starts
    // without one. A crossing puts the door's mask back on its first frame (ComboApplyPendingMask).
    COND_HOOK(OnSaveLoad, true, [](s16 fileNum) {
        ComboApplyPendingForm();
        Combo_MmOotClearMask();
    });
    COND_HOOK(OnGameStateUpdate, true, []() { ComboApplyPendingMask(); });
    // Debug (test harness only): COMBO_DEBUG_MM_RETURN=<oot entrance> — once Link has been in MM play ~2 s, cross
    // back to Hyrule at that entrance, like a crossed door does.
    if (const char* dbg = std::getenv("COMBO_DEBUG_MM_RETURN")) {
        static int sEntrance = (int)std::strtol(dbg, nullptr, 0);
        static int sAfter = std::strchr(dbg, ',') ? std::atoi(std::strchr(dbg, ',') + 1) : 60; // "<entr>,<frames>"
        COND_HOOK(OnGameStateUpdate, true, []() {
            static int sFrames = 0;
            static bool sDone = false;
            if (sDone || gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr)
                return;
            if (++sFrames < sAfter)
                return;
            sDone = true;
            fprintf(stderr, "[ComboShip] COMBO_DEBUG_MM_RETURN -> OOT entrance 0x%X (MM form %d)\n", sEntrance,
                    MM_GetComboForm());
            Combo_RequestCrossSwitch(sEntrance | 0x10000);
        });
    }
}
static RegisterShipInitFunc sComboFormCarryInit(RegisterComboFormCarry, {});

// ---- the worn mask across the doors (owner 2026-11-14: "if i don a mask in OOT i expect to be wearing it in MM") ----
// Token (same as OOT's SOH_GetComboMask): -1 none, an OOT item id for an OOT mask, 0x100 | MM item id for an MM mask.
// Transformation masks travel as the form.
extern "C" int Combo_MmOotWornMask(void);
extern "C" int Combo_MmOotWearMask(int ootItem);
static int sComboPendingMask = -2; // -2 = nothing pending

static int MmMaskForItem(int mmItem) {
    for (int i = 0; i < PLAYER_MASK_MAX - 1; i++) {
        if (Player_MaskIdToItemId(i) == mmItem) {
            return i + 1;
        }
    }
    return PLAYER_MASK_NONE;
}

extern "C" COMBO_EXPORT int MM_GetComboMask(void) {
    if (gSaveContext.save.playerForm != PLAYER_FORM_HUMAN) {
        return -1;
    }
    if (Combo_MmOotWornMask() >= 0) {
        return Combo_MmOotWornMask();
    }
    const int mask = gSaveContext.save.equippedMask;
    if ((mask == PLAYER_MASK_NONE) || (mask >= PLAYER_MASK_MAX)) {
        return -1;
    }
    const int item = Player_MaskIdToItemId(mask - 1);
    if ((item == ITEM_MASK_DEKU) || (item == ITEM_MASK_GORON) || (item == ITEM_MASK_ZORA) ||
        (item == ITEM_MASK_FIERCE_DEITY)) {
        return -1; // the form carries these
    }
    return 0x100 | item;
}

// Launcher push before MM boots / resumes: put the mask on the first frame Link is in play. MM keeps a worn mask
// whether or not it is on a button. OOT's Keaton / Bunny / Truth become Termina's own copy when MM owns it.
extern "C" COMBO_EXPORT void MM_SetComboMask(int token) {
    sComboPendingMask = token;
}

static void ComboApplyPendingMask() {
    if ((sComboPendingMask == -2) || (gPlayState == nullptr) || (GET_PLAYER(gPlayState) == nullptr)) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    const int token = sComboPendingMask;
    sComboPendingMask = -2;
    if (player->transformation != PLAYER_FORM_HUMAN) {
        return;
    }
    Combo_MmOotClearMask();
    int mask = PLAYER_MASK_NONE;
    if (token >= 0x100) {
        const int item = token & 0xFF;
        if (INV_CONTENT(item) == item) {
            mask = MmMaskForItem(item);
        }
    } else if ((token >= 0) && !Combo_MmOotWearMask(token)) {
        static const int sTwins[][2] = { { 0x24, ITEM_MASK_KEATON },
                                         { 0x27, ITEM_MASK_BUNNY },
                                         { 0x2B, ITEM_MASK_TRUTH } };
        for (const auto& t : sTwins) {
            if ((t[0] == token) && (INV_CONTENT(t[1]) == t[1])) {
                mask = MmMaskForItem(t[1]);
            }
        }
    }
    player->currentMask = mask;
    gSaveContext.save.equippedMask = mask;
    fprintf(stderr, "[ComboShip] mask on arrival in Termina: token 0x%X -> MM mask %d, OOT mask %d\n", token, mask,
            Combo_MmOotWornMask());
}

// Launcher push (before every MM boot/resume): OOT's Link is adult, and his tunic.
extern "C" COMBO_EXPORT void MM_SetComboLinkState(int adult, int tunic) {
    sLinkAdult = adult != 0;
    sTunic = tunic;
}

// ComboShip: adult Link in Termina holding MM's Great Fairy's Sword: MM's own blade in OOT's adult closed fist (the
// hand limb's matrix is current; called from Player_PostLimbDrawGameplay's left-hand branch).
extern "C" void Combo_MM_AdultDrawHeld(PlayState* play) {
    if (!sAdultGfsPending) {
        return;
    }
    sAdultGfsPending = false;
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Translate(COMBO_GFS_ADULT_GRIP_X, COMBO_GFS_ADULT_GRIP_Y, COMBO_GFS_ADULT_GRIP_Z, MTXMODE_APPLY);
    Matrix_Scale(COMBO_GFS_ADULT_SCALE, COMBO_GFS_ADULT_SCALE, COMBO_GFS_ADULT_SCALE, MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)"__OTR__objects/object_link_child/gLinkHumanGreatFairysSwordDL");
    {
        // The blade's DL sets its own environment colour, which the limbs drawn after the hand (the torso, the right
        // arm, the sheath) then inherited: part of the tunic came out black until the sword was put away (owner
        // 2026-11-14). Put the adult tunic colour back, as Combo_MM_AdultDrawPrep set it.
        const Color_RGB8 c = AdultTunicColor();
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, c.r, c.g, c.b, 0);
    }
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}
