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

json Other(const char* game, const char* check, const char* player, const char* item, bool advancement) {
    return {
        { "game", game }, { "check", check }, { "player", player }, { "item", item }, { "advancement", advancement }
    };
}

// A finished consolidated seed of two games, the shape comborando's finalize writes before it is stamped: a native
// cross-game item is already in "foreign"; the checks that will hold other players' items hold native junk.
json PlainSeed() {
    return {
        { "fileType", kSeedFileType },
        { "seed", "AP_1" },
        { "masterSeed", 1 },
        { "oot",
          { { "placements",
              { { "Kokiri Chest", "Blue Rupee" }, { "Deku Reward", "Red Rupee" }, { "Link Gift", "Deku Stick" } } } } },
        { "mm", { { "placements", { { "Clock Town Chest", "Junk" }, { "Moon Pot", "Junk" } } } } },
        { "foreign", json::array({ { { "checkGame", "oot" },
                                     { "checkName", "Link Gift" },
                                     { "itemGame", "mm" },
                                     { "itemName", "Hookshot" },
                                     { "displayName", "Hookshot (MM)" },
                                     { "advancement", true } } }) }
    };
}

json TwoOthers() {
    return json::array({ Other("oot", "Kokiri Chest", "Zelda", "Progressive Hookshot (OoT)", true),
                         Other("mm", "Moon Pot", "Impa", "Rupees (50)", false) });
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

    const json filler = ApForeignEntry(Other("mm", "Moon Pot", "Impa", "Rupees (50)", false));
    CHECK(!filler.contains("advancement"));
    CHECK(filler["category"] == kApJunkCategory);
    CHECK(filler["displayName"] == "Impa's Rupees (50)");
}

void TestStampingMakesTheSlotsSeed() {
    const json seed = ApSeed();
    CHECK(seed["fileType"] == kApSeedFileType);
    CHECK(seed["fileType"] != kSeedFileType && seed["fileType"] != kLegacySeedFileType); // an older build refuses it
    CHECK(IsSeedFileType(seed["fileType"]) && IsApSeedFileType(seed["fileType"]));
    CHECK(!IsApSeedFileType(kSeedFileType));
    CHECK(seed[kApKey][kApFormatKey] == kApSeedFormat);
    CHECK(seed[kApKey][kApSlotNameKey] == "Link" && seed[kApKey][kApSeedNameKey] == "AP_42");
    CHECK(seed[kApKey][kApOthersKey].size() == 2);
    CHECK(seed["foreign"].size() == 3); // the cross-game item stays, one entry per other player's item follows
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
    CHECK(oot.size() == 2 && mm.size() == 1); // the cross-game item, and one of the other players' items per game

    const ForeignItem& hookshot = oot.at("Kokiri Chest");
    CHECK(hookshot.ap && hookshot.apPlayer == "Zelda");
    CHECK(hookshot.advancement && hookshot.category == kApMajorCategory && !hookshot.trap && !hookshot.HasDisguise());
    CHECK(hookshot.itemGame == GAME_OOT); // the check's own game: nothing for the other game to draw or take
    CHECK(hookshot.displayName == "Zelda's Progressive Hookshot");
    CHECK(ShownForeignName(hookshot, nullptr) == "Zelda's Progressive Hookshot");
    CHECK(ShownForeignName(hookshot, "Hookshot") == "Zelda's Progressive Hookshot"); // a model's tier name never wins

    const ForeignItem& rupees = mm.at("Moon Pot");
    CHECK(rupees.ap && !rupees.advancement && rupees.category == kApJunkCategory);

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
    CHECK(mm["Moon Pot"] == kForeignSentinelNameMM);
    CHECK(mm["Clock Town Chest"] == "Junk");
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
    TestStampingMakesTheSlotsSeed();
    TestASeedWithNoOtherPlayersStillBelongsToItsSlot();
    TestStampingNeedsTheSlotAndTheRoom();
    TestTheLoaderReadsTheForeignKind();
    TestEachGameApplySeesTheSentinelAtTheOtherPlayersChecks();
    TestSeedsThatCannotBePlayedAreRefused();
    TestAnotherPlayersItemNeedsARealCheckOfItsGame();
    TestTheForeignEntriesAndTheListAgree();
    TestTheSaveRecordNamesTheSlotAndStartsAtNothingReceived();
    TestSentLocationsFollowTheObtainedChecksIncludingTheOtherPlayersItems();
    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
