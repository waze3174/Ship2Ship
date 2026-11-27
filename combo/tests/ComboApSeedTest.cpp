// combo/tests/ComboApSeedTest.cpp
//
// Tests for the Archipelago slot seed (ComboApSeed.h) and the foreign kind it adds (CrossForeign.h): what a stamped
// seed holds, what the loaders make of it, and which seeds are refused. Build and run: `cmake --build <dir> --target
// comboApSeedTest`, then run the exe; it exits 0 when every check passes.
#include <string>

#include "rando/ComboApSeed.h"
#include "ComboTestCheck.h"

namespace {

using nlohmann::json;
using namespace ComboRando;

constexpr uint32_t kDisplaySeed = 123456789;

json Other(const char* game, const char* check, const char* player, const char* item, bool advancement,
           const char* apClass = nullptr) {
    json other = {
        { "game", game }, { "check", check }, { "player", player }, { "item", item }, { "advancement", advancement }
    };
    if (apClass != nullptr) {
        other[kApClassKey] = apClass;
    }
    return other;
}

// A finished consolidated seed of two games, the shape comborando's finalize writes before it is stamped: a native
// cross-game item is already in "foreign"; the checks that will hold other players' items hold native junk.
json PlainSeed() {
    return { { "fileType", kSeedFileType },
             { "seed", "AP_1" },
             { "masterSeed", 1 },
             { "oot",
               { { "placements",
                   { { "Kokiri Chest", "Blue Rupee" },
                     { "Deku Reward", "Red Rupee" },
                     { "Link Gift", "Deku Stick" },
                     { "Zora Chest", "Green Rupee" } } } } },
             { "mm", { { "placements", { { "Clock Town Chest", "Junk" }, { "Moon Pot", "Junk" } } } } },
             { "foreign", json::array({ { { "checkGame", "oot" },
                                          { "checkName", "Link Gift" },
                                          { "itemGame", "mm" },
                                          { "itemName", "Hookshot" },
                                          { "displayName", "Hookshot (MM)" },
                                          { "advancement", true } } }) } };
}

json TwoOthers() {
    return json::array({ Other("oot", "Kokiri Chest", "Zelda", "Progressive Hookshot (OoT)", true, "progression"),
                         Other("mm", "Moon Pot", "Impa", "Rupees (50)", false, "filler"),
                         Other("oot", "Zora Chest", "Sheik", "Magic Meter (OoT)", false, "useful") });
}

json ApSeed() {
    json seed = PlainSeed();
    CHECK(StampApSeed(seed, { { kApSlotNameKey, "Link" }, { kApSeedNameKey, "AP_42" } }, TwoOthers(), kDisplaySeed)
              .empty());
    return seed;
}

std::string ProblemAfter(void (*damage)(json&)) {
    json seed = ApSeed();
    damage(seed);
    return ApSeedProblem(seed);
}

void TestNamesDropTheGameTag() {
    CHECK(StripApGameTag("Progressive Hookshot (OoT)") == "Progressive Hookshot");
    CHECK(StripApGameTag("Hookshot (MM)") == "Hookshot");
    CHECK(StripApGameTag("Hookshot (OOT)") == "Hookshot");
    CHECK(StripApGameTag("Rupees (50)") == "Rupees (50)"); // another game's item keeps its own parentheses
    CHECK(StripApGameTag("Sword") == "Sword");
    CHECK(StripApGameTag(" (MM)") == " (MM)"); // nothing left of the name: leave it
    CHECK(ApShownName("Zelda", "Progressive Hookshot (OoT)") == "Zelda's Progressive Hookshot");
    CHECK(ApLocationName(GAME_OOT, "Kokiri Chest") == "Kokiri Chest (OoT)");
    CHECK(ApLocationName(GAME_MM, "Moon Pot") == "Moon Pot (MM)");
}

void TestTheForeignEntryDescribesTheOtherPlayersItem() {
    const json progression = ApForeignEntry(Other("oot", "Kokiri Chest", "Zelda", "Progressive Hookshot (OoT)", true));
    CHECK(progression["ap"] == true);
    CHECK(progression["apPlayer"] == "Zelda");
    CHECK(progression["checkGame"] == "oot" && progression["itemGame"] == "oot"); // never the other game's
    CHECK(progression["checkName"] == "Kokiri Chest");
    CHECK(progression["itemName"] == "Progressive Hookshot (OoT)");
    CHECK(progression["displayName"] == "Zelda's Progressive Hookshot");
    CHECK(progression["advancement"] == true);
    CHECK(progression["category"] == kApMajorCategory);
    CHECK(progression[kApForeignClassKey] == kApClassProgression);

    const json useful = ApForeignEntry(Other("oot", "Zora Chest", "Sheik", "Magic Meter (OoT)", false, "useful"));
    CHECK(!useful.contains("advancement"));
    CHECK(useful["category"] == kApMinorCategory && useful[kApForeignClassKey] == kApClassUseful);

    const json filler = ApForeignEntry(Other("mm", "Moon Pot", "Impa", "Rupees (50)", false));
    CHECK(!filler.contains("advancement"));
    CHECK(filler["category"] == kApJunkCategory);
    CHECK(filler["displayName"] == "Impa's Rupees (50)");

    const json trap = ApForeignEntry(Other("mm", "Moon Pot", "Impa", "Bomb Trap", false, "trap"));
    CHECK(trap["category"] == kApJunkCategory && trap[kApForeignClassKey] == kApClassTrap);
    CHECK(!trap.contains("trap")); // nothing is granted for another player's item, so no trap fires here
}

void TestAnItemWithNoClassTakesItsLookFromItsImportance() {
    const json important = ApForeignEntry(Other("oot", "Kokiri Chest", "Zelda", "Hookshot", true));
    CHECK(important[kApForeignClassKey] == kApClassProgression && important["category"] == kApMajorCategory);
    const json minor = ApForeignEntry(Other("oot", "Kokiri Chest", "Zelda", "Hookshot", false));
    CHECK(minor[kApForeignClassKey] == kApClassFiller && minor["category"] == kApJunkCategory);
    // A class this build does not know is ignored the same way.
    const json unknown = ApForeignEntry(Other("oot", "Kokiri Chest", "Zelda", "Hookshot", true, "legendary"));
    CHECK(unknown[kApForeignClassKey] == kApClassProgression);
}

void TestEachClassHasItsModelAndContainerLook() {
    CHECK(ApLookFromClass(kApClassProgression, false) == ApLook::Progression);
    CHECK(ApLookFromClass(kApClassUseful, true) == ApLook::Useful); // the class wins over the flag
    CHECK(ApLookFromClass(kApClassFiller, true) == ApLook::Junk);
    CHECK(ApLookFromClass(kApClassTrap, true) == ApLook::Junk);
    CHECK(ApLookFromClass("", true) == ApLook::Progression); // a stage 3 seed has no class
    CHECK(ApLookFromClass("", false) == ApLook::Junk);
    CHECK(ApLookFromClass("legendary", false) == ApLook::Junk);

    CHECK(std::string(ApModelPath(ApLook::Progression)) == "objects/object_archipelago_item/gArchipelagoProgressiveDL");
    CHECK(std::string(ApModelPath(ApLook::Useful)) == "objects/object_archipelago_item/gArchipelagoItemDL");
    CHECK(std::string(ApModelPath(ApLook::Junk)) == "objects/object_archipelago_item/gArchipelagoJunkDL");
    CHECK(std::string(ApCategoryForLook(ApLook::Progression)) == kApMajorCategory);
    CHECK(std::string(ApCategoryForLook(ApLook::Useful)) == kApMinorCategory);
    CHECK(std::string(ApCategoryForLook(ApLook::Junk)) == kApJunkCategory);
}

void TestStampingMakesTheSlotsSeed() {
    const json seed = ApSeed();
    CHECK(seed["fileType"] == kApSeedFileType);
    CHECK(seed["fileType"] != kSeedFileType && seed["fileType"] != kLegacySeedFileType); // an older build refuses it
    CHECK(IsSeedFileType(seed["fileType"]) && IsApSeedFileType(seed["fileType"]));
    CHECK(!IsApSeedFileType(kSeedFileType));
    CHECK(seed[kApKey][kApFormatKey] == kApSeedFormat);
    CHECK(seed[kApKey][kApSlotNameKey] == "Link" && seed[kApKey][kApSeedNameKey] == "AP_42");
    CHECK(seed[kApKey][kApOthersKey].size() == 3);
    CHECK(seed["foreign"].size() == 4); // the cross-game item stays, one entry per other player's item follows
    CHECK(seed["foreign"][0]["itemGame"] == "mm" && !seed["foreign"][0].contains("ap"));
    CHECK(seed["displaySeed"] == kDisplaySeed);
    CHECK(seed["file_hash"] == json::array({ 1, 23, 45, 67, 89 })); // ten digits, zero-padded: 01 23 45 67 89
    CHECK(ApFileHash(7) == json::array({ 0, 0, 0, 0, 7 }));
    CHECK(ApSeedProblem(seed).empty());
}

void TestASeedWithNoOtherPlayersStillBelongsToItsSlot() {
    json seed = PlainSeed();
    CHECK(StampApSeed(seed, { { kApSlotNameKey, "Solo" }, { kApSeedNameKey, "AP_7" } }, json::array(), kDisplaySeed)
              .empty());
    CHECK(seed["foreign"].size() == 1);
    CHECK(ApSeedProblem(seed).empty());
    CHECK(ApSaveRecord(seed)[kApSlotNameKey] == "Solo");
}

void TestStampingNeedsTheSlotAndTheRoom() {
    json seed = PlainSeed();
    CHECK(!StampApSeed(seed, json::object(), json::array(), kDisplaySeed).empty());
    CHECK(!StampApSeed(seed, { { kApSlotNameKey, "Link" } }, json::array(), kDisplaySeed).empty());
    CHECK(!StampApSeed(seed, { { kApSeedNameKey, "AP_42" } }, json::array(), kDisplaySeed).empty());
    CHECK(!StampApSeed(seed, "Link", json::array(), kDisplaySeed).empty());
    CHECK(seed == PlainSeed()); // refused means untouched
}

void TestTheLoaderReadsTheForeignKind() {
    Combo_SetForeignJson(ApSeed().dump().c_str());
    const auto oot = LoadForeignForGame(0, GAME_OOT);
    const auto mm = LoadForeignForGame(0, GAME_MM);
    CHECK(oot.size() == 3 && mm.size() == 1); // the cross-game item, and one of the other players' items per game

    const ForeignItem& hookshot = oot.at("Kokiri Chest");
    CHECK(hookshot.ap && hookshot.apPlayer == "Zelda");
    CHECK(hookshot.advancement && hookshot.category == kApMajorCategory && !hookshot.trap && !hookshot.HasDisguise());
    CHECK(hookshot.apLook == ApLook::Progression);
    CHECK(hookshot.itemGame == GAME_OOT); // the check's own game: nothing for the other game to draw or take
    CHECK(hookshot.displayName == "Zelda's Progressive Hookshot");
    CHECK(ShownForeignName(hookshot, nullptr) == "Zelda's Progressive Hookshot");
    CHECK(ShownForeignName(hookshot, "Hookshot") == "Zelda's Progressive Hookshot"); // a model's tier name never wins

    const ForeignItem& rupees = mm.at("Moon Pot");
    CHECK(rupees.ap && !rupees.advancement && rupees.category == kApJunkCategory && rupees.apLook == ApLook::Junk);

    const ForeignItem& magic = oot.at("Zora Chest");
    CHECK(magic.ap && !magic.advancement && magic.category == kApMinorCategory && magic.apLook == ApLook::Useful);

    const ForeignItem& cross = oot.at("Link Gift");
    CHECK(!cross.ap && cross.itemGame == GAME_MM && ShownForeignName(cross, nullptr) == "Hookshot (MM)");

    CHECK(LoadForeignByItem(0, GAME_MM).count("Hookshot") == 1);
    CHECK(LoadForeignByItem(0, GAME_OOT).empty()); // another player's item is not one of this game's items
    Combo_SetForeignJson(nullptr);
}

void TestEachGameApplySeesTheSentinelAtTheOtherPlayersChecks() {
    const json seed = ApSeed();
    const json oot = ApplyPayloadFromConsolidated(seed, GAME_OOT);
    const json mm = ApplyPayloadFromConsolidated(seed, GAME_MM);
    CHECK(oot["Kokiri Chest"] == kForeignSentinelNameOOT);
    CHECK(oot["Link Gift"] == kForeignSentinelNameOOT);
    CHECK(oot["Deku Reward"] == "Red Rupee"); // an own item is untouched
    CHECK(oot["Zora Chest"] == kForeignSentinelNameOOT);
    CHECK(mm["Moon Pot"] == kForeignSentinelNameMM);
    CHECK(mm["Clock Town Chest"] == "Junk");
}

void TestASeedFromBeforeTheClassesStillLoadsAndLooksByImportance() {
    json seed = ApSeed();
    for (auto& other : seed[kApKey][kApOthersKey]) {
        other.erase(kApClassKey);
    }
    for (auto& entry : seed["foreign"]) {
        entry.erase(kApForeignClassKey);
    }
    CHECK(ApSeedProblem(seed).empty());
    Combo_SetForeignJson(seed.dump().c_str());
    const auto oot = LoadForeignForGame(0, GAME_OOT);
    CHECK(oot.at("Kokiri Chest").apLook == ApLook::Progression); // it was advancement
    CHECK(oot.at("Zora Chest").apLook == ApLook::Junk);          // the useful bit was never sent
    Combo_SetForeignJson(nullptr);
}

void TestUnsafeTextBecomesSafeText() {
    CHECK(SafeApText("Zelda", "x") == "Zelda");
    CHECK(SafeApText("100% Zelda", "x") == "100 Zelda"); // the colour code mark goes, the gap closes
    CHECK(SafeApText("%rRed^&text@#c#$0|", "x") == "rRed text c 0");
    CHECK(SafeApText("[[d]] {{item}}", "x") == "d item");
    CHECK(SafeApText("a\nb\tc\x01\x1F"
                     "d\x7F",
                     "x") == "a b c d");
    CHECK(SafeApText("  Two   Spaces  ", "x") == "Two Spaces");
    CHECK(SafeApText("Caf\xC3\xA9", "x") == "Caf?"); // one character, one '?'
    CHECK(SafeApText("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA", "x") == "???");
    CHECK(SafeApText("a\xBF"
                     "b",
                     "x") == "a?b"); // a stray byte MM would read as its end-of-message code
    CHECK(SafeApText("%^&", "fallback") == "fallback");
    CHECK(SafeApText("", "fallback") == "fallback");
    CHECK(SafeApText(std::string(200, 'a'), "x").size() == kApTextMaxLength);
    CHECK(SafeApText(std::string(63, 'a') + " b", "x") == std::string(63, 'a')); // no dangling space after the cut
    for (const char* hostile : { "%r^&@#$|{}[]", "\x01\x02\xBF\xFF", "[[d]]{{item}}" }) {
        CHECK(IsApTextSafe(SafeApText(hostile, "x")));
    }
    CHECK(!IsApTextSafe("a%b") && !IsApTextSafe("caf\xC3\xA9") && !IsApTextSafe("a\nb") && IsApTextSafe("Zelda's Key"));
    const std::string once = SafeApText("A%B\xC3\xA9 [[d]]", "x");
    CHECK(SafeApText(once, "x") == once);
}

void TestTheShownNameIsSafeWhateverThePlayerAndItemAreCalled() {
    CHECK(ApShownName("%rLink^", "Boss Key (MM)") == "rLink's Boss Key");
    CHECK(ApShownName("", "") == "Someone's an item");
    CHECK(ApShownName("Z", " (OoT)") == "Z's (OoT)"); // a name that is only the tag is kept, as StripApGameTag does
    const json entry = ApForeignEntry(Other("oot", "Kokiri Chest", "&@Evil#", "%gKey^[[d]]", true));
    CHECK(IsApTextSafe(entry["displayName"]) && entry["displayName"] == "Evil's gKey d");
}

void TestTheLoaderRewritesWhatAHandMadeFileSays() {
    json seed = ApSeed();
    json& entry = seed["foreign"][1];
    CHECK(entry["ap"] == true);
    entry["displayName"] = "%rCheater^@";
    entry["apPlayer"] = "Ca\xC3\xA9\n[[d]]";
    entry["itemName"] = "%gKey (OoT)";
    ApSanitizeSeed(seed);
    CHECK(entry["apPlayer"] == "Ca? d");
    CHECK(entry["displayName"] == "Ca? d's gKey");
    CHECK(entry["itemName"] == "%gKey (OoT)");           // the item's own name is never shown, so it is not rewritten
    CHECK(seed["foreign"][0] == ApSeed()["foreign"][0]); // the cross-game item is untouched
    CHECK(seed[kApKey] == ApSeed()[kApKey]);             // so is the record of the generation
    const json again = seed;
    ApSanitizeSeed(seed);
    CHECK(seed == again);

    json plain = PlainSeed();
    const json before = plain;
    ApSanitizeSeed(plain);
    CHECK(plain == before);
    json notAnArray = { { "foreign", 3 } };
    ApSanitizeSeed(notAnArray);
    CHECK(notAnArray == json({ { "foreign", 3 } }));
}

void TestSeedsThatCannotBePlayedAreRefused() {
    CHECK(ApSeedProblem(PlainSeed()) == "not an Archipelago seed");
    CHECK(ApSeedProblem(json::array()) == "not an Archipelago seed");
    CHECK(ApSeedProblem(ApSeed()).empty());

    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApFormatKey] = kApSeedFormat + 1; }).empty()); // a newer build's
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApFormatKey] = 0; }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey].erase(kApFormatKey); }).empty());
    CHECK(!ProblemAfter([](json& s) { s.erase(kApKey); }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApSlotNameKey] = ""; }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey].erase(kApSeedNameKey); }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey].erase(kApOthersKey); }).empty());
}

void TestAnotherPlayersItemNeedsARealCheckOfItsGame() {
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApOthersKey][0]["check"] = "Nowhere"; }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApOthersKey][0]["game"] = "mm"; }).empty()); // Kokiri Chest is OoT's
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApOthersKey][0]["game"] = "n64"; }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApOthersKey][0][kApClassKey] = "legendary"; }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApOthersKey][0][kApClassKey] = 3; }).empty());
    CHECK(!ProblemAfter([](json& s) { s["foreign"][1][kApForeignClassKey] = "legendary"; }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApOthersKey][1] = s[kApKey][kApOthersKey][0]; }).empty());
    CHECK(!ProblemAfter([](json& s) { s["oot"]["placements"].erase("Kokiri Chest"); }).empty());
}

void TestTheForeignEntriesAndTheListAgree() {
    CHECK(!ProblemAfter([](json& s) { s["foreign"].erase(s["foreign"].begin() + 1); }).empty()); // one entry missing
    CHECK(!ProblemAfter([](json& s) { s["foreign"][1]["itemGame"] = "mm"; }).empty());
    CHECK(!ProblemAfter([](json& s) { s["foreign"][1]["displayName"] = ""; }).empty());
    CHECK(!ProblemAfter([](json& s) { s["foreign"][2] = s["foreign"][1]; }).empty());
    CHECK(!ProblemAfter([](json& s) { s[kApKey][kApOthersKey].erase(s[kApKey][kApOthersKey].begin()); }).empty());
}

void TestTheSaveRecordNamesTheSlotAndStartsAtNothingReceived() {
    const json record = ApSaveRecord(ApSeed());
    CHECK(record[kApSlotNameKey] == "Link" && record[kApSeedNameKey] == "AP_42");
    CHECK(record[kApReceivedIndexKey] == 0);
    CHECK(record.size() == 3);
}

void TestTheFileRecordReadsBackWhatWasStored() {
    json combo = { { kApKey, ApSaveRecord(ApSeed()) } };
    ApFileRecord record = ApFileRecordOf(combo);
    CHECK(record.valid && record.slotName == "Link" && record.seedName == "AP_42" && record.receivedIndex == 0);
    CHECK(record.server.empty() && record.connectSlot == "Link" && !record.autoConnect);

    ApStoreReceivedIndex(combo, 17);
    ApStoreConnection(combo, "localhost:38281", "Link2", true);
    record = ApFileRecordOf(combo);
    CHECK(record.receivedIndex == 17 && record.server == "localhost:38281" && record.connectSlot == "Link2" &&
          record.autoConnect);
    CHECK(combo[kApKey][kApSlotNameKey] == "Link"); // the file's own slot is never rewritten

    ApStoreConnection(combo, "localhost:38281", "", false);
    CHECK(ApFileRecordOf(combo).connectSlot == "Link" && !ApFileRecordOf(combo).autoConnect);
}

void TestAFileWithoutARecordOrWithABadOneHasNone() {
    json combo = json::object();
    CHECK(!ApFileRecordOf(combo).valid);
    ApStoreReceivedIndex(combo, 5); // nothing to write into
    ApStoreConnection(combo, "a", "b", true);
    CHECK(combo.empty());

    combo[kApKey] = { { kApSlotNameKey, "Link" }, { kApSeedNameKey, "AP_1" }, { kApReceivedIndexKey, -4 } };
    CHECK(ApFileRecordOf(combo).valid && ApFileRecordOf(combo).receivedIndex == 0);
    combo[kApKey][kApReceivedIndexKey] = "many";
    CHECK(ApFileRecordOf(combo).receivedIndex == 0);
    combo[kApKey] = { { kApSlotNameKey, "Link" } };
    CHECK(!ApFileRecordOf(combo).valid);
    combo[kApKey] = 3;
    CHECK(!ApFileRecordOf(combo).valid);
}

void TestSentLocationsFollowTheObtainedChecksIncludingTheOtherPlayersItems() {
    // Collecting "Kokiri Chest" (another player's item) marks it obtained like any check, so it is reported.
    const auto sent = ApSentLocations(R"(["Kokiri Chest","Deku Reward"])", R"(["Moon Pot"])");
    CHECK(sent.size() == 3);
    CHECK(sent[0] == "Kokiri Chest (OoT)" && sent[1] == "Deku Reward (OoT)" && sent[2] == "Moon Pot (MM)");
    CHECK(ApSentLocations("[]", "[]").empty());
    CHECK(ApSentLocations("not json", R"([1,"Moon Pot"])").size() == 1); // garbage is skipped, never thrown
}

} // namespace

int main() {
    TestNamesDropTheGameTag();
    TestTheForeignEntryDescribesTheOtherPlayersItem();
    TestAnItemWithNoClassTakesItsLookFromItsImportance();
    TestEachClassHasItsModelAndContainerLook();
    TestUnsafeTextBecomesSafeText();
    TestTheShownNameIsSafeWhateverThePlayerAndItemAreCalled();
    TestTheLoaderRewritesWhatAHandMadeFileSays();
    TestStampingMakesTheSlotsSeed();
    TestASeedWithNoOtherPlayersStillBelongsToItsSlot();
    TestStampingNeedsTheSlotAndTheRoom();
    TestTheLoaderReadsTheForeignKind();
    TestEachGameApplySeesTheSentinelAtTheOtherPlayersChecks();
    TestASeedFromBeforeTheClassesStillLoadsAndLooksByImportance();
    TestSeedsThatCannotBePlayedAreRefused();
    TestAnotherPlayersItemNeedsARealCheckOfItsGame();
    TestTheForeignEntriesAndTheListAgree();
    TestTheSaveRecordNamesTheSlotAndStartsAtNothingReceived();
    TestTheFileRecordReadsBackWhatWasStored();
    TestAFileWithoutARecordOrWithABadOneHasNone();
    TestSentLocationsFollowTheObtainedChecksIncludingTheOtherPlayersItems();
    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
