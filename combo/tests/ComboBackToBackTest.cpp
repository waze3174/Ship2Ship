// combo/tests/ComboBackToBackTest.cpp
//
// Tests for the pure rules of the back-to-back mode: the page's options and how they are stored in the slot's mode,
// when the locks are in force, and which Ocarina of Time items they keep out of Termina. No game DLL. Build and run:
// `cmake --build <dir> --target comboBackToBackTest`, then run the exe; it exits 0 when every check passes.
#include <map>
#include <set>
#include <string>

#include "rando/ComboBackToBackLocks.h"
#include "rando/ComboBackToBackSettings.h"
#include "rando/ComboCrossGate.h"
#include "rando/ComboMode.h"
#include "ComboTestCheck.h"

namespace {

namespace B2B = ComboRando::BackToBack;
namespace Gate = ComboRando::CrossGate;
using ComboRando::ComboMode;

constexpr int kOotUnlistedItem = 0x2C; // an item OoT alone has that no switch names

bool Locked(int options, int ootItem, bool mmHolds) {
    return B2B::Locked(options, B2B::ClassifyOotItem(ootItem), mmHolds);
}

void TestOptionTable() {
    std::set<int> bits;
    std::set<std::string> cvars;
    for (const B2B::OptionDef& option : B2B::kOptions) {
        bits.insert(option.bit);
        cvars.insert(option.cvar);
        CHECK(std::string(option.cvar).rfind("gCombo.BackToBack.", 0) == 0);
        CHECK((option.bit & (option.bit - 1)) == 0); // one bit each
    }
    CHECK(bits.size() == B2B::kOptionCount);
    CHECK(cvars.size() == B2B::kOptionCount);

    // The defaults the owner chose: magic, bottles, wallet and rupees carry; hearts, songs and masks, and every
    // OoT-only item are off.
    const int defaults = B2B::DefaultOptions();
    CHECK(defaults == (B2B::kMagicCarry | B2B::kBottlesCarry | B2B::kWalletCarry));
}

void TestOptionsFromCVars() {
    std::map<std::string, int> cvars;
    auto get = [&](const char* name, int fallback) {
        auto found = cvars.find(name);
        return found == cvars.end() ? fallback : found->second;
    };
    CHECK(B2B::OptionsFromCVars(get) == B2B::DefaultOptions()); // nothing set: the defaults
    cvars["gCombo.BackToBack.Magic"] = 0;
    cvars["gCombo.BackToBack.Hearts"] = 1;
    cvars["gCombo.BackToBack.Items.Hammer"] = 1;
    const int options = B2B::OptionsFromCVars(get);
    CHECK((options & B2B::kMagicCarry) == 0);
    CHECK((options & B2B::kHeartsCarry) != 0);
    CHECK((options & B2B::kHammer) != 0);
    CHECK((options & B2B::kBottlesCarry) != 0);
    CHECK((options & B2B::kBoots) == 0);
}

void TestModeStoresOptions() {
    const int options = B2B::kHeartsCarry | B2B::kBoots;
    const ComboMode made = ComboMode::BackToBack(ComboRando::OOT_QUEST_MQ, options);
    const ComboMode read = ComboMode::FromJson(made.ToJson());
    CHECK(read.IsBackToBack());
    CHECK(read.quest == ComboRando::OOT_QUEST_MQ);
    CHECK(read.options == options);

    // A back-to-back slot made before the options were stored takes the page's defaults.
    nlohmann::json older = made.ToJson();
    older.erase(ComboRando::kOptionsKey);
    CHECK(ComboMode::FromJson(older).options == B2B::DefaultOptions());

    // No other mode writes or reads them.
    CHECK(!ComboMode::VanillaMm().ToJson().contains(ComboRando::kOptionsKey));
    CHECK(!ComboMode::VanillaOot(ComboRando::OOT_QUEST_NORMAL).ToJson().contains(ComboRando::kOptionsKey));
    CHECK(!ComboMode::Rando(ComboRando::COMBO_GAMES_BOTH).ToJson().contains(ComboRando::kOptionsKey));
    CHECK(ComboMode::FromJson(nlohmann::json::object()).options == 0);
}

void TestLocksActive() {
    const ComboMode backToBack = ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, B2B::DefaultOptions());
    CHECK(B2B::LocksActive(Gate::Mask(backToBack, false, false)));
    CHECK(B2B::LocksActive(Gate::Mask(backToBack, true, false)));
    CHECK(B2B::LocksActive(Gate::Mask(backToBack, true, true)) == false); // both beaten: everything is usable
    CHECK(Gate::IsBackToBack(Gate::Mask(backToBack, true, false)));

    // Every other slot has no locks at all.
    CHECK(!B2B::LocksActive(Gate::Mask(ComboMode::Rando(ComboRando::COMBO_GAMES_BOTH), false, false)));
    CHECK(!B2B::LocksActive(Gate::Mask(ComboMode::VanillaMm(), false, false)));
    CHECK(!B2B::LocksActive(Gate::Mask(ComboMode::VanillaOot(ComboRando::OOT_QUEST_NORMAL), false, false)));
    CHECK(!B2B::LocksActive(Gate::kAllOpen));
}

void TestItemsWithAnMmCounterpart() {
    using namespace B2B::OotItem;
    for (int item : { kOotBow, kOotBomb, kOotArrowFire, kOotArrowIce, kOotArrowLight, kOotStick, kOotNut, kOotBombchu,
                      kOotLens, kOotBean, kOotOcarinaFairy, kOotOcarinaTime, kOotHookshot, kOotLongshot }) {
        CHECK(Locked(0, item, false));
        CHECK(!Locked(0, item, true));
        CHECK(Locked(~0, item, false)); // no switch unlocks one before MM's own copy is found
    }
}

void TestBottles() {
    using namespace B2B::OotItem;
    for (int item : { kOotBottleFirst, 0x15, 0x1A, kOotBottleLast }) { // empty, a potion, milk, a Poe
        CHECK(Locked(B2B::kBottlesCarry, item, false));                // carried, but MM has no bottle yet
        CHECK(!Locked(B2B::kBottlesCarry, item, true));                // carried and MM has its first
        CHECK(Locked(0, item, true));                                  // not carried: they stay in Hyrule
    }
    CHECK(Locked(B2B::kBottlesCarry, kOotLetterRuto, true)); // Ruto's letter is OoT's alone
}

void TestMasks() {
    using namespace B2B::OotItem;
    for (int item : { kOotMaskKeaton, kOotMaskBunny, kOotMaskGoron, kOotMaskZora, kOotMaskTruth }) {
        CHECK(Locked(0, item, false));
        CHECK(!Locked(0, item, true));
        CHECK(!Locked(B2B::kSongsMasksShared, item, false)); // shared: usable from the start
    }
    CHECK(Locked(B2B::kSongsMasksShared, 0x25, false)); // the Skull Mask is OoT's alone: the switch does not cover it
}

void TestOotOnlyItems() {
    using namespace B2B::OotItem;
    struct Row {
        int item;
        int option;
    };
    const Row rows[] = { { kOotDinsFire, B2B::kDinsFire },     { kOotFaroresWind, B2B::kFaroresWind },
                         { kOotNayrusLove, B2B::kNayrusLove }, { kOotBoomerang, B2B::kBoomerang },
                         { kOotSlingshot, B2B::kSlingshot },   { kOotHammer, B2B::kHammer } };
    for (const Row& row : rows) {
        CHECK(Locked(0, row.item, true));
        CHECK(!Locked(row.option, row.item, false));
        CHECK(Locked(~row.option, row.item, true)); // another switch does not unlock it
    }
    CHECK(Locked(0, kOotUnlistedItem, true));
    CHECK(Locked(~0, kOotUnlistedItem, true)); // no switch: locked until both games are beaten
}

void TestGear() {
    CHECK(B2B::Locked(0, B2B::GearLock(false), false));
    CHECK(B2B::Locked(0, B2B::GearLock(true), false));
    CHECK(!B2B::Locked(B2B::kTunics, B2B::GearLock(false), false));
    CHECK(B2B::Locked(B2B::kTunics, B2B::GearLock(true), false));
    CHECK(!B2B::Locked(B2B::kBoots, B2B::GearLock(true), false));
}

// Both games beaten: no item of any kind is locked, whatever the page chose, and nothing is locked in any other mode.
void TestPostGameUnlocksEverything() {
    const ComboMode backToBack = ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, 0);
    const int bothBeaten = Gate::Mask(backToBack, true, true);
    const int ganonOnly = Gate::Mask(backToBack, true, false);
    const int rando = Gate::Mask(ComboMode::Rando(ComboRando::COMBO_GAMES_BOTH), false, false);
    constexpr int kLastOotItem = 0x9D;
    for (int item = 0; item <= kLastOotItem; item++) {
        const B2B::ItemLock lock = B2B::ClassifyOotItem(item);
        CHECK(!B2B::LockedInTermina(bothBeaten, 0, lock, false));
        CHECK(!B2B::LockedInTermina(rando, 0, lock, false));
        CHECK(B2B::LockedInTermina(ganonOnly, 0, lock, false)); // with nothing chosen and nothing found, all locked
    }
    CHECK(!B2B::LockedInTermina(bothBeaten, 0, B2B::GearLock(true), false));
    CHECK(!B2B::LockedInTermina(bothBeaten, 0, B2B::GearLock(false), false));
    CHECK(B2B::LockedInTermina(ganonOnly, 0, B2B::GearLock(true), false));
}

} // namespace

int main() {
    TestOptionTable();
    TestOptionsFromCVars();
    TestModeStoresOptions();
    TestLocksActive();
    TestItemsWithAnMmCounterpart();
    TestBottles();
    TestMasks();
    TestOotOnlyItems();
    TestGear();
    TestPostGameUnlocksEverything();
    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
