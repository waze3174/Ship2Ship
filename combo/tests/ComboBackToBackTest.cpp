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

constexpr int kOotUnlistedItem = 0x2C; // an item OoT alone has that no switch names (the Sold Out sign)
constexpr int kOotMaskSkull = 0x25;
constexpr int kLastOotItem = 0x9D;
constexpr int kLegacyOptionBits = 13; // the bits of the layout before the early-use switches: 0 to 12
constexpr B2B::OptionBits kLegacyOptionMask = (1ULL << kLegacyOptionBits) - 1;

bool Locked(B2B::OptionBits options, int ootItem, bool mmHolds) {
    return B2B::Locked(options, B2B::ClassifyOotItem(ootItem), mmHolds);
}

bool IsTransformMask(int ootItem) {
    return ootItem == B2B::OotItem::kOotMaskGoron || ootItem == B2B::OotItem::kOotMaskZora;
}

// The items no switch covers and no wait applies to: OoT's trade items, the letters, the masks MM has no copy of, and
// the two transformation masks (worn plain or transforming, decided where they are worn).
bool NeverLocked(int ootItem) {
    constexpr int kWeirdEgg = 0x21;
    constexpr int kZeldasLetter = 0x23;
    constexpr int kMaskSpooky = 0x26;
    constexpr int kMaskGerudo = 0x2A;
    constexpr int kRocsFeather = 0x9D;
    switch (ootItem) {
        case B2B::OotItem::kOotLetterRuto:
        case kWeirdEgg:
        case kZeldasLetter:
        case kOotMaskSkull:
        case kMaskSpooky:
        case kMaskGerudo:
        case kOotUnlistedItem:
        case kRocsFeather:
            return true;
        default:
            return IsTransformMask(ootItem);
    }
}

// The lock rules of the layout before the early-use switches, written out item by item: what a file that stored an
// options int then must still do.
bool LegacyLocked(B2B::OptionBits options, int ootItem, bool mmHolds) {
    using namespace B2B::OotItem;
    switch (ootItem) {
        case kOotStick:
        case kOotNut:
        case kOotBomb:
        case kOotBow:
        case kOotArrowFire:
        case kOotArrowIce:
        case kOotArrowLight:
        case kOotOcarinaFairy:
        case kOotOcarinaTime:
        case kOotBombchu:
        case kOotHookshot:
        case kOotLongshot:
        case kOotLens:
        case kOotBean:
            return !mmHolds;
        case kOotMaskKeaton:
        case kOotMaskBunny:
        case kOotMaskTruth:
            return (options & B2B::kSongsCarry) == 0 && !mmHolds;
        case kOotDinsFire:
            return (options & B2B::kDinsFire) == 0;
        case kOotFaroresWind:
            return (options & B2B::kFaroresWind) == 0;
        case kOotNayrusLove:
            return (options & B2B::kNayrusLove) == 0;
        case kOotBoomerang:
            return (options & B2B::kBoomerang) == 0;
        case kOotSlingshot:
            return (options & B2B::kSlingshot) == 0;
        case kOotHammer:
            return (options & B2B::kHammer) == 0;
        default:
            break;
    }
    if (ootItem >= kOotBottleFirst && ootItem <= kOotBottleLast && ootItem != kOotLetterRuto)
        return (options & B2B::kBottlesCarry) == 0 || !mmHolds;
    return true;
}

void TestOptionTable() {
    std::set<int> bits;
    std::set<std::string> cvars;
    for (const B2B::OptionDef& option : B2B::kOptions) {
        bits.insert(option.bit);
        cvars.insert(option.cvar);
        CHECK(std::string(option.cvar).rfind("gCombo.BackToBack.", 0) == 0);
        CHECK((option.bit & (option.bit - 1)) == 0); // one bit each
        CHECK(option.bit != 0);
        if (option.group == B2B::OptionGroup::EarlyUse)
            CHECK(!option.defaultOn); // every item Majora's Mask has too waits for its own by default
        if (option.group == B2B::OptionGroup::OotOnlyItems)
            CHECK(option.defaultOn);
    }
    CHECK(bits.size() == B2B::kOptionCount);
    CHECK(cvars.size() == B2B::kOptionCount);

    // The defaults the owner chose: bottles, wallet and rupees carry, and every OoT-only item is usable in Termina.
    // Hearts, the double magic meter and songs are off.
    const B2B::OptionBits ootOnlyItems = B2B::kDinsFire | B2B::kFaroresWind | B2B::kNayrusLove | B2B::kBoomerang |
                                         B2B::kSlingshot | B2B::kHammer | B2B::kBoots | B2B::kTunics;
    const B2B::OptionBits defaults = B2B::DefaultOptions();
    CHECK(defaults == (B2B::kBottlesCarry | B2B::kWalletCarry | ootOnlyItems));
    CHECK((defaults & B2B::kEarlyUseMasks) == 0); // the Goron and Zora masks are worn plain by default
    CHECK((defaults & B2B::kMagicCarry) == 0);
    CHECK((defaults & B2B::kSongsCarry) == 0);
    CHECK((defaults & B2B::kHeartsCarry) == 0);
    CHECK((defaults & B2B::kResetOotConsumables) == 0); // the Song of Time empties only MM's own by default
    CHECK((defaults & B2B::kEarlyMagic) == 0);          // magic waits for MM's Great Fairy by default
}

void TestOptionsFromCVars() {
    std::map<std::string, int> cvars;
    auto get = [&](const char* name, int fallback) {
        auto found = cvars.find(name);
        return found == cvars.end() ? fallback : found->second;
    };
    CHECK(B2B::OptionsFromCVars(get) == B2B::DefaultOptions()); // nothing set: the defaults
    cvars["gCombo.BackToBack.Magic"] = 1;
    cvars["gCombo.BackToBack.Hearts"] = 1;
    cvars["gCombo.BackToBack.Items.Hammer"] = 0;
    cvars["gCombo.BackToBack.Early.Bow"] = 1;
    const B2B::OptionBits options = B2B::OptionsFromCVars(get);
    CHECK((options & B2B::kMagicCarry) != 0);
    CHECK((options & B2B::kHeartsCarry) != 0);
    CHECK((options & B2B::kHammer) == 0);
    CHECK((options & B2B::kEarlyBow) != 0);
    CHECK((options & B2B::kEarlyBombs) == 0);
    CHECK((options & B2B::kBottlesCarry) != 0);
    CHECK((options & B2B::kBoots) != 0);
    cvars["gCombo.BackToBack.Early.GoronMask"] = 1;
    CHECK((B2B::OptionsFromCVars(get) & B2B::kEarlyGoronMask) != 0);
    CHECK((B2B::OptionsFromCVars(get) & B2B::kEarlyZoraMask) == 0);
    CHECK((B2B::OptionsFromCVars(get) & B2B::kEarlyMagic) == 0);
    cvars["gCombo.BackToBack.Early.Magic"] = 1;
    CHECK((B2B::OptionsFromCVars(get) & B2B::kEarlyMagic) != 0);
}

void TestModeStoresOptions() {
    const B2B::OptionBits options = B2B::kHeartsCarry | B2B::kBoots | B2B::kEarlyBow | B2B::kEarlyGoronMask |
                                    B2B::kEarlyZoraMask | B2B::kEarlyMagic; // bit 31 and above survive the JSON
    const ComboMode made = ComboMode::BackToBack(ComboRando::OOT_QUEST_MQ, options);
    const ComboMode read = ComboMode::FromJson(made.ToJson());
    CHECK(read.IsBackToBack());
    CHECK(read.quest == ComboRando::OOT_QUEST_MQ);
    CHECK(read.options == options);
    CHECK(made.ToJson().at(ComboRando::kOptionsLayoutKey) == B2B::kOptionsLayout); // the key goes with the int

    // A back-to-back slot made before the options were stored takes the page's defaults.
    nlohmann::json older = made.ToJson();
    older.erase(ComboRando::kOptionsKey);
    CHECK(ComboMode::FromJson(older).options == B2B::DefaultOptions());

    // No other mode writes or reads them.
    CHECK(!ComboMode::VanillaMm().ToJson().contains(ComboRando::kOptionsKey));
    CHECK(!ComboMode::VanillaOot(ComboRando::OOT_QUEST_NORMAL).ToJson().contains(ComboRando::kOptionsKey));
    CHECK(!ComboMode::Rando(ComboRando::COMBO_GAMES_BOTH).ToJson().contains(ComboRando::kOptionsKey));
    CHECK(!ComboMode::VanillaMm().ToJson().contains(ComboRando::kOptionsLayoutKey));
    CHECK(!ComboMode::Rando(ComboRando::COMBO_GAMES_BOTH).ToJson().contains(ComboRando::kOptionsLayoutKey));
    CHECK(ComboMode::FromJson(nlohmann::json::object()).options == 0);
}

// A mode stored without the layout key holds the first layout: every choice it made is kept, and each item keeps the
// lock it had. Every early-use bit is clear (the item waits), except that the old songs and masks switch also set the
// five mask bits, the Goron and Zora masks among them (they transformed at once).
void TestStoredOptionsFromTheFirstLayout() {
    for (B2B::OptionBits stored = 0; stored <= kLegacyOptionMask; stored++) {
        nlohmann::json json = ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, stored).ToJson();
        json.erase(ComboRando::kOptionsLayoutKey);
        const B2B::OptionBits options = ComboMode::FromJson(json).options;
        CHECK((options & kLegacyOptionMask) == stored); // every old choice kept
        const B2B::OptionBits newBits = options & ~kLegacyOptionMask;
        CHECK(newBits == (((stored & B2B::kSongsCarry) != 0) ? B2B::kEarlyUseMasks : 0));
        CHECK(((options & B2B::kEarlyGoronMask) != 0) == ((stored & B2B::kSongsCarry) != 0));
        CHECK(((options & B2B::kEarlyZoraMask) != 0) == ((stored & B2B::kSongsCarry) != 0));
        for (int item = 0; item <= kLastOotItem; item++) {
            if (B2B::ClassifyOotItem(item).kind == B2B::LockKind::Free)
                continue; // the items no switch covers are never locked now, whatever an older file chose
            for (bool mmHolds : { false, true })
                CHECK(Locked(options, item, mmHolds) == LegacyLocked(stored, item, mmHolds));
        }
    }
    // The current layout is left alone.
    const B2B::OptionBits current = B2B::kEarlyBow | B2B::kEarlyGoronMask | B2B::kBoots | B2B::kEarlyMagic;
    CHECK(B2B::MigrateStoredOptions(current, B2B::kOptionsLayout) == current);
    CHECK(B2B::MigrateStoredOptions(B2B::kSongsCarry, B2B::kOptionsLayout) == B2B::kSongsCarry);
}

// A file made by 0.2.2 (before the layout key): its layout was marked by bit 30, now the Zora Mask's switch, and its
// songs switch meant only the songs. The owner's 2026-11-25 file is one (bugs #31-#37).
void TestStoredOptionsFromTheMarkedLayout() {
    const B2B::OptionBits songsAndEarly = B2B::kSongsCarry | B2B::kBoots | B2B::kEarlyOcarina | B2B::kEarlyHookshot |
                                          B2B::kEarlyKeatonMask | B2B::kEarlyBunnyHood | B2B::kEarlyMaskOfTruth;
    for (const B2B::OptionBits stored : { B2B::OptionBits{ 0 }, B2B::OptionBits{ B2B::kSongsCarry }, songsAndEarly }) {
        nlohmann::json json =
            ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, stored | B2B::kMarkedLayoutBit).ToJson();
        json.erase(ComboRando::kOptionsLayoutKey);
        const B2B::OptionBits options = ComboMode::FromJson(json).options;
        CHECK(options == stored);                     // every choice kept, the marker gone
        CHECK((options & B2B::kEarlyGoronMask) == 0); // worn plain until MM's own, as 0.2.2 did
        CHECK((options & B2B::kEarlyZoraMask) == 0);
    }
}

// A mode stored with layout 2 (0.2.2, bits 0 to 30 in a 31-bit int) holds the same bits: every choice kept, and Magic,
// which that layout never had, waits for MM's Great Fairy.
void TestStoredOptionsFromLayoutTwo() {
    const B2B::OptionBits everyLayoutTwoBit = (1ULL << 31) - 1;
    for (const B2B::OptionBits stored : { B2B::OptionBits{ 0 }, B2B::DefaultOptions(), everyLayoutTwoBit,
                                          B2B::OptionBits{ B2B::kEarlyGoronMask | B2B::kEarlyZoraMask } }) {
        nlohmann::json json = ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, stored).ToJson();
        json[ComboRando::kOptionsLayoutKey] = B2B::kFirstKeyedOptionsLayout;
        const B2B::OptionBits options = ComboMode::FromJson(json).options;
        CHECK(options == stored); // bit 30 is the Zora Mask's switch here, not a marker
        CHECK((options & B2B::kEarlyMagic) == 0);
    }
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

// Bug #36/#37 (2026-11-25): the Megaton Hammer, the Longshot, the tunics and the boots greyed out for child Link in a
// back-to-back slot's Termina, by OoT's age rule, with their switches on.
void TestTerminaAgeRule() {
    const ComboMode backToBack = ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, B2B::DefaultOptions());
    CHECK(!B2B::TerminaAgeRuleApplies(Gate::Mask(backToBack, false, false)));
    CHECK(!B2B::TerminaAgeRuleApplies(Gate::Mask(backToBack, true, false)));
    CHECK(!B2B::TerminaAgeRuleApplies(Gate::Mask(backToBack, true, true))); // both beaten: as before
    CHECK(B2B::TerminaAgeRuleApplies(Gate::Mask(ComboMode::Rando(ComboRando::COMBO_GAMES_BOTH), false, false)));
    CHECK(B2B::TerminaAgeRuleApplies(Gate::kAllOpen));
}

void TestItemsWithAnMmCounterpart() {
    using namespace B2B::OotItem;
    struct Row {
        int item;
        B2B::OptionBits option;
    };
    const Row rows[] = {
        { kOotStick, B2B::kEarlySticks },
        { kOotNut, B2B::kEarlyNuts },
        { kOotBomb, B2B::kEarlyBombs },
        { kOotBow, B2B::kEarlyBow },
        { kOotArrowFire, B2B::kEarlyFireArrows },
        { kOotArrowIce, B2B::kEarlyIceArrows },
        { kOotArrowLight, B2B::kEarlyLightArrows },
        { kOotOcarinaFairy, B2B::kEarlyOcarina },
        { kOotOcarinaTime, B2B::kEarlyOcarina },
        { kOotBombchu, B2B::kEarlyBombchus },
        { kOotHookshot, B2B::kEarlyHookshot },
        { kOotLongshot, B2B::kEarlyHookshot },
        { kOotLens, B2B::kEarlyLens },
        { kOotBean, B2B::kEarlyBeans },
        { kOotMaskKeaton, B2B::kEarlyKeatonMask },
        { kOotMaskBunny, B2B::kEarlyBunnyHood },
        { kOotMaskTruth, B2B::kEarlyMaskOfTruth },
    };
    for (const Row& row : rows) {
        CHECK(Locked(0, row.item, false));           // waits for MM's own by default
        CHECK(!Locked(0, row.item, true));           // and is usable once MM's is found
        CHECK(!Locked(row.option, row.item, false)); // its own switch lets it in early
        CHECK(Locked(~row.option, row.item, false)); // no other switch does
        CHECK(Locked(B2B::DefaultOptions(), row.item, false));
    }
}

void TestBottles() {
    using namespace B2B::OotItem;
    constexpr int kCarriedAndEarly = B2B::kBottlesCarry | B2B::kEarlyBottles;
    for (int item : { kOotBottleFirst, 0x15, 0x1A, kOotBottleLast }) { // empty, a potion, milk, a Poe
        CHECK(Locked(B2B::kBottlesCarry, item, false));                // carried, but waits for MM's first bottle
        CHECK(!Locked(B2B::kBottlesCarry, item, true));                // carried and MM has its first
        CHECK(!Locked(kCarriedAndEarly, item, false));                 // carried and set to skip the wait
        CHECK(Locked(0, item, true));                                  // not carried: they stay in Hyrule
        CHECK(Locked(B2B::kEarlyBottles, item, true));                 // skipping the wait does not carry them
    }
    CHECK(!Locked(0, kOotLetterRuto, false)); // Ruto's letter is OoT's alone: no bottle rule, never locked
}

void TestTransformationMasks() {
    using namespace B2B::OotItem;
    // The Goron and Zora masks are never locked: Link can always wear them in Termina. Whether they transform him is
    // decided where they are worn, by whether he holds MM's own.
    for (int item : { kOotMaskGoron, kOotMaskZora }) {
        for (const B2B::OptionBits options :
             { B2B::OptionBits{ 0 }, ~B2B::OptionBits{ 0 }, B2B::DefaultOptions(), B2B::kEarlyUseMasks }) {
            CHECK(!Locked(options, item, false));
            CHECK(!Locked(options, item, true));
        }
    }
}

// The items only OoT has and no switch names are in Termina from the start, with the locks in force.
void TestItemsWithNoSwitchAreNeverLocked() {
    using namespace B2B::OotItem;
    for (int item : { kOotLetterRuto, 0x21, 0x23, kOotMaskSkull, 0x26, 0x2A, kOotUnlistedItem, 0x9D }) {
        CHECK(NeverLocked(item));
        CHECK(B2B::ClassifyOotItem(item).kind == B2B::LockKind::Free);
        for (B2B::OptionBits options : { B2B::OptionBits{ 0 }, B2B::DefaultOptions(), ~B2B::OptionBits{ 0 } })
            CHECK(!Locked(options, item, false));
    }
}

void TestOotOnlyItems() {
    using namespace B2B::OotItem;
    struct Row {
        int item;
        B2B::OptionBits option;
    };
    const Row rows[] = { { kOotDinsFire, B2B::kDinsFire },     { kOotFaroresWind, B2B::kFaroresWind },
                         { kOotNayrusLove, B2B::kNayrusLove }, { kOotBoomerang, B2B::kBoomerang },
                         { kOotSlingshot, B2B::kSlingshot },   { kOotHammer, B2B::kHammer } };
    for (const Row& row : rows) {
        CHECK(Locked(0, row.item, true));
        CHECK(!Locked(row.option, row.item, false));
        CHECK(Locked(~row.option, row.item, true)); // another switch does not unlock it
    }
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
    for (int item = 0; item <= kLastOotItem; item++) {
        const B2B::ItemLock lock = B2B::ClassifyOotItem(item);
        CHECK(!B2B::LockedInTermina(bothBeaten, 0, lock, false));
        CHECK(!B2B::LockedInTermina(rando, 0, lock, false));
        // With nothing chosen and nothing found, all are locked but the items nothing locks.
        CHECK(B2B::LockedInTermina(ganonOnly, 0, lock, false) == (lock.kind != B2B::LockKind::Free));
        CHECK(!NeverLocked(item) || lock.kind == B2B::LockKind::Free);
    }
    CHECK(!B2B::LockedInTermina(bothBeaten, 0, B2B::GearLock(true), false));
    CHECK(!B2B::LockedInTermina(bothBeaten, 0, B2B::GearLock(false), false));
    CHECK(B2B::LockedInTermina(ganonOnly, 0, B2B::GearLock(true), false));
}

// The hint a locked item gives on the Hyrule page: it waits for Majora's Mask's own copy, which it names, or the file
// keeps it in Hyrule.
void TestLockHints() {
    using namespace B2B::OotItem;
    // Every item that waits for an MM copy names it.
    for (int item : { kOotStick,      kOotNut,          kOotBomb,        kOotBow,       kOotArrowFire, kOotArrowIce,
                      kOotArrowLight, kOotOcarinaFairy, kOotOcarinaTime, kOotBombchu,   kOotHookshot,  kOotLongshot,
                      kOotLens,       kOotBean,         kOotMaskKeaton,  kOotMaskBunny, kOotMaskTruth, kOotBottleFirst,
                      0x15,           kOotBottleLast }) {
        const B2B::ItemLock lock = B2B::ClassifyOotItem(item);
        CHECK(lock.kind == B2B::LockKind::UntilMmHolds || lock.kind == B2B::LockKind::Bottle);
        CHECK(B2B::MmCounterpartName(item) != nullptr);
        CHECK(B2B::ReasonLocked(B2B::kBottlesCarry, lock) == B2B::LockReason::WaitsForMm);
    }
    CHECK(std::string(B2B::MmCounterpartName(kOotLongshot)) == "Hookshot"); // MM's Hookshot unlocks both
    CHECK(std::string(B2B::MmCounterpartName(kOotBottleLast)) == "first bottle");
    // Bottles the file does not carry stay in Hyrule, and so does an OoT-only item whose switch is off.
    CHECK(B2B::ReasonLocked(0, B2B::ClassifyOotItem(kOotBottleFirst)) == B2B::LockReason::StaysInHyrule);
    CHECK(B2B::ReasonLocked(0, B2B::ClassifyOotItem(kOotHammer)) == B2B::LockReason::StaysInHyrule);
    CHECK(B2B::ReasonLocked(0, B2B::GearLock(true)) == B2B::LockReason::StaysInHyrule);
    // An item with no MM copy has no name to give.
    CHECK(B2B::MmCounterpartName(kOotHammer) == nullptr);
}

// A loaded file lets the player change its item switches (the live rules) and nothing it was made with.
void TestEditingALoadedFile() {
    const B2B::OptionBits carries = B2B::kHeartsCarry | B2B::kMagicCarry | B2B::kBottlesCarry | B2B::kWalletCarry |
                                    B2B::kSongsCarry | B2B::kEarlyMagic;
    const B2B::OptionBits editable = B2B::EditableOptionsMask();
    CHECK((editable & carries) == 0); // the carries and the one-time Magic carry stay as the file was made
    for (const B2B::OptionDef& option : B2B::kOptions) {
        const bool isCarry = (carries & option.bit) != 0;
        CHECK(B2B::EditableOnLoadedFile(option) == !isCarry);
        CHECK(((editable & option.bit) != 0) == !isCarry);
    }
    CHECK((editable & B2B::kEarlyBow) != 0);
    CHECK((editable & B2B::kEarlyGoronMask) != 0);
    CHECK((editable & B2B::kDinsFire) != 0);
    CHECK((editable & B2B::kResetOotConsumables) != 0); // a rule of the cycle reset, not a carry

    const B2B::OptionBits made = B2B::kHeartsCarry | B2B::kEarlyMagic | B2B::kEarlyBow | B2B::kDinsFire;
    // Asking for everything on changes the item switches only.
    const B2B::OptionBits all = B2B::ApplyEditedOptions(made, ~B2B::OptionBits{ 0 });
    CHECK((all & ~editable) == (made & ~editable));
    CHECK((all & editable) == editable);
    // Asking for nothing turns the item switches off and keeps the carries.
    const B2B::OptionBits none = B2B::ApplyEditedOptions(made, 0);
    CHECK((none & ~editable) == (made & ~editable));
    CHECK((none & editable) == 0);
    // Turning one on or off touches only that one.
    const B2B::OptionBits plusHammer = B2B::ApplyEditedOptions(made, made | B2B::kHammer);
    CHECK(plusHammer == (made | B2B::kHammer));
    CHECK(B2B::ApplyEditedOptions(made, made & ~B2B::kEarlyBow) == (made & ~B2B::kEarlyBow));
    // The edit survives the file's storage.
    const ComboMode edited = ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, plusHammer);
    CHECK(ComboMode::FromJson(edited.ToJson()).options == plusHammer);
}

void TestQuickSets() {
    const B2B::OptionBits purist = B2B::QuickSetOptions(B2B::QuickSet::Purist);
    CHECK(purist == 0);
    CHECK(B2B::QuickSetOptions(B2B::QuickSet::Default) == B2B::DefaultOptions());
    const B2B::OptionBits everything = B2B::QuickSetOptions(B2B::QuickSet::Everything);
    B2B::OptionBits every = 0;
    for (const B2B::OptionDef& option : B2B::kOptions)
        every |= option.bit;
    CHECK(everything == every);
    // Everything lets every item in from the start, and Purist keeps every one waiting.
    for (int item : { B2B::OotItem::kOotBow, B2B::OotItem::kOotOcarinaTime, B2B::OotItem::kOotMaskBunny,
                      B2B::OotItem::kOotHookshot, B2B::OotItem::kOotDinsFire, B2B::OotItem::kOotBottleFirst }) {
        CHECK(!Locked(everything, item, false));
        CHECK(Locked(purist, item, false));
    }
}

} // namespace

int main() {
    TestOptionTable();
    TestOptionsFromCVars();
    TestModeStoresOptions();
    TestLocksActive();
    TestTerminaAgeRule();
    TestStoredOptionsFromTheFirstLayout();
    TestStoredOptionsFromTheMarkedLayout();
    TestStoredOptionsFromLayoutTwo();
    TestEditingALoadedFile();
    TestLockHints();
    TestQuickSets();
    TestItemsWithAnMmCounterpart();
    TestBottles();
    TestTransformationMasks();
    TestItemsWithNoSwitchAreNeverLocked();
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
