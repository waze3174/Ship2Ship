// ComboShip co-op combat: per-game enemy/boss sync glue. NOT a standalone header: included once by
//   soh/soh/Network/Anchor/CoopEnemySync.cpp   (COOP_GAME 0)
//   mm/2s2h/Network/Anchor/CoopEnemySync.cpp    (COOP_GAME 1)
// after that game's headers, so `Actor`, `PlayState`, GameInteractor etc. resolve to that game's types. The
// includer defines:
//   COOP_GAME (0 OOT, 1 MM), COOP_SPEED(actor) (lvalue), COOP_YDIST(actor) (lvalue), COOP_SCENE(play),
//   static bool CoopG_Connected();      // Anchor connected and at least one other online client
//   static uint32_t CoopG_OwnClientId();
//   static void CoopG_Send(nlohmann::json payload); // stamps clientId + originGame and hands to the launcher
//   COOP_INSTANCE_SIZE(actor)  bytes of the actor's instance (its overlay profile)
//   COOP_ELEM_T, COOP_EL_ACFLAGS(el), COOP_EL_ACDMG(el), COOP_CYL_EL(c), COOP_JNT_EL(e), COOP_TRIS_EL(e),
//   COOP_QUAD_EL(q): collider element names (OOT ColliderInfo info/bumperFlags, MM ColliderElement elem/acElemFlags)
//   COOP_HAS_SPHERE  1 when the game has COLSHAPE_SPHERE (MM)
//   COOP_EL_ACHITEL(el) (OOT acHitInfo / MM acHitElem), COOP_EL_AT(el) (toucher / atDmgInfo),
//   COOP_EL_ACHITPOS(el) (bumper.hitPos / acDmgInfo.hitPos), COOP_COMBO_TOUCH (OOT: toucher has comboMm* bytes)
//   static Actor* CoopG_Puppet(uint32_t clientId); // that teammate's spawned puppet, or nullptr
//   COOP_PLAY_SFX(id, pos, token, freq, vol, reverb)  the game's lowest sfx request (Audio_PlaySfxGeneral /
//   AudioSfx_PlaySfx), whose entry calls CoopEnemyGlue::OnSfx
// and calls CoopEnemyGlue::SkelRegister from its SkelAnime_Init* hook (z_skelanime.c).
//
// MIRRORS (wire v3): the owner of an enemy is its single authority. Everyone else in the room shows a MIRROR of the
// owner's copy: its update never runs; every logic frame it is posed (SkelAnime state + raw jointTable), moved
// (Entry) and given the owner's hurtboxes (AC/OC colliders, never AT) from the owner's snapshot.
// Design + what is built vs. owed: docs/plans/full-coop-sync.md, "Co-op combat".

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <zlib.h>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "coop/CoopBase64.h"
#include "coop/CoopEnemySync.h"
#include "coop/CoopHash.h"
#include "coop/CoopMirror.h"
#include "coop/CoopProtocol.h"

namespace CoopEnemyGlue {
using namespace Coop::Enemy;
using Coop::HashCombine;

// Room settings. Registered as WORLD CVars (combo/coop/WorldCvarsOot.inl), so the Anchor room owner's values
// are overlaid on every joiner for the session (COMBO_WORLD_CVARS) and restored when they leave.
static constexpr const char* kCvarSync = "gCombo.Coop.EnemySync";          // 1 = share enemies/bosses
static constexpr const char* kCvarHpScale = "gCombo.Coop.EnemyHpScale";    // 1 = HP x player count
static constexpr const char* kCvarHpPercent = "gCombo.Coop.EnemyHpPercent"; // % added per extra player

static constexpr const char* kGameName = COOP_GAME == 1 ? "mm" : "oot";

struct PeerInfo {
    int scene = -1;
    int room = -1;
    Vec3f pos = { 0, 0, 0 };
    uint64_t lastMs = 0;
};

struct KeyState {
    uint32_t owner = 0;
    uint32_t epoch = 0;
    bool hasEntry = false;
    Entry last;
    uint64_t lastRecvMs = 0;
    // mirror section (owner's pose + hurtboxes), latest from the owner
    bool hasRec = false;
    bool recFresh = false; // not applied yet
    uint32_t recFrame = 0; // owner's logic frame it was captured on
    Coop::Mirror::Rec rec;
    // draw state (step 8): the owner's instance image, rebuilt from keyframes + XOR deltas
    std::vector<uint8_t> dsImage;
    uint32_t dsFrame = 0;
    bool dsFresh = false;
    nlohmann::json dsRelocs, dsLocal;
    int dsDrawKind = 2; // 0 null, 1 library + dsDrawDelta, 2 keep ours
    uint64_t dsDrawDelta = 0;
    int dsShadowAlpha = -1;
    float dsShadowScale = 0;
};

struct ActorState {
    bool keyed = false;
    bool eligible = false;
    bool setup = false;
    bool boss = false;
    uint64_t key = 0;
    int spawnParams = 0;
    uint64_t parentKey = 0;
    uint64_t firstSeenMs = 0;
    uint64_t orphanSinceMs = 0;
    // HP scaling
    int baseHealth = -1; // native max (vanilla bar); re-seeded when the actor resets its own health (phases)
    int64_t wideHp = 0;  // wide HP pool: the real scaled HP (see CoopEnemySync.h)
    int64_t wideMax = 0;
    double k = 1.0;      // HP factor for the players in THIS actor's room (scene-wide actors: the scene)
    int players = 1;
    // per-frame
    int preHealth = 0;
    bool frozenForDeath = false;
    // combat
    uint64_t lockUntilMs = 0;
    uint64_t lastLocalHitMs = 0;
    int recentLocalDmg = 0;
    int proxFrames = 0;
    uint64_t dyingSinceMs = 0;
    bool child = false; // not an enemy itself: spawned by a synced enemy's update (projectile, egg, effect actor)
    // mirrors
    bool mirror = false;         // owned by a teammate: shown from its snapshots, update skipped
    bool hasRec = false;         // owner: captured after this frame's update
    Coop::Mirror::Rec rec;
    // draw state (owner): the last image sent, for XOR deltas
    std::vector<uint8_t> dsSent;
    uint32_t dsSentFrame = 0;
    uint32_t dsKeyFrame = 0;
    // forwarded hits (owner): the hitter's element / collider the injected hit points at (stable: map node)
    COOP_ELEM_T hitElem{};
    Collider hitCol{};
};

// A hit a teammate's mirror took, to be replayed on the owner's copy (step 3: hits go to the owner).
struct FwdHit {
    uint32_t off = 0;   // collider byte offset in the instance
    int elem = -1;      // element index (jntSph / tris), -1 = the collider's single element
    int damage = 0;     // colChkInfo.damage / damageEffect computed by the mirror's CollisionCheck_Damage
    int damageEffect = 0;
    uint32_t tFlags = 0; // the hitting element's toucher (AT) info
    int tEffect = 0, tDamage = 0, tMm = 0, tMmAt = 0;
    int16_t hitPos[3] = {};
    uint32_t from = 0;
};

static std::unordered_map<Actor*, ActorState> sActors;
static std::unordered_map<uint64_t, Actor*> sByKey;
static std::unordered_map<uint64_t, KeyState> sKeys;
static std::map<uint32_t, PeerInfo> sPeers;
static std::unordered_set<uint64_t> sDead; // setup actors killed while a teammate shared the scene
static std::unordered_map<uint64_t, int> sOrdinals;
static std::vector<nlohmann::json> sEvents;
static std::unordered_map<uint64_t, std::vector<FwdHit>> sPendingHits; // owner: forwarded hits, by key
// An enemy attack that landed on OUR puppet on its owner's screen, replayed into our Link (step 4).
struct PlayerHit {
    uint64_t key = 0;
    uint32_t from = 0; // the enemy's owner
    bool blocked = false;
    uint32_t tFlags = 0;
    int tEffect = 0, tDamage = 0, tMm = 0, tMmAt = 0;
};
static std::vector<PlayerHit> sPendingPlayerHits;
static std::map<uint32_t, uint64_t> sLastRelayMs; // owner: per target clientId
// Sounds (step 5): owner side, every sfx request whose position lives inside an enemy we own.
struct OwnedRange {
    uintptr_t base;
    size_t size;
    uint64_t key;
};
static std::vector<OwnedRange> sOwnedRanges; // sorted by base; rebuilt every Tick
struct SfxEv {
    uint64_t key;
    uint32_t posOff;
    uint16_t sfx;
    uint8_t token;
    int32_t freqOff, volOff, reverbOff; // -1: the default scale / reverb
};
static std::vector<SfxEv> sSfxQueue;
static std::thread::id sGameThread;
static bool sReplayingSfx = false;
static bool sSpawningMirror = false;                       // receiver: spawning an owner's child locally
static std::unordered_map<uint64_t, uint64_t> sChildTried; // receiver: child key -> last spawn attempt
// Particle effects (step 7): EffectSs spawns made while an enemy we own updates.
static constexpr size_t kFxBytes = 0x50; // >= every EffectSs init-params struct in both games (largest 0x44)
static std::vector<nlohmann::json> sFxQueue;
static bool sReplayingFx = false;
static int sFxTraceLeft = 40;
static int sSfxTraceLeft = 40;
static int sScene = -1;
static uint32_t sFrame = 0;
static bool sRemoteKill = false;  // true while WE kill an actor because a teammate's copy died
static bool sTransition = true;   // scene teardown/startup: kills are unloads, never deaths
static Actor* sUpdating = nullptr; // synced actor whose update is running (parent of its spawns)
static double sK = 1.0;  // scene-wide factor (trace only; each actor scales by its own room's count)
static int sPlayers = 1; // players in the scene
static std::vector<int> sPeerRooms; // rooms of the teammates in our scene, refreshed every Tick
static bool sDeadDirty = false;
static bool sRegistered = false;

// ── SkelAnime registry (SkelAnime_Init* hook in each game's z_skelanime.c) ──
// Addresses of every SkelAnime initialized this play session. An actor's skeletons are the registered ones inside its
// instance; entries are dropped when their actor is destroyed and all of them at Play_Destroy (arena reset).
static std::set<uintptr_t> sSkels;
static std::set<std::string> sAnimPaths; // interned animation resource paths written into mirrors

static void SkelRegister(SkelAnime* skel) {
    if (gPlayState != nullptr) // actors only live in a play state (title / file select skeletons are not ours)
        sSkels.insert((uintptr_t)skel);
}

static void SkelForget(uintptr_t base, size_t size) {
    sSkels.erase(sSkels.lower_bound(base), sSkels.lower_bound(base + size));
}

static std::vector<SkelAnime*> ActorSkels(Actor* a, size_t size) {
    std::vector<SkelAnime*> out;
    uintptr_t base = (uintptr_t)a;
    for (auto it = sSkels.lower_bound(base); it != sSkels.end() && *it + sizeof(SkelAnime) <= base + size; ++it)
        out.push_back((SkelAnime*)*it);
    return out;
}

static bool IsResPath(const void* p) {
    return p != nullptr && std::strncmp((const char*)p, "__OTR__", 7) == 0;
}

// Both clients must run the same build to pair (the mirror section copies raw engine structs).
static uint64_t BuildTag() {
    static const uint64_t tag = [] {
        uint64_t h = Coop::Fnv1a64(std::string(__DATE__ " " __TIME__) + kGameName);
        h = HashCombine(h, sizeof(Actor));
        h = HashCombine(h, sizeof(SkelAnime));
        h = HashCombine(h, sizeof(ColliderCylinder));
        h = HashCombine(h, sizeof(ColliderJntSphElement));
        h = HashCombine(h, sizeof(ColliderTrisElement));
        h = HashCombine(h, sizeof(ColliderQuad));
        return HashCombine(h, kWireVersion);
    }();
    return tag;
}

static size_t InstanceSize(Actor* a) {
    size_t n = COOP_INSTANCE_SIZE(a);
    return n >= sizeof(Actor) ? n : sizeof(Actor);
}

// Actor flags a mirror takes from its owner: lock-on allowed (bit 0), hostile (bit 2), lens-only (bit 7).
static constexpr uint32_t kMirrorFlagMask = (1u << 0) | (1u << 2) | (1u << 7);

static uint64_t NowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool Trace() {
    static const bool on = std::getenv("COMBO_DEBUG_COOP_TRACE") != nullptr;
    return on;
}

static bool SyncOn() {
    return CVarGetInteger(kCvarSync, 1) != 0;
}

static bool InPlay() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr;
}

static bool Eligible(Actor* a) {
    return a->category == ACTORCAT_ENEMY || a->category == ACTORCAT_BOSS;
}

static const char* TeamId() {
    return CVarGetString("gRemote.Anchor.TeamId", "default");
}

// Teammates whose last packet put them in our scene.
static int ScenePeerCount(uint64_t now) {
    int n = 0;
    for (auto& [id, p] : sPeers)
        if (p.scene == sScene && now - p.lastMs < kPeerStaleMs)
            ++n;
    return n;
}

static const PeerInfo* ScenePeer(uint32_t id, uint64_t now) {
    auto it = sPeers.find(id);
    if (it == sPeers.end() || it->second.scene != sScene || now - it->second.lastMs >= kPeerStaleMs)
        return nullptr;
    return &it->second;
}

static int EnvFrame(const char* name);

// Step 8 generic instance copy; COMBO_DEBUG_COOP_NODS=1 (env, harness) turns it off to compare.
static bool DsOn() {
    static const bool off = std::getenv("COMBO_DEBUG_COOP_NODS") != nullptr;
    return !off;
}

// COMBO_DEBUG_COOP_FAKEROOM=<frame>,<room> (env at launch only): from that frame on we report and act as if we were in
// <room>, the way a real room change looks to teammates (harness for the "owner left the room" hand-off).
static int MyRoom() {
    static const int fakeAt = EnvFrame("COMBO_DEBUG_COOP_FAKEROOM");
    static const int fakeRoom = [] {
        const char* v = std::getenv("COMBO_DEBUG_COOP_FAKEROOM");
        const char* c = v ? std::strchr(v, ',') : nullptr;
        return c ? std::atoi(c + 1) : 0;
    }();
    if (fakeAt >= 0 && (int)sFrame >= fakeAt)
        return fakeRoom;
    return gPlayState->roomCtx.curRoom.num;
}

// 0 = we have the lowest clientId of everyone in the scene (the scene authority).
static int Rank(uint64_t now) {
    uint32_t me = CoopG_OwnClientId();
    int r = 0;
    for (auto& [id, p] : sPeers)
        if (id < me && p.scene == sScene && now - p.lastMs < kPeerStaleMs)
            ++r;
    return r;
}

// Same, among the players in an actor's room (room < 0: the scene).
static int RoomRank(int actorRoom, uint64_t now) {
    uint32_t me = CoopG_OwnClientId();
    int r = 0;
    for (auto& [id, p] : sPeers)
        if (id < me && p.scene == sScene && now - p.lastMs < kPeerStaleMs && InActorRoom(actorRoom, p.room))
            ++r;
    return r;
}

// Rooms of the teammates in our scene (for the per-actor player count).
static std::vector<int> ScenePeerRooms(uint64_t now) {
    std::vector<int> rooms;
    for (auto& [id, p] : sPeers)
        if (p.scene == sScene && now - p.lastMs < kPeerStaleMs)
            rooms.push_back(p.room);
    return rooms;
}

static float DistTo(Actor* a, const Vec3f& p) {
    float dx = a->world.pos.x - p.x, dy = a->world.pos.y - p.y, dz = a->world.pos.z - p.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void ResetScene() {
    sActors.clear();
    sByKey.clear();
    sKeys.clear();
    sDead.clear();
    sOrdinals.clear();
    sEvents.clear();
    sPendingHits.clear();
    sPendingPlayerHits.clear();
    sChildTried.clear();
    sUpdating = nullptr;
    sDeadDirty = false;
}

static void RemoteKill(Actor* a) {
    if (a->update == nullptr)
        return;
    sRemoteKill = true;
    Actor_Kill(a);
    sRemoteKill = false;
}

// ── HP scaling: wide pool ──
static double ActorFactor(int players) {
    return CVarGetInteger(kCvarHpScale, 1) ? PlayerFactor(players, CVarGetInteger(kCvarHpPercent, 100)) : 1.0;
}

static bool HasPool(const ActorState& st) {
    return st.baseHealth > 0 && st.wideMax > 0;
}

static void WriteNative(Actor* a, const ActorState& st) {
    a->colChkInfo.health = (u8)NativeFromWide(st.wideHp, st.wideMax, st.baseHealth);
}

static void SeedPool(Actor* a, ActorState& st, const char* why) {
    st.baseHealth = a->colChkInfo.health;
    st.wideMax = WideMax(st.baseHealth, st.k);
    st.wideHp = st.wideMax;
    if (Trace() && st.baseHealth > 0)
        SPDLOG_INFO("[CoopEnemy] {} hp pool actor 0x{:X} key {} room {} native {} -> pool {}/{} (room players {}, k "
                    "{:.2f}, {})",
                    kGameName, (int)a->id, Coop::HexU64(st.key), (int)a->room, st.baseHealth, st.wideHp, st.wideMax,
                    st.players, st.k, why);
}

static void RescalePool(Actor* a, ActorState& st) {
    if (!HasPool(st))
        return;
    int64_t newMax = WideMax(st.baseHealth, st.k);
    int64_t before = st.wideHp;
    st.wideHp = RescaleWide(st.wideHp, st.wideMax, newMax);
    st.wideMax = newMax;
    WriteNative(a, st);
    if (Trace())
        SPDLOG_INFO("[CoopEnemy] {} hp pool actor 0x{:X} key {} room {} rescaled {} -> {}/{} (room players {}, k "
                    "{:.2f})",
                    kGameName, (int)a->id, Coop::HexU64(st.key), (int)a->room, before, st.wideHp, st.wideMax,
                    st.players, st.k);
}

// Pre-update, a hit of `dmg` is about to be subtracted from the native field by the actor's own code
// (Actor_ApplyDamage or `health -= damage`). Set native so that after the subtraction it lands on the pool's
// new fraction; if the pool empties, let the subtraction reach 0 so the actor's real death runs. Constant
// subtracters (Volvagia/Phantom Ganon/Ganon -= 2) are covered by padding at least 2.
static void PrePadHit(Actor* a, ActorState& st, int dmg) {
    if (!HasPool(st) || dmg <= 0)
        return;
    int64_t after = st.wideHp - dmg;
    if (after > 0) {
        int native = NativeFromWide(after, st.wideMax, st.baseHealth);
        a->colChkInfo.health = (u8)std::min(255, native + std::max(dmg, 2));
    } else {
        a->colChkInfo.health = (u8)std::min<int>(a->colChkInfo.health, dmg);
    }
}


// ── mirror section: capture (owner, after the actor's update) ──
namespace M = Coop::Mirror;
static constexpr uint8_t kAcHit = 1 << 1, kAcBounced = 1 << 7, kOc1Hit = 1 << 1, kOc2HitPlayer = 1 << 0;
static constexpr uint8_t kElemHit = 1 << 1;

static void FillElem(M::Elem& e, const COOP_ELEM_T& el) {
    e.acElemFlags = COOP_EL_ACFLAGS(el) & ~kElemHit;
    e.ocElemFlags = el.ocElemFlags & ~kElemHit;
    e.acDmgFlags = COOP_EL_ACDMG(el);
}

static size_t ColliderBytes(uint8_t shape) {
    switch (shape) {
        case M::kJntSph: return sizeof(ColliderJntSph);
        case M::kCylinder: return sizeof(ColliderCylinder);
        case M::kTris: return sizeof(ColliderTris);
        case M::kQuad: return sizeof(ColliderQuad);
#if COOP_HAS_SPHERE
        case M::kSphere: return sizeof(ColliderSphere);
#endif
        default: return 0;
    }
}

static bool CaptureCol(Collider* col, M::Col& c) {
    c.shape = col->shape;
    c.acFlags = col->acFlags & ~(kAcHit | kAcBounced);
    c.ocFlags1 = col->ocFlags1 & ~kOc1Hit;
    c.ocFlags2 = col->ocFlags2 & ~kOc2HitPlayer;
    switch (col->shape) {
        case M::kCylinder: {
            auto* cy = (ColliderCylinder*)col;
            c.g = { (float)cy->dim.radius, (float)cy->dim.height, (float)cy->dim.yShift,
                    (float)cy->dim.pos.x,  (float)cy->dim.pos.y,  (float)cy->dim.pos.z };
            FillElem(c.single, COOP_CYL_EL(cy));
            return true;
        }
        case M::kJntSph: {
            auto* js = (ColliderJntSph*)col;
            if (js->elements == nullptr || js->count < 0 || js->count > (s32)M::kMaxElems)
                return false;
            for (s32 i = 0; i < js->count; ++i) {
                ColliderJntSphElement* el = &js->elements[i];
                M::Elem e;
                const Sphere16& w = el->dim.worldSphere;
                e.g = { (float)w.center.x, (float)w.center.y, (float)w.center.z, (float)w.radius };
                FillElem(e, COOP_JNT_EL(el));
                c.elems.push_back(e);
            }
            return true;
        }
        case M::kTris: {
            auto* tr = (ColliderTris*)col;
            if (tr->elements == nullptr || tr->count < 0 || tr->count > (s32)M::kMaxElems)
                return false;
            for (s32 i = 0; i < tr->count; ++i) {
                ColliderTrisElement* el = &tr->elements[i];
                M::Elem e;
                for (int v = 0; v < 3; ++v) {
                    e.g.push_back(el->dim.vtx[v].x);
                    e.g.push_back(el->dim.vtx[v].y);
                    e.g.push_back(el->dim.vtx[v].z);
                }
                FillElem(e, COOP_TRIS_EL(el));
                c.elems.push_back(e);
            }
            return true;
        }
        case M::kQuad: {
            auto* q = (ColliderQuad*)col;
            for (int v = 0; v < 4; ++v) {
                c.g.push_back(q->dim.quad[v].x);
                c.g.push_back(q->dim.quad[v].y);
                c.g.push_back(q->dim.quad[v].z);
            }
            FillElem(c.single, COOP_QUAD_EL(q));
            return true;
        }
#if COOP_HAS_SPHERE
        case M::kSphere: {
            auto* sp = (ColliderSphere*)col;
            const Sphere16& w = sp->dim.worldSphere;
            c.g = { (float)w.center.x, (float)w.center.y, (float)w.center.z, (float)w.radius };
            FillElem(c.single, sp->elem);
            return true;
        }
#endif
        default: return false;
    }
}

static void CaptureRec(Actor* a, ActorState& st) {
    M::Rec& r = st.rec;
    r = M::Rec{};
    r.key = st.key;
    r.focus[0] = a->focus.pos.x;
    r.focus[1] = a->focus.pos.y;
    r.focus[2] = a->focus.pos.z;
    r.focusRot[0] = a->focus.rot.x;
    r.focusRot[1] = a->focus.rot.y;
    r.focusRot[2] = a->focus.rot.z;
    r.scale[0] = a->scale.x;
    r.scale[1] = a->scale.y;
    r.scale[2] = a->scale.z;
    r.yOffset = a->shape.yOffset;
    r.flags = a->flags & kMirrorFlagMask;
    if (!st.setup && st.parentKey != 0) {
        r.parentKey = st.parentKey;
        r.params = (int16_t)st.spawnParams;
    }
    const size_t size = InstanceSize(a);
    for (SkelAnime* sk : ActorSkels(a, size)) {
        if (r.skels.size() >= M::kMaxSkels)
            break;
        M::Skel k;
        k.off = (uint32_t)((uintptr_t)sk - (uintptr_t)a);
        if (IsResPath(sk->animation))
            k.anim = (const char*)sk->animation;
        k.curFrame = sk->curFrame;
        k.playSpeed = sk->playSpeed;
        k.startFrame = sk->startFrame;
        k.endFrame = sk->endFrame;
        k.morphWeight = sk->morphWeight;
        k.animLength = sk->animLength;
        k.mode = sk->mode;
        if (sk->jointTable != nullptr && sk->limbCount > 0 && sk->limbCount <= M::kMaxLimbs)
            k.joints.assign((const int16_t*)sk->jointTable, (const int16_t*)sk->jointTable + sk->limbCount * 3);
        r.skels.push_back(std::move(k));
    }
    // The colliders this update registered for hits taken (AC) and body pushes (OC). AT is the owner's alone.
    std::map<uint32_t, M::Col> cols;
    auto scan = [&](Collider** list, s32 count, uint8_t bit) {
        for (s32 i = 0; i < count; ++i) {
            Collider* col = list[i];
            uintptr_t off = (uintptr_t)col - (uintptr_t)a;
            if (col == nullptr || (uintptr_t)col < (uintptr_t)a || !M::FitsInstance((uint32_t)off, ColliderBytes(col->shape), size) ||
                ColliderBytes(col->shape) == 0)
                continue;
            auto it = cols.find((uint32_t)off);
            if (it == cols.end()) {
                M::Col c;
                c.off = (uint32_t)off;
                if (!CaptureCol(col, c))
                    continue;
                it = cols.emplace((uint32_t)off, std::move(c)).first;
            }
            it->second.lists |= bit;
        }
    };
    CollisionCheckContext* cc = &gPlayState->colChkCtx;
    scan(cc->colAC, std::min<s32>(cc->colACCount, (s32)(sizeof(cc->colAC) / sizeof(cc->colAC[0]))), M::kInAC);
    scan(cc->colOC, std::min<s32>(cc->colOCCount, (s32)(sizeof(cc->colOC) / sizeof(cc->colOC[0]))), M::kInOC);
    for (auto& [off, c] : cols)
        if (r.cols.size() < M::kMaxCols)
            r.cols.push_back(std::move(c));
    st.hasRec = true;
}

// ── mirror section: apply (receiver, every logic frame before the enemies update) ──
static void ApplyElem(COOP_ELEM_T& el, const M::Elem& e) {
    COOP_EL_ACFLAGS(el) = (COOP_EL_ACFLAGS(el) & kElemHit) | (e.acElemFlags & ~kElemHit);
    el.ocElemFlags = (el.ocElemFlags & kElemHit) | (e.ocElemFlags & ~kElemHit);
    COOP_EL_ACDMG(el) = e.acDmgFlags;
}

static bool ApplyCol(Collider* col, const M::Col& c) {
    if (col->shape != c.shape)
        return false;
    col->acFlags = (col->acFlags & (kAcHit | kAcBounced)) | (c.acFlags & ~(kAcHit | kAcBounced));
    col->ocFlags1 = (col->ocFlags1 & kOc1Hit) | (c.ocFlags1 & ~kOc1Hit);
    col->ocFlags2 = (col->ocFlags2 & kOc2HitPlayer) | (c.ocFlags2 & ~kOc2HitPlayer);
    switch (c.shape) {
        case M::kCylinder: {
            auto* cy = (ColliderCylinder*)col;
            cy->dim.radius = (s16)c.g[0];
            cy->dim.height = (s16)c.g[1];
            cy->dim.yShift = (s16)c.g[2];
            cy->dim.pos.x = (s16)c.g[3];
            cy->dim.pos.y = (s16)c.g[4];
            cy->dim.pos.z = (s16)c.g[5];
            ApplyElem(COOP_CYL_EL(cy), c.single);
            return true;
        }
        case M::kJntSph: {
            auto* js = (ColliderJntSph*)col;
            if (js->elements == nullptr || js->count != (s32)c.elems.size())
                return false;
            for (s32 i = 0; i < js->count; ++i) {
                ColliderJntSphElement* el = &js->elements[i];
                const M::Elem& e = c.elems[i];
                el->dim.worldSphere.center.x = (s16)e.g[0];
                el->dim.worldSphere.center.y = (s16)e.g[1];
                el->dim.worldSphere.center.z = (s16)e.g[2];
                el->dim.worldSphere.radius = (s16)e.g[3];
                ApplyElem(COOP_JNT_EL(el), e);
            }
            return true;
        }
        case M::kTris: {
            auto* tr = (ColliderTris*)col;
            if (tr->elements == nullptr || tr->count != (s32)c.elems.size())
                return false;
            for (s32 i = 0; i < tr->count; ++i) {
                const M::Elem& e = c.elems[i];
                Vec3f v0 = { e.g[0], e.g[1], e.g[2] }, v1 = { e.g[3], e.g[4], e.g[5] }, v2 = { e.g[6], e.g[7], e.g[8] };
                Collider_SetTrisVertices(tr, i, &v0, &v1, &v2);
                ApplyElem(COOP_TRIS_EL(&tr->elements[i]), e);
            }
            return true;
        }
        case M::kQuad: {
            auto* q = (ColliderQuad*)col;
            Vec3f v[4];
            for (int i = 0; i < 4; ++i)
                v[i] = { c.g[i * 3], c.g[i * 3 + 1], c.g[i * 3 + 2] };
            Collider_SetQuadVertices(q, &v[0], &v[1], &v[2], &v[3]);
            ApplyElem(COOP_QUAD_EL(q), c.single);
            return true;
        }
#if COOP_HAS_SPHERE
        case M::kSphere: {
            auto* sp = (ColliderSphere*)col;
            sp->dim.worldSphere.center.x = (s16)c.g[0];
            sp->dim.worldSphere.center.y = (s16)c.g[1];
            sp->dim.worldSphere.center.z = (s16)c.g[2];
            sp->dim.worldSphere.radius = (s16)c.g[3];
            ApplyElem(sp->elem, c.single);
            return true;
        }
#endif
        default: return false;
    }
}

static const char* InternPath(const std::string& path) {
    return sAnimPaths.insert(path).first->c_str();
}

static const char* PathTail(const std::string& path) {
    size_t at = path.rfind('/');
    return path.c_str() + (at == std::string::npos ? 0 : at + 1);
}

static void ApplyRec(Actor* a, ActorState& st, KeyState& ks) {
    const M::Rec& r = ks.rec;
    a->focus.pos = { r.focus[0], r.focus[1], r.focus[2] };
    a->focus.rot.x = r.focusRot[0];
    a->focus.rot.y = r.focusRot[1];
    a->focus.rot.z = r.focusRot[2];
    a->scale = { r.scale[0], r.scale[1], r.scale[2] };
    a->shape.yOffset = r.yOffset;
    a->flags = (a->flags & ~kMirrorFlagMask) | (r.flags & kMirrorFlagMask);
    const size_t size = InstanceSize(a);
    int posed = 0;
    for (const M::Skel& k : r.skels) {
        uintptr_t addr = (uintptr_t)a + k.off;
        if (!M::FitsInstance(k.off, sizeof(SkelAnime), size) || !sSkels.count(addr))
            continue;
        SkelAnime* sk = (SkelAnime*)addr;
        if (sk->jointTable == nullptr || (size_t)sk->limbCount * 3 != k.joints.size())
            continue;
        if (!k.anim.empty()) {
            bool same = IsResPath(sk->animation) && k.anim == (const char*)sk->animation;
            if (!same || sk->mode != k.mode)
                Animation_Change(sk, (AnimationHeader*)InternPath(k.anim), k.playSpeed, k.startFrame, k.endFrame,
                                 k.mode, 0.0f);
        }
        sk->curFrame = k.curFrame;
        sk->playSpeed = k.playSpeed;
        sk->startFrame = k.startFrame;
        sk->endFrame = k.endFrame;
        sk->morphWeight = k.morphWeight;
        sk->animLength = k.animLength;
        std::memcpy(sk->jointTable, k.joints.data(), k.joints.size() * sizeof(int16_t));
        ++posed;
    }
    int regAC = 0, regOC = 0;
    for (const M::Col& c : r.cols) {
        size_t bytes = ColliderBytes(c.shape);
        if (bytes == 0 || !M::FitsInstance(c.off, bytes, size))
            continue;
        Collider* col = (Collider*)((uintptr_t)a + c.off);
        if (col->actor != a || !ApplyCol(col, c))
            continue;
        if (c.lists & M::kInAC) {
            CollisionCheck_SetAC(gPlayState, &gPlayState->colChkCtx, col);
            ++regAC;
        }
        if (c.lists & M::kInOC) {
            CollisionCheck_SetOC(gPlayState, &gPlayState->colChkCtx, col);
            ++regOC;
        }
    }
    if (Trace() && ks.recFresh && (ks.recFrame % 60) == 0) {
        const M::Skel* k0 = r.skels.empty() ? nullptr : &r.skels[0];
        SPDLOG_INFO("[CoopEnemy] {} mirror apply f={} key {} actor 0x{:X} update skipped, skels {}/{} anim {} frame "
                    "{:.2f} j0 ({},{},{}) cols AC {} OC {}",
                    kGameName, ks.recFrame, Coop::HexU64(st.key), (int)a->id, posed, r.skels.size(),
                    k0 ? PathTail(k0->anim) : "-", k0 ? k0->curFrame : 0.0f,
                    k0 && k0->joints.size() >= 3 ? k0->joints[0] : 0, k0 && k0->joints.size() >= 3 ? k0->joints[1] : 0,
                    k0 && k0->joints.size() >= 3 ? k0->joints[2] : 0, regAC, regOC);
    }
    ks.recFresh = false;
}

// ── hits on a mirror go to the owner ──
static COOP_ELEM_T* ColElem(Collider* col, int idx) {
    switch (col->shape) {
        case M::kCylinder: return idx < 0 ? &COOP_CYL_EL((ColliderCylinder*)col) : nullptr;
        case M::kQuad: return idx < 0 ? &COOP_QUAD_EL((ColliderQuad*)col) : nullptr;
#if COOP_HAS_SPHERE
        case M::kSphere: return idx < 0 ? &((ColliderSphere*)col)->elem : nullptr;
#endif
        case M::kJntSph: {
            auto* js = (ColliderJntSph*)col;
            return idx >= 0 && idx < js->count && js->elements ? &COOP_JNT_EL(&js->elements[idx]) : nullptr;
        }
        case M::kTris: {
            auto* tr = (ColliderTris*)col;
            return idx >= 0 && idx < tr->count && tr->elements ? &COOP_TRIS_EL(&tr->elements[idx]) : nullptr;
        }
        default: return nullptr;
    }
}

static int ElemCount(Collider* col) {
    if (col->shape == M::kJntSph)
        return ((ColliderJntSph*)col)->count;
    if (col->shape == M::kTris)
        return ((ColliderTris*)col)->count;
    return 0;
}

// Receiver, before the mirror is re-posed: the hurtboxes we registered last frame may have been hit by our Link (or
// our arrows, bombs...). Collision already played the local hit spark / sword sound and computed colChkInfo.damage
// with the actor's own damage table. Send the hit to the owner and clear it here: the mirror itself never reacts.
static void ForwardHits(Actor* a, ActorState& st, const KeyState& ks) {
    const size_t size = InstanceSize(a);
    bool sent = false;
    for (const M::Col& c : ks.rec.cols) {
        size_t bytes = ColliderBytes(c.shape);
        if (!(c.lists & M::kInAC) || bytes == 0 || !M::FitsInstance(c.off, bytes, size))
            continue;
        Collider* col = (Collider*)((uintptr_t)a + c.off);
        if (col->actor != a || col->shape != c.shape || !(col->acFlags & kAcHit))
            continue;
        int n = ElemCount(col);
        for (int i = (n > 0 ? 0 : -1); i < (n > 0 ? n : 0); ++i) {
            COOP_ELEM_T* el = ColElem(col, i);
            if (el == nullptr || !(COOP_EL_ACFLAGS(*el) & kElemHit))
                continue;
            COOP_EL_ACFLAGS(*el) &= ~kElemHit;
            if (sent)
                continue; // one hit per actor per frame, like one CollisionCheck_Damage result
            nlohmann::json ev = { { "k", Coop::HexU64(st.key) }, { "e", "hit" }, { "c", c.off }, { "i", i },
                                  { "d", (int)a->colChkInfo.damage }, { "de", (int)a->colChkInfo.damageEffect } };
            if (auto* at = COOP_EL_ACHITEL(*el)) {
                ev["tf"] = COOP_EL_AT(*at).dmgFlags;
                ev["te"] = (int)COOP_EL_AT(*at).effect;
                ev["td"] = (int)COOP_EL_AT(*at).damage;
#if COOP_COMBO_TOUCH
                ev["tm"] = (int)COOP_EL_AT(*at).comboMmDmg;
                ev["ta"] = (int)COOP_EL_AT(*at).comboMmAtDamage;
#endif
            }
            const Vec3s& hp = COOP_EL_ACHITPOS(*el);
            ev["hp"] = { hp.x, hp.y, hp.z };
            sEvents.push_back(ev);
            sent = true;
            if (Trace())
                SPDLOG_INFO("[CoopEnemy] {} mirror hit -> owner {}: key {} actor 0x{:X} col +0x{:X} elem {} dmg {} effect {} "
                            "toucher 0x{:08X}",
                            kGameName, ks.owner, Coop::HexU64(st.key), (int)a->id, c.off, i,
                            (int)a->colChkInfo.damage, (int)a->colChkInfo.damageEffect,
                            ev.value("tf", (uint32_t)0));
        }
        col->acFlags &= ~(kAcHit | kAcBounced);
    }
    if (sent) {
        a->colChkInfo.damage = 0;
        a->colChkInfo.damageEffect = 0;
    }
}

// Owner, before the actor's update: replay a forwarded hit exactly where collision would have left it, so the actor's
// own code reacts to it as to a local hit (hit reaction, sounds, damage through the HP pool, death).
static bool InjectHit(Actor* a, ActorState& st, const FwdHit& h) {
    size_t bytes = 0;
    const size_t size = InstanceSize(a);
    Collider* col = (Collider*)((uintptr_t)a + h.off);
    if (h.off + sizeof(Collider) > size || (bytes = ColliderBytes(col->shape)) == 0 ||
        !M::FitsInstance(h.off, bytes, size) || col->actor != a)
        return true; // unusable: drop
    if (col->acFlags & kAcHit)
        return false; // a local hit landed this frame: replay ours next frame
    COOP_ELEM_T* el = ColElem(col, h.elem);
    if (el == nullptr)
        return true;
    Actor* hitter = CoopG_Puppet(h.from);
    if (hitter == nullptr)
        hitter = &GET_PLAYER(gPlayState)->actor;
    st.hitElem = COOP_ELEM_T{};
    COOP_EL_AT(st.hitElem).dmgFlags = h.tFlags;
    COOP_EL_AT(st.hitElem).effect = (u8)h.tEffect;
    COOP_EL_AT(st.hitElem).damage = (u8)h.tDamage;
#if COOP_COMBO_TOUCH
    COOP_EL_AT(st.hitElem).comboMmDmg = (u8)h.tMm;
    COOP_EL_AT(st.hitElem).comboMmAtDamage = (u8)h.tMmAt;
#endif
    st.hitCol = Collider{};
    st.hitCol.actor = hitter;
    st.hitCol.atFlags = 1 | (1 << 3); // AT_ON | AT_TYPE_PLAYER
    st.hitCol.shape = M::kQuad;
    col->acFlags |= kAcHit;
    col->ac = hitter;
    COOP_EL_ACFLAGS(*el) |= kElemHit;
    el->acHit = &st.hitCol;
    COOP_EL_ACHITEL(*el) = &st.hitElem;
    COOP_EL_ACHITPOS(*el) = { h.hitPos[0], h.hitPos[1], h.hitPos[2] };
    a->colChkInfo.damage = (u8)h.damage;
    a->colChkInfo.damageEffect = (u8)h.damageEffect;
    a->colChkInfo.acHitEffect = (u8)h.tEffect; // CollisionCheck_SetATvsAC copies the toucher's effect here
    st.lockUntilMs = NowMs() + kClaimLockMs * (st.boss ? 2u : 1u); // mid-fight: no proximity hand-off
    if (Trace())
        SPDLOG_INFO("[CoopEnemy] {} owner applies forwarded hit from client {}: key {} actor 0x{:X} col +0x{:X} elem {} "
                    "dmg {} effect {} toucher 0x{:08X} native hp {}",
                    kGameName, h.from, Coop::HexU64(st.key), (int)a->id, h.off, h.elem, h.damage, h.damageEffect,
                    h.tFlags, (int)a->colChkInfo.health);
    return true;
}

// ── enemy attacks hit players only on the enemy's owner (step 4) ──
static constexpr uint32_t kPS1Dead = 1u << 7, kPS1Shielding = 1u << 22, kPS1Damaged = 1u << 26; // same bits in both
static constexpr uint8_t kAcTypeAll = (1 << 3) | (1 << 4) | (1 << 5), kAcTypeEnemy = 1 << 4;

// The synced actor (that we own) behind an attack: the enemy itself, or (step 6) a child it spawned.
static const ActorState* OwnedAttacker(Actor* at) {
    if (at == nullptr)
        return nullptr;
    auto it = sActors.find(at);
    if (it == sActors.end() || !it->second.eligible)
        return nullptr;
    auto ks = sKeys.find(it->second.key);
    if (ks == sKeys.end() || ks->second.owner != CoopG_OwnClientId())
        return nullptr;
    return &it->second;
}

static void RelayPlayerHit(uint32_t target, Actor* attacker, COOP_ELEM_T* atEl, bool blocked) {
    const ActorState* st = OwnedAttacker(attacker);
    if (st == nullptr || atEl == nullptr)
        return; // hits from actors nobody syncs (or someone else owns) are not ours to give
    const uint64_t now = NowMs();
    uint64_t& last = sLastRelayMs[target];
    if (now - last < 250) // the puppet shows the target's invincibility a round trip late: one hit per swing
        return;
    last = now;
    nlohmann::json p;
    p["type"] = Coop::kPktActorPlayerHit;
    p["game"] = kGameName;
    p["scene"] = sScene;
    p["targetTeamId"] = TeamId();
    p["t"] = target;
    p["k"] = Coop::HexU64(st->key);
    p["b"] = blocked;
    p["tf"] = COOP_EL_AT(*atEl).dmgFlags;
    p["te"] = (int)COOP_EL_AT(*atEl).effect;
    p["td"] = (int)COOP_EL_AT(*atEl).damage;
#if COOP_COMBO_TOUCH
    p["tm"] = (int)COOP_EL_AT(*atEl).comboMmDmg;
    p["ta"] = (int)COOP_EL_AT(*atEl).comboMmAtDamage;
#endif
    CoopG_Send(p);
    if (Trace())
        SPDLOG_INFO("[CoopEnemy] {} owner: actor 0x{:X} key {} {} teammate {}'s puppet, relayed (frame {}, toucher "
                    "0x{:08X} dmg {} effect {})",
                    kGameName, (int)attacker->id, Coop::HexU64(st->key), blocked ? "was BLOCKED by" : "hit", target, sFrame,
                    (uint32_t)COOP_EL_AT(*atEl).dmgFlags, (int)COOP_EL_AT(*atEl).damage, (int)COOP_EL_AT(*atEl).effect);
}

// Every frame from DummyPlayer_Update while PvP is off for this teammate.
static void PuppetCoop(Player* pl, uint32_t clientId) {
    Collider* cyl = &pl->cylinder.base;
    COOP_ELEM_T& cel = COOP_CYL_EL(&pl->cylinder);
    Collider* sq = &pl->shieldQuad.base;
    COOP_ELEM_T& sel = COOP_QUAD_EL(&pl->shieldQuad);
    const bool active = InPlay() && !sTransition && SyncOn() && CoopG_Connected();
    if (active) {
        if (sq->acFlags & (kAcHit | kAcBounced))
            RelayPlayerHit(clientId, sq->ac, COOP_EL_ACHITEL(sel), true);
        else if (cyl->acFlags & kAcHit)
            RelayPlayerHit(clientId, cyl->ac, COOP_EL_ACHITEL(cel), false);
    }
    cyl->acFlags &= ~(kAcHit | kAcBounced);
    COOP_EL_ACFLAGS(cel) &= ~kElemHit;
    sq->acFlags &= ~(kAcHit | kAcBounced);
    COOP_EL_ACFLAGS(sel) &= ~kElemHit;
    // Takes enemy-aligned attacks only (never a teammate's sword).
    cyl->acFlags = (cyl->acFlags & ~kAcTypeAll) | kAcTypeEnemy;
    if (!active)
        return;
    Collider_UpdateCylinder(&pl->actor, &pl->cylinder);
    // Like Player_UpdateCommon: hurtable unless dead / already reeling / invincible (the target's own timer, mirrored by
    // PLAYER_UPDATE). A raised shield (stateFlags1 mirrored too) is registered by the puppet's own draw
    // (Player_UpdateShieldCollider, AC_HARD): the enemy's attack bounces off it and we relay "blocked".
    if (!(pl->stateFlags1 & (kPS1Dead | kPS1Damaged)) && pl->invincibilityTimer <= 0)
        CollisionCheck_SetAC(gPlayState, &gPlayState->colChkCtx, cyl);
}

// Target client, before our Link's update: put the hit where collision would have, so vanilla player code applies the
// damage, knockback, elemental effect and invincibility (or the shield recoil when blocked).
static bool InjectPlayerHit(Player* pl, const PlayerHit& h) {
    static COOP_ELEM_T sElem;
    static Collider sCol;
    Actor* attacker = nullptr;
    if (auto it = sByKey.find(h.key); it != sByKey.end() && it->second->update != nullptr)
        attacker = it->second; // our mirror of the enemy: knockback pushes away from where we see it
    if (attacker == nullptr)
        attacker = CoopG_Puppet(h.from);
    if (attacker == nullptr)
        return true; // nothing to be hit by on our side (not loaded here): drop
    Collider* cyl = &pl->cylinder.base;
    COOP_ELEM_T& cel = COOP_CYL_EL(&pl->cylinder);
    if (cyl->acFlags & kAcHit)
        return false; // something local hit us this frame: replay next frame
    sElem = COOP_ELEM_T{};
    COOP_EL_AT(sElem).dmgFlags = h.tFlags;
    COOP_EL_AT(sElem).effect = (u8)h.tEffect;
    COOP_EL_AT(sElem).damage = (u8)h.tDamage;
#if COOP_COMBO_TOUCH
    COOP_EL_AT(sElem).comboMmDmg = (u8)h.tMm;
    COOP_EL_AT(sElem).comboMmAtDamage = (u8)h.tMmAt;
#endif
    sCol = Collider{};
    sCol.actor = attacker;
    sCol.atFlags = 1 | kAcTypeEnemy; // AT_ON | AT_TYPE_ENEMY
    sCol.shape = M::kQuad;
    const int hpBefore = COOP_HEALTH;
    if (h.blocked) {
        Collider* sq = &pl->shieldQuad.base;
        COOP_ELEM_T& sel = COOP_QUAD_EL(&pl->shieldQuad);
        sq->acFlags |= kAcHit | kAcBounced;
        sq->ac = attacker;
        COOP_EL_ACFLAGS(sel) |= kElemHit;
        sel.acHit = &sCol;
        COOP_EL_ACHITEL(sel) = &sElem;
    } else {
        cyl->acFlags |= kAcHit;
        cyl->ac = attacker;
        COOP_EL_ACFLAGS(cel) |= kElemHit;
        cel.acHit = &sCol;
        COOP_EL_ACHITEL(cel) = &sElem;
        // CollisionCheck_SetATvsAC: the toucher's effect picks the player's reaction (MM: 2 fire, 3 ice, 4/7 shock...).
        pl->actor.colChkInfo.acHitEffect = (u8)h.tEffect;
        CollisionCheck_ApplyDamage(gPlayState, &gPlayState->colChkCtx, cyl, &cel); // table-less: toucher - defense
    }
    if (Trace())
        SPDLOG_INFO("[CoopEnemy] {} player hit relay applied: {} by key {} (owner {}) attacker 0x{:X} toucher 0x{:08X} "
                    "effect {} -> colChk damage {} health {}",
                    kGameName, h.blocked ? "BLOCKED" : "hit", Coop::HexU64(h.key), h.from, (int)attacker->id, h.tFlags,
                    h.tEffect, (int)pl->actor.colChkInfo.damage, hpBefore);
    return true;
}

// ── enemy sounds on every screen (step 5) ──
static const OwnedRange* OwnedRangeOf(const void* p, size_t bytes) {
    uintptr_t a = (uintptr_t)p;
    auto it = std::upper_bound(sOwnedRanges.begin(), sOwnedRanges.end(), a,
                               [](uintptr_t v, const OwnedRange& r) { return v < r.base; });
    if (it == sOwnedRanges.begin())
        return nullptr;
    --it;
    return a + bytes <= it->base + it->size ? &*it : nullptr;
}

static void RebuildOwnedRanges(bool active) {
    sOwnedRanges.clear();
    if (!active)
        return;
    const uint32_t me = CoopG_OwnClientId();
    for (auto& [a, st] : sActors) {
        if (!st.eligible || a->update == nullptr)
            continue;
        auto ks = sKeys.find(st.key);
        if (ks != sKeys.end() && ks->second.owner == me)
            sOwnedRanges.push_back({ (uintptr_t)a, InstanceSize(a), st.key });
    }
    std::sort(sOwnedRanges.begin(), sOwnedRanges.end(),
              [](const OwnedRange& x, const OwnedRange& y) { return x.base < y.base; });
}

// Entry of every sfx request (both games' lowest sfx function). Cheap unless the position is inside an enemy we own.
static void OnSfx(u16 sfxId, Vec3f* pos, u8 token, f32* freqScale, f32* vol, s8* reverbAdd) {
    if (sReplayingSfx || sOwnedRanges.empty() || pos == nullptr || std::this_thread::get_id() != sGameThread)
        return;
    const OwnedRange* r = OwnedRangeOf(pos, sizeof(Vec3f));
    if (r == nullptr || sSfxQueue.size() >= 64)
        return;
    auto offIn = [&](const void* q, size_t n) -> int32_t {
        uintptr_t v = (uintptr_t)q;
        return q != nullptr && v >= r->base && v + n <= r->base + r->size ? (int32_t)(v - r->base) : -1;
    };
    sSfxQueue.push_back({ r->key, (uint32_t)((uintptr_t)pos - r->base), sfxId, token, offIn(freqScale, sizeof(f32)),
                          offIn(vol, sizeof(f32)), offIn(reverbAdd, sizeof(s8)) });
}

// Receiver: the owner's enemy made a sound: play it at the same place inside our mirror (the audio thread follows that
// position like the owner's does).
static void ReplaySfx(const nlohmann::json& list) {
    for (auto& e : list) {
        if (!e.is_array() || e.size() != 7)
            continue;
        uint64_t key = Coop::ParseHexU64(e[0].get<std::string>());
        auto a = sByKey.find(key);
        if (a == sByKey.end())
            continue;
        auto st = sActors.find(a->second);
        if (st == sActors.end() || !st->second.mirror)
            continue; // we run this actor ourselves: its own code plays its sounds
        Actor* m = a->second;
        const size_t size = InstanceSize(m);
        uint32_t off = e[1].get<uint32_t>();
        if (off + sizeof(Vec3f) > size)
            continue;
        auto ptr = [&](int32_t o, size_t n, void* def) -> void* {
            return o >= 0 && (size_t)o + n <= size ? (void*)((uintptr_t)m + o) : def;
        };
        uint16_t sfx = (uint16_t)e[2].get<int>();
        sReplayingSfx = true;
        COOP_PLAY_SFX(sfx, (Vec3f*)((uintptr_t)m + off), (u8)e[3].get<int>(),
                      (f32*)ptr(e[4].get<int32_t>(), sizeof(f32), &gSfxDefaultFreqAndVolScale),
                      (f32*)ptr(e[5].get<int32_t>(), sizeof(f32), &gSfxDefaultFreqAndVolScale),
                      (s8*)ptr(e[6].get<int32_t>(), sizeof(s8), &gSfxDefaultReverb));
        sReplayingSfx = false;
        if (Trace() && sSfxTraceLeft > 0) {
            --sSfxTraceLeft;
            SPDLOG_INFO("[CoopEnemy] {} sfx replayed: key {} actor 0x{:X} sfx 0x{:04X} at mirror +0x{:X}", kGameName,
                        Coop::HexU64(key), (int)m->id, sfx, off);
        }
    }
}

// ── owner-spawned children (step 6) ──
static bool ChildAllowed(Actor* a) {
    switch (a->id) { // pickups and exits stay each player's own
        case ACTOR_EN_ITEM00:
        case ACTOR_EN_ELF:
        case ACTOR_EN_SI:
        case ACTOR_DOOR_WARP1:
        case ACTOR_ITEM_B_HEART:
            return false;
        default: break;
    }
    switch (a->category) {
        case ACTORCAT_ENEMY:
        case ACTORCAT_BOSS:
        case ACTORCAT_PROP:
        case ACTORCAT_MISC:
        case COOP_CAT_EXPLOSIVE:
            return true;
        default: return false;
    }
}

// Actor_Spawn (both games): spawned while a synced actor's update runs = its child.
static void OnSpawned(Actor* a) {
    if (sSpawningMirror || a == nullptr || !InPlay() || sTransition || sUpdating == nullptr || sActors.count(a))
        return;
    auto pit = sActors.find(sUpdating);
    if (pit == sActors.end() || !pit->second.eligible || !pit->second.keyed)
        return;
    if (!Eligible(a) && !ChildAllowed(a))
        return;
    ActorState st;
    st.spawnParams = a->params;
    st.parentKey = pit->second.key;
    st.child = !Eligible(a);
    sActors.emplace(a, st);
}

// Receiver: the owner streams a child we have never seen: spawn the same actor here as a mirror (it never updates; the
// owner's snapshots drive it and its kill event removes it).
static void SpawnMirrorChild(const M::Rec& r, const Entry& e, uint64_t now) {
    uint64_t& tried = sChildTried[r.key];
    if (tried != 0 && now - tried < 2000)
        return;
    tried = now;
    auto pa = sByKey.find(r.parentKey);
    Actor* parent = pa != sByKey.end() && pa->second->update != nullptr ? pa->second : nullptr;
    sSpawningMirror = true;
    Actor* c = parent != nullptr
                   ? Actor_SpawnAsChild(&gPlayState->actorCtx, parent, gPlayState, e.actorId, e.pos[0], e.pos[1],
                                        e.pos[2], e.shapeRot[0], e.shapeRot[1], e.shapeRot[2], r.params)
                   : Actor_Spawn(&gPlayState->actorCtx, gPlayState, e.actorId, e.pos[0], e.pos[1], e.pos[2],
                                 e.shapeRot[0], e.shapeRot[1], e.shapeRot[2], r.params);
    sSpawningMirror = false;
    if (c == nullptr)
        return;
    ActorState st;
    st.keyed = true;
    st.eligible = true;
    st.child = !Eligible(c);
    st.boss = c->category == ACTORCAT_BOSS;
    st.key = r.key;
    st.parentKey = r.parentKey;
    st.spawnParams = r.params;
    st.firstSeenMs = now;
    sActors[c] = st;
    sByKey[r.key] = c;
    if (Trace())
        SPDLOG_INFO("[CoopEnemy] {} child mirrored: spawned actor 0x{:X} params 0x{:04X} key {} (parent {} {})", kGameName,
                    (int)e.actorId, (uint16_t)r.params, Coop::HexU64(r.key), Coop::HexU64(r.parentKey),
                    parent ? "mirror" : "not here");
}

// ── particle effects on every screen (step 7) ──
// Init params are copied raw; pointer-sized words that are real addresses are relocated:
//   1 = inside a synced actor instance (its key + offset), 2 = inside the owner's Link (-> its puppet on receivers),
//   3 = inside this game library (module-relative: display lists / resource paths), stack junk past the struct -> 0.
// An effect holding any other address (a collision poly, an object segment) is not replayed.
#ifdef _WIN32
// The linker's __ImageBase is this DLL's load address; SizeOfImage comes from its PE header (no windows.h, whose
// macros collide with game code): e_lfanew at 0x3C, then PE signature (4) + file header (20) + SizeOfImage at 56.
extern "C" char __ImageBase;
static uintptr_t LibBase() {
    return (uintptr_t)&__ImageBase;
}

static bool InLib(uintptr_t v) {
    static const uintptr_t end = [] {
        const unsigned char* b = (const unsigned char*)&__ImageBase;
        uint32_t peOff, size;
        std::memcpy(&peOff, b + 0x3C, 4);
        std::memcpy(&size, b + peOff + 4 + 20 + 56, 4);
        return (uintptr_t)b + size;
    }();
    return v >= LibBase() && v < end;
}
#else
static uintptr_t LibBase() {
    static uintptr_t base = [] {
        Dl_info info{};
        return dladdr((void*)&EffectSs_Spawn, &info) ? (uintptr_t)info.dli_fbase : (uintptr_t)0;
    }();
    return base;
}

static bool InLib(uintptr_t v) {
    Dl_info info{};
    return LibBase() != 0 && dladdr((void*)v, &info) && (uintptr_t)info.dli_fbase == LibBase();
}
#endif

static bool LooksLikeAddress(uint64_t v) {
    return v >= (1ull << 40) && v < (1ull << 47);
}

static void OnEffectSpawn(s32 type, s32 priority, void* params) {
    if (sReplayingFx || params == nullptr || sUpdating == nullptr || sOwnedRanges.empty() ||
        std::this_thread::get_id() != sGameThread || sFxQueue.size() >= 48)
        return;
    const OwnedRange* r = OwnedRangeOf(sUpdating, sizeof(Actor));
    if (r == nullptr || r->base != (uintptr_t)sUpdating)
        return; // only effects our own enemies make while they update
    uint8_t buf[kFxBytes];
    std::memcpy(buf, params, kFxBytes);
    const uintptr_t stackHere = (uintptr_t)&buf;
    const uintptr_t link = (uintptr_t)GET_PLAYER(gPlayState);
    nlohmann::json relocs = nlohmann::json::array();
    for (size_t off = 0; off + 8 <= kFxBytes; off += 8) {
        uint64_t v;
        std::memcpy(&v, buf + off, 8);
        if (!LooksLikeAddress(v))
            continue;
        if (v + 0x100000 > stackHere && v < stackHere + 0x100000) { // the caller's stack beyond the struct
            std::memset(buf + off, 0, 8);
            continue;
        }
        if (const OwnedRange* o = OwnedRangeOf((void*)v, 1)) {
            relocs.push_back({ off, 1, Coop::HexU64(o->key), v - o->base });
        } else if (v >= link && v < link + sizeof(Player)) {
            relocs.push_back({ off, 2, "", v - link });
        } else if (InLib(v)) {
            relocs.push_back({ off, 3, "", v - LibBase() });
        } else {
            bool found = false; // another synced actor (a mirror we show)
            for (auto& [a, st] : sActors) {
                if (st.eligible && v >= (uintptr_t)a && v < (uintptr_t)a + InstanceSize(a)) {
                    relocs.push_back({ off, 1, Coop::HexU64(st.key), v - (uintptr_t)a });
                    found = true;
                    break;
                }
            }
            if (!found)
                return; // an address teammates cannot map: skip this effect rather than hand them a wild pointer
        }
    }
    sFxQueue.push_back({ Coop::HexU64(r->key), type, priority, Coop::Base64Encode(buf, kFxBytes), relocs });
    if (Trace() && sFxTraceLeft > 0) {
        --sFxTraceLeft;
        SPDLOG_INFO("[CoopEnemy] {} owner effect: key {} EffectSs type {} prio {} relocs {}", kGameName,
                    Coop::HexU64(r->key), type, priority, relocs.size());
    }
}

static void ReplayFx(const nlohmann::json& list, uint32_t from) {
    for (auto& e : list) {
        if (!e.is_array() || e.size() != 5)
            continue;
        uint64_t key = Coop::ParseHexU64(e[0].get<std::string>());
        auto a = sByKey.find(key);
        if (a == sByKey.end())
            continue;
        auto st = sActors.find(a->second);
        if (st == sActors.end() || !st->second.mirror)
            continue;
        std::vector<uint8_t> buf;
        if (!Coop::Base64Decode(e[3].get<std::string>(), buf) || buf.size() != kFxBytes)
            continue;
        bool ok = true;
        for (auto& rl : e[4]) {
            size_t off = rl[0].get<size_t>();
            int kind = rl[1].get<int>();
            uint64_t delta = rl[3].get<uint64_t>();
            uintptr_t v = 0;
            if (kind == 1) {
                auto t = sByKey.find(Coop::ParseHexU64(rl[2].get<std::string>()));
                if (t != sByKey.end() && delta < InstanceSize(t->second))
                    v = (uintptr_t)t->second + delta;
            } else if (kind == 2) {
                if (Actor* pup = CoopG_Puppet(from); pup != nullptr && delta < sizeof(Player))
                    v = (uintptr_t)pup + delta;
            } else if (kind == 3 && LibBase() != 0) {
                v = LibBase() + delta;
            }
            if (v == 0 || off + 8 > kFxBytes) {
                ok = false;
                break;
            }
            std::memcpy(buf.data() + off, &v, 8);
        }
        if (!ok)
            continue;
        const int type = e[1].get<int>(), prio = e[2].get<int>();
        sReplayingFx = true;
        EffectSs_Spawn(gPlayState, type, prio, buf.data());
        sReplayingFx = false;
        if (Trace() && sFxTraceLeft > 0) {
            --sFxTraceLeft;
            SPDLOG_INFO("[CoopEnemy] {} effect replayed: key {} actor 0x{:X} EffectSs type {} prio {} relocs {}",
                        kGameName, Coop::HexU64(key), (int)a->second->id, type, prio, e[4].size());
        }
    }
}

// ── draw state beyond the pose (step 8): the actor's own instance data ──
// The owner sends its instance bytes past the Actor base (alpha / fade timers, eye and texture indices, limb
// visibility, colours, the current actionFunc...), minus the SkelAnime and registered collider structs (sent in the
// mirror record), as zlib(XOR with the previous image), a keyframe every 20 frames. Address words are relocated like
// effect params (own instance -> the mirror, another synced actor -> by key, the game library -> module-relative);
// any other address (heap arrays, the player, object segments) and handle words keep the receiver's own value.
static constexpr uint32_t kDsKeyEvery = 20;

struct ByteRange {
    size_t lo, hi;
};

static std::vector<ByteRange> DsExcluded(Actor* a, const M::Rec& r) {
    std::vector<ByteRange> ex;
    for (SkelAnime* sk : ActorSkels(a, InstanceSize(a)))
        ex.push_back({ (size_t)((uintptr_t)sk - (uintptr_t)a), (size_t)((uintptr_t)sk - (uintptr_t)a) + sizeof(SkelAnime) });
    for (const M::Col& c : r.cols)
        ex.push_back({ c.off, c.off + ColliderBytes(c.shape) });
    return ex;
}

static bool DsSkip(const std::vector<ByteRange>& ex, size_t lo, size_t hi) {
    for (const ByteRange& r : ex)
        if (lo < r.hi && hi > r.lo)
            return true;
    return false;
}

// Owner: image + relocations of an actor we own (after its update).
static void DsCapture(Actor* a, std::vector<uint8_t>& img, nlohmann::json& relocs, nlohmann::json& local,
                      const M::Rec& r) {
    const size_t size = InstanceSize(a);
    const uint8_t* base = (const uint8_t*)a;
    img.assign(base + sizeof(Actor), base + size);
    relocs = nlohmann::json::array();
    local = nlohmann::json::array();
    const auto ex = DsExcluded(a, r);
    for (size_t off = (sizeof(Actor) + 7) & ~(size_t)7; off + 8 <= size; off += 8) {
        uint64_t v;
        std::memcpy(&v, base + off, 8);
        if (!LooksLikeAddress(v) || DsSkip(ex, off, off + 8))
            continue;
        std::memset(img.data() + (off - sizeof(Actor)), 0, 8);
        if (v >= (uintptr_t)a && v < (uintptr_t)a + size) {
            relocs.push_back({ off, 1, "", v - (uintptr_t)a });
        } else if (InLib(v)) {
            relocs.push_back({ off, 3, "", v - LibBase() });
        } else {
            bool found = false;
            for (auto& [o, st] : sActors) {
                if (o != a && st.eligible && st.keyed && v >= (uintptr_t)o && v < (uintptr_t)o + InstanceSize(o)) {
                    relocs.push_back({ off, 2, Coop::HexU64(st.key), v - (uintptr_t)o });
                    found = true;
                    break;
                }
            }
            if (!found)
                local.push_back(off);
        }
    }
}

// zlib's uLong is 32-bit on Windows; these buffers are a few KB.
static bool Deflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    uLongf n = compressBound((uLong)in.size());
    out.resize(n);
    if (compress2(out.data(), &n, in.data(), (uLong)in.size(), 6) != Z_OK)
        return false;
    out.resize(n);
    return true;
}

static bool Inflate(const std::vector<uint8_t>& in, size_t expect, std::vector<uint8_t>& out) {
    out.resize(expect);
    uLongf n = (uLongf)expect;
    return uncompress(out.data(), &n, in.data(), (uLong)in.size()) == Z_OK && n == expect;
}

// Owner, at send time: one 'ds' entry for an actor we own.
static nlohmann::json DsEntry(Actor* a, ActorState& st) {
    std::vector<uint8_t> img;
    nlohmann::json relocs, local;
    DsCapture(a, img, relocs, local, st.rec);
    const bool key = st.dsSent.size() != img.size() || sFrame - st.dsKeyFrame >= kDsKeyEvery || st.dsSentFrame + 1 != sFrame;
    std::vector<uint8_t> x = img;
    if (!key)
        for (size_t i = 0; i < x.size(); ++i)
            x[i] ^= st.dsSent[i];
    std::vector<uint8_t> z;
    if (!Deflate(x, z))
        return nullptr;
    nlohmann::json e;
    e["k"] = Coop::HexU64(st.key);
    e["n"] = img.size();
    e["b"] = key ? -1 : (int64_t)st.dsSentFrame;
    e["z"] = Coop::Base64Encode(z.data(), z.size());
    e["r"] = relocs;
    e["l"] = local;
    uintptr_t d = (uintptr_t)a->draw;
    e["d"] = d == 0 ? nlohmann::json({ 0, 0 }) : InLib(d) ? nlohmann::json({ 1, d - LibBase() }) : nlohmann::json({ 2, 0 });
    e["sa"] = a->shape.shadowAlpha;
    e["ss"] = a->shape.shadowScale;
    if (key)
        st.dsKeyFrame = sFrame;
    st.dsSent = std::move(img);
    st.dsSentFrame = sFrame;
    return e;
}

// Receiver: fold an owner's 'ds' entry into the key's image (keyframe, or XOR onto the previous frame's image).
static void DsReceive(const nlohmann::json& e, uint32_t frame) {
    uint64_t key = Coop::ParseHexU64(e.value("k", std::string()));
    auto it = sKeys.find(key);
    if (it == sKeys.end())
        return;
    KeyState& ks = it->second;
    size_t n = e.value("n", (size_t)0);
    int64_t base = e.value("b", (int64_t)-1);
    std::vector<uint8_t> z, x;
    if (n == 0 || n > 0x10000 || !Coop::Base64Decode(e.value("z", std::string()), z) || !Inflate(z, n, x))
        return;
    if (base < 0) {
        ks.dsImage = std::move(x);
    } else {
        if ((int64_t)ks.dsFrame != base || ks.dsImage.size() != n) {
            ks.dsImage.clear(); // missed a frame: wait for the next keyframe
            return;
        }
        for (size_t i = 0; i < n; ++i)
            ks.dsImage[i] ^= x[i];
    }
    ks.dsFrame = frame;
    ks.dsFresh = true;
    ks.dsRelocs = e.value("r", nlohmann::json::array());
    ks.dsLocal = e.value("l", nlohmann::json::array());
    auto d = e.value("d", nlohmann::json::array());
    ks.dsDrawKind = d.size() == 2 ? d[0].get<int>() : 2;
    ks.dsDrawDelta = d.size() == 2 ? d[1].get<uint64_t>() : 0;
    ks.dsShadowAlpha = e.value("sa", -1);
    ks.dsShadowScale = e.value("ss", 0.0f);
}

// Receiver, MirrorPass: write the owner's image over the mirror (before the pose / hurtboxes are applied).
static int sDsTraceLeft = 30;
static void DsApply(Actor* a, ActorState& st, KeyState& ks) {
    if (!ks.dsFresh || ks.dsImage.empty())
        return;
    ks.dsFresh = false;
    const size_t size = InstanceSize(a);
    if (ks.dsImage.size() != size - sizeof(Actor))
        return; // a different actor build / size: never write
    const auto ex = DsExcluded(a, ks.rec);
    std::vector<uint8_t> keep(size - sizeof(Actor), 0); // 1 = byte keeps our own value
    for (const ByteRange& r : ex)
        for (size_t i = std::max(r.lo, sizeof(Actor)); i < std::min(r.hi, size); ++i)
            keep[i - sizeof(Actor)] = 1;
    auto keepWord = [&](size_t off) {
        for (size_t i = off; i < off + 8 && i < size; ++i)
            keep[i - sizeof(Actor)] = 1;
    };
    for (auto& l : ks.dsLocal)
        keepWord(l.get<size_t>());
    std::vector<std::pair<size_t, uintptr_t>> writes;
    for (auto& rl : ks.dsRelocs) {
        size_t off = rl[0].get<size_t>();
        int kind = rl[1].get<int>();
        uint64_t delta = rl[3].get<uint64_t>();
        uintptr_t v = 0;
        if (kind == 1 && delta < size)
            v = (uintptr_t)a + delta;
        else if (kind == 3 && LibBase() != 0)
            v = LibBase() + delta;
        else if (kind == 2) {
            auto t = sByKey.find(Coop::ParseHexU64(rl[2].get<std::string>()));
            if (t != sByKey.end() && delta < InstanceSize(t->second))
                v = (uintptr_t)t->second + delta;
        }
        keepWord(off);
        if (v != 0 && off + 8 <= size)
            writes.push_back({ off, v });
    }
    uint8_t* dst = (uint8_t*)a + sizeof(Actor);
    size_t copied = 0;
    for (size_t i = 0; i < keep.size(); ++i)
        if (!keep[i]) {
            dst[i] = ks.dsImage[i];
            ++copied;
        }
    for (auto& [off, v] : writes)
        std::memcpy((uint8_t*)a + off, &v, 8);
    if (ks.dsDrawKind == 0)
        a->draw = nullptr;
    else if (ks.dsDrawKind == 1 && LibBase() != 0)
        a->draw = (ActorFunc)(LibBase() + ks.dsDrawDelta);
    if (ks.dsShadowAlpha >= 0) {
        a->shape.shadowAlpha = (u8)ks.dsShadowAlpha;
        a->shape.shadowScale = ks.dsShadowScale;
    }
    if (Trace() && sDsTraceLeft > 0 && (ks.dsFrame % 60) == 0) {
        --sDsTraceLeft;
        SPDLOG_INFO("[CoopEnemy] {} draw-state applied f={} key {} actor 0x{:X}: {} of {} instance bytes copied, {} "
                    "pointers relocated, {} words kept local, draw {}, shadowAlpha {}",
                    kGameName, ks.dsFrame, Coop::HexU64(st.key), (int)a->id, copied, keep.size(), writes.size(),
                    ks.dsLocal.size(), ks.dsDrawKind == 0 ? "off" : ks.dsDrawKind == 1 ? "owner's" : "ours",
                    ks.dsShadowAlpha);
    }
}

// ── keying (lazily, on first update: OOT stamps the setup index after Init) ──
static void Claim(KeyState& ks, ActorState& st, const char* why);

static void KeyActor(Actor* a, ActorState& st, uint64_t now) {
    st.keyed = true;
    st.firstSeenMs = now;
    st.eligible = Eligible(a) || st.child;
    if (!st.eligible)
        return;
    st.boss = a->category == ACTORCAT_BOSS;
    int idx = GetActorListIndex(a);
    if (idx >= 0) {
        st.setup = true;
        // (scene, room, list index, id) already names one entry of the room's actor list. Params stay out of
        // the key: a client that connected after the room loaded only sees post-Init params.
        st.key = SetupKey(COOP_GAME, sScene, a->room, idx, a->id, 0);
    } else {
        uint64_t base = HashCombine(st.parentKey, ((uint64_t)(uint16_t)a->id << 16) | (uint16_t)st.spawnParams);
        int ord = sOrdinals[base]++;
        st.key = DynamicKey(COOP_GAME, sScene, st.parentKey, a->id, st.spawnParams, ord);
    }
    if (sByKey.count(st.key)) { // two live actors on one key (duplicate spawn data): keep the first only
        st.eligible = false;
        return;
    }
    sByKey[st.key] = a;
    // A child is driven by whoever drives its parent: the owner claims it at once (no unowned wait), so it streams
    // from its first frame (an Octorok rock lives about a second).
    if (st.parentKey != 0 && !st.setup) {
        auto pk = sKeys.find(st.parentKey);
        if (pk != sKeys.end() && pk->second.owner != 0 && pk->second.owner == CoopG_OwnClientId()) {
            KeyState& ks = sKeys[st.key];
            if (ks.owner == 0) {
                Claim(ks, st, "child of an owned actor");
                if (Trace())
                    SPDLOG_INFO("[CoopEnemy] {} child keyed: key {} actor 0x{:X} params 0x{:04X} parent {}", kGameName,
                                Coop::HexU64(st.key), (int)a->id, (uint16_t)st.spawnParams, Coop::HexU64(st.parentKey));
            }
        }
    }
    st.players = RoomPlayers(a->room, MyRoom(), sPeerRooms);
    st.k = ActorFactor(st.players);
    SeedPool(a, st, "init");
    WriteNative(a, st);
}

static ActorState* Track(Actor* a, uint64_t now) {
    auto it = sActors.find(a);
    if (it == sActors.end()) {
        ActorState st;
        st.spawnParams = a->params;
        it = sActors.emplace(a, st).first;
    }
    if (!it->second.keyed)
        KeyActor(a, it->second, now);
    return &it->second;
}

// ── ownership ──
static void Claim(KeyState& ks, ActorState& st, const char* why) {
    ks.owner = CoopG_OwnClientId();
    ks.epoch += 1;
    ks.hasRec = false;
    st.proxFrames = 0;
    st.orphanSinceMs = 0;
    if (Trace())
        SPDLOG_INFO("[CoopEnemy] {} claim key {} epoch {} ({})", kGameName, Coop::HexU64(st.key), ks.epoch, why);
}

static void OwnershipPass(uint64_t now) {
    const uint32_t me = CoopG_OwnClientId();
    const Vec3f myPos = GET_PLAYER(gPlayState)->actor.world.pos;
    const int rank = Rank(now);
    const int myRoom = MyRoom();
    for (auto& [a, st] : sActors) {
        if (!st.eligible || a->update == nullptr)
            continue;
        KeyState& ks = sKeys[st.key];
        if (ks.owner == me)
            continue;
        // We only drive actors of the room we are in (a copy of the previous room may linger during a room
        // transition: it belongs to whoever is in that room).
        if (!InActorRoom(a->room, myRoom))
            continue;
        if (ks.owner == 0) {
            // Unowned: the scene authority takes it at once; everyone else waits (rank-staggered) so the
            // authority's first entry usually arrives before anyone else claims.
            if (rank == 0 || now - st.firstSeenMs > 1000u + 500u * (uint64_t)rank)
                Claim(ks, st, "unowned");
            continue;
        }
        const PeerInfo* owner = ScenePeer(ks.owner, now);
        // The owner's latest presence is in another room than the actor: it no longer runs it ("owner left"),
        // claim at once instead of waiting kOrphanMs, staggered by rank among the players in the actor's room.
        bool leftRoom = owner != nullptr && OwnerLeftRoom(a->room, owner->room);
        bool silent = !ks.hasEntry || now - ks.lastRecvMs > kOrphanMs;
        if (owner == nullptr || leftRoom || silent) {
            if (st.orphanSinceMs == 0)
                st.orphanSinceMs = now;
            bool gone = owner == nullptr || leftRoom;
            uint64_t wait = (gone ? 0u : 500u) + 400u * (uint64_t)(leftRoom ? RoomRank(a->room, now) : rank);
            if (now - st.orphanSinceMs >= wait)
                Claim(ks, st, owner == nullptr ? "owner left" : leftRoom ? "owner left room" : "owner silent");
            continue;
        }
        st.orphanSinceMs = 0;
        // COMBO_DEBUG_COOP_NOPROX (env, harness): no proximity hand-off, so a teammate fighting next to the owner keeps
        // hitting a mirror (forwarded hits) instead of taking the enemy over.
        static const bool noProx = std::getenv("COMBO_DEBUG_COOP_NOPROX") != nullptr;
        bool locked = noProx || (ks.last.flags & kFlagLocked) != 0;
        if (!locked && ProximityChallenger(DistTo(a, myPos), DistTo(a, owner->pos))) {
            if (++st.proxFrames >= kProximityTicks * 2)
                Claim(ks, st, "nearest player");
        } else {
            st.proxFrames = 0;
        }
    }
}

// ── snapshot apply (receiver, pre-update) ──
static s16 LerpAngle(s16 cur, s16 tgt) {
    return (s16)(cur + (s16)(tgt - cur) / 2);
}

static void ApplyEntry(Actor* a, const KeyState& ks, uint64_t now) {
    const Entry& e = ks.last;
    float frames = (float)std::min<uint64_t>(now - ks.lastRecvMs, 200) / 50.0f; // extrapolate <= 4 frames
    Vec3f tgt = { e.pos[0] + e.vel[0] * frames, e.pos[1] + e.vel[1] * frames, e.pos[2] + e.vel[2] * frames };
    float err = DistTo(a, tgt);
    if (err >= 150.0f) {
        a->world.pos = tgt;
    } else {
        a->world.pos.x += (tgt.x - a->world.pos.x) * 0.5f;
        a->world.pos.y += (tgt.y - a->world.pos.y) * 0.5f;
        a->world.pos.z += (tgt.z - a->world.pos.z) * 0.5f;
    }
    a->world.rot.x = LerpAngle(a->world.rot.x, e.rot[0]);
    a->world.rot.y = LerpAngle(a->world.rot.y, e.rot[1]);
    a->world.rot.z = LerpAngle(a->world.rot.z, e.rot[2]);
    a->shape.rot.x = LerpAngle(a->shape.rot.x, e.shapeRot[0]);
    a->shape.rot.y = LerpAngle(a->shape.rot.y, e.shapeRot[1]);
    a->shape.rot.z = LerpAngle(a->shape.rot.z, e.shapeRot[2]);
    a->velocity.x = e.vel[0];
    a->velocity.y = e.vel[1];
    a->velocity.z = e.vel[2];
    COOP_SPEED(a) = e.speed;
    a->colChkInfo.health = e.health;
    if (e.freezeTimer > a->freezeTimer)
        a->freezeTimer = e.freezeTimer;
    if (e.colorFilterTimer > a->colorFilterTimer) {
        a->colorFilterParams = e.colorFilterParams;
        a->colorFilterTimer = e.colorFilterTimer;
    }
}

// "Retarget": the AI on a receiver reads xzDistToPlayer / yawTowardsPlayer, which Actor_UpdateAll filled in
// against the LOCAL Link. Point them at the owner's Link instead, so between snapshots the local AI chases the
// same player the owner's AI chases. (Code that reads GET_PLAYER directly still sees the local Link; the
// snapshots correct that.)
static void Retarget(Actor* a, const Vec3f& p) {
    Vec3f tp = p;
    a->xzDistToPlayer = Actor_WorldDistXZToPoint(a, &tp);
    COOP_YDIST(a) = p.y - a->world.pos.y;
    a->xyzDistToPlayerSq = SQ(a->xzDistToPlayer) + SQ(COOP_YDIST(a));
    a->yawTowardsPlayer = Actor_WorldYawTowardPoint(a, &tp);
}

// ── hooks ──
static void OnShouldUpdate(Actor* a, bool* should) {
    if (!InPlay() || sTransition)
        return;
    if (a == &GET_PLAYER(gPlayState)->actor) {
        // Trace only: our own Link caught by an enemy we own (collision found it locally, vanilla path) -- with the
        // puppet relays this shows one attack catching several players, each hit once.
        if (Trace()) {
            Player* me = GET_PLAYER(gPlayState);
            if ((me->cylinder.base.acFlags & kAcHit) && OwnedAttacker(me->cylinder.base.ac) != nullptr)
                SPDLOG_INFO("[CoopEnemy] {} owner: actor 0x{:X} key {} hit our own Link locally (frame {})", kGameName,
                            (int)me->cylinder.base.ac->id, Coop::HexU64(OwnedAttacker(me->cylinder.base.ac)->key),
                            sFrame);
        }
        if (!sPendingPlayerHits.empty() && InjectPlayerHit(GET_PLAYER(gPlayState), sPendingPlayerHits.front()))
            sPendingPlayerHits.erase(sPendingPlayerHits.begin());
        return;
    }
    sUpdating = nullptr;
    if (!Eligible(a) && !sActors.count(a))
        return; // fast path: only enemies/bosses (and actors we already track) cost anything
    const uint64_t now = NowMs();
    ActorState* st = Track(a, now);
    if (!st->eligible)
        return;
    sUpdating = a;
    if (st->setup && sDead.count(st->key)) {
        RemoteKill(a);
        *should = false;
        return;
    }
    if (SyncOn() && CoopG_Connected() && ScenePeerCount(now) > 0) {
        KeyState& ks = sKeys[st->key];
        const uint32_t me = CoopG_OwnClientId();
        // Owner: a teammate's hit on its mirror replays here, before our update, as if our collision found it.
        if (ks.owner == me) {
            auto ph = sPendingHits.find(st->key);
            if (ph != sPendingHits.end() && !ph->second.empty()) {
                if (InjectHit(a, *st, ph->second.front()))
                    ph->second.erase(ph->second.begin());
            }
        }
        // Combat claim: a hit landed on our copy while it is not a mirror (nobody streams it to us yet). Whoever
        // lands the hit drives the actor from here on. Hits on a mirror are forwarded to its owner instead.
        if (a->colChkInfo.damage > 0 && !st->mirror) {
            if (ks.owner != me)
                Claim(ks, *st, "hit");
            st->lockUntilMs = now + kClaimLockMs * (st->boss ? 2u : 1u);
        }
        if (ks.owner != me && ks.hasEntry) {
            if (st->mirror) {
                // A teammate owns it: its AI runs there only. Tick already posed/moved/collided it from the owner's
                // snapshot this frame.
                *should = false;
                return;
            }
            // Not a mirror while owned elsewhere: a boss whose owner's copy is dying runs its own death sequence
            // here (warp, heart container, cutscene camera are local).
            ApplyEntry(a, ks, now);
            if (const PeerInfo* o = ScenePeer(ks.owner, now))
                Retarget(a, o->pos);
        }
    }
    PrePadHit(a, *st, a->colChkInfo.damage);
    st->preHealth = a->colChkInfo.health;
}

static void Tick();

static void OnUpdated(Actor* a) {
    if (!InPlay())
        return;
    if (a == &GET_PLAYER(gPlayState)->actor) {
        Tick();
        return;
    }
    sUpdating = nullptr;
    auto it = sActors.find(a);
    if (it == sActors.end() || !it->second.eligible)
        return;
    ActorState& st = it->second;
    const uint64_t now = NowMs();
    int hp = a->colChkInfo.health;
    int dh = st.preHealth - hp;
    if (dh < 0 && HasPool(st) && hp > 0) {
        // The actor raised its own health with no snapshot involved: a phase reset (Ganon's second form,
        // Twinmold, Odolwa...). Re-seed the pool at the new native max instead of fighting it.
        SeedPool(a, st, "phase reset");
    }
    if (dh > 0 && HasPool(st)) {
        int64_t before = st.wideHp;
        st.wideHp -= dh;
        if (hp == 0)
            st.wideHp = std::min<int64_t>(st.wideHp, 0); // the actor's own death already started
        else if (st.wideHp <= 0)
            st.wideHp = 1; // unpredicted subtraction emptied the pool early: the next hit kills (PrePadHit)
        WriteNative(a, st);
        if (Trace())
            SPDLOG_INFO("[CoopEnemy] {} hit actor 0x{:X} key {} -{} pool {} -> {}/{} native {}", kGameName,
                        (int)a->id, Coop::HexU64(st.key), dh, before, st.wideHp, st.wideMax,
                        (int)a->colChkInfo.health);
    } else if (HasPool(st) && hp > 0) {
        WriteNative(a, st); // undo a pre-pad whose hit the actor ignored (immune / invulnerable phase)
    }
    if (dh > 0) {
        if (now - st.lastLocalHitMs > 1000)
            st.recentLocalDmg = 0;
        st.lastLocalHitMs = now;
        st.recentLocalDmg += dh;
        if (SyncOn() && CoopG_Connected() && ScenePeerCount(now) > 0) {
            KeyState& ks = sKeys[st.key];
            if (ks.owner != CoopG_OwnClientId())
                Claim(ks, st, "hp drop");
            st.lockUntilMs = now + kClaimLockMs * (st.boss ? 2u : 1u);
        }
    }
    if (st.preHealth > 0 && a->colChkInfo.health == 0 && st.dyingSinceMs == 0)
        st.dyingSinceMs = now;
    // Owner: capture the pose and hurtboxes this update produced, for the mirrors.
    if (SyncOn() && CoopG_Connected() && ScenePeerCount(now) > 0) {
        auto ks = sKeys.find(st.key);
        if (ks != sKeys.end() && ks->second.owner == CoopG_OwnClientId())
            CaptureRec(a, st);
    }
}

static void OnSpawnInit(Actor* a) {
    if (!InPlay() || a == nullptr || !Eligible(a))
        return;
    auto it = sActors.find(a);
    if (it != sActors.end())
        return;
    ActorState st;
    st.spawnParams = a->params; // before Init can rewrite params
    // Parent for runtime spawns: an explicit synced parent, else the synced actor whose update is running.
    Actor* parent = a->parent;
    auto pit = parent ? sActors.find(parent) : sActors.end();
    if (pit != sActors.end() && pit->second.eligible)
        st.parentKey = pit->second.key;
    else if (sUpdating != nullptr && sActors.count(sUpdating))
        st.parentKey = sActors[sUpdating].key;
    sActors.emplace(a, st);
}

static void OnKilled(Actor* a) {
    if (sRemoteKill || sTransition || !InPlay())
        return;
    auto it = sActors.find(a);
    if (it == sActors.end() || !it->second.eligible)
        return;
    ActorState& st = it->second;
    // Room unload (OOT func_80031B14 / MM's room-change kill): not a death.
    if (a->room >= 0 && a->room != gPlayState->roomCtx.curRoom.num && a->room != gPlayState->roomCtx.prevRoom.num)
        return;
    const uint64_t now = NowMs();
    if (!SyncOn() || !CoopG_Connected() || ScenePeerCount(now) == 0)
        return;
    bool death = a->colChkInfo.health == 0 || st.dyingSinceMs != 0;
    if (death && st.setup) {
        sDead.insert(st.key);
        sDeadDirty = true;
    }
    nlohmann::json ev = { { "k", Coop::HexU64(st.key) }, { "e", death ? "kill" : "despawn" } };
    if (st.setup)
        ev["s"] = 1;
    sEvents.push_back(ev);
    if (Trace())
        SPDLOG_INFO("[CoopEnemy] {} {} actor 0x{:X} key {}", kGameName, death ? "kill" : "despawn", (int)a->id,
                    Coop::HexU64(st.key));
}

static void OnDestroyed(Actor* a) {
    SkelForget((uintptr_t)a, InstanceSize(a));
    auto it = sActors.find(a);
    if (it == sActors.end())
        return;
    if (Trace() && it->second.parentKey != 0 && !it->second.setup && it->second.eligible)
        SPDLOG_INFO("[CoopEnemy] {} child destroyed: actor 0x{:X} key {} ({})", kGameName, (int)a->id,
                    Coop::HexU64(it->second.key), it->second.mirror ? "mirror" : "local");
    if (it->second.eligible) {
        auto k = sByKey.find(it->second.key);
        if (k != sByKey.end() && k->second == a)
            sByKey.erase(k);
    }
    if (sUpdating == a)
        sUpdating = nullptr;
    sActors.erase(it);
}

// ── send ──
static void FlushEvents() {
    if (sEvents.empty())
        return;
    nlohmann::json p;
    p["type"] = Coop::kPktActorEvent;
    p["game"] = kGameName;
    p["scene"] = sScene;
    p["targetTeamId"] = TeamId();
    p["events"] = sEvents;
    sEvents.clear();
    CoopG_Send(p);
}

static void SendSnapshot(uint64_t now, bool withEntries) {
    const uint32_t me = CoopG_OwnClientId();
    Player* player = GET_PLAYER(gPlayState);
    std::vector<uint8_t> blob;
    std::vector<M::Rec> recs;
    nlohmann::json dsl = nlohmann::json::array();
    int count = 0;
    if (withEntries) {
        for (auto& [a, st] : sActors) {
            if (!st.eligible || a->update == nullptr)
                continue;
            auto ks = sKeys.find(st.key);
            if (ks == sKeys.end() || ks->second.owner != me)
                continue;
            Entry e;
            e.key = st.key;
            e.epoch = ks->second.epoch;
            e.actorId = a->id;
            e.flags = (now < st.lockUntilMs ? kFlagLocked : 0) | (st.boss ? kFlagBoss : 0) |
                      (a->colChkInfo.health == 0 && st.dyingSinceMs != 0 ? kFlagDying : 0);
            e.health = a->colChkInfo.health;
            e.pos[0] = a->world.pos.x;
            e.pos[1] = a->world.pos.y;
            e.pos[2] = a->world.pos.z;
            e.rot[0] = a->world.rot.x;
            e.rot[1] = a->world.rot.y;
            e.rot[2] = a->world.rot.z;
            e.shapeRot[0] = a->shape.rot.x;
            e.shapeRot[1] = a->shape.rot.y;
            e.shapeRot[2] = a->shape.rot.z;
            e.vel[0] = a->velocity.x;
            e.vel[1] = a->velocity.y;
            e.vel[2] = a->velocity.z;
            e.speed = COOP_SPEED(a);
            e.k = (float)st.k;
            e.freezeTimer = a->freezeTimer;
            e.colorFilterParams = a->colorFilterParams;
            e.colorFilterTimer = a->colorFilterTimer;
            e.baseHealth = (uint8_t)std::clamp(st.baseHealth, 0, 255);
            e.wideHp = (int32_t)st.wideHp;
            e.wideMax = (int32_t)st.wideMax;
            EncodeEntry(blob, e);
            ++count;
            if (st.hasRec) {
                if (DsOn()) {
                    nlohmann::json d = DsEntry(a, st);
                    if (!d.is_null())
                        dsl.push_back(std::move(d));
                }
                recs.push_back(st.rec);
                if (Trace() && (sFrame % 60) == 0) {
                    const M::Skel* k0 = st.rec.skels.empty() ? nullptr : &st.rec.skels[0];
                    SPDLOG_INFO("[CoopEnemy] {} owner send f={} key {} actor 0x{:X} skels {} anim {} frame {:.2f} j0 "
                                "({},{},{}) cols {}",
                                kGameName, sFrame, Coop::HexU64(st.key), (int)a->id, st.rec.skels.size(),
                                k0 ? PathTail(k0->anim) : "-", k0 ? k0->curFrame : 0.0f,
                                k0 && k0->joints.size() >= 3 ? k0->joints[0] : 0,
                                k0 && k0->joints.size() >= 3 ? k0->joints[1] : 0,
                                k0 && k0->joints.size() >= 3 ? k0->joints[2] : 0, st.rec.cols.size());
                }
            }
        }
    }
    nlohmann::json p;
    p["type"] = Coop::kPktActorSnap;
    p["game"] = kGameName;
    p["quiet"] = true;
    p["targetTeamId"] = TeamId();
    p["scene"] = sScene;
    p["room"] = MyRoom();
    p["px"] = player->actor.world.pos.x;
    p["py"] = player->actor.world.pos.y;
    p["pz"] = player->actor.world.pos.z;
    p["v"] = kWireVersion;
    p["b"] = Coop::HexU64(BuildTag());
    p["f"] = sFrame;
    if (count > 0)
        p["blob"] = Coop::Base64Encode(blob.data(), blob.size());
    if (!sSfxQueue.empty()) {
        nlohmann::json sl = nlohmann::json::array();
        for (const SfxEv& e : sSfxQueue) {
            sl.push_back({ Coop::HexU64(e.key), e.posOff, e.sfx, e.token, e.freqOff, e.volOff, e.reverbOff });
            if (Trace() && sSfxTraceLeft > 0) {
                --sSfxTraceLeft;
                SPDLOG_INFO("[CoopEnemy] {} owner sfx: key {} sfx 0x{:04X} at +0x{:X}", kGameName, Coop::HexU64(e.key),
                            e.sfx, e.posOff);
            }
        }
        p["s"] = sl;
        sSfxQueue.clear();
    }
    if (!dsl.empty())
        p["ds"] = dsl;
    if (!sFxQueue.empty()) {
        p["fx"] = sFxQueue;
        sFxQueue.clear();
    }
    if (!recs.empty()) {
        std::vector<uint8_t> mb;
        M::EncodeBlob(mb, recs);
        p["m"] = Coop::Base64Encode(mb.data(), mb.size());
    }
    if (sDeadDirty || (sFrame % 40) == 0) {
        if (!sDead.empty()) {
            nlohmann::json dead = nlohmann::json::array();
            for (uint64_t k : sDead)
                dead.push_back(Coop::HexU64(k));
            p["dead"] = dead;
        }
        sDeadDirty = false;
    }
    CoopG_Send(p);
}

// ── harness (env at launch only; never reachable from a file or the menu) ──
static Actor* NearestSynced(int onlyId = -1) {
    Actor* best = nullptr;
    float bd = 1e9f;
    Vec3f me = GET_PLAYER(gPlayState)->actor.world.pos;
    for (auto& [a, st] : sActors) {
        if (!st.eligible || a->update == nullptr || a->colChkInfo.health == 0 || (onlyId >= 0 && a->id != onlyId))
            continue;
        // With an id filter, every client picks the same one (lowest key) so teammates meet at one enemy.
        float d = onlyId >= 0 ? (float)(st.key >> 40) : DistTo(a, me);
        if (d < bd) {
            bd = d;
            best = a;
        }
    }
    return best;
}

static int EnvFrame(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::atoi(v) : -1;
}

static void HarnessTick(uint64_t now) {
    static const int nearAt = EnvFrame("COMBO_DEBUG_COOP_NEAR");
    static const int killAt = EnvFrame("COMBO_DEBUG_COOP_KILL");
    static const int hitAt = EnvFrame("COMBO_DEBUG_COOP_HIT");
    if ((int)sFrame == nearAt) {
        // COMBO_DEBUG_COOP_NEAR=<frame>[,<actor id hex>[,<distance>]]: stand <distance> (default 70) units in front of
        // the nearest synced enemy (of that id), facing it (then COMBO_VPAD fights for real).
        static const int nearId = [] {
            const char* v = std::getenv("COMBO_DEBUG_COOP_NEAR");
            const char* c = v ? std::strchr(v, ',') : nullptr;
            return c ? (int)std::strtol(c + 1, nullptr, 16) : -1;
        }();
        if (Actor* t = NearestSynced(nearId)) {
            Player* pl = GET_PLAYER(gPlayState);
            static const float nearDist = [] { // optional 3rd field: distance (default 70)
                const char* v = std::getenv("COMBO_DEBUG_COOP_NEAR");
                const char* c = v ? std::strchr(v, ',') : nullptr;
                c = c ? std::strchr(c + 1, ',') : nullptr;
                return c ? (float)std::atof(c + 1) : 70.0f;
            }();
            Vec3f p = t->world.pos;
            p.z += nearDist;
            p.y += 100.0f; // drop onto the floor there (an enemy's y can sit below the ground at our spot: void out)
            pl->actor.world.pos = p;
            pl->actor.prevPos = p;
            pl->actor.home.pos = p;
            pl->actor.shape.rot.y = pl->actor.world.rot.y = pl->yaw = (s16)0x8000; // face -z, toward the enemy
            SPDLOG_INFO("[CoopEnemy] harness: moved next to actor 0x{:X} key {}", (int)t->id,
                        Coop::HexU64(sActors[t].key));
        }
    }
    // COMBO_DEBUG_COOP_HIT=<frame>[,<every>]: from that frame, a 1-damage hit on the same target every <every>
    // frames (default: one hit) through the real pre-pad -> native subtraction -> post-update pool path, the way
    // Actor_ApplyDamage applies colChkInfo.damage. Logs the hit count when the native bar reaches 0.
    static Actor* hitTarget = nullptr;
    static int hits = 0;
    static const int hitEvery = [] {
        const char* v = std::getenv("COMBO_DEBUG_COOP_HIT");
        const char* c = v ? std::strchr(v, ',') : nullptr;
        return c ? std::max(1, std::atoi(c + 1)) : 0;
    }();
    bool hitNow = hitAt >= 0 && ((int)sFrame == hitAt || (hitEvery > 0 && (int)sFrame > hitAt &&
                                                          ((int)sFrame - hitAt) % hitEvery == 0));
    if (hitNow) {
        if (hitTarget == nullptr || !sActors.count(hitTarget) || hitTarget->update == nullptr)
            hitTarget = (int)sFrame == hitAt ? NearestSynced() : nullptr;
        if (Actor* t = hitTarget; t != nullptr && t->colChkInfo.health > 0) {
            ActorState& st = sActors[t];
            PrePadHit(t, st, 1);
            st.preHealth = t->colChkInfo.health;
            t->colChkInfo.health = (u8)std::max(0, (int)t->colChkInfo.health - 1); // Actor_ApplyDamage
            t->colorFilterParams = 0x4000 | 0xFF;
            t->colorFilterTimer = 8;
            ++hits;
            OnUpdated(t);
            if (t->colChkInfo.health == 0) {
                SPDLOG_INFO("[CoopEnemy] harness: actor 0x{:X} key {} native 0 after {} hits (vanilla {} hits, k "
                            "{:.2f})",
                            (int)t->id, Coop::HexU64(st.key), hits, st.baseHealth, st.k);
                st.dyingSinceMs = now;
                Actor_Kill(t);
                hitTarget = nullptr;
            }
        }
    }
    // COMBO_DEBUG_COOP_JOIN=<frame>: from that frame, every 20 frames stand exactly where a teammate's puppet is (so one
    // attack volume around them catches both of us).
    static const int joinAt = EnvFrame("COMBO_DEBUG_COOP_JOIN");
    if (joinAt >= 0 && (int)sFrame >= joinAt && ((int)sFrame - joinAt) % 20 == 0) {
        for (auto& [id, peer] : sPeers) {
            if (Actor* pup = CoopG_Puppet(id)) {
                Player* pl = GET_PLAYER(gPlayState);
                pl->actor.world.pos = pl->actor.prevPos = pup->world.pos;
                if (((int)sFrame - joinAt) % 200 == 0)
                    SPDLOG_INFO("[CoopEnemy] harness: standing on teammate {}'s puppet ({:.0f},{:.0f},{:.0f})", id,
                                pup->world.pos.x, pup->world.pos.y, pup->world.pos.z);
                break;
            }
        }
    }
    // COMBO_DEBUG_COOP_ATK=<frame>,<every>,<actor id hex>: on the client that OWNS that enemy (lowest key of the id),
    // from <frame> every <every> frames the enemy swings one real AT cylinder (enemy-aligned, 1/2 heart, radius 60) at
    // our Link's position for one frame. With COMBO_DEBUG_COOP_NEAR both Links stand there, so ONE attack volume
    // catches our Link and the teammate's puppet in the same collision pass: each must be hit exactly once (ours by
    // vanilla collision, the teammate's through its relay).
    static const int atkAt = EnvFrame("COMBO_DEBUG_COOP_ATK");
    static ColliderCylinder atkCol;
    static Actor* atkOwner = nullptr;
    if (atkAt >= 0 && (int)sFrame >= atkAt) {
        static const int atkEvery = [] {
            const char* v = std::getenv("COMBO_DEBUG_COOP_ATK");
            const char* c = v ? std::strchr(v, ',') : nullptr;
            return c ? std::max(1, std::atoi(c + 1)) : 100;
        }();
        static const int atkId = [] {
            const char* v = std::getenv("COMBO_DEBUG_COOP_ATK");
            const char* c = v ? std::strchr(v, ',') : nullptr;
            c = c ? std::strchr(c + 1, ',') : nullptr;
            return c ? (int)std::strtol(c + 1, nullptr, 16) : -1;
        }();
        if (((int)sFrame - atkAt) % atkEvery == 0) {
            Actor* t = NearestSynced(atkId);
            if (t != nullptr && sKeys[sActors[t].key].owner == CoopG_OwnClientId()) {
                if (atkOwner != t) {
                    static ColliderCylinderInit init = {
                        { 0, 0x11 /* AT_ON | AT_TYPE_ENEMY */, 0, 0, 0, 1 /* COLSHAPE_CYLINDER */ },
                        { 0, { 0xFFCFFFFF, 0, 8 }, { 0, 0, 0 }, 1 /* ON */, 0, 0 },
                        { 60, 80, 0, { 0, 0, 0 } },
                    };
                    Collider_InitCylinder(gPlayState, &atkCol);
                    Collider_SetCylinder(gPlayState, &atkCol, t, &init);
                    atkOwner = t;
                }
                Vec3f lp = GET_PLAYER(gPlayState)->actor.world.pos;
                atkCol.base.atFlags &= ~(1 << 1 | 1 << 2); // AT_HIT / AT_BOUNCED from the previous swing
                atkCol.dim.pos = { (s16)lp.x, (s16)(lp.y - 10.0f), (s16)lp.z };
                CollisionCheck_SetAT(gPlayState, &gPlayState->colChkCtx, &atkCol.base);
                SPDLOG_INFO("[CoopEnemy] harness: actor 0x{:X} key {} swings one AT volume at ({:.0f},{:.0f},{:.0f}) "
                            "frame {}",
                            (int)t->id, Coop::HexU64(sActors[t].key), lp.x, lp.y, lp.z, sFrame);
            }
        }
    }
    // COMBO_DEBUG_COOP_CHILD=<frame>,<actor id hex>: on the client that OWNS that enemy (lowest key of the id), spawn a
    // copy of it from inside its update context, the way Queen Gohma lays eggs / an Octorok spits a rock (Actor_Spawn
    // while the parent's update runs); 200 frames later the child is killed (Actor_Kill), like a rock breaking.
    static const int childAt = EnvFrame("COMBO_DEBUG_COOP_CHILD");
    static Actor* harnessChild = nullptr;
    static int childKillAt = -1;
    if ((int)sFrame == childAt) {
        const char* v = std::getenv("COMBO_DEBUG_COOP_CHILD");
        const char* c = v ? std::strchr(v, ',') : nullptr;
        Actor* t = NearestSynced(c ? (int)std::strtol(c + 1, nullptr, 16) : -1);
        if (t != nullptr && sKeys[sActors[t].key].owner == CoopG_OwnClientId()) {
            Actor* prev = sUpdating;
            sUpdating = t;
            harnessChild = Actor_Spawn(&gPlayState->actorCtx, gPlayState, t->id, t->world.pos.x, t->world.pos.y,
                                       t->world.pos.z + 60.0f, 0, t->shape.rot.y, 0, sActors[t].spawnParams);
            sUpdating = prev;
            childKillAt = (int)sFrame + 200;
            SPDLOG_INFO("[CoopEnemy] harness: owner spawned child actor 0x{:X} of key {} -> {}", (int)t->id,
                        Coop::HexU64(sActors[t].key), harnessChild ? "ok" : "failed");
        } else {
            SPDLOG_INFO("[CoopEnemy] harness: child spawn skipped (not the owner)");
        }
    }
    if ((int)sFrame == childKillAt && harnessChild != nullptr && sActors.count(harnessChild) &&
        harnessChild->update != nullptr) {
        SPDLOG_INFO("[CoopEnemy] harness: owner kills its child key {}", Coop::HexU64(sActors[harnessChild].key));
        Actor_Kill(harnessChild);
        harnessChild = nullptr;
    }
    if ((int)sFrame == killAt) {
        // Kill the nearest synced enemy as its death would: claim, 0 HP, its random drop, Actor_Kill.
        if (Actor* t = NearestSynced()) {
            ActorState& st = sActors[t];
            KeyState& ks = sKeys[st.key];
            if (ks.owner != CoopG_OwnClientId())
                Claim(ks, st, "harness kill");
            t->colChkInfo.health = 0;
            st.dyingSinceMs = now;
            SPDLOG_INFO("[CoopEnemy] harness: kill actor 0x{:X} key {}", (int)t->id, Coop::HexU64(st.key));
            Item_DropCollectibleRandom(gPlayState, t, &t->world.pos, 0);
            Actor_Kill(t);
        }
    }
}

static void TraceDump(uint64_t now) {
    const uint32_t me = CoopG_OwnClientId();
    SPDLOG_INFO("[CoopEnemy] {} trace frame {} scene {} me {} peers-in-scene {} players {} k {:.2f}", kGameName,
                sFrame, sScene, me, ScenePeerCount(now), sPlayers, sK);
    int shown = 0;
    for (auto& [a, st] : sActors) {
        if (!st.eligible || a->update == nullptr || shown >= 16)
            continue;
        auto ks = sKeys.find(st.key);
        uint32_t owner = ks == sKeys.end() ? 0 : ks->second.owner;
        uint32_t epoch = ks == sKeys.end() ? 0 : ks->second.epoch;
        SPDLOG_INFO("[CoopEnemy]   0x{:04X} key {} room {} owner {}{} ep {} native {}/{} pool {} k {:.2f} pos "
                    "({:.0f},{:.0f},{:.0f})",
                    (int)a->id, Coop::HexU64(st.key), (int)a->room, owner, owner == me ? "*" : "", epoch,
                    (int)a->colChkInfo.health, st.baseHealth, (long long)st.wideHp, st.k, a->world.pos.x,
                    a->world.pos.y, a->world.pos.z);
        ++shown;
    }
}

// Teammate-owned actors become mirrors: posed, moved and collided from the owner's latest snapshot here, once per logic
// frame, before the enemies' own update slot (which OnShouldUpdate then skips). Runs inside Actor_UpdateAll after
// CollisionCheck_ClearContext, so the re-registered hurtboxes are checked next frame like any actor's.
static void MirrorPass(uint64_t now, bool active) {
    const uint32_t me = CoopG_OwnClientId();
    for (auto& [a, st] : sActors) {
        if (!st.eligible || a->update == nullptr) {
            st.mirror = false;
            continue;
        }
        auto it = sKeys.find(st.key);
        bool m = active && it != sKeys.end() && it->second.owner != me && it->second.owner != 0 &&
                 it->second.hasEntry && !(st.boss && (it->second.last.flags & kFlagDying));
        if (m != st.mirror && Trace())
            SPDLOG_INFO("[CoopEnemy] {} key {} actor 0x{:X} {}", kGameName, Coop::HexU64(st.key), (int)a->id,
                        m ? "is now a mirror of its owner" : "runs locally");
        st.mirror = m;
        if (!m)
            continue;
        KeyState& ks = it->second;
        if (ks.hasRec)
            ForwardHits(a, st, ks);
        if (DsOn())
            DsApply(a, st, ks);
        ApplyEntry(a, ks, now);
        if (ks.last.wideMax > 0 && ks.last.baseHealth > 0) { // the owner's pool is the truth
            st.baseHealth = ks.last.baseHealth;
            st.wideHp = ks.last.wideHp;
            st.wideMax = ks.last.wideMax;
        }
        if (ks.hasRec)
            ApplyRec(a, st, ks);
    }
}

// Once per frame from the local Link's update (players update before enemies and bosses).
static void Tick() {
    const uint64_t now = NowMs();
    int scene = COOP_SCENE(gPlayState);
    if (scene != sScene) {
        if (sScene != -1)
            ResetScene(); // scene changed without a Play_Destroy (OnPlayDestroy already reset otherwise)
        sScene = scene;
    }
    sTransition = false;
    sUpdating = nullptr;
    ++sFrame;

    const bool connected = CoopG_Connected();
    const int peers = connected ? ScenePeerCount(now) : 0;
    sPlayers = 1 + peers;
    sK = ActorFactor(sPlayers);
    if (connected)
        sPeerRooms = ScenePeerRooms(now);
    else
        sPeerRooms.clear();
    // HP scales by the players in each actor's OWN room: a teammate two rooms away does not toughen this one.
    const int myRoom = MyRoom();
    for (auto& [a, st] : sActors) {
        if (!st.eligible || !st.keyed || a->update == nullptr)
            continue;
        int n = RoomPlayers(a->room, myRoom, sPeerRooms);
        double k = ActorFactor(n);
        st.players = n;
        if (k != st.k) {
            st.k = k;
            if (a->colChkInfo.health > 0)
                RescalePool(a, st);
        }
    }

    HarnessTick(now);
    const bool active = connected && SyncOn() && peers > 0;
    if (active)
        OwnershipPass(now);
    MirrorPass(now, active);
    sGameThread = std::this_thread::get_id();
    RebuildOwnedRanges(active);
    if (!connected) {
        sSfxQueue.clear();
        sFxQueue.clear();
        return;
    }
    // Presence + owned actors every game logic frame (20 Hz), the same rate Anchor sends player movement.
    SendSnapshot(now, active);
    FlushEvents();
    if (Trace() && (sFrame % 60) == 0)
        TraceDump(now);
}

// ── inbound (game thread: called from the Anchor packet queue drain) ──
static void Demoted(uint64_t key, const Entry& theirs) {
    auto a = sByKey.find(key);
    if (a == sByKey.end())
        return;
    auto st = sActors.find(a->second);
    if (st == sActors.end())
        return;
    // Both of us hit it inside one round trip and their claim won: hand over the damage they never saw.
    int64_t mine = st->second.wideHp;
    if (NowMs() - st->second.lastLocalHitMs < 1000 && mine < theirs.wideHp && st->second.recentLocalDmg > 0) {
        int d = (int)std::min<int64_t>(st->second.recentLocalDmg, theirs.wideHp - mine);
        sEvents.push_back({ { "k", Coop::HexU64(key) }, { "e", "dmg" }, { "d", d } });
        st->second.recentLocalDmg = 0;
    }
}

static void OnSnap(const nlohmann::json& p, uint32_t from, uint64_t now) {
    PeerInfo& peer = sPeers[from];
    peer.scene = p.value("scene", -1);
    peer.room = p.value("room", -1);
    peer.pos = { p.value("px", 0.0f), p.value("py", 0.0f), p.value("pz", 0.0f) };
    peer.lastMs = now;
    if (peer.scene != sScene || !InPlay() || sTransition || p.value("v", 0u) != kWireVersion)
        return;
    if (p.value("b", std::string()) != Coop::HexU64(BuildTag())) {
        static bool warned = false;
        if (!warned)
            SPDLOG_WARN("[CoopEnemy] {} client {} runs a different build: enemy sync ignores it", kGameName, from);
        warned = true;
        return;
    }
    if (auto d = p.find("dead"); d != p.end() && d->is_array()) {
        for (auto& h : *d) {
            uint64_t key = h.is_string() ? Coop::ParseHexU64(h.get<std::string>()) : 0;
            if (key == 0 || !sDead.insert(key).second)
                continue;
            auto a = sByKey.find(key);
            if (a != sByKey.end())
                RemoteKill(a->second);
        }
    }
    if (!SyncOn())
        return;
    if (auto sl = p.find("s"); sl != p.end() && sl->is_array())
        ReplaySfx(*sl);
    if (auto fx = p.find("fx"); fx != p.end() && fx->is_array())
        ReplayFx(*fx, from);
    auto b = p.find("blob");
    if (b == p.end() || !b->is_string())
        return;
    std::vector<uint8_t> raw;
    std::vector<Entry> entries;
    if (!Coop::Base64Decode(b->get<std::string>(), raw) || !DecodeEntries(raw, entries))
        return;
    const uint32_t me = CoopG_OwnClientId();
    for (const Entry& e : entries) {
        KeyState& ks = sKeys[e.key];
        if (!ClaimBeats(e.epoch, from, ks.epoch, ks.owner))
            continue;
        if (ks.owner == me && from != me)
            Demoted(e.key, e);
        ks.owner = from;
        ks.epoch = e.epoch;
        ks.last = e;
        ks.hasEntry = true;
        ks.lastRecvMs = now;
    }
    auto m = p.find("m");
    if (m == p.end() || !m->is_string())
        return;
    std::vector<uint8_t> mraw;
    std::vector<M::Rec> recs;
    if (!Coop::Base64Decode(m->get<std::string>(), mraw) || !M::DecodeBlob(mraw, recs))
        return;
    const uint32_t f = p.value("f", 0u);
    for (M::Rec& r : recs) {
        auto it = sKeys.find(r.key);
        if (it == sKeys.end() || it->second.owner != from)
            continue;
        if (r.parentKey != 0 && !sByKey.count(r.key) && it->second.hasEntry && !sTransition)
            SpawnMirrorChild(r, it->second.last, now);
        it->second.rec = std::move(r);
        it->second.hasRec = true;
        it->second.recFresh = true;
        it->second.recFrame = f;
    }
    if (auto ds = p.find("ds"); ds != p.end() && ds->is_array())
        for (auto& e : *ds)
            if (auto k = sKeys.find(Coop::ParseHexU64(e.value("k", std::string()))); k != sKeys.end() && k->second.owner == from)
                DsReceive(e, f);
}

static void OnEvent(const nlohmann::json& p, uint32_t from) {
    if (p.value("scene", -1) != sScene || !InPlay())
        return;
    auto evs = p.find("events");
    if (evs == p.end() || !evs->is_array())
        return;
    const uint32_t me = CoopG_OwnClientId();
    for (auto& ev : *evs) {
        uint64_t key = Coop::ParseHexU64(ev.value("k", std::string()));
        std::string kind = ev.value("e", std::string());
        auto a = sByKey.find(key);
        Actor* actor = a == sByKey.end() ? nullptr : a->second;
        if (kind == "kill" || kind == "despawn") {
            if (kind == "kill" && ev.value("s", 0) == 1) {
                sDead.insert(key);
            }
            if (actor) {
                if (Trace())
                    SPDLOG_INFO("[CoopEnemy] {} remote {} actor 0x{:X} key {}", kGameName, kind, (int)actor->id,
                                Coop::HexU64(key));
                RemoteKill(actor);
            }
        } else if (kind == "hit" && actor && sKeys[key].owner == me) {
            FwdHit h;
            h.off = ev.value("c", 0u);
            h.elem = ev.value("i", -1);
            h.damage = ev.value("d", 0);
            h.damageEffect = ev.value("de", 0);
            h.tFlags = ev.value("tf", 0u);
            h.tEffect = ev.value("te", 0);
            h.tDamage = ev.value("td", 0);
            h.tMm = ev.value("tm", 0);
            h.tMmAt = ev.value("ta", 0);
            if (auto hp = ev.find("hp"); hp != ev.end() && hp->is_array() && hp->size() == 3)
                for (int i = 0; i < 3; ++i)
                    h.hitPos[i] = (int16_t)(*hp)[i].get<int>();
            h.from = from;
            auto& q = sPendingHits[key];
            if (q.size() < 8)
                q.push_back(h);
        } else if (kind == "dmg" && actor && sKeys[key].owner == me && actor->colChkInfo.health > 0) {
            int d = ev.value("d", 0);
            auto sit = sActors.find(actor);
            if (sit != sActors.end() && HasPool(sit->second)) {
                sit->second.wideHp = std::max<int64_t>(1, sit->second.wideHp - std::max(0, d));
                WriteNative(actor, sit->second);
            } else {
                actor->colChkInfo.health = (u8)std::max(1, (int)actor->colChkInfo.health - std::max(0, d));
            }
        }
    }
}

static void OnPacket(const nlohmann::json& p) {
    if (p.value("game", std::string()) != kGameName)
        return;
    if (p.value("targetTeamId", std::string("default")) != TeamId())
        return;
    uint32_t from = p.value("clientId", (uint32_t)0);
    if (from == 0 || from == CoopG_OwnClientId())
        return;
    std::string type = p.value("type", std::string());
    uint64_t now = NowMs();
    if (type == Coop::kPktActorSnap)
        OnSnap(p, from, now);
    else if (type == Coop::kPktActorPlayerHit) {
        if (p.value("t", 0u) != CoopG_OwnClientId() || p.value("scene", -1) != sScene || !InPlay() || !SyncOn())
            return;
        PlayerHit h;
        h.key = Coop::ParseHexU64(p.value("k", std::string()));
        h.from = from;
        h.blocked = p.value("b", false);
        h.tFlags = p.value("tf", 0u);
        h.tEffect = p.value("te", 0);
        h.tDamage = p.value("td", 0);
        h.tMm = p.value("tm", 0);
        h.tMmAt = p.value("ta", 0);
        if (sPendingPlayerHits.size() < 4)
            sPendingPlayerHits.push_back(h);
    }
    else if (type == Coop::kPktActorEvent)
        OnEvent(p, from);
}

static void Register() {
    if (sRegistered)
        return;
    sRegistered = true;
    auto* gi = GameInteractor::Instance;
    gi->RegisterGameHook<GameInteractor::ShouldActorInit>([](auto* actor, bool* should) {
        if (*should)
            OnSpawnInit((Actor*)actor);
    });
    gi->RegisterGameHook<GameInteractor::ShouldActorUpdate>(
        [](auto* actor, bool* should) { OnShouldUpdate((Actor*)actor, should); });
    gi->RegisterGameHook<GameInteractor::OnActorUpdate>([](auto* actor) { OnUpdated((Actor*)actor); });
    gi->RegisterGameHook<GameInteractor::OnActorKill>([](auto* actor) { OnKilled((Actor*)actor); });
    gi->RegisterGameHook<GameInteractor::OnActorDestroy>([](auto* actor) { OnDestroyed((Actor*)actor); });
    gi->RegisterGameHook<GameInteractor::OnPlayDestroy>([]() {
        sTransition = true;
        sOwnedRanges.clear();
        sSfxQueue.clear();
        sFxQueue.clear();
        sSkels.clear();
        ResetScene();
        sScene = -1;
    });
}

} // namespace CoopEnemyGlue
