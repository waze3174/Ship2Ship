// combo/tests/ComboApSyncTest.cpp
//
// Tests for the online client's bookkeeping (combo/net/ComboApSync.h): which checks to send, which received items to
// grant, and what a restart, a reconnect, a repeated list, a lost packet or another room does to either. Build and run:
// `cmake --build <dir> --target comboApSyncTest`, then run the exe; it exits 0 when every check passes.
#include <string>
#include <vector>

#include "net/ComboApSync.h"
#include "ComboTestCheck.h"

namespace {

using namespace ComboApSync;

ReceivedItem Item(int index, const char* name, const char* player = "Zelda", int64_t location = 100) {
    ReceivedItem item;
    item.index = index;
    item.itemName = name;
    item.playerName = player;
    item.location = location;
    return item;
}

std::vector<ReceivedItem> Run(int first, int count) {
    std::vector<ReceivedItem> items;
    for (int i = 0; i < count; ++i) {
        items.push_back(Item(first + i, ("Item " + std::to_string(first + i) + " (OoT)").c_str()));
    }
    return items;
}

std::vector<int> Indexes(const std::vector<ReceivedItem>& items) {
    std::vector<int> out;
    for (const auto& item : items) {
        out.push_back(item.index);
    }
    return out;
}

using Names = std::vector<std::string>;
using Ints = std::vector<int>;

// ---- send ----

void TestOnlyWhatTheServerLacksIsSent() {
    SendLedger ledger;
    ledger.ResetForConnection({ "A (OoT)", "B (OoT)" });
    const auto fresh = ledger.Plan({ "A (OoT)", "B (OoT)", "C (OoT)", "D (MM)" });
    CHECK((fresh == Names{ "C (OoT)", "D (MM)" }));
    CHECK(ledger.knownCount() == 4);
}

void TestACheckIsSentOncePerConnection() {
    SendLedger ledger;
    ledger.ResetForConnection({});
    CHECK(ledger.Plan({ "A (OoT)" }).size() == 1);
    CHECK(ledger.Plan({ "A (OoT)" }).empty());
    CHECK((ledger.Plan({ "A (OoT)", "B (OoT)" }) == Names{ "B (OoT)" }));
}

void TestAReconnectSendsWhatTheServerNeverGot() {
    SendLedger ledger;
    ledger.ResetForConnection({});
    CHECK(ledger.Plan({ "A (OoT)", "B (OoT)" }).size() == 2);
    // The link broke before the server processed B; its checked list on the next connect holds only A.
    ledger.ResetForConnection({ "A (OoT)" });
    CHECK((ledger.Plan({ "A (OoT)", "B (OoT)" }) == Names{ "B (OoT)" }));
}

void TestAChecksOpenedElsewhereAreNotSentAgain() {
    SendLedger ledger;
    ledger.ResetForConnection({});
    ledger.NoteServerChecked({ "Released (OoT)" });
    CHECK((ledger.Plan({ "Released (OoT)", "Own (OoT)" }) == Names{ "Own (OoT)" }));
}

void TestABacklogGoesOutInPackets() {
    Names names;
    for (int i = 0; i < 1203; ++i) {
        names.push_back("Check " + std::to_string(i));
    }
    const auto packets = SplitIntoPackets(names);
    CHECK(packets.size() == 3);
    CHECK(packets[0].size() == kMaxLocationsPerPacket);
    CHECK(packets[2].size() == 203);
    CHECK(packets[2].back() == "Check 1202");
    CHECK(SplitIntoPackets(Names{}).empty());
}

// ---- receive ----

void TestItemsAreGrantedInOrderFromTheSavedIndex() {
    ReceiveLedger ledger(0);
    ledger.Accept(Run(0, 5));
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 0, 1, 2, 3, 4 }));
    ledger.MarkGranted(5);
    CHECK(ledger.granted() == 5);
    CHECK(ledger.NextGrants(100).empty());
    CHECK(ledger.pending() == 0);
}

void TestARestartSkipsWhatTheSaveAlreadyHolds() {
    // The save says 3 were given; the server sends all 5 again on connect.
    ReceiveLedger ledger(3);
    ledger.Accept(Run(0, 5));
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 3, 4 }));
    ledger.MarkGranted(2);
    CHECK(ledger.granted() == 5);
}

void TestAReconnectNeverGrantsAnItemTwice() {
    ReceiveLedger ledger(0);
    ledger.Accept(Run(0, 4));
    ledger.MarkGranted(static_cast<int>(ledger.NextGrants(100).size()));
    // The link drops and returns: the full list arrives again, with one new item at the end.
    ledger.DropBuffered();
    ledger.Accept(Run(0, 5));
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 4 }));
}

void TestAPartOfTheListIsGrantedInChunks() {
    ReceiveLedger ledger(0);
    ledger.Accept(Run(0, 20));
    const auto first = ledger.NextGrants(kMaxGrantsPerTick);
    CHECK(first.size() == kMaxGrantsPerTick);
    ledger.MarkGranted(static_cast<int>(first.size()));
    const auto second = ledger.NextGrants(kMaxGrantsPerTick);
    CHECK(second.front().index == static_cast<int>(kMaxGrantsPerTick));
    CHECK(ledger.granted() == static_cast<int>(kMaxGrantsPerTick));
}

void TestAnItemThatCannotBeGrantedYetStaysNext() {
    ReceiveLedger ledger(0);
    ledger.Accept(Run(0, 3));
    CHECK(ledger.NextGrants(100).front().index == 0);
    // Nothing was marked (MM's save was not resident): the same items are offered again.
    CHECK(ledger.NextGrants(100).front().index == 0);
    CHECK(ledger.granted() == 0);
}

void TestPacketsOutOfOrderWaitForTheGap() {
    ReceiveLedger ledger(0);
    ledger.Accept(Run(2, 2)); // 2 and 3 arrive first
    CHECK(ledger.NextGrants(100).empty());
    CHECK(ledger.NeedsSync());
    ledger.NoteSyncAsked();
    CHECK(!ledger.NeedsSync());
    ledger.Accept(Run(0, 2)); // then 0 and 1
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 0, 1, 2, 3 }));
    CHECK(!ledger.NeedsSync());
}

void TestALostPacketIsFixedByTheFullListOfASync() {
    ReceiveLedger ledger(2);
    ledger.Accept(Run(4, 1)); // 2 and 3 never came
    CHECK(ledger.NeedsSync());
    ledger.NoteSyncAsked();
    ledger.Accept(Run(0, 5)); // the Sync's answer
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 2, 3, 4 }));
}

void TestARepeatedIndexIsKeptOnce() {
    ReceiveLedger ledger(0);
    ledger.Accept(Run(0, 3));
    ledger.Accept(Run(1, 3));
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 0, 1, 2, 3 }));
}

void TestAnUnindexedItemIsIgnored() {
    ReceiveLedger ledger(0);
    ledger.Accept({ Item(-1, "Cheat (OoT)") });
    CHECK(ledger.NextGrants(100).empty());
    CHECK(ledger.pending() == 0);
}

void TestAServerWithAShorterListThanTheFileIsReported() {
    ReceiveLedger ledger(10);
    ledger.Accept(Run(0, 6));
    CHECK(ledger.ServerBehind());
    CHECK(ledger.NextGrants(100).empty());
    CHECK(ledger.granted() == 10);
    // It catches up past the file: the new items are granted, the old ones not.
    ledger.Accept(Run(0, 12));
    CHECK(!ledger.ServerBehind());
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 10, 11 }));
}

void TestLoadingAnotherFileStartsOver() {
    ReceiveLedger ledger(7);
    ledger.Accept(Run(0, 10));
    ledger.Reset(2);
    CHECK(ledger.granted() == 2);
    CHECK(ledger.pending() == 0);
    ledger.Accept(Run(0, 4));
    CHECK((Indexes(ledger.NextGrants(100)) == Ints{ 2, 3 }));
    ledger.Reset(-5);
    CHECK(ledger.granted() == 0);
}

void TestAStartInventoryComesFirstAndIsGrantedOnce() {
    ReceiveLedger ledger(0);
    const std::vector<ReceivedItem> list = { Item(0, "Progressive Hookshot (OoT)", "Server", kStartInventoryLocation),
                                             Item(1, "Ocarina of Time (OoT)", "Server", kStartInventoryLocation),
                                             Item(2, "Hookshot (MM)", "Zelda", 5) };
    ledger.Accept(list);
    const auto grants = ledger.NextGrants(100);
    CHECK(grants.size() == 3);
    CHECK(grants[0].IsStartInventory() && grants[1].IsStartInventory() && !grants[2].IsStartInventory());
    ledger.MarkGranted(3);
    // Restarted: the same list again grants nothing.
    ledger.DropBuffered();
    ledger.Accept(list);
    CHECK(ledger.NextGrants(100).empty());
}

// ---- rooms and items ----

void TestAFileSyncsOnlyWithItsOwnRoom() {
    CHECK(JudgeRoom("AP_1", "AP_1") == RoomVerdict::kOk);
    CHECK(JudgeRoom("AP_1", "AP_2") == RoomVerdict::kWrongSeed);
    CHECK(JudgeRoom("", "AP_2") == RoomVerdict::kOk);
    CHECK(JudgeSlotGame("Ship2Ship") == RoomVerdict::kOk);
    CHECK(JudgeSlotGame("APQuest") == RoomVerdict::kWrongGame);
}

void TestAnItemsGameAndNativeNameComeFromItsTag() {
    const GrantTarget oot = ApGrantTarget("Progressive Hookshot (OoT)");
    CHECK(oot.valid && oot.game == ComboRando::GAME_OOT && oot.nativeName == "Progressive Hookshot");
    const GrantTarget mm = ApGrantTarget("Hookshot (MM)");
    CHECK(mm.valid && mm.game == ComboRando::GAME_MM && mm.nativeName == "Hookshot");
    const GrantTarget numbers = ApGrantTarget("Arrows (10) (OoT)");
    CHECK(numbers.valid && numbers.nativeName == "Arrows (10)");
    CHECK(!ApGrantTarget("Sword").valid);
    CHECK(!ApGrantTarget(" (OoT)").valid);
    CHECK(!ApGrantTarget("").valid);
    CHECK(!ApGrantTarget("Rupees (OOT)").valid);
}

// ---- the server's address ----

void TestABareAddressIsPlainUnlessTlsIsBuiltIn() {
    CHECK(NormalizeServerAddress("  localhost:38281 ", false) == "ws://localhost:38281");
    CHECK(NormalizeServerAddress("localhost:38281", true) == "localhost:38281");
    CHECK(NormalizeServerAddress("wss://archipelago.gg:12345", false) == "wss://archipelago.gg:12345");
    CHECK(NormalizeServerAddress("ws://127.0.0.1:38281", true) == "ws://127.0.0.1:38281");
    CHECK(NormalizeServerAddress("   ", false).empty());
}

void TestAnAddressThatCannotBeUsedSaysWhy() {
    CHECK(ServerAddressProblem("", false) == "enter the server address");
    CHECK(ServerAddressProblem("ws://localhost:38281", false).empty());
    CHECK(ServerAddressProblem("wss://archipelago.gg:1", true).empty());
    CHECK(!ServerAddressProblem("wss://archipelago.gg:1", false).empty());
    CHECK(!ServerAddressProblem("http://localhost", true).empty());
    CHECK(ServerAddressProblem("localhost:38281", true).empty());
}

void TestACertificateRefusalIsRecognisedAndShortened() {
    const std::string refusal = std::string(kCertificateRefusalPrefix) + ": self-signed certificate" +
                                kTrustedRootsNote + "the Windows certificate store, cacert.pem)";
    CHECK(IsCertificateRefusal(refusal));
    CHECK(CertificateRefusalReason(refusal) == "certificate could not be verified: self-signed certificate");
    CHECK(!IsCertificateRefusal("TLS handshake failed"));
    CHECK(!IsCertificateRefusal(""));
    CHECK(CertificateRefusalReason("certificate could not be verified") == "certificate could not be verified");
}

// ---- toasts ----

void TestToastsNameTheItemAndThePlayerSafely() {
    const ToastText got = ReceivedToast(Item(0, "Progressive Hookshot (OoT)", "Zel%da"));
    CHECK(got.prefix == "Progressive Hookshot" && got.message == "from" && got.suffix == "Zel da");
    const ToastText sent = ComboRando::ApSentToast("Hookshot (MM)", "Impa^");
    CHECK(sent.prefix == "Sent" && sent.message == "Hookshot to" && sent.suffix == "Impa");
    const ToastText foreign = ComboRando::ApSentToast("Sword Of Courage", "Hero");
    CHECK(foreign.message == "Sword Of Courage to");
    CHECK(ComboRando::ApSentToast("", "").suffix == ComboRando::kApUnknownPlayer);
}

void TestABurstShowsAFewToastsAndASummary() {
    const auto batch = Run(0, 10);
    CHECK(PlanToasts(batch).size() == kMaxItemToastsPerBatch + 1);
    CHECK(PlanToasts(batch).back().message == "6 more items");
    CHECK(PlanToasts(Run(0, 2)).size() == 2);
    CHECK(PlanToasts({}).empty());
}

void TestTheStartInventoryShowsNoToasts() {
    std::vector<ReceivedItem> start;
    for (int i = 0; i < 50; ++i) {
        start.push_back(Item(i, "Rupees (OoT)", "Server", kStartInventoryLocation));
    }
    CHECK(PlanToasts(start).empty());
    start.push_back(Item(50, "Bomb Bag (OoT)", "Zelda", 9));
    CHECK(PlanToasts(start).size() == 1);
}

void TestTheItemsHandlingTakesOtherWorldsAndTheStartInventoryOnly() {
    CHECK((kItemsHandling & 0b001) != 0);
    CHECK((kItemsHandling & 0b010) == 0);
    CHECK((kItemsHandling & 0b100) != 0);
}

} // namespace

int main() {
    TestOnlyWhatTheServerLacksIsSent();
    TestACheckIsSentOncePerConnection();
    TestAReconnectSendsWhatTheServerNeverGot();
    TestAChecksOpenedElsewhereAreNotSentAgain();
    TestABacklogGoesOutInPackets();
    TestItemsAreGrantedInOrderFromTheSavedIndex();
    TestARestartSkipsWhatTheSaveAlreadyHolds();
    TestAReconnectNeverGrantsAnItemTwice();
    TestAPartOfTheListIsGrantedInChunks();
    TestAnItemThatCannotBeGrantedYetStaysNext();
    TestPacketsOutOfOrderWaitForTheGap();
    TestALostPacketIsFixedByTheFullListOfASync();
    TestARepeatedIndexIsKeptOnce();
    TestAnUnindexedItemIsIgnored();
    TestAServerWithAShorterListThanTheFileIsReported();
    TestLoadingAnotherFileStartsOver();
    TestAStartInventoryComesFirstAndIsGrantedOnce();
    TestAFileSyncsOnlyWithItsOwnRoom();
    TestAnItemsGameAndNativeNameComeFromItsTag();
    TestABareAddressIsPlainUnlessTlsIsBuiltIn();
    TestAnAddressThatCannotBeUsedSaysWhy();
    TestACertificateRefusalIsRecognisedAndShortened();
    TestToastsNameTheItemAndThePlayerSafely();
    TestABurstShowsAFewToastsAndASummary();
    TestTheStartInventoryShowsNoToasts();
    TestTheItemsHandlingTakesOtherWorldsAndTheStartInventoryOnly();
    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
