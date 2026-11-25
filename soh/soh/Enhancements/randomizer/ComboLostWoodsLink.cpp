// soh/soh/Enhancements/randomizer/ComboLostWoodsLink.cpp — ComboShip: the Lost Woods link, OOT side (route and
// switches: combo/rando/ComboLostWoodsLink.h). Combo-owned file (soh GLOB_RECURSE).
//
//   Exit:   the north wrong turn in Lost Woods room 0 switches to MM's Lost Woods instead of looping back to Kokiri
//           Forest. Player_HandleExitsAndVoids asks Combo_OOT_LostWoodsLinkExit before the entrance override, so the
//           entrance tracker and the cross-game entrance redirect never see the wrong turn's own destination. While
//           the path is closed (a back-to-back slot, combo/rando/ComboCrossGate.h) the wrong turn is vanilla.
//   Mask Shop: while the link is on, the Market's Mask Shop door leads into OoT's own shop; ComboShip's switch to MM
//           there (OTRGlobals.cpp) asks Combo_OOT_LostWoodsLinkKeepsMaskShop first.
//   Marker: a stump (MM's cut tree) in front of that tunnel's mouth while the path is open. A small combo actor,
//           registered in ActorDB like MM's Obj_Aqua (ComboMmItems.cpp); it draws, and its trunk is solid like
//           OoT's trees.
#ifdef COMBO_BUILD

// C++ headers first: z64.h pulls <memory> under the extern "C" below
#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

#include "rando/ComboLostWoodsLink.h"
#include "soh/ActorDB.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "ComboForms.h" // Combo_MmDL
#include "ComboLostWoodsLink.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
void Combo_RequestLostWoodsCrossSwitch(int mmEntrance); // OTRGlobals.cpp
}

namespace {

namespace Link = ComboRando::LostWoodsLink;

constexpr s16 kMarkerRoom = 0;

// On the open hub floor (y 0, collision polygon 195) left of the north tunnel mouth, which is the opening in the wall
// at z -200. At least 80 units from every wall, off the walk from the Kokiri Forest arrival (x 0) up to the mouth, and
// beside the way out of the tunnel, where the Termina arrival is (spawn 3 at (-2,0,-308)).
constexpr f32 kMarkerX = -120.0f;
constexpr f32 kMarkerY = 0.0f;
constexpr f32 kMarkerZ = -110.0f;

// MM's cut tree is about 2200 units tall at scale 1; this makes it about 1.5 Link heights.
constexpr f32 kMarkerScale = 0.05f;
constexpr const char* kMarkerDL = "objects/object_keikoku_demo/gKeikokuDemoTallTreeCutDL";

// The trunk's collision cylinder, in world units. The model's trunk is an octagon about 260 units across the flats
// (13 here) and 2200 units tall (110 here); the roots flare wider, but only below about 25 units.
constexpr s16 kTrunkRadius = 13;
constexpr s16 kTrunkHeight = 110;

// The same collider settings as OoT's trees (EnWood02)
ColliderCylinderInit kTrunkCollider = {
    {
        COLTYPE_TREE,
        AT_NONE,
        AC_ON | AC_HARD | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK5,
        { 0x00000000, 0x00, 0x00 },
        { 0x0FC0074A, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_ON,
    },
    { kTrunkRadius, kTrunkHeight, 0, { 0, 0, 0 } },
};

struct LostWoodsMarker {
    Actor actor;
    ColliderCylinder collider;
};

static_assert(Link::kOotLostWoodsFromTermina == ENTR_LOST_WOODS_3, "OoT arrival from Termina is Lost Woods spawn 3");

void LostWoodsMarker_Init(Actor* thisx, PlayState* play) {
    LostWoodsMarker* marker = (LostWoodsMarker*)thisx;

    Actor_SetScale(thisx, kMarkerScale);
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    Collider_InitCylinder(play, &marker->collider);
    Collider_SetCylinder(play, &marker->collider, thisx, &kTrunkCollider);
}

void LostWoodsMarker_Destroy(Actor* thisx, PlayState* play) {
    Collider_DestroyCylinder(play, &((LostWoodsMarker*)thisx)->collider);
}

void LostWoodsMarker_Update(Actor* thisx, PlayState* play) {
    LostWoodsMarker* marker = (LostWoodsMarker*)thisx;

    Collider_UpdateCylinder(thisx, &marker->collider);
    CollisionCheck_SetAC(play, &play->colChkCtx, &marker->collider.base);
    CollisionCheck_SetOC(play, &play->colChkCtx, &marker->collider.base);
}

void LostWoodsMarker_Draw(Actor* thisx, PlayState* play) {
    Gfx* dl = (Gfx*)Combo_MmDL(kMarkerDL);

    if (dl != nullptr) {
        Gfx_DrawDListOpa(play, dl);
    }
}

// The link exists in this slot.
bool Active() {
    return Combo_LostWoodsLinkOn() != 0;
}

// The link exists and the Link on screen may take it: the stump is there and the wrong turn leaves for Termina.
bool PathOpen() {
    return Active() && Combo_LostWoodsToTerminaOpen() != 0;
}

short MarkerActorId() {
    static int sId = -1;

    if (sId >= 0) {
        return (short)sId;
    }
    if (ActorDB::Instance == nullptr) {
        return -1;
    }
    sId = ActorDB::Instance->RetrieveId("Combo_LostWoodsMarker");
    if (sId < 0) {
        ActorDBInit init = {
            "Combo_LostWoodsMarker",
            "Lost Woods link marker (MM tree stump)",
            -1, // next free id
            ACTORCAT_PROP,
            0,
            OBJECT_GAMEPLAY_KEEP,
            sizeof(LostWoodsMarker),
            (ActorFunc)LostWoodsMarker_Init,
            (ActorFunc)LostWoodsMarker_Destroy,
            (ActorFunc)LostWoodsMarker_Update,
            (ActorFunc)LostWoodsMarker_Draw,
            nullptr,
        };
        sId = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return (short)sId;
}

void SpawnMarkerInRoomZero() {
    PlayState* play = gPlayState;

    if (play == nullptr || play->sceneNum != SCENE_LOST_WOODS || play->roomCtx.curRoom.num != kMarkerRoom ||
        !PathOpen()) {
        return;
    }
    const short id = MarkerActorId();
    if (id >= 0) {
        Actor_Spawn(&play->actorCtx, play, id, kMarkerX, kMarkerY, kMarkerZ, 0, 0, 0, 0);
    }
}

void RegisterLostWoodsLinkMarker() {
    COND_HOOK(OnSceneSpawnActors, true, SpawnMarkerInRoomZero);
}

RegisterShipInitFunc sLostWoodsMarkerInit(RegisterLostWoodsLinkMarker, {});

} // namespace

extern "C" int Combo_OOT_LostWoodsLinkKeepsMaskShop(void) {
    return Active();
}

extern "C" int Combo_OOT_LostWoodsLinkExit(PlayState* play, const void* poly, unsigned int bgId) {
    if (bgId != BGCHECK_SCENE || play->sceneNum != SCENE_LOST_WOODS || !PathOpen()) {
        return 0;
    }
    const CollisionPoly* polyList = play->colCtx.colHeader->polyList;
    if ((const CollisionPoly*)poly - polyList != Link::kOotNorthWrongTurnPoly) {
        return 0;
    }
    SPDLOG_INFO("[ComboLostWoodsLink] OOT north wrong turn -> MM Lost Woods 0x{:X}", Link::kMmLostWoodsSpawn0);
    Combo_RequestLostWoodsCrossSwitch(Link::kMmLostWoodsSpawn0);
    return 1;
}

#endif // COMBO_BUILD
