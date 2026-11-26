// combo/tests/ComboSharedHealthSlotsTest.cpp
//
// Tests for which slot each game's shared health state belongs to (combo/ComboSharedHealthSlots.h). A small model of
// the launcher holds each slot's combo.health and each game's live state, and moves them with the same calls, in the
// same order, as ComboShip.cpp does at a save read (Combo_ReadGameSaveAs), a save write (Combo_WriteGameSave), a game
// switch (Combo_HandoffSharedHealth), a new file (Combo_OnOOTSaveInit), an erase and a copy. No game DLL. Build and
// run: `cmake --build <dir> --target comboSharedHealthSlotsTest`, then run the exe; it exits 0 when every check passes.
#include <map>
#include <string>

#include "ComboSharedHealthSlots.h"
#include "ComboTestCheck.h"

namespace {

constexpr int kOot = 0;
constexpr int kMm = 1;

// A state is a label ("A" = slot 0's hearts); "" is no state (a container without combo.health, or a game that holds
// none, which the launcher reads as health < 0).
struct LauncherModel {
    ComboHealth::SlotOwnership owners;
    std::map<int, std::string> container; // combo.health per slot
    std::string live[2];                  // each game's state
    int active = kOot;                    // g_comboHealthActiveGame, also the foreground game here

    bool Read(int game, int slot) {
        return ComboHealth::ApplySaveRead(
            owners, active, !live[active].empty(), game, slot, [&] { container[slot] = live[active]; },
            [&] { live[game] = container[slot]; });
    }
    void Write(int game, int slot) {
        if ((game == active) && owners.Owns(game, slot))
            container[slot] = live[game];
    }
    void Handoff(int fromGame, int toGame, int slot) {
        live[toGame] = live[fromGame];
        owners.HandedOff(fromGame, toGame);
        if (owners.Owns(toGame, slot))
            container[slot] = live[toGame];
    }
    void NewFile(int slot) {
        container.erase(slot);
        owners.NewFile(slot);
        live[kOot] = live[kMm] = "";
    }
    void Erase(int slot) {
        container.erase(slot);
        owners.Forget(slot);
    }
    void Copy(int from, int to) {
        container[to] = container[from];
        owners.Forget(to);
    }
};

LauncherModel ThreeSlots() {
    LauncherModel m;
    m.container = { { 0, "A" }, { 1, "B" }, { 2, "C" } };
    return m;
}

// The file select reads every slot at boot: no slot's hearts reach another slot, and the slot then loaded plays with
// its own. (Before: slots 1 and 2 were given slot 0's state, and slot 1 was played with it.)
void TestBootReadsKeepEachSlotsOwn() {
    LauncherModel m = ThreeSlots();
    for (int slot : { 0, 1, 2 })
        m.Read(kOot, slot);
    CHECK(m.container[0] == "A");
    CHECK(m.container[1] == "B");
    CHECK(m.container[2] == "C");
    CHECK(!m.Read(kOot, 1)); // loading slot 1 hands OoT slot 1's state ...
    CHECK(m.live[kOot] == "B");
    CHECK(m.owners.SlotOf(kOot) == 1);
    m.Write(kOot, 1); // ... and its saves store it as slot 1's
    CHECK(m.container[1] == "B");
    CHECK(m.container[0] == "A");
}

// The magic-desync fix: a read of the slot in play, by the game in play, keeps its live state (newer than the last
// save write) and refreshes the container from it. MM reads its save during play.
void TestReadMidPlayKeepsLiveState() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 1);
    m.live[kOot] = "B+fairy";
    CHECK(m.Read(kOot, 1));
    CHECK(m.live[kOot] == "B+fairy");
    CHECK(m.container[1] == "B+fairy");

    m.Handoff(kOot, kMm, 1); // Link enters Termina ...
    m.active = kMm;
    m.live[kMm] = "B+refill"; // ... a Great Fairy refills the meter there
    CHECK(m.Read(kMm, 1));    // MM reads its save mid-play
    CHECK(m.live[kMm] == "B+refill");
    CHECK(m.container[1] == "B+refill");
}

// Link arriving in MM while OoT is still the game in play: MM's read takes the slot's state, refreshed from OoT's live
// one first; the same when OoT reads while MM is in play (the dormant game).
void TestOtherGameTakesFreshCopy() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 1);
    m.live[kOot] = "B+damage";
    CHECK(!m.Read(kMm, 1));
    CHECK(m.container[1] == "B+damage");
    CHECK(m.live[kMm] == "B+damage");
    CHECK(m.owners.SlotOf(kMm) == 1);

    m.active = kMm;
    m.live[kMm] = "B+spell";
    CHECK(!m.Read(kOot, 1));
    CHECK(m.live[kOot] == "B+spell");
    CHECK(m.container[1] == "B+spell");
}

// A game switch carries the state both ways and stores it as the slot it came from.
void TestHandoffStoresOnlyItsOwnSlot() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 1);
    m.live[kOot] = "B2";
    m.Handoff(kOot, kMm, 1);
    CHECK(m.live[kMm] == "B2");
    CHECK(m.container[1] == "B2");
    CHECK(m.owners.SlotOf(kMm) == 1);

    // A handoff naming another slot than the state's (never expected) stores nothing there.
    m.Handoff(kMm, kOot, 2);
    CHECK(m.container[2] == "C");
}

// After a session in MM ends on OoT's file select (reset, owl-save quit), MM's state is handed to OoT; loading another
// slot then plays with that slot's own state, and nothing is written into it.
void TestReturnThenLoadAnotherSlot() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 1);
    m.Handoff(kOot, kMm, 1);
    m.active = kMm;
    m.live[kMm] = "B3";
    m.Handoff(kMm, kOot, 1);
    m.active = kOot;
    CHECK(m.container[1] == "B3");
    CHECK(!m.Read(kOot, 2));
    CHECK(m.live[kOot] == "C");
    CHECK(m.container[2] == "C");
    CHECK(m.container[1] == "B3");
}

// #30: a file erased and a new one made in its slot. The erased file's state must not be kept or stored for it, and
// the new file starts with none.
void TestEraseThenNewFileInSameSlot() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 0); // boot read: OoT holds slot 0's state
    CHECK(m.live[kOot] == "A");
    m.Erase(0);
    CHECK(m.owners.SlotOf(kOot) == ComboHealth::kNoSlot);
    m.Write(kOot, 0); // the new file's first write, before Combo_OnOOTSaveInit
    CHECK(m.container.count(0) == 0);
    m.NewFile(0);
    CHECK(m.owners.Owns(kOot, 0));
    CHECK(m.owners.Owns(kMm, 0));
    CHECK(!m.Read(kOot, 0)); // the load: an empty state from the new container
    CHECK(m.live[kOot].empty());
    m.live[kOot] = "N"; // the first tick
    m.Write(kOot, 0);
    CHECK(m.container[0] == "N");
}

// Without the new-file reset the erase alone still keeps the old state from being kept for the slot.
void TestEraseAloneDoesNotKeepOldState() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 0);
    m.Erase(0);
    m.container[0] = ""; // a new file's container, combo.health absent
    CHECK(!m.Read(kOot, 0));
    CHECK(m.live[kOot].empty());
}

// A copy onto a slot replaces its contents: a state taken from it before is not kept for the copy.
void TestCopyOntoSlotTakesCopiedState() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 2);
    m.Copy(0, 2);
    CHECK(!m.Read(kOot, 2));
    CHECK(m.live[kOot] == "A");
}

// The foreground game's write of a slot it holds no state of leaves that slot's combo.health alone.
void TestWriteOfAnotherSlotStoresNothing() {
    LauncherModel m = ThreeSlots();
    m.Read(kOot, 2);
    m.Write(kOot, 1);
    CHECK(m.container[1] == "B");
    m.Write(kOot, 2);
    CHECK(m.container[2] == "C");
}

void TestOwnershipEdges() {
    ComboHealth::SlotOwnership o;
    CHECK(o.SlotOf(kOot) == ComboHealth::kNoSlot);
    CHECK(!o.Owns(kOot, ComboHealth::kNoSlot));
    CHECK(o.SlotOf(-1) == ComboHealth::kNoSlot);
    CHECK(o.SlotOf(2) == ComboHealth::kNoSlot);
    o.Pushed(5, 1); // not a game: ignored
    CHECK(!o.Owns(kOot, 1) && !o.Owns(kMm, 1));
    o.Pushed(kOot, 1);
    o.HandedOff(kOot, 7); // not a game: ignored
    CHECK(o.SlotOf(kMm) == ComboHealth::kNoSlot);
    o.Forget(2); // another slot: kept
    CHECK(o.Owns(kOot, 1));
    o.Forget(1);
    CHECK(!o.Owns(kOot, 1));
    // An invalid game in play never has a live state of the slot read.
    int stores = 0;
    int pushes = 0;
    CHECK(!ComboHealth::ApplySaveRead(
        o, 9, true, kOot, 1, [&] { stores++; }, [&] { pushes++; }));
    CHECK(stores == 0 && pushes == 1);
}

} // namespace

int main() {
    TestBootReadsKeepEachSlotsOwn();
    TestReadMidPlayKeepsLiveState();
    TestOtherGameTakesFreshCopy();
    TestHandoffStoresOnlyItsOwnSlot();
    TestReturnThenLoadAnotherSlot();
    TestEraseThenNewFileInSameSlot();
    TestEraseAloneDoesNotKeepOldState();
    TestCopyOntoSlotTakesCopiedState();
    TestWriteOfAnotherSlotStoresNothing();
    TestOwnershipEdges();

    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks FAILED\n";
        return 1;
    }
    std::cout << "comboSharedHealthSlotsTest: all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
