// mm/2s2h/Rando/MiscBehavior/ComboLostWoodsLink.cpp — ComboShip: the Lost Woods link, MM side (route and switches:
// combo/rando/ComboLostWoodsLink.h). Combo-owned file (2ship GLOB_RECURSE).
//
//   Arrival      From OoT's Lost Woods Link lands at Z2_LOST_WOODS spawn 0 without the intro's entrance cutscene. The
//                intro actors (Skull Kid, Epona, Tatl) remove themselves when no cutscene is playing.
//   East exit    The sky drop to Before Clock Town (OPENINGDAN spawn 0, the intro fall) becomes OPENINGDAN spawn 1, the
//                Deku Butler area, and the intro's leftovers there (the Deku Nuts chest, the Butler scene) are marked
//                done the way SkipIntroSequence does.
//   Return       Walking up to the studded door in OPENINGDAN room 1 leaves for OoT's Lost Woods.
//   Clock Tower  The two stone door leaves in the hall no longer block the way down to the twisting corridor, and the
//                Happy Mask Salesman leads to OoT's Mask Shop (the hall is where Clock Town's door now ends).
#ifdef COMBO_BUILD

#include <cmath>

#include <libultraship/bridge/consolevariablebridge.h>
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
int Combo_GamesSolo(void);
void Combo_RequestCrossSwitch(int ootEntrance);
void Combo_RequestPortalReturn(void);
extern int gComboCrossArrival;
}

namespace {

namespace Link = ComboRando::LostWoodsLink;

static_assert(Link::kMmLostWoodsSpawn0 == ENTRANCE(LOST_WOODS, 0), "the OoT side's MM arrival entrance");

// Owner decision 2026-11-24: while the link is on, the Clock Tower's door from Clock Town ends in the hall, and the
// Happy Mask Salesman there leads to OoT's Mask Shop. Off: Clock Town's door goes straight to OoT, as without the link.
constexpr bool kClockTowerEntryStaysInMm = true;

// Lost Woods' east exit leads to the intro fall; the Butler area has no cutscene.
constexpr int kSkyDropEntrance = ENTRANCE(OPENING_DUNGEON, 0);
constexpr int kButlerAreaEntrance = ENTRANCE(OPENING_DUNGEON, 1);

constexpr int kLostWoodsArrivalSpawn = 0;

// A box in OPENINGDAN room 1 between the studded door (1230,0,-1140) and the pit respawn at spawn 3 (1380,0,-1140),
// so a respawn never lands inside it.
struct Box {
    f32 minX, maxX, minY, maxY, minZ, maxZ;

    bool Contains(const Vec3f& pos) const {
        return pos.x >= minX && pos.x <= maxX && pos.y >= minY && pos.y <= maxY && pos.z >= minZ && pos.z <= maxZ;
    }
};
constexpr Box kStuddedDoorBox = { 1235.0f, 1320.0f, -30.0f, 80.0f, -1210.0f, -1070.0f };

// The Happy Mask Salesman stands at (-106,-30,-79) in the hall (EN_OSN in Z2_INSIDETOWER room 0). Link triggers the
// way to OoT by pushing against him: within this distance of his centre (his collider is 30 wide, Link's 12), for this
// many frames, after having been this far away. The hall's route to the stone doors passes 106 units to his side.
constexpr f32 kSalesmanX = -106.0f;
constexpr f32 kSalesmanZ = -79.0f;
constexpr f32 kSalesmanTouchDistance = 55.0f;
constexpr f32 kSalesmanArmDistance = 150.0f;
constexpr f32 kSalesmanMinY = -60.0f;
constexpr f32 kSalesmanMaxY = 40.0f;
constexpr int kSalesmanPushFrames = 15;

// The stone door leaves are BgCtowerRot params 1 and 2; params 0 is the twisting corridor itself.
constexpr int kStoneDoorFirstParams = BGCTOWERROT_STONE_DOOR_MAIN;

// What SkipIntroSequence marks after the intro: the Butler area's switches and chest (the free Deku Nuts) and the
// Happy Mask Salesman's flag in the Clock Tower.
constexpr u32 kOpeningDanSwitchBits = (1 << 0) | (1 << 2);
constexpr u32 kOpeningDanChestBits = 1 << 0;
constexpr u32 kInsideTowerSwitchBits = 1 << 0;

// Fires once Link has been clear of the trigger and then stays in it for `pushFrames` frames.
struct Trigger {
    bool armed = false;
    bool fired = false;
    int insideFrames = 0;

    void Reset() {
        *this = Trigger{};
    }

    bool Update(bool inside, bool clear, int pushFrames) {
        if (fired) {
            return false;
        }
        if (!armed) {
            armed = clear;
            return false;
        }
        insideFrames = inside ? insideFrames + 1 : 0;
        fired = insideFrames >= pushFrames;
        return fired;
    }
};

Trigger sStuddedDoor;
Trigger sSalesman;

bool Active() {
    return CVarGetInteger(Link::kCVar, 0) != 0 && !Combo_GamesSolo();
}

void OrSceneFlags(s16 sceneId, u32 switchBits, u32 chestBits) {
    gSaveContext.cycleSceneFlags[sceneId].switch0 |= switchBits;
    gSaveContext.cycleSceneFlags[sceneId].chest |= chestBits;
    gSaveContext.save.saveInfo.permanentSceneFlags[sceneId].switch0 |= switchBits;
    gSaveContext.save.saveInfo.permanentSceneFlags[sceneId].chest |= chestBits;
}

// Before Clock Town is entered without the intro, so its intro flags are set by hand, in the cycle flags the scene
// loads from and in the permanent ones a new cycle starts from.
void MarkIntroDone() {
    OrSceneFlags(SCENE_OPENINGDAN, kOpeningDanSwitchBits, kOpeningDanChestBits);
    OrSceneFlags(SCENE_INSIDETOWER, kInsideTowerSwitchBits, 0);
}

f32 DistanceXZ(const Vec3f& pos, f32 x, f32 z) {
    return sqrtf(SQ(pos.x - x) + SQ(pos.z - z));
}

void UpdateStuddedDoorTrigger(const Vec3f& pos) {
    const bool inside = kStuddedDoorBox.Contains(pos);

    if (sStuddedDoor.Update(inside, !inside, 1)) {
        SPDLOG_INFO("[ComboLostWoodsLink] studded door -> OOT Lost Woods 0x{:X}", Link::kOotLostWoodsFromTermina);
        Combo_RequestCrossSwitch(Link::kOotLostWoodsFromTermina | Link::kOotEntranceFinalBit);
    }
}

void UpdateSalesmanTrigger(const Vec3f& pos) {
    const f32 distance = DistanceXZ(pos, kSalesmanX, kSalesmanZ);
    const bool inside = distance <= kSalesmanTouchDistance && pos.y >= kSalesmanMinY && pos.y <= kSalesmanMaxY;

    if (sSalesman.Update(inside, distance >= kSalesmanArmDistance, kSalesmanPushFrames)) {
        SPDLOG_INFO("[ComboLostWoodsLink] Happy Mask Salesman -> OOT Mask Shop");
        Combo_RequestPortalReturn();
    }
}

void UpdateTriggers() {
    PlayState* play = gPlayState;

    if (play == nullptr || !Active() || gSaveContext.gameMode != GAMEMODE_NORMAL ||
        play->transitionTrigger != TRANS_TRIGGER_OFF || Player_InCsMode(play)) {
        return;
    }
    const Vec3f& pos = GET_PLAYER(play)->actor.world.pos;

    if (play->sceneId == SCENE_OPENINGDAN) {
        UpdateStuddedDoorTrigger(pos);
    } else if (play->sceneId == SCENE_INSIDETOWER && kClockTowerEntryStaysInMm) {
        UpdateSalesmanTrigger(pos);
    }
}

void HandleSceneInit(s8 sceneId, s8 spawnNum) {
    sStuddedDoor.Reset();
    sSalesman.Reset();
    if (Active() && (sceneId == SCENE_LOST_WOODS || sceneId == SCENE_INSIDETOWER)) {
        MarkIntroDone();
    }
}

// The leaves would close the hall off from the corridor: they only open for Link coming from the corridor side.
void HandleStoneDoorInit(Actor* actor) {
    if (Active() && actor->params >= kStoneDoorFirstParams) {
        Actor_Kill(actor);
    }
}

void RegisterComboLostWoodsLink() {
    COND_HOOK(OnSceneInit, true, HandleSceneInit);
    COND_HOOK(OnGameStateUpdate, true, UpdateTriggers);
    COND_ID_HOOK(OnActorInit, ACTOR_BG_CTOWER_ROT, true, HandleStoneDoorInit);
}

RegisterShipInitFunc sLostWoodsLinkInit(RegisterComboLostWoodsLink, {});

} // namespace

extern "C" int Combo_MM_LostWoodsLinkExit(int entrance) {
    if (gPlayState == nullptr || gPlayState->sceneId != SCENE_LOST_WOODS || entrance != kSkyDropEntrance || !Active()) {
        return entrance;
    }
    MarkIntroDone();
    SPDLOG_INFO("[ComboLostWoodsLink] Lost Woods east exit -> Before Clock Town, Butler area");
    return kButlerAreaEntrance;
}

extern "C" int Combo_MM_LostWoodsLinkSkipsEntranceCs(void) {
    return gComboCrossArrival != 0 && gPlayState != nullptr && gPlayState->sceneId == SCENE_LOST_WOODS &&
           gPlayState->curSpawn == kLostWoodsArrivalSpawn && Active();
}

extern "C" int Combo_MM_ClockTowerEntryStaysInMm(void) {
    return kClockTowerEntryStaysInMm && Active();
}

#endif // COMBO_BUILD
