#pragma once
// ComboShip co-op combat: enemy/boss sync core (full-coop-sync plan §7 / §10, "Co-op combat" section).
// Header-only, game-agnostic, unit tested in coop_tests. The per-game glue (CoopEnemySyncGame.inl, included
// by soh/soh/Network/Anchor/CoopEnemySync.cpp and mm/2s2h/Network/Anchor/CoopEnemySync.cpp) owns the
// actors; this file owns everything that must be byte-identical between the two DLLs and between clients:
// sync keys, the wire entry, the ownership rule and the HP-scaling math.
//
// Protocol (both games, same shape; `game` = "oot" | "mm" keys everything by game):
//   COMBO_ACTOR_SNAP  (each client -> team, quiet, every game logic frame = 20 Hz, like Anchor's PLAYER_UPDATE)
//     { game, scene, room, px, py, pz, blob = base64(Entry[]), dead? = [hex keys] }
//     The header is the sender's presence (scene + Link position): it drives the player count, ownership
//     distance and the "retarget" trick. `blob` holds only actors the sender OWNS.
//   COMBO_ACTOR_EVENT (any -> team)
//     { game, scene, events: [ { k: hex key, e: "kill" | "despawn" | "dmg", d?: damage } ] }

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "CoopHash.h"

namespace Coop::Enemy {

// Bump on any change to Entry's byte layout or the key derivation.
// v2: wide HP pool (wideHp/wideMax) replaces the 127 cap + refund
// v3: mirror section "m" (CoopMirror.h: pose + hurtboxes), build tag "b", owner frame "f"
constexpr uint32_t kWireVersion = 3;
constexpr size_t kEntryBytes = 80;

// Stamped into state.combo.layoutHash so SessionGuard only pairs builds that speak the same actor wire. Mirrors copy
// raw engine structs, so the hash is build-specific too (the launcher's build time; the game DLLs additionally stamp
// and check their own build tag on every snapshot, see BuildTag in CoopEnemySyncGame.inl).
#ifndef COMBO_LAYOUT_BUILD_ID
#define COMBO_LAYOUT_BUILD_ID __DATE__ " " __TIME__
#endif
inline uint64_t LayoutHash(int game) {
    uint64_t h = Fnv1a64(game == 1 ? "combo-enemysync-mm-v3" : "combo-enemysync-oot-v3");
    h = HashCombine(h, Fnv1a64(COMBO_LAYOUT_BUILD_ID));
    return HashCombine(h, (uint64_t)kEntryBytes);
}


// ── keys ──
// Setup actors (spawned from the scene/room actor list): identical on every client running the same
// scene, independent of spawn order or pointer values.
inline uint64_t SetupKey(int game, int scene, int room, int listIndex, int actorId, int spawnParams) {
    uint64_t h = HashCombine(0x5E7u + (uint64_t)game, (uint64_t)(uint32_t)scene);
    h = HashCombine(h, (uint64_t)(uint32_t)(room & 0xFF));
    h = HashCombine(h, (uint64_t)(uint32_t)listIndex);
    h = HashCombine(h, (uint64_t)(uint32_t)(actorId & 0xFFFF));
    return HashCombine(h, (uint64_t)(uint32_t)(spawnParams & 0xFFFF));
}

// Runtime spawns: keyed by the synced actor whose update spawned it (or the scene, parentKey = 0), its own
// id/params, and how many identical (parent,id,params) spawns came before it this scene visit.
inline uint64_t DynamicKey(int game, int scene, uint64_t parentKey, int actorId, int spawnParams, int ordinal) {
    uint64_t h = HashCombine(0xD1Au + (uint64_t)game, (uint64_t)(uint32_t)scene);
    h = HashCombine(h, parentKey);
    h = HashCombine(h, (uint64_t)(uint32_t)(actorId & 0xFFFF));
    h = HashCombine(h, (uint64_t)(uint32_t)(spawnParams & 0xFFFF));
    return HashCombine(h, (uint64_t)(uint32_t)ordinal) | 1ull; // odd: never collides with "no key" (0)
}

// ── wire entry ──
enum EntryFlags : uint8_t {
    kFlagLocked = 1 << 0, // owner hit it recently: proximity claims are suppressed
    kFlagBoss = 1 << 1,
    kFlagDying = 1 << 2, // owner's copy reached 0 HP from > 0 (death sequence running there)
};

struct Entry {
    uint64_t key = 0;
    uint32_t epoch = 0;
    int16_t actorId = 0;
    uint8_t flags = 0;
    uint8_t health = 0;
    float pos[3] = {};
    int16_t rot[3] = {};      // world.rot
    int16_t shapeRot[3] = {}; // shape.rot
    float vel[3] = {};
    float speed = 0;
    float k = 1;               // owner's HP factor (informational: the debug trace prints it)
    uint16_t freezeTimer = 0;  // stun / freeze (Deku nut, ice arrow)
    uint16_t colorFilterParams = 0;
    uint8_t colorFilterTimer = 0;
    uint8_t baseHealth = 0; // unscaled max HP (the native bar's max), informational
    int32_t wideHp = 0;     // wide HP pool (see below); 0/0 = actor has no pool
    int32_t wideMax = 0;
};

namespace detail {
template <typename T> inline void Put(std::vector<uint8_t>& b, const T& v) {
    uint8_t tmp[sizeof(T)];
    std::memcpy(tmp, &v, sizeof(T));
    b.insert(b.end(), tmp, tmp + sizeof(T)); // little-endian on every shipping target (x86-64, arm64)
}
template <typename T> inline T Get(const uint8_t*& p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
}
} // namespace detail

inline void EncodeEntry(std::vector<uint8_t>& out, const Entry& e) {
    using detail::Put;
    size_t start = out.size();
    Put(out, e.key);
    Put(out, e.epoch);
    Put(out, e.actorId);
    Put(out, e.flags);
    Put(out, e.health);
    for (float f : e.pos)
        Put(out, f);
    for (int16_t s : e.rot)
        Put(out, s);
    for (int16_t s : e.shapeRot)
        Put(out, s);
    for (float f : e.vel)
        Put(out, f);
    Put(out, e.speed);
    Put(out, e.k);
    Put(out, e.freezeTimer);
    Put(out, e.colorFilterParams);
    Put(out, e.colorFilterTimer);
    Put(out, e.baseHealth);
    Put(out, e.wideHp);
    Put(out, e.wideMax);
    out.resize(start + kEntryBytes, 0);
}

// Returns false on a truncated/garbage blob (nothing is applied from it).
inline bool DecodeEntries(const std::vector<uint8_t>& blob, std::vector<Entry>& out) {
    out.clear();
    if (blob.size() % kEntryBytes != 0)
        return false;
    using detail::Get;
    for (size_t off = 0; off < blob.size(); off += kEntryBytes) {
        const uint8_t* p = blob.data() + off;
        Entry e;
        e.key = Get<uint64_t>(p);
        e.epoch = Get<uint32_t>(p);
        e.actorId = Get<int16_t>(p);
        e.flags = Get<uint8_t>(p);
        e.health = Get<uint8_t>(p);
        for (float& f : e.pos)
            f = Get<float>(p);
        for (int16_t& s : e.rot)
            s = Get<int16_t>(p);
        for (int16_t& s : e.shapeRot)
            s = Get<int16_t>(p);
        for (float& f : e.vel)
            f = Get<float>(p);
        e.speed = Get<float>(p);
        e.k = Get<float>(p);
        e.freezeTimer = Get<uint16_t>(p);
        e.colorFilterParams = Get<uint16_t>(p);
        e.colorFilterTimer = Get<uint8_t>(p);
        e.baseHealth = Get<uint8_t>(p);
        e.wideHp = Get<int32_t>(p);
        e.wideMax = Get<int32_t>(p);
        bool finite = std::isfinite(e.pos[0]) && std::isfinite(e.pos[1]) && std::isfinite(e.pos[2]) &&
                      std::isfinite(e.vel[0]) && std::isfinite(e.vel[1]) && std::isfinite(e.vel[2]) &&
                      std::isfinite(e.speed) && std::isfinite(e.k);
        if (!finite || e.key == 0)
            return false;
        out.push_back(e);
    }
    return true;
}

// ── ownership ──
// One owner per actor drives it (runs its AI authoritatively) and streams it; everyone else applies the
// stream. A claim bumps the epoch. Conflicts: higher epoch wins; on a tie the lower clientId wins. Pure, so
// every client resolves the same pair of claims the same way.
inline bool ClaimBeats(uint32_t epochA, uint32_t ownerA, uint32_t epochB, uint32_t ownerB) {
    if (ownerB == 0)
        return true;
    if (epochA != epochB)
        return epochA > epochB;
    return ownerA <= ownerB;
}

// Proximity hand-off ("the enemy goes after whoever is nearest"): a non-owner takes over once it has been
// clearly closer than the owner for a while, unless the owner is mid-fight (locked).
constexpr float kProximityRatio = 0.7f;  // challenger must be < 70% of the owner's distance...
constexpr int kProximityTicks = 10;      // ...for this many consecutive send ticks (~1 s)
constexpr float kProximityMaxDist = 1500.0f;
constexpr uint32_t kClaimLockMs = 1500;  // after a hit (bosses: x2)
constexpr uint32_t kOrphanMs = 4000;     // owner silent about this actor this long = orphaned
constexpr uint32_t kPeerStaleMs = 6000;  // no packet from a peer this long = gone from the scene

inline bool ProximityChallenger(float myDist, float ownerDist) {
    return myDist < kProximityMaxDist && myDist < ownerDist * kProximityRatio;
}

// ── rooms: a client only runs its loaded room ──
// An actor with room >= 0 belongs to that room; room < 0 (bosses, scene-wide actors) belongs to the whole scene.
inline bool InActorRoom(int actorRoom, int playerRoom) {
    return actorRoom < 0 || playerRoom == actorRoom;
}

// The owner's latest presence puts it in another room than the actor: it can no longer drive it ("owner left").
inline bool OwnerLeftRoom(int actorRoom, int ownerRoom) {
    return !InActorRoom(actorRoom, ownerRoom);
}

// Players fighting this actor: me (if I am in its room) + teammates in its room (scene-wide actor: everyone in
// the scene). Never below 1, so a client driving an actor alone keeps vanilla HP.
inline int RoomPlayers(int actorRoom, int myRoom, const std::vector<int>& peerRooms) {
    int n = InActorRoom(actorRoom, myRoom) ? 1 : 0;
    for (int r : peerRooms)
        n += InActorRoom(actorRoom, r) ? 1 : 0;
    return std::max(1, n);
}

// ── HP scaling by player count: the wide HP pool ──
// k = 1 + (n - 1) * pct / 100. Default pct = 100: HP x N (owner ruling 2026-11-12). No upper bound tied to u8.
constexpr int kMaxPctPerPlayer = 1500; // x16 per extra player
inline double PlayerFactor(int players, int pctPerExtraPlayer) {
    if (players <= 1 || pctPerExtraPlayer <= 0)
        return 1.0;
    return 1.0 + (double)(players - 1) * (double)std::min(pctPerExtraPlayer, kMaxPctPerPlayer) / 100.0;
}

// The scaled HP lives combo-side in a 32-bit pool: wideMax = nativeMax * k, any size. The actor's own u8
// colChkInfo.health only ever shows the pool's FRACTION of its vanilla bar:
//     native = ceil(wideHp * nativeMax / wideMax), clamped 1..nativeMax while wideHp > 0, 0 only when empty.
// So the native field never leaves vanilla range (no u8 wrap, no (s8) misreads in bosses), phase checks such as
// "health <= half" fire at the same fraction of the bar, and the actor dies exactly when the pool empties.
inline int32_t WideMax(int nativeMax, double k) {
    if (nativeMax <= 0)
        return 0;
    double v = std::ceil((double)nativeMax * std::max(1.0, k) - 1e-9);
    return (int32_t)std::min(v, 2.0e9);
}

inline int NativeFromWide(int64_t wideHp, int64_t wideMax, int nativeMax) {
    if (wideHp <= 0 || nativeMax <= 0)
        return 0;
    if (wideMax <= 0)
        return nativeMax;
    int64_t v = (wideHp * (int64_t)nativeMax + wideMax - 1) / wideMax; // ceil
    return (int)std::clamp<int64_t>(v, 1, nativeMax);
}

// Player count changed mid-fight: keep the pool's fraction. Never revives (0 stays 0), never kills (>0 stays >=1).
inline int32_t RescaleWide(int64_t wideHp, int64_t oldMax, int64_t newMax) {
    if (wideHp <= 0 || oldMax <= 0 || newMax <= 0)
        return (int32_t)std::max<int64_t>(0, wideHp);
    double v = std::round((double)wideHp * (double)newMax / (double)oldMax);
    return (int32_t)std::clamp(v, 1.0, (double)newMax);
}

} // namespace Coop::Enemy
