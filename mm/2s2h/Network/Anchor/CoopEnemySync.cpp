// ComboShip co-op combat (2026-11-13): enemy + boss sync over Anchor and enemy HP scaling by player count,
// MM side. The logic is shared with OOT in combo/coop/CoopEnemySyncGame.inl (+ the pure core in
// combo/coop/CoopEnemySync.h); this file only binds it to 2Ship's types and the MMAnchor adapter.
// See docs/plans/full-coop-sync.md, "Co-op combat", and docs/deviations/anchor.md.
#ifdef COMBO_BUILD

#include "MMAnchor.h"
#include "CoopEnemySync.h"
#include <spdlog/spdlog.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ObjectExtension/ActorListIndex.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

#define COOP_GAME 1
#define COOP_SPEED(a) ((a)->speed)
#define COOP_YDIST(a) ((a)->playerHeightRel)
#define COOP_SCENE(play) ((int)(play)->sceneId)
#define COOP_INSTANCE_SIZE(a) ((size_t)gActorOverlayTable[(a)->id].profile->instanceSize)
#define COOP_ELEM_T ColliderElement
#define COOP_EL_ACFLAGS(el) ((el).acElemFlags)
#define COOP_EL_ACDMG(el) ((el).acDmgInfo.dmgFlags)
#define COOP_CYL_EL(c) ((c)->elem)
#define COOP_JNT_EL(e) ((e)->base)
#define COOP_TRIS_EL(e) ((e)->base)
#define COOP_QUAD_EL(q) ((q)->elem)
#define COOP_HAS_SPHERE 1
#define COOP_EL_ACHITEL(el) ((el).acHitElem)
#define COOP_EL_AT(el) ((el).atDmgInfo)
#define COOP_EL_ACHITPOS(el) ((el).acDmgInfo.hitPos)
#define COOP_COMBO_TOUCH 0
#define COOP_HEALTH ((int)gSaveContext.save.saveInfo.playerData.health)
#define COOP_PLAY_SFX AudioSfx_PlaySfx
#define COOP_CAT_EXPLOSIVE ACTORCAT_EXPLOSIVES

static bool CoopG_Connected() {
    MMAnchor* anchor = MMAnchor::Instance;
    if (anchor == nullptr || !anchor->isActive) {
        return false;
    }
    for (auto& [id, c] : anchor->clients) {
        if (c.online && !c.self && id != anchor->ownClientId) {
            return true;
        }
    }
    return false;
}

static uint32_t CoopG_OwnClientId() {
    return MMAnchor::Instance ? MMAnchor::Instance->ownClientId : 0;
}

static void CoopG_Send(nlohmann::json payload) {
    if (MMAnchor::Instance != nullptr) {
        MMAnchor::Instance->SendCoopJson(payload);
    }
}

// The teammate's puppet actor in our play state, if it is spawned (validated against the live player list).
static Actor* CoopG_Puppet(uint32_t clientId) {
    MMAnchor* anchor = MMAnchor::Instance;
    if (anchor == nullptr || gPlayState == nullptr) {
        return nullptr;
    }
    auto it = anchor->clients.find(clientId);
    if (it == anchor->clients.end() || it->second.player == nullptr) {
        return nullptr;
    }
    Actor* want = &it->second.player->actor;
    // MMAnchor moves the puppet from the PLAYER list to NPC (ShouldActorInit), so look there (and PLAYER, in case).
    for (int cat : { ACTORCAT_NPC, ACTORCAT_PLAYER }) {
        for (Actor* x = gPlayState->actorCtx.actorLists[cat].first; x != nullptr; x = x->next) {
            if (x == want && x->update != nullptr) {
                return x;
            }
        }
    }
    return nullptr;
}

#include "coop/CoopEnemySyncGame.inl"

void CoopEnemySync_Init() {
    CoopEnemyGlue::Register();
}

void CoopEnemySync_OnPacket(const nlohmann::json& payload) {
    try {
        CoopEnemyGlue::OnPacket(payload);
    } catch (const std::exception& e) { SPDLOG_ERROR("[CoopEnemy] mm packet: {}", e.what()); }
}

bool CoopEnemySync_IsRemoteKill() {
    return CoopEnemyGlue::sRemoteKill;
}

void CoopEnemySync_PuppetCoop(Player* puppet, uint32_t clientId) {
    CoopEnemyGlue::PuppetCoop(puppet, clientId);
}

// Every sfx request (AudioSfx_PlaySfx): enemy sounds of the enemies we own go to teammates' mirrors.
extern "C" void MmCoop_ActorSfx(u16 sfxId, Vec3f* pos, u8 token, f32* freqScale, f32* vol, s8* reverbAdd) {
    CoopEnemyGlue::OnSfx(sfxId, pos, token, freqScale, vol, reverbAdd);
}

// Actor_Spawn (z_actor.c): children of the enemies we sync.
extern "C" void MmCoop_ActorSpawned(Actor* actor) {
    CoopEnemyGlue::OnSpawned(actor);
}

// EffectSs_Spawn (z_effect_soft_sprite.c): particle effects of the enemies we own go to teammates' mirrors.
extern "C" void MmCoop_EffectSpawn(s32 type, s32 priority, void* initParams) {
    CoopEnemyGlue::OnEffectSpawn(type, priority, initParams);
}

// SkelAnime_Init* (z_skelanime.c): the mirror section finds an actor's skeletons in this registry.
extern "C" void MmCoop_SkelInit(SkelAnime* skelAnime) {
    CoopEnemyGlue::SkelRegister(skelAnime);
}

#endif // COMBO_BUILD
