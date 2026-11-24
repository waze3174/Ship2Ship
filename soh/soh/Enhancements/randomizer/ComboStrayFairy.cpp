// soh/soh/Enhancements/randomizer/ComboStrayFairy.cpp — ComboShip: MM stray fairies at OOT checks, randomizer side.
//
// The actor itself (MM's EnElforg, ported line for line) is soh/src/code/z_en_item00_combo_strayfairy.c. This file
// answers its randomizer questions and ports the other half of MM's Great Fairy's Mask reaction:
//   * which EnItem00 pickups are an MM stray fairy (the check's foreign item, drawn by MM as gStrayFairySkel);
//   * collecting one sets the same flag the EnItem00 would (OOT's randomizer then queues and grants the check);
//   * a pot / crate / beehive still holding a stray fairy keeps MM's "fairy nearby" flag up (MM's ObjTsubo,
//     ObjKibako2, ObjTaru, ObjComb set ACTORCTX_FLAG_3 while they hold one, and update off-screen to do so), which the
//     Great Fairy's Mask hair reacts to;
//   * the test-harness spawn (COMBO_DEBUG_SPAWN_STRAYFAIRY).
// See docs/deviations/ui-menu.md "MM items in Hyrule" (Great Fairy's Mask).

#include <cstdlib>
#include <cstring>
#include <string>

#include <libultraship/libultraship.h>

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/hook_handlers.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/Enhancements/randomizer/RCToRandInf.h"
#include "soh/ShipInit.hpp"
#include "ComboForms.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
extern s32 gComboStrayFairyNear; // z_en_item00_combo_strayfairy.c (MM's ACTORCTX_FLAG_3)
int Randomizer_ComboStrayFairyArea(int32_t rc); // draw.cpp
}

extern void EnItem00_DrawRandomizedItem(EnItem00* enItem00, PlayState* play);

#ifdef COMBO_BUILD

namespace {

const bool sDbg = Combo_DebugEnv("COMBO_DEBUG_STRAYFAIRY");

// The check holds an MM stray fairy (as its foreign item, or a trap disguised as one: it draws as one).
bool IsStrayFairyCheck(RandomizerCheck rc) {
    if (rc == RC_UNKNOWN_CHECK || rc >= RC_MAX) {
        return false;
    }
    auto loc = Rando::Context::GetInstance()->GetItemLocation(rc);
    if (loc == nullptr || loc->GetPlacedRandomizerGet() != RG_COMBO_FOREIGN) {
        return false;
    }
    return Randomizer_ComboStrayFairyArea(rc) >= 0;
}

// MM's containers set ACTORCTX_FLAG_3 from their update while they hold a stray fairy (ObjTsubo / ObjKibako2 /
// ObjTaru / ObjComb), with ACTOR_FLAG_UPDATE_CULLING_DISABLED so they do it off-screen too.
void ContainerHoldsFairyUpdate(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    const auto identity = ObjectExtension::GetInstance().Get<CheckIdentity>(actor);
    if (identity == nullptr || identity->randomizerCheck == RC_UNKNOWN_CHECK || identity->randomizerCheck >= RC_MAX ||
        identity->randomizerInf == RAND_INF_MAX || Flags_GetRandomizerInf(identity->randomizerInf)) {
        return;
    }
    if (!IsStrayFairyCheck(identity->randomizerCheck)) {
        return;
    }
    gComboStrayFairyNear = true;
    if (!(actor->flags & ACTOR_FLAG_UPDATE_CULLING_DISABLED)) {
        actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
        if (sDbg) {
            Combo_DebugLog("[ComboShip] stray fairy: container actor 0x%X holds rc=%d (fairy-nearby flag)\n",
                           actor->id, (int)identity->randomizerCheck);
        }
    }
}

// ---- Test harness: COMBO_DEBUG_SPAWN_STRAYFAIRY=<check name, '+' for space | auto>[,freestanding][,dist=N][,delay=N]
// Places the REAL check's pickup in front of Link, as its container drop (pots: Item_DropCollectible2 +
// ITEM00_SOH_DUMMY) would, or as a freestanding item. "auto" = the first OOT check holding a foreign stray fairy that
// is not collected yet. The real check: collecting it marks it and delivers the fairy to MM's save.
struct HarnessSpawn {
    bool parsed = false;
    bool active = false;
    bool done = false;
    std::string checkName;
    bool freestanding = false;
    float dist = 150.0f;
    int delay = 40;
    int frames = 0;
};
HarnessSpawn sHarness;

void HarnessParse() {
    sHarness.parsed = true;
    const char* env = std::getenv("COMBO_DEBUG_SPAWN_STRAYFAIRY");
    if (env == nullptr || !Combo_HarnessEnv("COMBO_DEBUG_SPAWN_STRAYFAIRY")) {
        return;
    }
    std::string s(env);
    size_t pos = 0;
    bool first = true;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        if (comma == std::string::npos) {
            comma = s.size();
        }
        std::string tok = s.substr(pos, comma - pos);
        if (first) {
            for (char& c : tok) {
                if (c == '+') {
                    c = ' '; // check names have spaces; the harness env can't carry them
                }
            }
            sHarness.checkName = tok;
            first = false;
        } else if (tok == "freestanding") {
            sHarness.freestanding = true;
        } else if (tok.rfind("dist=", 0) == 0) {
            sHarness.dist = std::strtof(tok.c_str() + 5, nullptr);
        } else if (tok.rfind("delay=", 0) == 0) {
            sHarness.delay = std::atoi(tok.c_str() + 6);
        }
        pos = comma + 1;
    }
    sHarness.active = !sHarness.checkName.empty();
    Combo_DebugLog("[ComboShip] harness: COMBO_DEBUG_SPAWN_STRAYFAIRY '%s' %s dist %.0f delay %d (IS_RANDO=%d)\n",
                   sHarness.checkName.c_str(), sHarness.freestanding ? "freestanding" : "container drop",
                   sHarness.dist, sHarness.delay, (int)IS_RANDO);
}

RandomizerCheck HarnessFindCheck() {
    for (int i = RC_UNKNOWN_CHECK + 1; i < RC_MAX; i++) {
        RandomizerCheck rc = (RandomizerCheck)i;
        Rando::Location* loc = Rando::StaticData::GetLocation(rc);
        if (loc == nullptr) {
            continue;
        }
        if (sHarness.checkName == "auto") {
            auto il = Rando::Context::GetInstance()->GetItemLocation(rc);
            if (il != nullptr && !il->HasObtained() && IsStrayFairyCheck(rc)) {
                return rc;
            }
        } else if (loc->GetName() == sHarness.checkName) {
            return rc;
        }
    }
    return RC_UNKNOWN_CHECK;
}

void HarnessPlayerUpdate() {
    if (!sHarness.parsed) {
        HarnessParse();
    }
    if (!sHarness.active || sHarness.done || gPlayState == nullptr || !IS_RANDO) {
        return;
    }
    if (++sHarness.frames < sHarness.delay) {
        return;
    }
    sHarness.done = true;
    RandomizerCheck rc = HarnessFindCheck();
    if (rc == RC_UNKNOWN_CHECK) {
        Combo_DebugLog("[ComboShip] harness: no check '%s' holding a stray fairy\n", sHarness.checkName.c_str());
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    Vec3f pos = player->actor.world.pos;
    pos.x += Math_SinS(player->actor.shape.rot.y) * sHarness.dist;
    pos.z += Math_CosS(player->actor.shape.rot.y) * sHarness.dist;
    CheckIdentity id{ RAND_INF_MAX, RC_UNKNOWN_CHECK };
    IdentifyCheck(&id, Rando::StaticData::GetLocation(rc)); // the randomizer inf a pot/crate pickup carries
    EnItem00* item00 = (EnItem00*)Item_DropCollectible2(gPlayState, &pos, ITEM00_SOH_DUMMY);
    if (item00 == nullptr) {
        Combo_DebugLog("[ComboShip] harness: spawn failed\n");
        return;
    }
    item00->randoInf = id.randomizerInf;
    item00->itemEntry = Rando::Context::GetInstance()->GetFinalGIEntry(rc, true, GI_NONE);
    item00->actor.draw = (ActorFunc)EnItem00_DrawRandomizedItem;
    if (sHarness.freestanding) {
        item00->ogParams = ITEM00_RUPEE_BLUE; // as a freestanding item whose init turned it into ITEM00_SOH_DUMMY
        item00->randoCheck = rc;
    } else {
        item00->actor.velocity.y = 8.0f; // as ObjTsubo_RandomizerSpawnCollectible
        item00->actor.speedXZ = 2.0f;
    }
    Combo_DebugLog("[ComboShip] harness: spawned check %d '%s' (%s, area %d) at (%.0f %.0f %.0f), randoInf %d\n",
                   (int)rc, Rando::StaticData::GetLocation(rc)->GetName().c_str(),
                   sHarness.freestanding ? "freestanding" : "container drop", Randomizer_ComboStrayFairyArea(rc), pos.x,
                   pos.y, pos.z, (int)id.randomizerInf);
}

} // namespace

// The check this EnItem00 pickup is, when its item is an MM stray fairy; -1 = not one; -2 = not known yet (MM's
// description not resolvable this frame; asked again next frame).
extern "C" s32 Combo_StrayFairyCheckOfItem00(EnItem00* item00) {
    if (item00 == nullptr || item00->itemEntry.modIndex != MOD_RANDOMIZER ||
        item00->itemEntry.getItemId != RG_COMBO_FOREIGN) {
        return -1;
    }
    RandomizerCheck rc = (RandomizerCheck)item00->itemEntry.comboForeignCheck;
    if (rc == RC_UNKNOWN_CHECK || rc >= RC_MAX) {
        return -1;
    }
    const int area = Randomizer_ComboStrayFairyArea(rc);
    if (area == -2) {
        return -2;
    }
    return (area >= 0) ? (s32)rc : -1;
}

// What touching the EnItem00 would have done for an ITEM00_SOH_DUMMY pickup (hook_handlers.cpp
// VB_GIVE_ITEM_FROM_ITEM_00): set its randomizer inf (or its collectible flag), which queues the check.
extern "C" void Combo_StrayFairyCollect(EnItem00* item00) {
    if (item00->randoInf != RAND_INF_MAX) {
        Flags_SetRandomizerInf(item00->randoInf);
    } else {
        Flags_SetCollectible(gPlayState, item00->collectibleFlag);
    }
}

void RegisterComboStrayFairy() {
    bool shouldRegister = IS_RANDO;
    // pots, small crates, large crates, beehives (MM: ObjTsubo, ObjTaru/ObjKibako, ObjKibako2, ObjComb)
    COND_ID_HOOK(OnActorUpdate, ACTOR_OBJ_TSUBO, shouldRegister, ContainerHoldsFairyUpdate);
    COND_ID_HOOK(OnActorUpdate, ACTOR_OBJ_KIBAKO, shouldRegister, ContainerHoldsFairyUpdate);
    COND_ID_HOOK(OnActorUpdate, ACTOR_OBJ_KIBAKO2, shouldRegister, ContainerHoldsFairyUpdate);
    COND_ID_HOOK(OnActorUpdate, ACTOR_OBJ_COMB, shouldRegister, ContainerHoldsFairyUpdate);
    COND_HOOK(OnPlayerUpdate, Combo_HarnessEnv("COMBO_DEBUG_SPAWN_STRAYFAIRY"), HarnessPlayerUpdate);
    if (sDbg) {
        Combo_DebugLog("[ComboShip] stray fairy hooks: IS_RANDO=%d\n", (int)shouldRegister);
    }
}

static RegisterShipInitFunc initFunc(RegisterComboStrayFairy, { "IS_RANDO" });

#endif
