// ComboShip co-op combat (2026-11-13): enemy + boss sync over Anchor and enemy HP scaling by player count,
// OOT side. The logic is shared with MM in combo/coop/CoopEnemySyncGame.inl (+ the pure core in
// combo/coop/CoopEnemySync.h); this file only binds it to SoH's types and Anchor object.
// See docs/plans/full-coop-sync.md, "Co-op combat", and docs/deviations/anchor.md.
#ifdef COMBO_BUILD

#include "Anchor.h"
#include "CoopEnemySync.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ObjectExtension/ActorListIndex.h"
#include "soh/ActorDB.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
void CollisionCheck_ApplyDamage(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider, ColliderInfo* info);
}

#define COOP_GAME 0
#define COOP_SPEED(a) ((a)->speedXZ)
#define COOP_YDIST(a) ((a)->yDistToPlayer)
#define COOP_SCENE(play) ((int)(play)->sceneNum)
extern "C" ActorDBEntry* ActorDB_Retrieve(const int id);
#define COOP_INSTANCE_SIZE(a) ((size_t)ActorDB_Retrieve((a)->id)->instanceSize)
#define COOP_ELEM_T ColliderInfo
#define COOP_EL_ACFLAGS(el) ((el).bumperFlags)
#define COOP_EL_ACDMG(el) ((el).bumper.dmgFlags)
#define COOP_CYL_EL(c) ((c)->info)
#define COOP_JNT_EL(e) ((e)->info)
#define COOP_TRIS_EL(e) ((e)->info)
#define COOP_QUAD_EL(q) ((q)->info)
#define COOP_HAS_SPHERE 0
#define COOP_EL_ACHITEL(el) ((el).acHitInfo)
#define COOP_EL_AT(el) ((el).toucher)
#define COOP_EL_ACHITPOS(el) ((el).bumper.hitPos)
#define COOP_COMBO_TOUCH 1
#define COOP_HEALTH ((int)gSaveContext.health)
#define COOP_PLAY_SFX Audio_PlaySfxGeneral
#define COOP_CAT_EXPLOSIVE ACTORCAT_EXPLOSIVE

static bool CoopG_Connected() {
    Anchor* anchor = Anchor::Instance;
    if (anchor == nullptr || !anchor->isConnected) {
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
    return Anchor::Instance ? Anchor::Instance->ownClientId : 0;
}

static void CoopG_Send(nlohmann::json payload) {
    if (Anchor::Instance != nullptr) {
        Anchor::Instance->SendJsonToRemote(payload);
    }
}

// The teammate's puppet actor in our play state, if it is spawned (validated against the live player list).
static Actor* CoopG_Puppet(uint32_t clientId) {
    Anchor* anchor = Anchor::Instance;
    if (anchor == nullptr || gPlayState == nullptr) {
        return nullptr;
    }
    auto it = anchor->clients.find(clientId);
    if (it == anchor->clients.end() || it->second.player == nullptr) {
        return nullptr;
    }
    Actor* want = &it->second.player->actor;
    for (Actor* x = gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head; x != nullptr; x = x->next) {
        if (x == want && x->update != nullptr) {
            return x;
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
    } catch (const std::exception& e) { SPDLOG_ERROR("[CoopEnemy] oot packet: {}", e.what()); }
}

bool CoopEnemySync_IsRemoteKill() {
    return CoopEnemyGlue::sRemoteKill;
}

void CoopEnemySync_PuppetCoop(Player* puppet, uint32_t clientId) {
    CoopEnemyGlue::PuppetCoop(puppet, clientId);
}

// Every sfx request (Audio_PlaySfxGeneral): enemy sounds of the enemies we own go to teammates' mirrors.
extern "C" void SohCoop_ActorSfx(u16 sfxId, Vec3f* pos, u8 token, f32* freqScale, f32* vol, s8* reverbAdd) {
    CoopEnemyGlue::OnSfx(sfxId, pos, token, freqScale, vol, reverbAdd);
}

// Actor_Spawn (z_actor.c): children of the enemies we sync.
extern "C" void SohCoop_ActorSpawned(Actor* actor) {
    CoopEnemyGlue::OnSpawned(actor);
}

// EffectSs_Spawn (z_effect_soft_sprite.c): particle effects of the enemies we own go to teammates' mirrors.
extern "C" void SohCoop_EffectSpawn(s32 type, s32 priority, void* initParams) {
    CoopEnemyGlue::OnEffectSpawn(type, priority, initParams);
}

// SkelAnime_Init* (z_skelanime.c): the mirror section finds an actor's skeletons in this registry.
extern "C" void SohCoop_SkelInit(SkelAnime* skelAnime) {
    CoopEnemyGlue::SkelRegister(skelAnime);
}

#endif // COMBO_BUILD
