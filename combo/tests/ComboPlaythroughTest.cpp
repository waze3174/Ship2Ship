// combo/tests/ComboPlaythroughTest.cpp
//
// Tests for the cross-game playthrough walk and the requiredness pare-down (ComboPlaythrough.h) against two fake
// logic oracles, so no game module is needed. Build and run: `cmake --build <dir> --target comboPlaythroughTest`,
// then run the exe; it exits 0 when every check passes.
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "rando/ComboPlaythrough.h"
#include "ComboTestCheck.h"

namespace {

using nlohmann::json;
using namespace ComboRando;

std::vector<std::string> gOotOwned, gMmOwned;
std::string gReachable;

void ResetOot() {
    gOotOwned.clear();
}
void ResetMm() {
    gMmOwned.clear();
}
void AddOwned(std::vector<std::string>& owned, const char* arrayJson) {
    for (const auto& n : json::parse(arrayJson))
        owned.push_back(n.get<std::string>());
}
void SetOotOwned(const char* arrayJson) {
    AddOwned(gOotOwned, arrayJson);
}
void SetMmOwned(const char* arrayJson) {
    AddOwned(gMmOwned, arrayJson);
}
bool Owns(const std::vector<std::string>& owned, const char* name) {
    return std::find(owned.begin(), owned.end(), name) != owned.end();
}

// OoT: the temple and side chests are open, "Jabu Chest" needs a bottle in an OoT bottle slot (SoH's HasBottle) and
// Ganon needs the Master Sword. MM: its chest and Majora are open.
const char* OotReachable() {
    json out = json::array({ "Temple Chest", "Side Chest" });
    if (Owns(gOotOwned, "Empty Bottle"))
        out.push_back("Jabu Chest");
    if (Owns(gOotOwned, "Master Sword"))
        out.push_back("Ganon");
    gReachable = out.dump();
    return gReachable.c_str();
}
const char* MmReachable() {
    gReachable = json::array({ "MM Chest", "Moon Majora Pot 01" }).dump();
    return gReachable.c_str();
}
void PlaceNothing(const char*, const char*) {
}
uint8_t PortalOpen() {
    return 1;
}

const OracleFns kOot{ ResetOot, SetOotOwned, OotReachable, PlaceNothing, PortalOpen, nullptr };
const OracleFns kMm{ ResetMm, SetMmOwned, MmReachable, PlaceNothing, nullptr, nullptr };

const uint32_t kSharedBottles = 1u << SF_BOTTLES;

// The shape of seed shared/1009: an MM bottle in an OoT temple is the only bottle OoT can get, and OoT's next
// progress needs a bottle. With Shared Bottles it counts in OoT as an Empty Bottle.
const char* kSpoiler = R"({
  "oot": {"Temple Chest": "Bottle with Red Potion", "Jabu Chest": "Master Sword", "Side Chest": "Boomerang"},
  "mm": {"MM Chest": "Green Rupee"},
  "foreign": [{"checkGame": "oot", "checkName": "Temple Chest", "itemGame": "mm",
               "itemName": "Bottle with Red Potion", "advancement": true}],
  "sharedItems": ["bottles"]})";
const char* kOotDump = R"({"pool": [{"name": "Master Sword", "advancement": true},
                                    {"name": "Boomerang", "advancement": true}]})";
const char* kMmDump = R"({"pool": [{"name": "Green Rupee", "advancement": false}]})";

PlaythroughResult Walk(uint32_t sharedMask, json& spheres) {
    spheres = json::array();
    return RunPlaythrough(kSpoiler, kOot, kMm, "test", nullptr, &spheres, kOotDump, kMmDump, /*portalGated*/ true,
                          /*progressionOnly*/ false, CwGoal{}, /*mmStart*/ false, sharedMask);
}

void TestPooledBottleFromMmReachesTheNextOotSearch() {
    json spheres;
    const PlaythroughResult r = Walk(kSharedBottles, spheres);
    CHECK(r.beatable);
    CHECK(r.beatableSphere == 2); // sphere 0 finds the MM bottle, sphere 1 the Master Sword behind the OoT bottle
    CHECK(spheres.size() == 2);
    CHECK(r.unreachableOot == 0); // the full-inventory pass mirrors the bottle too
    CHECK(r.unreachableMm == 0);
}

void TestWithoutSharedBottlesTheMmBottleStaysInMm() {
    json spheres;
    const PlaythroughResult r = Walk(0u, spheres);
    CHECK(!r.beatable);
    CHECK(r.unreachableOot == 1); // Jabu Chest
}

void TestPareDownSeesThePooledBottle() {
    const RequirednessResult r =
        PareDownPlaythrough(kSpoiler, kOot, kMm, nullptr, kOotDump, kMmDump, {}, {}, DefaultGanonMajoraGoal,
                            /*portalGated*/ true, /*mmStart*/ false);
    // A walk that never wins marks every advancement item required; this one wins, so only the real path is.
    CHECK(r.requiredByCheck.at("oot:Temple Chest"));
    CHECK(r.requiredByCheck.at("oot:Jabu Chest"));
    CHECK(!r.requiredByCheck.at("oot:Side Chest"));
}

} // namespace

int main() {
    TestPooledBottleFromMmReachesTheNextOotSearch();
    TestWithoutSharedBottlesTheMmBottleStaysInMm();
    TestPareDownSeesThePooledBottle();
    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
