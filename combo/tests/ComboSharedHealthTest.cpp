// combo/tests/ComboSharedHealthTest.cpp
//
// Tests for the shared health bar's heart tally (combo/ComboSharedHealth.h): a heart item changes a game's own fields
// the way that game's Item_Give does, and ComboHealth::Tick runs as each game's frame hook runs it. Every piece or
// container is worth half (an eighth of a heart per quarter), so one Heart Container is half a heart, and the bar shows
// a new heart only once the eighths make one (#39). No game DLL. Build and run:
// `cmake --build <dir> --target comboSharedHealthTest`, then run the exe; it exits 0 when every check passes.
#include <string>

#include "ComboSharedHealth.h"
#include "ComboTestCheck.h"

namespace {

using ComboHealth::kGameMm;
using ComboHealth::kGameOot;

constexpr int16_t kHeart = 0x10;

// One game's native health fields, changed as its own code changes them.
struct GameFields {
    int16_t capacity = ComboHealth::kBaseCapacity;
    int16_t health = ComboHealth::kBaseCapacity;
    uint32_t questItems = 0;
    uint8_t doubleDefense = 0;
    int8_t defenseHearts = 0;
    int growthWeight = 1;

    ComboHealth::Native Native() {
        ComboHealth::Native n{}; // no magic, no ammo: those parts of the tick skip themselves
        n.growthWeight = growthWeight;
        n.healthCapacity = &capacity;
        n.health = &health;
        n.questItems = &questItems;
        n.doubleDefenseFlag = &doubleDefense;
        n.defenseHearts = &defenseHearts;
        return n;
    }
    int Pieces() const {
        return (int)((questItems & ComboHealth::kHeartPieceMask) >> ComboHealth::kHeartPieceShift);
    }
    // Item_Give(ITEM_HEART_CONTAINER), the same in both games.
    void GiveContainer() {
        capacity += kHeart;
        health += kHeart;
    }
    // Item_Give(ITEM_HEART_PIECE), then the roll at four pieces, as each game's give path rolls it.
    void GivePiece() {
        questItems += 1u << ComboHealth::kHeartPieceShift;
        if (Pieces() >= 4) {
            questItems &= ~ComboHealth::kHeartPieceMask;
            capacity += kHeart;
            health += kHeart;
        }
    }
};

int Tally(const ComboHealth::Runtime& rt) {
    return rt.s.quarters[kGameOot] + rt.s.quarters[kGameMm];
}

// A game switch or a save read: the arriving game takes the state as JSON, with no runtime memory.
void TakeState(ComboHealth::Runtime& to, const ComboHealth::Runtime& from) {
    const std::string json = ComboHealth::ToJson(from);
    ComboHealth::FromJson(to, json.c_str());
}

void TestContainerIsHalfAHeart() {
    for (int game : { kGameOot, kGameMm }) {
        ComboHealth::Runtime rt;
        GameFields g;
        ComboHealth::Tick(rt, game, g.Native());
        g.GiveContainer();
        ComboHealth::Tick(rt, game, g.Native());
        CHECK(Tally(rt) == 4);
        CHECK(g.capacity == ComboHealth::kBaseCapacity); // the bar still shows 3 hearts
        CHECK(g.health == ComboHealth::kBaseCapacity);
        CHECK(ComboHealth::LeftoverEighths(rt.s) == 4); // the pause screen's piece meter: half a heart
        CHECK(g.Pieces() == 2);                         // the game's own counter, in whole quarters

        g.GiveContainer();
        ComboHealth::Tick(rt, game, g.Native());
        CHECK(Tally(rt) == 8);
        CHECK(g.capacity == ComboHealth::kBaseCapacity + kHeart);
        CHECK(ComboHealth::LeftoverEighths(rt.s) == 0);
    }
}

void TestContainerCountedOnce() {
    ComboHealth::Runtime rt;
    GameFields g;
    ComboHealth::Tick(rt, kGameOot, g.Native());
    g.GiveContainer();
    for (int frame = 0; frame < 10; frame++) {
        ComboHealth::Tick(rt, kGameOot, g.Native());
    }
    CHECK(Tally(rt) == 4);
}

void TestContainerCompletesAHalfHeart() {
    ComboHealth::Runtime rt;
    GameFields g;
    ComboHealth::Tick(rt, kGameMm, g.Native());
    for (int i = 0; i < 4; i++) { // four pieces: half a heart, rolled natively at the fourth
        g.GivePiece();
        ComboHealth::Tick(rt, kGameMm, g.Native());
    }
    CHECK(Tally(rt) == 4);
    CHECK(g.capacity == ComboHealth::kBaseCapacity);
    g.GiveContainer();
    ComboHealth::Tick(rt, kGameMm, g.Native());
    CHECK(Tally(rt) == 8);
    CHECK(g.capacity == ComboHealth::kBaseCapacity + kHeart);
}

void TestOneContainerInEachGameMakesAHeart() {
    ComboHealth::Runtime ootRt;
    ComboHealth::Runtime mmRt;
    GameFields oot;
    GameFields mm;
    ComboHealth::Tick(ootRt, kGameOot, oot.Native());
    oot.GiveContainer();
    ComboHealth::Tick(ootRt, kGameOot, oot.Native());
    TakeState(mmRt, ootRt); // Link crosses into Termina
    ComboHealth::Tick(mmRt, kGameMm, mm.Native());
    CHECK(mm.capacity == ComboHealth::kBaseCapacity);
    mm.GiveContainer();
    ComboHealth::Tick(mmRt, kGameMm, mm.Native());
    CHECK(Tally(mmRt) == 8);
    CHECK(mm.capacity == ComboHealth::kBaseCapacity + kHeart);
    TakeState(ootRt, mmRt); // and back: Hyrule shows the same heart
    ComboHealth::Tick(ootRt, kGameOot, oot.Native());
    CHECK(oot.capacity == ComboHealth::kBaseCapacity + kHeart);
}

void TestContainerOnFirstTickAfterRead() {
    ComboHealth::Runtime rt;
    GameFields g;
    ComboHealth::Tick(rt, kGameOot, g.Native());
    const ComboHealth::Runtime saved = rt;
    TakeState(rt, saved); // a save read hands the game a state: no runtime memory until the next tick
    g.GiveContainer();    // the pickup lands before that tick
    ComboHealth::Tick(rt, kGameOot, g.Native());
    CHECK(Tally(rt) == 4);
}

void TestContainerDeliveredWhileDormant() {
    ComboHealth::Runtime ootRt;
    ComboHealth::Runtime mmRt;
    GameFields oot;
    GameFields mm;
    ComboHealth::Tick(mmRt, kGameMm, mm.Native()); // MM was played: the state knows what MM's save holds
    TakeState(ootRt, mmRt);
    ComboHealth::Tick(ootRt, kGameOot, oot.Native());
    mm.GiveContainer(); // MM_GrantCrossItem into the dormant save
    TakeState(mmRt, ootRt);
    ComboHealth::Tick(mmRt, kGameMm, mm.Native());
    CHECK(Tally(mmRt) == 4);
    CHECK(mm.capacity == ComboHealth::kBaseCapacity);
}

void TestCapacityStopsAtTwentyHearts() {
    ComboHealth::Runtime rt;
    rt.s.quarters[kGameOot] = 68; // both games fully collected: 3 + 34 / 2 = 20 hearts
    rt.s.quarters[kGameMm] = 68;
    rt.s.seen[kGameOot] = rt.s.seen[kGameMm] = true;
    CHECK(ComboHealth::SharedCapacity(rt.s) == ComboHealth::kMaxCapacity);
    GameFields g;
    g.capacity = ComboHealth::kMaxCapacity;
    ComboHealth::Tick(rt, kGameOot, g.Native());
    g.GiveContainer();
    ComboHealth::Tick(rt, kGameOot, g.Native());
    CHECK(g.capacity == ComboHealth::kMaxCapacity);
    CHECK(g.health <= ComboHealth::kMaxCapacity);
    CHECK(ComboHealth::LeftoverEighths(rt.s) == 0);
}

// The log line each game's frame hook writes (HeartsBeforeTick / HeartsLogLine).
std::string TickAndLog(ComboHealth::Runtime& rt, int game, GameFields& g) {
    const ComboHealth::HeartsBefore before = ComboHealth::HeartsBeforeTick(rt, g.Native());
    ComboHealth::Tick(rt, game, g.Native());
    return ComboHealth::HeartsLogLine(rt, before, game == kGameOot ? "OOT" : "MM");
}

void TestLogLines() {
    ComboHealth::Runtime rt;
    GameFields g;
    const std::string entry = TickAndLog(rt, kGameOot, g);
    CHECK(entry == "[ComboShip] shared hearts, OOT took the state (its save held 3 hearts): 3 hearts + 0/8 "
                   "(eighths: OoT 0, MM 0)");
    CHECK(TickAndLog(rt, kGameOot, g).empty()); // a quiet frame
    g.GiveContainer();
    CHECK(TickAndLog(rt, kGameOot, g) ==
          "[ComboShip] shared hearts, OOT tally 4/8: 3 hearts + 4/8 (eighths: OoT 4, MM 0)");

    // Another file's state at the cap, taken by a file whose own save holds 3 hearts (#30): the line says both.
    ComboHealth::Runtime capped;
    capped.s.quarters[kGameOot] = capped.s.quarters[kGameMm] = 68;
    capped.s.seen[kGameOot] = capped.s.seen[kGameMm] = true;
    capped.s.written[kGameOot] = capped.s.written[kGameMm] = ComboHealth::kMaxCapacity;
    capped.s.writtenPieces[kGameOot] = capped.s.writtenPieces[kGameMm] = 0;
    ComboHealth::Runtime mmRt;
    TakeState(mmRt, capped);
    GameFields mm;
    CHECK(TickAndLog(mmRt, kGameMm, mm) == "[ComboShip] shared hearts, MM took the state (its save held 3 hearts): "
                                           "20 hearts + 0/8 (eighths: OoT 68, MM 68)");

    // Sharing off: TickIfEnabled releases instead of ticking, and nothing is logged.
    ComboHealth::Runtime off;
    const ComboHealth::HeartsBefore before = ComboHealth::HeartsBeforeTick(off, g.Native());
    ComboHealth::Release(off);
    CHECK(ComboHealth::HeartsLogLine(off, before, "OOT").empty());
}

// A back-to-back file turns sharing on with hearts already earned. The game in play keeps every one of them; what it
// picks up afterwards counts at the shared rule (half), and the other game's growth joins when it is first seen.
void TestTurningSharingOnInABackToBackFileTakesNoHeart() {
    constexpr int kWeight = 2;
    GameFields mm;
    mm.growthWeight = kWeight;
    mm.capacity = mm.health = ComboHealth::kBaseCapacity + 9 * kHeart; // 12 hearts and two pieces
    mm.questItems = 2u << ComboHealth::kHeartPieceShift;
    mm.health -= 3 * kHeart / 2;
    const int16_t healthBefore = mm.health;
    ComboHealth::Runtime rt;
    ComboHealth::Tick(rt, kGameMm, mm.Native());
    CHECK(mm.capacity == ComboHealth::kBaseCapacity + 9 * kHeart);
    CHECK(mm.Pieces() == 2);
    CHECK(mm.health == healthBefore);

    // Its next container is half a heart, as in any shared file.
    mm.GiveContainer();
    ComboHealth::Tick(rt, kGameMm, mm.Native());
    CHECK(mm.capacity == ComboHealth::kBaseCapacity + 10 * kHeart); // the half heart completes the two pieces' half
    CHECK(mm.Pieces() == 0);

    // Hyrule's game, first seen later, joins with its own hearts in full as well: 20 hearts in all.
    const int16_t healthAtSwitch = mm.health;
    ComboHealth::Runtime ootRt;
    TakeState(ootRt, rt);
    GameFields oot;
    oot.growthWeight = kWeight;
    oot.capacity = oot.health = ComboHealth::kMaxCapacity;
    ComboHealth::Tick(ootRt, kGameOot, oot.Native());
    CHECK(oot.capacity == ComboHealth::kMaxCapacity);
    CHECK(oot.health == healthAtSwitch); // current health is the one shared across the switch

    // Without the weight the same first sight costs the hearts: 12 become 7 and a half.
    GameFields plain;
    plain.capacity = plain.health = ComboHealth::kBaseCapacity + 9 * kHeart;
    ComboHealth::Runtime plainRt;
    ComboHealth::Tick(plainRt, kGameMm, plain.Native());
    CHECK(plain.capacity == ComboHealth::kBaseCapacity + 4 * kHeart);
}

} // namespace

int main() {
    TestContainerIsHalfAHeart();
    TestContainerCountedOnce();
    TestContainerCompletesAHalfHeart();
    TestOneContainerInEachGameMakesAHeart();
    TestContainerOnFirstTickAfterRead();
    TestContainerDeliveredWhileDormant();
    TestCapacityStopsAtTwentyHearts();
    TestLogLines();
    TestTurningSharingOnInABackToBackFileTakesNoHeart();

    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks FAILED\n";
        return 1;
    }
    std::cout << "comboSharedHealthTest: all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
