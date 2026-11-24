// soh/soh/Enhancements/randomizer/ComboForms.cpp — ComboShip: Majora's Mask transformations in Hyrule (owner ask
// 2026-11-12: "items and transformations work anywhere between games"). Combo-owned file (soh GLOB_RECURSE).
//
// The mirror of mm/2s2h/Rando/MiscBehavior/ComboAdultBody.cpp. MM's transformation forms share Link's whole rig with
// OOT (the same 22-limb player skeleton, the same animation format), and MM's Goron walks, runs, jumps and climbs with
// the SAME human `link_*` animations OOT already plays. So a form in Hyrule = OOT's player and movement with:
//   - the form's MM skeleton drawn instead of Link's (loaded from MM's resident ResourceManager), every limb model
//     submitted as an "__OTR__@mm:" routed display list; hand picks remapped to the form's own hands;
//   - the form's eyes on segment 8; no sheath, no shield, no gauntlets/boots;
//   - the form's body dimensions (z_player.c: Combo_FormAgeProperties);
//   - B (sword) off and C limited to masks and the ocarina, as in Termina, until each form's moves are ported.
// Logic credits nothing for a form until its moves are real (full-coop-sync.md §B4 Tier C rule).

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <fstream>
#include <unordered_map>

#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceManagerScope.h>

#include "ComboForms.h"
#include "ComboPause.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
}

extern "C" s32 Player_OverrideLimbDrawPause(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                            void* arg); // z_player_lib.c

namespace {

int sForm = COMBO_FORM_HUMAN;

struct FormDef {
    const char* skel;
    const char* obj;    // "__OTR__objects/<object>/"
    const char* prefix; // limb/hand model name prefix in that object
};
const FormDef kForms[COMBO_FORM_MAX] = {
    { nullptr, nullptr, nullptr },                                                                            // human
    { "__OTR__objects/object_link_goron/gLinkGoronSkel", "__OTR__objects/object_link_goron/", "gLinkGoron" }, // goron
    { "__OTR__objects/object_link_zora/gLinkZoraSkel", "__OTR__objects/object_link_zora/", "gLinkZora" },     // zora
    { "__OTR__objects/object_link_nuts/gLinkDekuSkel", "__OTR__objects/object_link_nuts/", "gLinkDeku" },     // deku
    { "__OTR__objects/object_link_boy/gLinkFierceDeitySkel", "__OTR__objects/object_link_boy/",
      "gLinkFierceDeity" }, // Fierce Deity (MM's object_link_boy, routed to MM's archive)
};

struct SkelCache {
    std::shared_ptr<Ship::IResource> res;
    FlexSkeletonHeader* skel = nullptr;
    bool tried = false;
};
SkelCache sSkels[COMBO_FORM_MAX];

void ClearCaches() {
    for (auto& s : sSkels)
        s = SkelCache{};
}

FlexSkeletonHeader* FormSkeleton(int form) {
    static bool sHooked = (Ship::CrossRMRegistry::RegisterTeardownListener(&ClearCaches), true);
    (void)sHooked;
    if (form <= COMBO_FORM_HUMAN || form >= COMBO_FORM_MAX || kForms[form].skel == nullptr)
        return nullptr;
    SkelCache& c = sSkels[form];
    if (c.tried)
        return c.skel;
    c.tried = true;
    auto rm = Ship::CrossRMRegistry::Get("mm");
    if (rm == nullptr)
        return nullptr;
    try {
        Ship::ResourceManagerScope scope(rm); // the Skeleton factory nested-loads limbs through the active RM
        c.res = rm->LoadResource(kForms[form].skel);
    } catch (...) { c.res.reset(); }
    FlexSkeletonHeader* skel = c.res ? (FlexSkeletonHeader*)c.res->GetRawPointer() : nullptr;
    if (skel == nullptr || skel->sh.limbCount + 1 != PLAYER_LIMB_MAX) {
        fprintf(stderr, "[ComboForms] MM form skeleton %s unavailable\n", kForms[form].skel);
        c.res.reset();
        return nullptr;
    }
    c.skel = skel;
    return skel;
}

// "__OTR__objects/<obj>/X" -> stable "__OTR__@mm:objects/<obj>/X".
const char* Routed(const std::string& otrPath) {
    static std::unordered_map<std::string, std::string> sInterned;
    auto it = sInterned.find(otrPath);
    if (it == sInterned.end())
        it = sInterned.emplace(otrPath, "__OTR__@mm:" + otrPath.substr(7)).first;
    return it->second.c_str();
}

const char* FormDefault(int form, s32 limbIndex) {
    FlexSkeletonHeader* skel = FormSkeleton(form);
    if (skel == nullptr || limbIndex < 1 || limbIndex >= PLAYER_LIMB_MAX)
        return nullptr;
    LodLimb* limb = (LodLimb*)((void**)skel->sh.segment)[limbIndex - 1];
    const char* dl = limb ? (const char*)limb->dLists[0] : nullptr;
    return (dl != nullptr && strncmp(dl, "__OTR__", 7) == 0) ? Routed(dl) : nullptr;
}

OverrideLimbDrawOpa sInner = nullptr;

// MM: the guitar is out from frame 6 of pz_gakkistart and through pz_gakkiplay.
bool ZoraGuitarOut(Player* player) {
    const char* anim = (const char*)player->skelAnime.animation;
    if (anim == nullptr) {
        return false;
    }
    return (strcmp(anim, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_gakkiplay") == 0) ||
           ((strcmp(anim, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_gakkistart") == 0) &&
            (player->skelAnime.curFrame >= 6.0f));
}

s32 FormOverride(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg) {
    const s32 ret = sInner != nullptr ? sInner(play, limbIndex, dList, pos, rot, arg) : 0;
    if (*dList == nullptr)
        return ret;
    // The gameplay overrides get the Player; the pause screen's (Player_OverrideLimbDrawPause) gets a u8[2] of sword
    // and shield on the stack. Read as a Player, the Zora's guitar test followed a garbage animation pointer into
    // strcmp: pausing as Zora crashed wherever that stack held a bad pointer (owner 2026-11-14: Gerudo Valley, the
    // hideout). The pause Link has no animation or held items: his hands are open.
    Player* const data = (sInner == (OverrideLimbDrawOpa)Player_OverrideLimbDrawPause) ? nullptr : (Player*)arg;
    const std::string obj = kForms[sForm].obj;
    const char* chosen = (const char*)*dList;
    const bool isPath = strncmp(chosen, "__OTR__", 7) == 0;
    if (limbIndex == PLAYER_LIMB_SHEATH) {
        *dList = nullptr; // forms carry no sheath or shield
    } else if (limbIndex == PLAYER_LIMB_L_HAND) {
        // OOT's override (Player_OverrideLimbDrawGameplayDefault) hands back a LOADED Gfx*, not the resource path, so
        // the path test alone never matched there: read what Link holds from his hand model type as well.
        const u8 lhType = (data != nullptr) ? ((Player*)data)->leftHandType : PLAYER_MODELTYPE_LH_OPEN;
        const bool closed = (isPath && strstr(chosen, "Closed") != nullptr) || (lhType == PLAYER_MODELTYPE_LH_CLOSED);
        const bool bottle = (isPath && strstr(chosen, "Bottle") != nullptr) || (lhType == PLAYER_MODELTYPE_LH_BOTTLE);
        const bool sword = (isPath && (strstr(chosen, "Sword") != nullptr || strstr(chosen, "Bgs") != nullptr)) ||
                           (lhType == PLAYER_MODELTYPE_LH_SWORD) || (lhType == PLAYER_MODELTYPE_LH_SWORD_2) ||
                           (lhType == PLAYER_MODELTYPE_LH_BGS);
        const std::string pre = obj + kForms[sForm].prefix;
        if (sForm == COMBO_FORM_DEKU) {
            *dList = (Gfx*)Routed(pre + "LeftHandDL"); // the Deku has one hand model
        } else if ((sForm == COMBO_FORM_ZORA) && (lhType == PLAYER_MODELTYPE_LH_HAMMER)) {
            // The owner's hammer Zora: OOT's hammer model is adult Link's hand holding it, as for the hookshot.
            *dList = (Gfx*)"__OTR__objects/object_link_boy/gLinkAdultLeftHandHoldingHammerNearDL";
        } else if ((sForm == COMBO_FORM_ZORA) && (data != nullptr) && ZoraGuitarOut((Player*)data)) {
            // MM Player_OverrideLimbDrawGameplayDefault: the Zora's left hand holds his guitar while he plays it.
            // The limb draws the guitar alone, sized by MM's keys (func_80125CE0: coming out D_801C0538, playing
            // D_801C0560 -- with the two stop keys 2S2H adds for the table's overrun); the hand that holds it is drawn
            // after the limb (z_player_lib.c, MM func_80128640).
            struct Key {
                s16 frame;
                s16 x, y, z;
            };
            static const Key kOut[] = {
                { 0, 100, 100, 100 }, { 5, 100, 100, 100 }, { 6, 0, 0, 0 }, { 8, 100, 100, 100 }, { 14, 100, 100, 100 }
            };
            static const Key kPlay[] = { { 0, 100, 100, 100 }, { 2, 95, 95, 100 },   { 3, 105, 105, 100 },
                                         { 5, 102, 102, 102 }, { 0, 100, 100, 100 }, { 9, 100, 100, 100 } };
            Player* p = (Player*)data;
            const char* anim = (const char*)p->skelAnime.animation;
            const Key* k =
                (strcmp(anim, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_gakkistart") == 0) ? kOut : kPlay;
            const f32 cur = p->skelAnime.curFrame;
            const s32 curI = (s32)cur;
            s16 next;
            do { // MM func_80124618
                next = k[1].frame;
                k++;
            } while (next < curI);
            const f32 start = k[-1].frame;
            const f32 t = (next != start) ? (cur - start) / (next - start) : 0.0f;
            Matrix_TranslateRotateZYX(pos, rot);
            *pos = { 0.0f, 0.0f, 0.0f };
            *rot = { 0, 0, 0 };
            Matrix_Scale((k[-1].x + (k->x - k[-1].x) * t) * 0.01f, (k[-1].y + (k->y - k[-1].y) * t) * 0.01f,
                         (k[-1].z + (k->z - k[-1].z) * t) * 0.01f, MTXMODE_APPLY);
            *dList = (Gfx*)Routed(obj + "object_link_zora_DL_00E2A0");
        } else if (sForm == COMBO_FORM_FIERCE_DEITY) {
            *dList = (Gfx*)Routed(pre + (sword    ? "LeftHandHoldingSwordDL"
                                         : bottle ? "LeftHandHoldBottleDL"
                                                  : "LeftHandDL"));
        } else {
            *dList = (Gfx*)Routed(pre + (bottle   ? "LeftHandHoldBottleDL"
                                         : closed ? "LeftHandClosedDL"
                                                  : "LeftHandOpenDL"));
        }
    } else if (limbIndex == PLAYER_LIMB_R_HAND) {
        const u8 rhType = (data != nullptr) ? ((Player*)data)->rightHandType : PLAYER_MODELTYPE_RH_OPEN;
        const bool closed = (isPath && strstr(chosen, "Closed") != nullptr) || (rhType == PLAYER_MODELTYPE_RH_CLOSED);
        if ((sForm == COMBO_FORM_ZORA) && (rhType == PLAYER_MODELTYPE_RH_HOOKSHOT)) {
            // The owner's hookshot Zora: OOT's only always-present hookshot model is adult Link's hand holding it
            // (child Link's RH_HOOKSHOT hand is empty), so that hand carries it.
            *dList = (Gfx*)"__OTR__objects/object_link_boy/gLinkAdultRightHandHoldingHookshotNearDL";
        } else if ((sForm == COMBO_FORM_DEKU) || (sForm == COMBO_FORM_FIERCE_DEITY)) {
            *dList = (Gfx*)Routed(obj + kForms[sForm].prefix + "RightHandDL");
        } else {
            *dList = (Gfx*)Routed(obj + kForms[sForm].prefix + (closed ? "RightHandClosedDL" : "RightHandOpenDL"));
        }
    } else {
        *dList = (Gfx*)FormDefault(sForm, limbIndex);
    }
    return ret;
}

} // namespace

extern "C" int Combo_ZoraGuitarOut(void* player) {
    return (sForm == COMBO_FORM_ZORA) && (player != nullptr) && ZoraGuitarOut((Player*)player);
}

extern "C" int Combo_OotForm(void) {
    return sForm;
}

extern "C" void Combo_OotSetForm(int form) {
    sForm =
        (form > COMBO_FORM_HUMAN && form < COMBO_FORM_MAX && FormSkeleton(form) != nullptr) ? form : COMBO_FORM_HUMAN;
}

extern "C" int Combo_OotFormDrawActive(void* actor) {
    Actor* a = (Actor*)actor;
    return a != nullptr && a->id == ACTOR_PLAYER && sForm != COMBO_FORM_HUMAN && FormSkeleton(sForm) != nullptr;
}

extern "C" void** Combo_OotFormLimbs(s32* dListCount) {
    FlexSkeletonHeader* skel = FormSkeleton(sForm);
    if (skel == nullptr)
        return nullptr;
    if (dListCount)
        *dListCount = skel->dListCount;
    return (void**)skel->sh.segment;
}

extern "C" void* Combo_OotFormWrapOverride(void* inner) {
    sInner = (OverrideLimbDrawOpa)inner;
    return (void*)&FormOverride;
}

// OOT eye order: open, half, closed, left, right, wide, down, wincing. MM's: open, half, closed, right, left, up, down,
// wincing. The Goron has open/half/closed/surprised only (MM maps directions to open and wincing to surprised).
extern "C" void* Combo_OotFormDrawPrep(void* gfxIn, s32 eyeIndex, s32 mouthIndex) {
    Gfx* gfx = (Gfx*)gfxIn;
    Ship::ResourceManagerScope scope(Ship::CrossRMRegistry::Get("mm")); // segment textures resolve at record time
    if (sForm == COMBO_FORM_GORON) {
        static const char* const kGoronEyes[] = {
            "__OTR__objects/object_link_goron/gLinkGoronEyesOpenTex",
            "__OTR__objects/object_link_goron/gLinkGoronEyesHalfTex",
            "__OTR__objects/object_link_goron/gLinkGoronEyesClosedTex",
            "__OTR__objects/object_link_goron/gLinkGoronEyesSurprisedTex",
        };
        int e = 0;
        if (eyeIndex == 1)
            e = 1;
        else if (eyeIndex == 2)
            e = 2;
        else if (eyeIndex == 5 || eyeIndex == 7)
            e = 3;
        gSPSegment(gfx++, 0x08, (uintptr_t)kGoronEyes[e]);
        gSPSegment(gfx++, 0x09, (uintptr_t)kGoronEyes[0]); // no mouth texture: keep the segment valid
    } else if (sForm == COMBO_FORM_ZORA) {
        static const char* const kZoraEyes[8] = {
            "__OTR__objects/object_link_zora/gLinkZoraEyesOpenTex",
            "__OTR__objects/object_link_zora/gLinkZoraEyesHalfTex",
            "__OTR__objects/object_link_zora/gLinkZoraEyesClosedTex",
            "__OTR__objects/object_link_zora/gLinkZoraEyesLeftTex",
            "__OTR__objects/object_link_zora/gLinkZoraEyesRightTex",
            "__OTR__objects/object_link_zora/gLinkZoraEyesUpTex",
            "__OTR__objects/object_link_zora/gLinkZoraEyesDownTex",
            "__OTR__objects/object_link_zora/gLinkZoraEyesWincingTex",
        }; // indexed by OOT's eye order (OOT "left" = MM "left" texture, etc.)
        static const char* const kZoraMouths[4] = {
            "__OTR__objects/object_link_zora/gLinkZoraMouthClosedTex",
            "__OTR__objects/object_link_zora/gLinkZoraMouthHalfTex",
            "__OTR__objects/object_link_zora/gLinkZoraMouthOpenTex",
            "__OTR__objects/object_link_zora/gLinkZoraMouthSmileTex",
        };
        gSPSegment(gfx++, 0x08, (uintptr_t)kZoraEyes[(eyeIndex >= 0 && eyeIndex < 8) ? eyeIndex : 0]);
        gSPSegment(gfx++, 0x09, (uintptr_t)kZoraMouths[(mouthIndex >= 0 && mouthIndex < 4) ? mouthIndex : 0]);
    } else if (sForm == COMBO_FORM_FIERCE_DEITY) {
        gSPSegment(gfx++, 0x08, (uintptr_t) "__OTR__objects/object_link_boy/gLinkFierceDeityEyesTex");
        gSPSegment(gfx++, 0x09, (uintptr_t) "__OTR__objects/object_link_boy/gLinkFierceDeityMouthTex");
    } else {
        // The Deku's head reads neither segment (MM points them at whatever); keep them on a valid texture.
        gSPSegment(gfx++, 0x08, (uintptr_t) "__OTR__objects/object_link_goron/gLinkGoronEyesOpenTex");
        gSPSegment(gfx++, 0x09, (uintptr_t) "__OTR__objects/object_link_goron/gLinkGoronEyesOpenTex");
    }
    return gfx;
}

// Only masks (to change form back) and the ocarina while transformed — Termina's rule — until moves are ported.
extern "C" int Combo_OotFormBlocksItem(s32 item) {
    if (sForm == COMBO_FORM_HUMAN || item == ITEM_NONE || item == ITEM_NONE_FE)
        return 0;
    if ((item != ITEM_NONE) && (item == Combo_FormHeldBItem()))
        return 0; // held B: the Zora's fins fly as OOT's boomerang, the Deku's bubble as a slingshot seed
    if ((sForm == COMBO_FORM_FIERCE_DEITY) && (item == ITEM_SWORD_BGS))
        return 0; // Fierce Deity's Double Helix (Player_GetItemOnButton gives it as B)
    if (item == ITEM_OCARINA_FAIRY || item == ITEM_OCARINA_TIME)
        return 0;
    // Owner ruling 2026-11-14: the Zora keeps the Hookshot / Longshot. A Zora who swims into the Water Temple (his
    // walk on the bottom stands in for the Iron Boots) would otherwise be stuck at its hookshot targets. OOT's own
    // water rule still applies: in water only on the bottom (sunk), as for iron-booted Link.
    if ((sForm == COMBO_FORM_ZORA) && ((item == ITEM_HOOKSHOT) || (item == ITEM_LONGSHOT)))
        return 0;
    // Owner 2026-11-14: "let zora link hold the hammer too" (OOT's age rule still applies: adult only).
    if ((sForm == COMBO_FORM_ZORA) && (item == ITEM_HAMMER))
        return 0;
    if (item >= ITEM_MASK_KEATON && item <= ITEM_MASK_TRUTH)
        return 0;
    return 1;
}

// Form moves as items: the Zora throws its fins as a boomerang (hold B, as in MM).
extern "C" int Combo_ZoraFinsActive(void) {
    return (sForm == COMBO_FORM_ZORA) && CVarGetInteger("gCombo.Forms.ItemAnalogs", 1);
}

// Form moves as items, held B (MM: tap B = the jab/spin, hold B = the ranged move): the item OOT's own code runs it
// as. Zora fins = boomerang, Deku bubble = slingshot (infinite, no seeds). ITEM_NONE = no held-B move.
extern "C" int Combo_FormHeldBItem(void) {
    if (!CVarGetInteger("gCombo.Forms.ItemAnalogs", 1))
        return ITEM_NONE;
    if (sForm == COMBO_FORM_ZORA)
        return ITEM_BOOMERANG;
    if (sForm == COMBO_FORM_DEKU)
        return ITEM_SLINGSHOT;
    return ITEM_NONE;
}

extern "C" int Combo_DekuBubbleActive(void) {
    return (sForm == COMBO_FORM_DEKU) && CVarGetInteger("gCombo.Forms.ItemAnalogs", 1);
}

// An MM display list by its MM resource path ("objects/gameplay_keep/..."), routed to MM's ResourceManager.
extern "C" const void* Combo_MmDL(const char* mmPath) {
    return Routed(std::string("__OTR__") + mmPath);
}

// Forms in the loaded save: the "Transformations in Hyrule" option for a randomizer save, the separate (off by
// default) "Transformations in vanilla OoT" option for a vanilla one.
extern "C" int Combo_FormsAllowed(void) {
    return IS_RANDO ? CVarGetInteger("gCombo.Forms.InHyrule", 1) : CVarGetInteger("gCombo.Forms.InVanillaOot", 0);
}

// A form the slot loaded before this one left behind (or a worn MM mask) has no place in a vanilla save that doesn't
// allow forms. Runs on every save load (ShipInit "IS_RANDO").
static void ResetFormOfPreviousSlot() {
    if (!Combo_FormsAllowed()) {
        sForm = COMBO_FORM_HUMAN;
        Combo_PauseClearMmMask();
    }
}
static RegisterShipInitFunc resetFormInitFunc(ResetFormOfPreviousSlot, { "IS_RANDO" });

// The transformation masks. OOT's own Goron Mask IS MM's Goron Mask (Shared Items), so wearing it makes the Goron.
extern "C" int Combo_OotFormForMask(s32 itemAction) {
    if (!Combo_FormsAllowed())
        return COMBO_FORM_HUMAN;
    if (itemAction == PLAYER_IA_MASK_GORON)
        return COMBO_FORM_GORON;
    if (itemAction == PLAYER_IA_MASK_ZORA)
        return COMBO_FORM_ZORA;
    return COMBO_FORM_HUMAN;
}

// Debug harness log for C seams (stderr; the C side has no stdio here).
// Debug traces (COMBO_DEBUG_* env vars): to stderr and into the game's own log (ComboShip.log), so a player can run
// with a trace switched on and send the log.
extern "C" void Combo_DebugLog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stderr);
    size_t n = strlen(buf);
    while ((n > 0) && (buf[n - 1] == '\n')) {
        buf[--n] = '\0';
    }
    SPDLOG_INFO("{}", buf);
}
// Test switches that CHANGE behaviour: env var at launch only, never combo-debug.txt.
extern "C" int Combo_HarnessEnv(const char* name) {
    return std::getenv(name) != nullptr;
}

// A trace is on when its env var is set, or when its name is a line of combo-debug.txt in the data folder
// (ComboShip-data next to the AppImage): players who launch from a file manager can switch traces on too.
// Combo_DebugEnv is for LOG-ONLY traces; anything that changes gameplay uses Combo_HarnessEnv (2026-11-13: a
// behaviour switch reachable from this file pressed B on its own in OOT).
extern "C" int Combo_DebugEnv(const char* name) {
    static std::unordered_map<std::string, bool> sFromFile;
    static bool sRead = false;
    if (std::getenv(name) != nullptr)
        return 1;
    if (!sRead) {
        sRead = true;
        try {
            std::ifstream in(Ship::Context::GetPathRelativeToAppDirectory("combo-debug.txt"));
            std::string line;
            while (std::getline(in, line)) {
                line.erase(0, line.find_first_not_of(" \t\r"));
                line.erase(line.find_last_not_of(" \t\r") + 1);
                if (!line.empty() && line[0] != '#')
                    sFromFile[line] = true;
            }
            if (!sFromFile.empty())
                SPDLOG_INFO("[ComboShip] combo-debug.txt: {} trace(s) on", sFromFile.size());
        } catch (...) {}
    }
    return sFromFile.count(name) != 0;
}

// The curled Goron (MM's gLinkGoronCurledDL, spikes from gLinkGoronRollingSpikesAndEffectDL), drawn in place of the
// skeleton while rolling. The actor's model matrix is current; the ball sits a body-radius above the feet and spins.
extern "C" {
extern s16 gComboGoronBallSpin;
extern s16 gComboGoronBallTilt;
extern s16 gComboGoronBallWobble;
extern s16 gComboGoronBallWobbleYaw;
extern f32 gComboGoronBallStretch;
extern f32 gComboGoronBallBlue;
extern s32 gComboGoronSpikes;
extern s32 gComboGoronBallCharge;
}

extern "C" void Combo_OotFormDrawBall(PlayState* play, s16 shapeYaw) {
    // MM Player_Draw's ball (mm z_player.c ~13401): lifted by its own height, wobbled about the skid axis, banked,
    // squashed/stretched, then rolled; env colour white -> blue with the pound (D_8085D580 -> D_8085D584 by
    // unk_B10[0]); the spike shell object_link_goron_DL_00C540 while the spikes are out. Drawn in the player's model
    // space (MM's is world space: the yaw terms are made relative to the ball's heading). (Not
    // gLinkGoronRollingSpikesAndEffectDL: it reads animated-material segments MM's AnimatedMat sets.)
    static const char* kBall = "__OTR__@mm:objects/object_link_goron/gLinkGoronCurledDL";
    static const char* kSpikes = "__OTR__@mm:objects/object_link_goron/object_link_goron_DL_00C540";
    // MM draws the ball after Scene_SetRenderModeXlu(play, 0, 1): segment 0x0C = a run of end-markers, because the
    // ball's display list calls into 0x0C at small offsets. OOT's player has 0x0C on its 2-command cull list, so those
    // offset calls ran past it into garbage. Give it MM's "render mode: none" list.
    static Gfx sRenderModeNone[] = {
        gsSPEndDisplayList(),
        gsSPEndDisplayList(),
        gsSPEndDisplayList(),
        gsSPEndDisplayList(),
    };
    const f32 tall = gComboGoronBallStretch + 1.0f;
    const f32 wide = 1.0f - (gComboGoronBallStretch * 0.5f);
    const f32 blue = std::clamp(gComboGoronBallBlue, 0.0f, 1.0f);
    const u8 r = (u8)(255 + (80 - 255) * blue), g = (u8)(255 + (80 - 255) * blue), b = (u8)(255 + (200 - 255) * blue);
    const f32 kBinang = (f32)(M_PI / 0x8000);

    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)sRenderModeNone);
    Matrix_Push();
    Matrix_Translate(0.0f, 1200.0f * tall, 0.0f, MTXMODE_APPLY);
    if (gComboGoronBallWobble != 0) {
        const s16 rel = (s16)(gComboGoronBallWobbleYaw - shapeYaw);
        Matrix_RotateY(rel * kBinang, MTXMODE_APPLY);
        Matrix_RotateX(gComboGoronBallWobble * kBinang, MTXMODE_APPLY);
        Matrix_RotateY(-rel * kBinang, MTXMODE_APPLY);
    }
    Matrix_RotateZ(gComboGoronBallTilt * kBinang, MTXMODE_APPLY);
    Matrix_Scale(1.15f * wide, 1.15f * tall, 1.15f * std::max(tall, wide), MTXMODE_APPLY);
    Matrix_RotateX(gComboGoronBallSpin * kBinang, MTXMODE_APPLY);
    gDPSetEnvColor(POLY_OPA_DISP++, r, g, b, 255);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)kBall);
    if (gComboGoronSpikes != 0) {
        if (gComboGoronSpikes < 3) {
            Combo_GoronBallExtras(play, gComboGoronSpikes, gComboGoronBallCharge, 0); // the spikes pop out
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        }
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)kSpikes);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    Combo_GoronBallExtras(play, gComboGoronSpikes, gComboGoronBallCharge, 1); // afterimages + charge aura
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------------------- //
// MM resources by path, raw (loaded through MM's ResourceManager; kept for the session, dropped on RM teardown).
namespace {
std::unordered_map<std::string, std::shared_ptr<Ship::IResource>> sMmRaw;

void* MmRaw(const char* otrPath) {
    static bool sHooked = (Ship::CrossRMRegistry::RegisterTeardownListener([]() { sMmRaw.clear(); }), true);
    (void)sHooked;
    auto it = sMmRaw.find(otrPath);
    if (it == sMmRaw.end()) {
        std::shared_ptr<Ship::IResource> res;
        auto rm = Ship::CrossRMRegistry::Get("mm");
        if (rm != nullptr) {
            try {
                Ship::ResourceManagerScope scope(rm);
                res = rm->LoadResource(otrPath);
            } catch (...) { res.reset(); }
        }
        if (res == nullptr) {
            fprintf(stderr, "[ComboForms] MM resource %s unavailable\n", otrPath);
        }
        it = sMmRaw.emplace(otrPath, res).first;
    }
    return it->second ? it->second->GetRawPointer() : nullptr;
}

// MM's AnimatedMaterial (z64scene.h), as 2ship's TextureAnimation resource lays it out.
struct MmPrimColor {
    u8 r, g, b, a, lodFrac;
};
struct MmEnvColor {
    u8 r, g, b, a;
};
struct MmColorParams {
    u16 keyFrameLength;
    u16 keyFrameCount;
    MmPrimColor* primColors;
    MmEnvColor* envColors;
    u16* keyFrames;
};
struct MmScrollParams {
    s8 xStep;
    s8 yStep;
    u8 width;
    u8 height;
};
struct MmCycleParams {
    u16 keyFrameLength;
    void** textureList;
    u8* textureIndexList;
};
struct MmAnimatedMaterial {
    s8 segment;
    s16 type;
    void* params;
};

s32 MatLerp(s32 min, s32 max, f32 norm) {
    return (s32)((max - min) * norm) + min;
}

} // namespace

// (extern "C": OPEN_DISPS declares frame-interpolation hooks at block scope, which need C linkage)
extern "C" {
static void MatSegment(PlayState* play, u32 flags, s32 segment, const void* gfx) {
    OPEN_DISPS(play->state.gfxCtx);
    if (flags & 1) {
        gSPSegment(POLY_OPA_DISP++, segment, (uintptr_t)gfx);
    }
    if (flags & 2) {
        gSPSegment(POLY_XLU_DISP++, segment, (uintptr_t)gfx);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void MatColor(PlayState* play, u32 flags, s32 segment, const MmPrimColor& prim, const MmEnvColor* env,
                     f32 alpha) {
    Gfx* gfx = (Gfx*)Graph_Alloc(play->state.gfxCtx, 3 * sizeof(Gfx));
    MatSegment(play, flags, segment, gfx);
    gDPSetPrimColor(gfx++, 0, prim.lodFrac, prim.r, prim.g, prim.b, (u8)(prim.a * alpha));
    if (env != nullptr) {
        gDPSetEnvColor(gfx++, env->r, env->g, env->b, env->a);
    }
    gSPEndDisplayList(gfx++);
}
}

// MM AnimatedMat_DrawMain (mm z_scene_proc.c) over an MM TextureAnimation resource: OOT has no animated materials, so
// the six MM material types are ported here (scroll, two-layer scroll, colour, colour lerp, colour Lagrange - drawn
// as lerp, texture cycle). flags: 1 = OPA, 2 = XLU. MM's own scene textures cycle through MM paths (routed).
extern "C" void Combo_MmAnimatedMatStep(PlayState* play, const char* mmOtrPath, f32 alphaRatio, u32 flags, u32 step);
extern "C" void Combo_MmAnimatedMat(PlayState* play, const char* mmOtrPath, f32 alphaRatio, u32 flags) {
    Combo_MmAnimatedMatStep(play, mmOtrPath, alphaRatio, flags, play->gameplayFrames);
}

// MM AnimatedMat_DrawStep: the same, at a caller-chosen step (MM's EnTest crater drives it from its animation frame).
extern "C" void Combo_MmAnimatedMatStep(PlayState* play, const char* mmOtrPath, f32 alphaRatio, u32 flags, u32 step) {
    MmAnimatedMaterial* mat = (MmAnimatedMaterial*)MmRaw(mmOtrPath);
    s32 segment;

    if ((mat == nullptr) || (mat->segment == 0)) {
        return;
    }
    do {
        segment = mat->segment;
        const s32 seg = ABS(segment) + 7;
        switch (mat->type) {
            case 0: {
                MmScrollParams* p = (MmScrollParams*)mat->params;
                MatSegment(play, flags, seg,
                           Gfx_TexScrollEx(play->state.gfxCtx, p->xStep * step, -(s32)(p->yStep * step), p->width,
                                           p->height, p->xStep, -(s32)(p->yStep)));
                break;
            }
            case 1: {
                MmScrollParams* p = (MmScrollParams*)mat->params;
                MatSegment(play, flags, seg,
                           Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, p[0].xStep * step, -(s32)(p[0].yStep * step),
                                              p[0].width, p[0].height, 1, p[1].xStep * step, -(s32)(p[1].yStep * step),
                                              p[1].width, p[1].height, p[0].xStep, -(s32)(p[0].yStep), p[1].xStep,
                                              -(s32)(p[1].yStep)));
                break;
            }
            case 2: {
                MmColorParams* p = (MmColorParams*)mat->params;
                const s32 frame = step % p->keyFrameLength;
                MatColor(play, flags, seg, p->primColors[frame], p->envColors ? &p->envColors[frame] : nullptr,
                         alphaRatio);
                break;
            }
            case 3:
            case 4: {
                // (type 4 interpolates with a Lagrange polynomial through the keys; drawn here with type 3's lerp)
                MmColorParams* p = (MmColorParams*)mat->params;
                const s32 frame = step % p->keyFrameLength;
                u16* keys = p->keyFrames;
                s32 i = 1;
                while ((p->keyFrameCount > i) && (frame >= keys[i])) {
                    i++;
                }
                if (i >= p->keyFrameCount) {
                    i = p->keyFrameCount - 1;
                }
                const s32 start = keys[i - 1];
                const s32 len = keys[i] - start;
                const f32 norm = (len != 0) ? (f32)(frame - start) / (f32)len : 0.0f;
                const MmPrimColor& lo = p->primColors[i - 1];
                const MmPrimColor& hi = p->primColors[i];
                MmPrimColor prim = { (u8)MatLerp(lo.r, hi.r, norm), (u8)MatLerp(lo.g, hi.g, norm),
                                     (u8)MatLerp(lo.b, hi.b, norm), (u8)MatLerp(lo.a, hi.a, norm),
                                     (u8)MatLerp(lo.lodFrac, hi.lodFrac, norm) };
                MmEnvColor env = {};
                if (p->envColors != nullptr) {
                    const MmEnvColor& elo = p->envColors[i - 1];
                    const MmEnvColor& ehi = p->envColors[i];
                    env = { (u8)MatLerp(elo.r, ehi.r, norm), (u8)MatLerp(elo.g, ehi.g, norm),
                            (u8)MatLerp(elo.b, ehi.b, norm), (u8)MatLerp(elo.a, ehi.a, norm) };
                }
                MatColor(play, flags, seg, prim, p->envColors ? &env : nullptr, alphaRatio);
                break;
            }
            case 5: {
                MmCycleParams* p = (MmCycleParams*)mat->params;
                const s32 frame = step % p->keyFrameLength;
                const char* tex = (const char*)p->textureList[p->textureIndexList[frame]];
                if ((tex != nullptr) && (strncmp(tex, "__OTR__", 7) == 0)) {
                    MatSegment(play, flags, seg, Routed(tex));
                }
                break;
            }
            default:
                break;
        }
        mat++;
    } while (segment >= 0);
}

// ---------------------------------------------------------------------------------------------------------------- //
// The Goron's guard: MM swaps the whole body for gLinkGoronShieldingSkel playing pg_gurdmotion
// (gLinkGoronShieldingAnim) while Player_Action_18 runs (mm z_player.c 11241, 15353, 13481).
namespace {
SkelAnime sGuardSkel;
Vec3s sGuardJoints[48];
Vec3s sGuardMorph[48];
bool sGuardReady = false;
PlayState* sGuardPlay = nullptr;

s32 GuardOverride(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* data) {
    if ((*dList != nullptr) && (strncmp((const char*)*dList, "__OTR__", 7) == 0)) {
        *dList = (Gfx*)Routed((const char*)*dList);
    }
    return 0;
}
} // namespace

extern "C" void Combo_GoronGuardStart(PlayState* play) {
    FlexSkeletonHeader* skel = (FlexSkeletonHeader*)MmRaw("__OTR__objects/object_link_goron/gLinkGoronShieldingSkel");
    sGuardReady = false;
    if ((skel == nullptr) || (skel->sh.limbCount + 1 > (s32)ARRAY_COUNT(sGuardJoints))) {
        return;
    }
    SkelAnime_InitFlex(play, &sGuardSkel, skel,
                       (AnimationHeader*)Routed("__OTR__objects/object_link_goron/gLinkGoronShieldingAnim"),
                       sGuardJoints, sGuardMorph, skel->sh.limbCount + 1);
    sGuardReady = true;
    sGuardPlay = play;
}

extern "C" void Combo_GoronGuardUpdate(PlayState* play) {
    if (sGuardReady && (sGuardPlay == play)) {
        SkelAnime_Update(&sGuardSkel);
    }
}

// 1 = drew the guard (z_player_lib.c skips the body).
extern "C" int Combo_GoronGuardDraw(PlayState* play) {
    if (!sGuardReady || (sGuardPlay != play)) {
        return 0;
    }
    SkelAnime_DrawFlexOpa(play, sGuardSkel.skeleton, sGuardSkel.jointTable, sGuardSkel.dListCount, GuardOverride,
                          nullptr, nullptr);
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------- //
// MM Player_DrawZoraShield (mm z_player_lib.c 2425): the barrier's crackling shell, its 80 vertices' alpha set from
// object_link_zora_U8_011710 scaled by the barrier strength, with the shell's animated material. Drawn in Link's model
// space (the caller places it as MM's Player_Draw does).
extern "C" void Combo_ZoraBarrierDraw(PlayState* play, s16 strength) {
    Vtx* vtx = (Vtx*)MmRaw("__OTR__objects/object_link_zora/object_link_zora_Vtx_011210");
    u8* alpha = (u8*)MmRaw("__OTR__objects/object_link_zora/object_link_zora_U8_011710");
    const f32 scale = strength * (10.0f / 51.0f);

    if ((vtx == nullptr) || (alpha == nullptr) || (strength == 0)) {
        return;
    }
    for (s32 i = 0; i < 80; i++) {
        vtx[i].v.cn[3] = (alpha[i] * strength) >> 8;
    }
    if (Combo_DebugEnv("COMBO_DEBUG_BARRIER")) {
        static bool sOnce = false;
        if (!sOnce) {
            sOnce = true;
            s32 lo[3] = { 99999, 99999, 99999 }, hi[3] = { -99999, -99999, -99999 };
            for (s32 i = 0; i < 80; i++) {
                for (s32 k = 0; k < 3; k++) {
                    lo[k] = std::min<s32>(lo[k], vtx[i].v.ob[k]);
                    hi[k] = std::max<s32>(hi[k], vtx[i].v.ob[k]);
                }
            }
            Combo_DebugLog("[ComboShip] barrier vtx x %d..%d y %d..%d z %d..%d\n", lo[0], hi[0], lo[1], hi[1], lo[2],
                           hi[2]);
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    Combo_MmAnimatedMat(play, "__OTR__objects/object_link_zora/object_link_zora_Matanimheader_012A80", 1.0f, 2);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)Routed("__OTR__objects/object_link_zora/object_link_zora_DL_011760"));
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------------------- //
// MM's Goron afterimages (Player.unk_3D0, mm z_player_lib.c func_80122BA4 / func_80122C20 / func_80122D44): an action
// requests a kind (1 = the pound slam's ball, 2 = the spike burst, 3 = the punching fist); the next draw of that thing
// stores its matrix in a ring of four; each frame the ghosts fade (by 255/5, the fist by 255/3) and are drawn
// translucent in their kind's colour.
namespace {
struct Ghost {
    u8 kind;
    u8 alpha;
    MtxF mf;
};
struct {
    u8 request;
    s8 index;
    Ghost ghosts[4];
} sGhosts;

// MM Scene_SetRenderModeXlu's tables (z_scene_proc.c): the Goron display lists call segment 0x0C for their render mode.
Gfx sRenderModeNoneDL[] = { gsSPEndDisplayList(), gsSPEndDisplayList(), gsSPEndDisplayList(), gsSPEndDisplayList() };
Gfx sRenderModeXluDL[] = {
    gsDPSetRenderMode(AA_EN | Z_CMP | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU | FORCE_BL |
                          GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1),
                      G_RM_AA_ZB_XLU_SURF2),
    gsSPEndDisplayList(),
    gsDPSetRenderMode(AA_EN | Z_CMP | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU | FORCE_BL |
                          GBL_c1(G_BL_CLR_FOG, G_BL_A_SHADE, G_BL_CLR_IN, G_BL_1MA),
                      G_RM_AA_ZB_XLU_SURF2),
    gsSPEndDisplayList(),
};

const char* kGhostDL[3] = {
    "__OTR__@mm:objects/object_link_goron/gLinkGoronCurledDL",
    "__OTR__@mm:objects/object_link_goron/gLinkGoronRollingSpikesAndEffectDL",
    "__OTR__@mm:objects/object_link_goron/gLinkGoronGoronPunchEffectDL",
};
const u8 kGhostColor[3][3] = { { 180, 200, 255 }, { 155, 0, 0 }, { 255, 0, 0 } };

void GhostCapture(int kind, int alpha) { // MM func_80122BA4
    if (kind == sGhosts.request) {
        if (--sGhosts.index < 0) {
            sGhosts.index = 3;
        }
        Ghost& g = sGhosts.ghosts[sGhosts.index];
        g.kind = (u8)kind;
        g.alpha = (u8)alpha;
        Matrix_Get(&g.mf);
        sGhosts.request = 0;
    }
}
} // namespace

extern "C" void Combo_GoronGhostRequest(int kind) {
    sGhosts.request = (u8)kind;
}

extern "C" void Combo_GoronGhostFade(void) { // MM func_80122C20
    for (auto& g : sGhosts.ghosts) {
        if (g.alpha != 0) {
            const int step = (g.kind == 3) ? (255 / 3) : (255 / 5);
            g.alpha = (step >= g.alpha) ? 0 : (u8)(g.alpha - step);
        }
    }
}

extern "C" void Combo_GoronGhostClear(void) {
    memset(&sGhosts, 0, sizeof(sGhosts));
}

extern "C" void Combo_GoronGhostDraw(PlayState* play) { // MM func_80122D44 (end of Player_Draw)
    bool matDone = false;
    if (Combo_DebugEnv("COMBO_DEBUG_GHOSTS")) {
        int live = 0;
        for (auto& g : sGhosts.ghosts) {
            live += (g.alpha != 0) && (g.alpha != 255);
        }
        if (live != 0) {
            Combo_DebugLog("[ComboShip] ghosts live %d kinds %d/%d/%d/%d\n", live, sGhosts.ghosts[0].kind,
                           sGhosts.ghosts[1].kind, sGhosts.ghosts[2].kind, sGhosts.ghosts[3].kind);
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    for (auto& g : sGhosts.ghosts) {
        if ((g.alpha == 0) || (g.alpha == 255) || (g.kind < 1) || (g.kind > 3)) {
            continue;
        }
        Matrix_Put(&g.mf);
        gDPPipeSync(POLY_XLU_DISP++);
        if (!matDone && (g.kind == 2)) {
            Combo_MmAnimatedMat(play, "__OTR__objects/object_link_goron/object_link_goron_Matanimheader_013138", 1.0f,
                                2);
            matDone = true;
        }
        gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)sRenderModeXluDL);
        gDPSetEnvColor(POLY_XLU_DISP++, kGhostColor[g.kind - 1][0], kGhostColor[g.kind - 1][1],
                       kGhostColor[g.kind - 1][2], g.alpha);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)kGhostDL[g.kind - 1]);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// MM func_80127488: the red flash on the punching fist (drawn at the hand's matrix), which also leaves its afterimage.
extern "C" void Combo_GoronPunchFist(PlayState* play, int alpha) {
    if ((alpha != 0) && Combo_DebugEnv("COMBO_DEBUG_GHOSTS")) {
        Combo_DebugLog("[ComboShip] fist flash %d\n", alpha);
    }
    if (alpha == 0) {
        GhostCapture(3, alpha);
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, alpha);
    gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)sRenderModeXluDL);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)kGhostDL[2]);
    CLOSE_DISPS(play->state.gfxCtx);
    GhostCapture(3, alpha);
}

// The ball's extras (MM Player_Draw 13440-13480), drawn right after the ball at its matrix: the afterimage captures,
// the spikes popping out (func_80124618 over D_8085D540: scale 0 -> 1 across the first two frames) and, while the
// roll charges (actionVar1 >= 5), the red charge aura (object_link_goron_DL_0127B0 / DL_0134D0 with their animated
// materials), which grows with the charge and fades as the spikes come out.
extern "C" void Combo_GoronBallExtras(PlayState* play, int spikes, int charge, int phase) {
    if (phase == 0) { // before the spike shell: its pop-out scale
        if ((spikes != 0) && (spikes < 3)) {
            const f32 s = (spikes >= 2) ? 1.0f : (spikes / 2.0f);
            Matrix_Scale(s, s, s, MTXMODE_APPLY);
        }
        return;
    }
    GhostCapture(1, 255);
    GhostCapture(2, 255);
    if ((spikes < 3) && (charge >= 5)) {
        f32 sc = (charge - 4) * 0.02f;
        const u8 alpha = (spikes != 0) ? (u8)((-spikes * 0x55) + 0xFF) : (u8)(200.0f * sc);
        if (spikes != 0) {
            sc = 0.65f;
        }
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Scale(1.0f, sc, sc, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        Combo_MmAnimatedMat(play, "__OTR__objects/object_link_goron/object_link_goron_Matanimheader_013138", 1.0f, 2);
        gDPSetEnvColor(POLY_XLU_DISP++, 155, 0, 0, alpha);
        gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)sRenderModeNoneDL);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)Routed("__OTR__objects/object_link_goron/object_link_goron_DL_0127B0"));
        Combo_MmAnimatedMat(play, "__OTR__objects/object_link_goron/object_link_goron_Matanimheader_014684", 1.0f, 2);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)Routed("__OTR__objects/object_link_goron/object_link_goron_DL_0134D0"));
        CLOSE_DISPS(play->state.gfxCtx);
    }
}
