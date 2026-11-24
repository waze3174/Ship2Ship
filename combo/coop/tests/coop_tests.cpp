// combo/coop/tests/coop_tests.cpp — headless unit tests for the shared co-op core (full-coop-sync §13).
// Links no game DLL. Build: `cmake --build build-cmake --target coop_tests`, run the binary; exit 0 = pass.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <zlib.h>

#include "coop/CoopBase64.h"
#include "coop/CoopHash.h"
#include "coop/CoopEnemySync.h"
#include "coop/CoopMirror.h"
#include "coop/CoopProtocol.h"
#include "coop/SeedIdentity.h"
#include "coop/SeedShare.h"
#include "coop/SeedShareCore.h"
#include "coop/SessionGuard.h"
#include "rando/SharedItems.h"

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++gChecks;                                                               \
        if (!(cond)) {                                                           \
            ++gFailures;                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static void TestHex() {
    for (uint64_t v : { 0ull, 1ull, 0xDEADBEEFull, 0xFFFFFFFFFFFFFFFFull, 0x0123456789abcdefull }) {
        CHECK(Coop::ParseHexU64(Coop::HexU64(v)) == v);
        CHECK(Coop::HexU64(v).size() == 16);
    }
    CHECK(Coop::ParseHexU64("zz") == 0);
    CHECK(Coop::ParseHexU64("") == 0);
    CHECK(Coop::ParseHexU64("11112222333344445") == 0); // 17 digits
    nlohmann::json j = { { "a", "00000000000000ff" }, { "b", 7 } };
    CHECK(Coop::JsonU64(j, "a") == 255);
    CHECK(Coop::JsonU64(j, "b") == 7);
    CHECK(Coop::JsonU64(j, "missing") == 0);
}

static void TestBase64() {
    std::mt19937 rng(1234);
    for (size_t len = 0; len < 300; ++len) {
        std::vector<uint8_t> in(len);
        for (auto& b : in)
            b = static_cast<uint8_t>(rng());
        std::string e = Coop::Base64Encode(in.data(), in.size());
        std::vector<uint8_t> out;
        CHECK(Coop::Base64Decode(e, out));
        CHECK(out == in);
    }
    std::vector<uint8_t> out;
    CHECK(!Coop::Base64Decode("abc", out));      // bad length
    CHECK(!Coop::Base64Decode("ab$=", out));     // bad char
    CHECK(!Coop::Base64Decode("a=bc", out));     // pad in the middle
    CHECK(!Coop::Base64Decode("ab==ab==", out)); // pad before the end
    CHECK(Coop::Base64Decode("", out) && out.empty());
}

static nlohmann::json SampleSeed() {
    nlohmann::json s;
    s["fileType"] = "ComboShipRandomizer";
    s["version"] = 1;
    s["seed"] = "hello";
    s["masterSeed"] = 12345u;
    s["displaySeed"] = 999u;
    s["file_hash"] = { 1, 2, 3, 4, 5 };
    s["oot"] = { { "settings", { { "gRandoSettings.Forest", 1 } } },
                 { "enabledTricks", nlohmann::json::array({ "RT_A" }) },
                 { "placements", { { "KF Midos Top Left Chest", "Bow" } } } };
    s["mm"] = { { "settings", { { "gRando.ShuffleGrass", 0 } } }, { "placements", nlohmann::json::object() } };
    s["worldCvars"] = { { "oot", { { "gEnhancements.NewDrops", 1 } } }, { "mm", nlohmann::json::object() } };
    s["playthrough"] = nlohmann::json::array({ "sphere 0" });
    return s;
}

static void TestSeedIdentity() {
    auto a = SampleSeed();
    auto idA = Coop::ComputeSeedIdentity(a);
    CHECK(idA.seedId != 0);

    // Stamping does not change the identity (identity fields are excluded).
    auto stamped = a;
    auto idS = Coop::StampSeedIdentity(stamped);
    CHECK(idS.seedId == idA.seedId);
    CHECK(Coop::ComputeSeedIdentity(stamped).seedId == idA.seedId);
    CHECK(Coop::JsonU64(stamped, "seedId") == idA.seedId);

    // Round-trips through text, pretty or compact (the transfer re-dumps; the file is dump(2)).
    CHECK(Coop::ComputeSeedIdentity(nlohmann::json::parse(a.dump(2))).seedId == idA.seedId);
    CHECK(Coop::ComputeSeedIdentity(nlohmann::json::parse(a.dump())).seedId == idA.seedId);

    // Derived blocks don't matter...
    auto b = a;
    b["playthrough"] = nlohmann::json::array({ "different" });
    b["displaySeed"] = 1u;
    CHECK(Coop::ComputeSeedIdentity(b).seedId == idA.seedId);
    // ...world-shaping content does.
    auto c = a;
    c["oot"]["placements"]["KF Midos Top Left Chest"] = "Hookshot";
    CHECK(Coop::ComputeSeedIdentity(c).seedId != idA.seedId);
    auto d = a;
    d["worldCvars"]["oot"]["gEnhancements.NewDrops"] = 0;
    auto idD = Coop::ComputeSeedIdentity(d);
    CHECK(idD.seedId != idA.seedId);
    CHECK(idD.worldCvarsHash != idA.worldCvarsHash);
    CHECK(idD.settingsHash == idA.settingsHash);
    auto e = a;
    e["mm"]["settings"]["gRando.ShuffleGrass"] = 1;
    CHECK(Coop::ComputeSeedIdentity(e).settingsHash != idA.settingsHash);
    // Old seeds without worldCvars still get an id.
    auto f = a;
    f.erase("worldCvars");
    CHECK(Coop::ComputeSeedIdentity(f).seedId != 0);
    CHECK(Coop::ComputeSeedIdentity(nlohmann::json()).seedId == 0);
}

static void TestMeta() {
    Coop::PeerMeta m;
    m.present = true;
    m.protocol = Coop::kCoopProtocol;
    m.buildHash = "abc";
    m.seedId = 0xFEDCBA9876543210ull;
    m.layoutHashMm = 42;
    m.game = 1;
    m.sceneId = 7;
    m.roomNum = 3;
    m.timeBucket = 4;
    auto back = Coop::MetaFromJson(nlohmann::json::parse(Coop::MetaToJson(m).dump()));
    CHECK(back.present && back.protocol == m.protocol && back.buildHash == "abc");
    CHECK(back.seedId == m.seedId && back.layoutHashMm == 42 && back.layoutHashOot == 0);
    CHECK(back.game == 1 && back.sceneId == 7 && back.roomNum == 3 && back.timeBucket == 4);
    CHECK(!Coop::MetaFromJson(nlohmann::json()).present);
    // Garbage fields read as defaults, never throw.
    auto g = Coop::MetaFromJson(nlohmann::json{ { "protocol", "x" }, { "seedId", 5.5 } });
    CHECK(g.present);
}

static void TestGuard() {
    using Coop::GuardVerdict;
    Coop::PeerMeta self, peer, legacy;
    self.present = peer.present = true;
    self.protocol = peer.protocol = Coop::kCoopProtocol;
    self.seedId = peer.seedId = 77;
    self.layoutHashOot = peer.layoutHashOot = 9;

    CHECK(Coop::ClassifyPacket("PLAYER_UPDATE") == Coop::GuardClass::Ungated);
    CHECK(Coop::ClassifyPacket("GIVE_ITEM") == Coop::GuardClass::SeedGated);
    CHECK(Coop::ClassifyPacket("COMBO_ACTOR_SNAP") == Coop::GuardClass::WorldGated);
    CHECK(Coop::ClassifyPacket("COMBO_ROOM_STATE") == Coop::GuardClass::WorldGated);

    CHECK(Coop::Judge("PLAYER_UPDATE", true, peer, self) == GuardVerdict::Accept);
    CHECK(Coop::Judge("GIVE_ITEM", true, peer, self) == GuardVerdict::Accept);
    CHECK(Coop::Judge("COMBO_ACTOR_SNAP", true, peer, self, 0) == GuardVerdict::Accept);

    auto other = peer;
    other.seedId = 78;
    CHECK(Coop::Judge("GIVE_ITEM", true, other, self) == GuardVerdict::DropSeed);
    CHECK(Coop::Judge("SET_FLAG", true, other, self) == GuardVerdict::DropSeed);
    CHECK(Coop::Judge("PLAYER_UPDATE", true, other, self) == GuardVerdict::Accept);

    // One side without a loaded save: progression passes (nothing to corrupt), world sync doesn't.
    auto noSave = peer;
    noSave.seedId = 0;
    CHECK(Coop::Judge("GIVE_ITEM", true, noSave, self) == GuardVerdict::Accept);
    CHECK(Coop::Judge("COMBO_ACTOR_EVENT", true, noSave, self) == GuardVerdict::DropNoSeed);
    // Two unseeded (vanilla) saves share the vanilla world: enemies sync (co-op combat 2026-11-13).
    auto vanillaSelf = self;
    vanillaSelf.seedId = 0;
    CHECK(Coop::Judge("COMBO_ACTOR_SNAP", true, noSave, vanillaSelf, 0) == GuardVerdict::Accept);

    auto proto = peer;
    proto.protocol = 999;
    CHECK(Coop::Judge("GIVE_ITEM", true, proto, self) == GuardVerdict::DropProtocol);

    // Legacy peers: progression keeps working, world sync is refused.
    CHECK(Coop::Judge("GIVE_ITEM", true, legacy, self) == GuardVerdict::Accept);
    CHECK(Coop::Judge("COMBO_ACTOR_SNAP", true, legacy, self) == GuardVerdict::DropLegacy);
    CHECK(Coop::Judge("COMBO_ACTOR_SNAP", false, peer, self) == GuardVerdict::DropLegacy);

    // Layout is per game and must be known on both sides.
    CHECK(Coop::Judge("COMBO_ACTOR_SNAP", true, peer, self, 1) == GuardVerdict::DropLayout);
    auto lay = peer;
    lay.layoutHashOot = 10;
    CHECK(Coop::Judge("COMBO_ACTOR_SNAP", true, lay, self, 0) == GuardVerdict::DropLayout);
}

static void TestAssembler() {
    std::string payload(1000003, 'x');
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(i * 7919) % 64];
    auto chunks = Coop::SplitChunks(payload, 20000); // 51 chunks: under kSeedMaxChunks
    int n = static_cast<int>(chunks.size());
    CHECK(n == static_cast<int>((payload.size() + 19999) / 20000));

    // Shuffled, duplicated, with foreign and malformed chunks mixed in.
    std::vector<int> order;
    for (int i = 0; i < n; ++i) {
        order.push_back(i);
        if (i % 5 == 0)
            order.push_back(i); // duplicate
    }
    std::mt19937 rng(99);
    std::shuffle(order.begin(), order.end(), rng);

    Coop::SeedAssembler a;
    a.Begin(555, n);
    CHECK(a.Active() && !a.Complete());
    CHECK(!a.Add(556, 0, n, chunks[0]));     // other seed
    CHECK(!a.Add(555, -1, n, chunks[0]));    // bad index
    CHECK(!a.Add(555, n, n, chunks[0]));     // out of range
    CHECK(!a.Add(555, 0, n + 1, chunks[0])); // n disagrees
    for (size_t k = 0; k < order.size(); ++k) {
        if (k == order.size() / 2)
            CHECK(!a.Complete()); // not done halfway
        CHECK(a.Add(555, order[k], n, chunks[static_cast<size_t>(order[k])]));
    }
    CHECK(a.Complete());
    CHECK(a.Join() == payload);

    // A dropped chunk leaves it incomplete; a re-request (Begin again) restarts cleanly.
    a.Begin(555, n);
    for (int i = 0; i < n; ++i)
        if (i != 3)
            a.Add(555, i, n, chunks[static_cast<size_t>(i)]);
    CHECK(!a.Complete());
    CHECK(a.Received() == n - 1);
    a.Add(555, 3, n, chunks[3]);
    CHECK(a.Complete() && a.Join() == payload);

    // Size limits.
    a.Begin(1, Coop::kSeedMaxChunks + 1);
    CHECK(!a.Active());
    a.Begin(1, 0);
    CHECK(!a.Active());
    CHECK(Coop::SplitChunks("", 10).size() == 1);
}

// The SeedShare wire pipeline end to end: json -> zlib -> base64 -> chunks -> reassemble -> verify.
static void TestSeedPipeline() {
    auto seed = SampleSeed();
    for (int i = 0; i < 5000; ++i)
        seed["oot"]["placements"]["Check " + std::to_string(i)] = "Item " + std::to_string(i % 37);
    auto id = Coop::ComputeSeedIdentity(seed);
    std::string compact = seed.dump();
    uLongf cap = compressBound(static_cast<uLong>(compact.size()));
    std::string z(cap, '\0');
    CHECK(compress2(reinterpret_cast<Bytef*>(z.data()), &cap, reinterpret_cast<const Bytef*>(compact.data()),
                    static_cast<uLong>(compact.size()), Z_BEST_COMPRESSION) == Z_OK);
    z.resize(cap);
    auto chunks = Coop::SplitChunks(Coop::Base64Encode(z), 1024);
    Coop::SeedAssembler a;
    a.Begin(id.seedId, static_cast<int>(chunks.size()));
    for (int i = static_cast<int>(chunks.size()) - 1; i >= 0; --i)
        a.Add(id.seedId, i, static_cast<int>(chunks.size()), chunks[static_cast<size_t>(i)]);
    CHECK(a.Complete());
    std::vector<uint8_t> zb;
    CHECK(Coop::Base64Decode(a.Join(), zb));
    std::string raw(compact.size(), '\0');
    uLongf len = static_cast<uLongf>(raw.size());
    CHECK(uncompress(reinterpret_cast<Bytef*>(raw.data()), &len, zb.data(), static_cast<uLong>(zb.size())) == Z_OK);
    CHECK(len == compact.size());
    CHECK(Coop::ComputeSeedIdentity(nlohmann::json::parse(raw)).seedId == id.seedId);
}

static void TestEffectiveHost() {
    SeedShare::RosterView r;
    r.ownClientId = 5;
    r.ownerClientId = 2;
    r.ownTeam = "default";
    auto peer = [](uint32_t id, bool online, const char* team = "default") {
        SeedShare::Peer p;
        p.clientId = id;
        p.online = online;
        p.teamId = team;
        return p;
    };
    r.peers = { peer(2, true), peer(7, true) };
    CHECK(r.EffectiveHost() == 2); // owner online
    r.peers = { peer(2, false), peer(7, true) };
    CHECK(r.EffectiveHost() == 5); // owner left: lowest online teammate (self)
    r.peers = { peer(2, false), peer(3, true), peer(1, true, "other") };
    CHECK(r.EffectiveHost() == 3); // other teams don't count
    r.ownerClientId = 5;
    CHECK(r.EffectiveHost() == 5); // we are the owner
}

// Pooled Shared Items (bottles): what each game's logic is credited with.
static void TestPooledMirror() {
    using namespace ComboRando;
    const uint32_t mask = 1u << SF_BOTTLES;
    auto bottles = [](const std::vector<std::string>& owned, bool oot) {
        size_t n = 0;
        for (auto& o : owned)
            for (auto& nm : PooledNames(SF_BOTTLES, oot))
                if (o == nm) {
                    ++n;
                    break;
                }
        return n;
    };
    std::vector<std::string> oot = { "Bottle with Fairy", "Empty Bottle", "Kokiri Sword" };
    std::vector<std::string> mm = { "Bottle of Milk", "Bottle of Chateau Romani", "Empty Bottle", "Hookshot" };
    PooledMirrorState st;
    ApplyPooledMirror(mask, oot, mm, st);
    CHECK(bottles(oot, true) == 5);       // 2 found + 3 mirrored
    CHECK(bottles(mm, false) == 5);       // 3 found + 2 mirrored
    ApplyPooledMirror(mask, oot, mm, st); // idempotent
    CHECK(bottles(oot, true) == 5 && bottles(mm, false) == 5);
    mm.push_back("Bottle With Gold Dust"); // a new MM find reaches OOT once
    ApplyPooledMirror(mask, oot, mm, st);
    CHECK(bottles(oot, true) == 6 && bottles(mm, false) == 6);
    oot.push_back("Bottle with Ruto's Letter");
    ApplyPooledMirror(mask, oot, mm, st);
    CHECK(bottles(oot, true) == 7 && bottles(mm, false) == 7);
    // Family off: nothing moves.
    std::vector<std::string> o2 = { "Empty Bottle" }, m2;
    PooledMirrorState st2;
    ApplyPooledMirror(0, o2, m2, st2);
    CHECK(m2.empty());
    CHECK(SharedFamilyByIndex(SF_BOTTLES).pooled && !SharedFamilyByIndex(SF_BOW).pooled);
    CHECK(SF_COUNT <= 32); // the shared mask is a u32
}

// Co-op combat (2026-11-13): enemy sync wire, keys, ownership rule and HP scaling.
static void TestEnemySync() {
    using namespace Coop::Enemy;
    // Keys: stable, sensitive to every input, never 0.
    uint64_t k = SetupKey(0, 0x55, 2, 7, 0x0095, 0x0003);
    CHECK(k == SetupKey(0, 0x55, 2, 7, 0x0095, 0x0003));
    CHECK(k != SetupKey(1, 0x55, 2, 7, 0x0095, 0x0003));
    CHECK(k != SetupKey(0, 0x56, 2, 7, 0x0095, 0x0003));
    CHECK(k != SetupKey(0, 0x55, 3, 7, 0x0095, 0x0003));
    CHECK(k != SetupKey(0, 0x55, 2, 8, 0x0095, 0x0003));
    CHECK(k != SetupKey(0, 0x55, 2, 7, 0x0096, 0x0003));
    CHECK(k != SetupKey(0, 0x55, 2, 7, 0x0095, 0x0004));
    uint64_t d0 = DynamicKey(0, 0x55, k, 0x10, 0, 0);
    CHECK(d0 != 0 && d0 != DynamicKey(0, 0x55, k, 0x10, 0, 1) && d0 == DynamicKey(0, 0x55, k, 0x10, 0, 0));
    CHECK(LayoutHash(0) != LayoutHash(1) && LayoutHash(0) != 0);

    // Wire round trip.
    std::vector<uint8_t> blob;
    Entry a;
    a.key = k;
    a.epoch = 3;
    a.actorId = 0x95;
    a.flags = kFlagLocked | kFlagBoss;
    a.health = 17;
    a.pos[0] = 1.5f;
    a.pos[1] = -200.25f;
    a.pos[2] = 3000.0f;
    a.rot[1] = -0x4000;
    a.shapeRot[2] = 0x1234;
    a.vel[0] = -2.0f;
    a.speed = 4.5f;
    a.k = 2.0f;
    a.freezeTimer = 40;
    a.colorFilterParams = 0x40FF;
    a.colorFilterTimer = 9;
    a.baseHealth = 8;
    EncodeEntry(blob, a);
    Entry b = a;
    b.key = d0;
    b.health = 0;
    EncodeEntry(blob, b);
    CHECK(blob.size() == 2 * kEntryBytes);
    std::vector<Entry> out;
    CHECK(DecodeEntries(blob, out) && out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].key == k && out[0].epoch == 3 && out[0].actorId == 0x95 && out[0].health == 17);
        CHECK(out[0].pos[1] == -200.25f && out[0].rot[1] == -0x4000 && out[0].shapeRot[2] == 0x1234);
        CHECK(out[0].vel[0] == -2.0f && out[0].speed == 4.5f && out[0].k == 2.0f && out[0].freezeTimer == 40);
        CHECK(out[0].colorFilterParams == 0x40FF && out[0].colorFilterTimer == 9 && out[0].baseHealth == 8);
        CHECK(out[0].flags == (kFlagLocked | kFlagBoss) && out[1].key == d0 && out[1].health == 0);
    }
    auto trunc = blob;
    trunc.pop_back();
    CHECK(!DecodeEntries(trunc, out));
    auto nan = blob;
    float qnan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(nan.data() + 16, &qnan, 4); // pos[0] of entry 0
    CHECK(!DecodeEntries(nan, out));

    // Ownership: higher epoch wins; tie -> lower clientId; anything beats "no owner".
    CHECK(ClaimBeats(1, 50, 0, 0));
    CHECK(ClaimBeats(2, 50, 1, 10));
    CHECK(!ClaimBeats(1, 50, 2, 10));
    CHECK(ClaimBeats(3, 10, 3, 50) && !ClaimBeats(3, 50, 3, 10));
    CHECK(ClaimBeats(3, 10, 3, 10)); // the owner's own stream keeps applying
    CHECK(ProximityChallenger(100, 300) && !ProximityChallenger(250, 300) && !ProximityChallenger(1600, 9000));

    // HP: x N by default, uncapped wide pool, native bar shows the pool's fraction.
    CHECK(PlayerFactor(1, 100) == 1.0 && PlayerFactor(2, 100) == 2.0 && PlayerFactor(4, 100) == 4.0);
    CHECK(PlayerFactor(3, 50) == 2.0 && PlayerFactor(2, 0) == 1.0 && PlayerFactor(2, 1500) == 16.0);
    CHECK(WideMax(10, 4.0) == 40 && WideMax(0, 4.0) == 0 && WideMax(200, 16.0) == 3200 && WideMax(8, 1.0) == 8);
    // Ganondorf-sized pool far above 255: native stays inside 1..40 until the pool is empty.
    int32_t wm = WideMax(40, 16.0);
    CHECK(wm == 640);
    CHECK(NativeFromWide(640, wm, 40) == 40 && NativeFromWide(320, wm, 40) == 20 && NativeFromWide(1, wm, 40) == 1);
    CHECK(NativeFromWide(0, wm, 40) == 0 && NativeFromWide(-5, wm, 40) == 0 && NativeFromWide(17, wm, 40) == 2);
    // Every 1-damage hit on a native-10 boss at k=4 costs one pool point; the bar crosses half at hit 20.
    int64_t pool = WideMax(10, 4.0);
    int hits = 0, halfAt = 0;
    while (pool > 0) {
        --pool;
        ++hits;
        int n = NativeFromWide(pool, 40, 10);
        CHECK(n >= 0 && n <= 10 && (n == 0) == (pool == 0));
        if (!halfAt && n <= 5)
            halfAt = hits;
    }
    CHECK(hits == 40 && halfAt == 20);
    CHECK(RescaleWide(20, 40, 80) == 40 && RescaleWide(0, 40, 80) == 0 && RescaleWide(1, 80, 8) == 1);
    // Rooms: an owner in another room has left; scene-wide actors (room -1) are never left.
    CHECK(OwnerLeftRoom(2, 3) && !OwnerLeftRoom(2, 2) && !OwnerLeftRoom(-1, 7) && OwnerLeftRoom(0, -1));
    CHECK(InActorRoom(-1, 5) && InActorRoom(4, 4) && !InActorRoom(4, 5));
    // HP players are counted in the actor's room only (scene-wide: everyone).
    CHECK(RoomPlayers(1, 1, { 1, 2, 1 }) == 3);
    CHECK(RoomPlayers(1, 2, { 1 }) == 1);
    CHECK(RoomPlayers(1, 2, { 3 }) == 1); // nobody in its room: still vanilla, never 0
    CHECK(RoomPlayers(-1, 0, { 1, 2 }) == 3);
    CHECK(RoomPlayers(3, 3, {}) == 1);
}

static void TestMirror() {
    using namespace Coop::Mirror;
    Rec r;
    r.key = 0xABCDEF0123ull;
    r.focus[1] = 40.5f;
    r.focusRot[1] = -1234;
    r.scale[0] = r.scale[1] = r.scale[2] = 0.01f;
    r.yOffset = 1200.0f;
    r.flags = 0x85;
    Skel s;
    s.off = 0x14C;
    s.anim = "__OTR__objects/object_st/gSkulltulaIdleAnim";
    s.curFrame = 7.5f;
    s.playSpeed = 1.0f;
    s.endFrame = 20.0f;
    s.animLength = 21.0f;
    s.mode = 2;
    for (int i = 0; i < 30 * 3; ++i)
        s.joints.push_back((int16_t)(i * 97 - 3000));
    r.skels.push_back(s);
    Col cyl;
    cyl.off = 0x200;
    cyl.shape = kCylinder;
    cyl.lists = kInAC | kInOC;
    cyl.acFlags = 0x09;
    cyl.ocFlags1 = 0x39;
    cyl.g = { 18, 50, 0, 1.f, 2.f, 3.f };
    cyl.single.acElemFlags = 5;
    cyl.single.acDmgFlags = 0xFFCFFFFF;
    r.cols.push_back(cyl);
    Col js;
    js.off = 0x260;
    js.shape = kJntSph;
    js.lists = kInAC;
    for (int i = 0; i < 3; ++i) {
        Elem e;
        e.acElemFlags = 1;
        e.g = { (float)i, 2.f, 3.f, 15.f };
        js.elems.push_back(e);
    }
    r.cols.push_back(js);
    Col tri;
    tri.off = 0x300;
    tri.shape = kTris;
    Elem te;
    te.g = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    tri.elems.push_back(te);
    r.cols.push_back(tri);
    Rec r2 = r;
    r2.key = 77;
    r2.parentKey = 0x1234567890ull;
    r2.params = -2;
    r2.skels.clear();
    std::vector<uint8_t> blob;
    EncodeBlob(blob, { r, r2 });
    std::vector<Rec> back;
    CHECK(DecodeBlob(blob, back) && back.size() == 2);
    if (back.size() == 2) {
        const Rec& b = back[0];
        CHECK(b.key == r.key && b.focus[1] == 40.5f && b.focusRot[1] == -1234 && b.yOffset == 1200.0f && b.flags == 0x85);
        CHECK(b.skels.size() == 1 && b.skels[0].anim == s.anim && b.skels[0].joints == s.joints &&
              b.skels[0].curFrame == 7.5f && b.skels[0].mode == 2 && b.skels[0].off == 0x14C);
        CHECK(b.cols.size() == 3 && b.cols[0].g == cyl.g && b.cols[0].single.acDmgFlags == 0xFFCFFFFF &&
              b.cols[0].lists == (kInAC | kInOC));
        CHECK(b.cols[1].elems.size() == 3 && b.cols[1].elems[2].g[0] == 2.f && b.cols[2].elems[0].g[8] == 9.f);
        CHECK(back[1].key == 77 && back[1].skels.empty() && back[1].cols.size() == 3);
        CHECK(back[1].parentKey == 0x1234567890ull && back[1].params == -2 && back[0].parentKey == 0);
    }
    // Truncated / garbage blobs decode to nothing.
    for (size_t cut : { (size_t)1, (size_t)5, blob.size() / 2, blob.size() - 1 }) {
        std::vector<uint8_t> t(blob.begin(), blob.begin() + cut);
        CHECK(!DecodeBlob(t, back) && back.empty());
    }
    std::vector<uint8_t> bad = blob;
    bad[0] = 99;
    CHECK(!DecodeBlob(bad, back));
    std::vector<uint8_t> nan = blob;
    float qnan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(nan.data() + 3 + 4 + 8, &qnan, 4); // first record's focus.x
    CHECK(!DecodeBlob(nan, back));
    // SkelAnimes of an actor: inside its instance only, by offset.
    auto offs = SkelOffsets({ 0x1000 + 0x300, 0x1000 + 0x14C, 0x5000, 0x1000 + 0x14C, 0x1000 + 0x3F0 }, 0x1000, 0x400, 0x44);
    CHECK(offs.size() == 2 && offs[0] == 0x14C && offs[1] == 0x300);
    CHECK(FitsInstance(0x3B4, 0x4C, 0x400) && !FitsInstance(0x3B5, 0x4C, 0x400));
}

int main() {
    TestHex();
    TestBase64();
    TestSeedIdentity();
    TestMeta();
    TestGuard();
    TestEnemySync();
    TestMirror();
    TestAssembler();
    TestSeedPipeline();
    TestEffectiveHost();
    TestPooledMirror();
    std::printf("coop_tests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
