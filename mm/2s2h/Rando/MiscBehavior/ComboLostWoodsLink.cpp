// mm/2s2h/Rando/MiscBehavior/ComboLostWoodsLink.cpp — ComboShip: the Lost Woods link, MM side (route and switches:
// combo/rando/ComboLostWoodsLink.h). Combo-owned file (2ship GLOB_RECURSE).
//
//   Arrival      From OoT's Lost Woods Link lands at Z2_LOST_WOODS spawn 0 without the intro's entrance cutscene. The
//                intro actors (Skull Kid, Epona, Tatl) remove themselves when no cutscene is playing.
//   East exit    The sky drop to Before Clock Town (OPENINGDAN spawn 0, the intro fall) becomes Clock Tower spawn 0,
//                the far end of the twisting corridor, without that spawn's entrance cutscene. Link never enters the
//                Deku Butler area, which needs the Deku Mask.
//   Return       The corridor's exit polygons (INSIDETOWER exit 1, normally to OPENINGDAN spawn 1) leave for OoT's
//                Lost Woods.
//   Clock Tower  The two stone door leaves in the hall no longer block the way down to the twisting corridor (the hall
//                is where Clock Town's door now ends). The Happy Mask Salesman there is left as in vanilla.
// Return and the stone doors follow the cross gate (combo/rando/ComboCrossGate.h): while the way into Hyrule is closed
// (a back-to-back slot before Majora is beaten) the leaves stay, so the hall's door locks up as in vanilla, and the
// corridor's far end leads where it does in vanilla.
#ifdef COMBO_BUILD

#include <spdlog/spdlog.h>

#include "rando/ComboLostWoodsLink.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "ComboLostWoodsLink.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "overlays/actors/ovl_Bg_Ctower_Rot/z_bg_ctower_rot.h"
// BenPort.cpp
void Combo_RequestCrossSwitch(int ootEntrance);
int Combo_CrossToHyruleOpen(void);
extern int gComboCrossArrival;
}

namespace {

namespace Link = ComboRando::LostWoodsLink;

static_assert(Link::kMmLostWoodsSpawn0 == ENTRANCE(LOST_WOODS, 0), "the OoT side's MM arrival entrance");

// Owner decision 2026-11-24: while the link is on, the Clock Tower's door from Clock Town ends in the hall, and the
// corridor end is the only way to OoT. Off: Clock Town's door goes straight to OoT, as without the link.
constexpr bool kClockTowerEntryStaysInMm = true;

// Lost Woods' east exit leads to the intro fall; the link sends it to the Clock Tower's corridor end instead (spawn 0,
// (11,-296,-2317), facing up the corridor, 63 units from the exit polygons behind it).
constexpr int kSkyDropEntrance = ENTRANCE(OPENING_DUNGEON, 0);
constexpr int kCorridorEndEntrance = ENTRANCE(CLOCK_TOWER_INTERIOR, 0);
constexpr int kCorridorEndSpawn = 0;

// The Clock Tower corridor's far end (INSIDETOWER exit 1, polygons 551 and 552) normally leads to the Deku Butler area.
constexpr int kCorridorEndExit = ENTRANCE(OPENING_DUNGEON, 1);

constexpr int kLostWoodsArrivalSpawn = 0;

// The stone door leaves are BgCtowerRot params 1 and 2; params 0 is the twisting corridor itself.
constexpr int kStoneDoorFirstParams = BGCTOWERROT_STONE_DOOR_MAIN;

// What SkipIntroSequence marks after the intro: the Happy Mask Salesman's flag in the Clock Tower.
constexpr u32 kInsideTowerSwitchBits = 1 << 0;

// Set by the east exit: the next scene load is the corridor end, whose entrance cutscene is skipped. Any scene init
// clears it.
bool sSkipCorridorEndCutscene = false;

bool Active() {
    return Combo_LostWoodsLinkOn() != 0;
}

// The Clock Tower is entered without the intro, so the Salesman's flag is set by hand, in the cycle flags the scene
// loads from and in the permanent ones a new cycle starts from.
void MarkIntroDone() {
    gSaveContext.cycleSceneFlags[SCENE_INSIDETOWER].switch0 |= kInsideTowerSwitchBits;
    gSaveContext.save.saveInfo.permanentSceneFlags[SCENE_INSIDETOWER].switch0 |= kInsideTowerSwitchBits;
}

void HandleSceneInit(s8 sceneId, s8 spawnNum) {
    sSkipCorridorEndCutscene = false;
    if (Active() && (sceneId == SCENE_LOST_WOODS || sceneId == SCENE_INSIDETOWER)) {
        MarkIntroDone();
    }
}

// The leaves would close the hall off from the corridor: they only open for Link coming from the corridor side. With
// the way into Hyrule closed they stay, and the hall has no way back down.
void HandleStoneDoorInit(Actor* actor) {
    if (Active() && Combo_CrossToHyruleOpen() && actor->params >= kStoneDoorFirstParams) {
        Actor_Kill(actor);
    }
}

void RegisterComboLostWoodsLink() {
    COND_HOOK(OnSceneInit, true, HandleSceneInit);
    COND_ID_HOOK(OnActorInit, ACTOR_BG_CTOWER_ROT, true, HandleStoneDoorInit);
}

RegisterShipInitFunc sLostWoodsLinkInit(RegisterComboLostWoodsLink, {});

} // namespace

extern "C" int Combo_MM_LostWoodsLinkExit(int entrance) {
    if (gPlayState == nullptr || !Active()) {
        return entrance;
    }
    if (gPlayState->sceneId == SCENE_LOST_WOODS && entrance == kSkyDropEntrance) {
        MarkIntroDone();
        sSkipCorridorEndCutscene = true;
        SPDLOG_INFO("[ComboLostWoodsLink] Lost Woods east exit -> Clock Tower corridor end");
        return kCorridorEndEntrance;
    }
    if (gPlayState->sceneId == SCENE_INSIDETOWER && entrance == kCorridorEndExit && Combo_CrossToHyruleOpen()) {
        SPDLOG_INFO("[ComboLostWoodsLink] Clock Tower corridor end -> OOT Lost Woods 0x{:X}",
                    Link::kOotLostWoodsFromTermina);
        Combo_RequestCrossSwitch(Link::kOotLostWoodsFromTermina | Link::kOotEntranceFinalBit);
    }
    return entrance;
}

extern "C" int Combo_MM_LostWoodsLinkSkipsEntranceCs(void) {
    if (gPlayState == nullptr || !Active()) {
        return 0;
    }
    const bool fromOotLostWoods = gComboCrossArrival != 0 && gPlayState->sceneId == SCENE_LOST_WOODS &&
                                  gPlayState->curSpawn == kLostWoodsArrivalSpawn;
    const bool atCorridorEnd = sSkipCorridorEndCutscene && gPlayState->sceneId == SCENE_INSIDETOWER &&
                               gPlayState->curSpawn == kCorridorEndSpawn;
    return fromOotLostWoods || atCorridorEnd;
}

extern "C" int Combo_MM_ClockTowerEntryStaysInMm(void) {
    return kClockTowerEntryStaysInMm && Active();
}

#endif // COMBO_BUILD
