// combo/tests/ComboOracleServerTest.cpp
//
// Tests for the Archipelago oracle protocol (ComboOracleServer.h) against two fake logic oracles, so no game module is
// needed: open, reach, finalize and close, their errors, and the reach mask. Build and run:
// `cmake --build <dir> --target comboOracleServerTest`, then run the exe; it exits 0 when every check passes.
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "rando/ComboOracleServer.h"
#include "ComboTestCheck.h"

namespace {

using nlohmann::json;
using namespace ComboRando;

std::unordered_set<std::string> gOotOwned, gMmOwned;

// OoT: "Start Chest" is open, "Key Chest" needs the Small Key, "Never Chest" is never reachable and Ganon needs the
// Master Sword. MM: "MM Chest" is open and Majora needs Odolwa's Remains.
void ResetOot() {
    gOotOwned.clear();
}
void ResetMm() {
    gMmOwned.clear();
}
void SetOwned(std::unordered_set<std::string>& owned, const char* arrayJson) {
    owned.clear();
    for (const auto& n : json::parse(arrayJson))
        owned.insert(n.get<std::string>());
}
void SetOotOwned(const char* arrayJson) {
    SetOwned(gOotOwned, arrayJson);
}
void SetMmOwned(const char* arrayJson) {
    SetOwned(gMmOwned, arrayJson);
}
std::string gReachable;
const char* OotReachable() {
    json out = json::array({ "Start Chest" });
    if (gOotOwned.count("Small Key"))
        out.push_back("Key Chest");
    if (gOotOwned.count("Master Sword"))
        out.push_back("Ganon");
    gReachable = out.dump();
    return gReachable.c_str();
}
const char* MmReachable() {
    json out = json::array({ "MM Chest" });
    if (gMmOwned.count("Odolwa's Remains")) {
        out.push_back("Moon Majora Pot 01");
        out.push_back("Remains Chest");
    }
    gReachable = out.dump();
    return gReachable.c_str();
}
void PlaceNothing(const char*, const char*) {
}
uint8_t PortalOpen() {
    return 1;
}

const OracleFns kOot{ ResetOot, SetOotOwned, OotReachable, PlaceNothing, PortalOpen, nullptr };
const OracleFns kMm{ ResetMm, SetMmOwned, MmReachable, PlaceNothing, nullptr, nullptr };

const char* kOotDump = R"({
  "checks": [{"name": "Start Chest"}, {"name": "Key Chest"}, {"name": "Never Chest"}],
  "pool": [{"name": "Small Key", "advancement": true, "category": "smallKey"},
           {"name": "Master Sword", "advancement": true, "category": "major"},
           {"name": "Blue Rupee", "advancement": false, "category": "junk"}],
  "fixed": [], "items": [{"name": "Ice Trap", "trap": true}], "accessibility": {}})";
const char* kMmDump = R"({
  "checks": [{"name": "MM Chest"}, {"name": "Remains Chest"}],
  "pool": [{"name": "Odolwa's Remains", "advancement": true, "category": "major"},
           {"name": "Green Rupee", "advancement": false, "category": "junk"}],
  "fixed": [], "items": []})";

OracleBackend FakeBackend() {
    OracleBackend backend;
    backend.open = [](const json& request, OracleWorld& world) -> std::string {
        if (request.value("fail", false))
            return "the fake setup failed";
        world.setup.sohDump = kOotDump;
        world.setup.mmDump = kMmDump;
        world.masterSeed = request.value("masterSeed", 0u);
        world.session = std::make_unique<CrossWorldSession>(kOot, kMm, world.masterSeed, OotAccess::ALL_REACHABLE,
                                                            world.config.goal, GAME_OOT, 0u, COMBO_GAMES_BOTH);
        return world.session->Prepare(kOotDump, kMmDump, "");
    };
    backend.finalize = [](OracleWorld&, std::vector<CwPlacement>& placements, json& seed) {
        seed = { { "placements", placements.size() }, { "foreign", json::array() } };
        return std::string();
    };
    return backend;
}

json Call(OracleServer& server, const json& request) {
    return server.Handle(request);
}

json Owned(std::initializer_list<std::pair<const char*, int>> oot,
           std::initializer_list<std::pair<const char*, int>> mm) {
    json items = { { "oot", json::object() }, { "mm", json::object() } };
    for (const auto& [name, count] : oot)
        items["oot"][name] = count;
    for (const auto& [name, count] : mm)
        items["mm"][name] = count;
    return { { "cmd", "reach" }, { "items", items } };
}

void TestMaskHex() {
    CHECK(OracleMaskToHex({}) == "0");
    CHECK(OracleMaskToHex({ true, false, false, false, false, true }) == "21");
    CHECK(OracleMaskToHex({ false, false, false, false, true }) == "10");
}

void TestCallsNeedAnOpenSeed() {
    OracleServer server(FakeBackend());
    CHECK(!Call(server, { { "cmd", "reach" } })["ok"].get<bool>());
    CHECK(!Call(server, { { "cmd", "finalize" } })["ok"].get<bool>());
    CHECK(!Call(server, { { "cmd", "nonsense" } })["ok"].get<bool>());
    CHECK(!Call(server, json::array())["ok"].get<bool>());
    CHECK(Call(server, { { "cmd", "hello" } })["protocol"] == kOracleProtocolVersion);
}

void TestOpenDescribesTheSeed() {
    OracleServer server(FakeBackend());
    CHECK(!Call(server, { { "cmd", "open" }, { "fail", true } })["ok"].get<bool>());
    const json opened = Call(server, { { "cmd", "open" }, { "masterSeed", 7 }, { "id", 42 } });
    CHECK(opened["ok"].get<bool>());
    CHECK(opened["id"] == 42);
    CHECK(opened["locations"].size() == 5);
    CHECK(opened["locations"][0] == json::array({ "oot", "Start Chest" }));
    CHECK(opened["neverReachable"] == json::array({ 2 })); // Never Chest
    bool sawFiller = false, sawProgression = false;
    for (const auto& item : opened["items"]) {
        sawFiller = sawFiller || (item["name"] == "Blue Rupee" && item["class"] == "filler");
        sawProgression = sawProgression || (item["name"] == "Small Key" && item["class"] == "progression");
    }
    CHECK(sawFiller && sawProgression);
}

void TestReachAnswersTheClosedQuestion() {
    OracleServer server(FakeBackend());
    Call(server, { { "cmd", "open" }, { "masterSeed", 1 } });
    const json none = Call(server, Owned({}, {}));
    CHECK(none["ok"].get<bool>());
    CHECK(none["mask"] == "09"); // Start Chest (bit 0) and MM Chest (bit 3)
    CHECK(!none["win"]["won"].get<bool>());
    const json key = Call(server, Owned({ { "Small Key", 1 } }, {}));
    CHECK(key["mask"] == "0b"); // + Key Chest (bit 1)
    const json won = Call(server, Owned({ { "Master Sword", 1 } }, { { "Odolwa's Remains", 1 } }));
    CHECK(won["win"]["won"].get<bool>() && won["win"]["ganon"].get<bool>() && won["win"]["majora"].get<bool>());
    const json odd = Call(server, Owned({ { "Not An Item", 1 } }, {}));
    CHECK(odd["unknown"].size() == 1);
    CHECK(!Call(server, { { "cmd", "reach" }, { "items", { { "oot", { { "Small Key", -1 } } } } } })["ok"].get<bool>());
}

void TestMoreItemsNeverShrinkTheMask() {
    OracleServer server(FakeBackend());
    Call(server, { { "cmd", "open" }, { "masterSeed", 1 } });
    const std::vector<std::pair<const char*, const char*>> order = {
        { "oot", "Small Key" }, { "mm", "Odolwa's Remains" }, { "oot", "Master Sword" }, { "oot", "Blue Rupee" }
    };
    json items = { { "oot", json::object() }, { "mm", json::object() } };
    uint64_t before = 0;
    for (const auto& [game, name] : order) {
        items[game][name] = 1;
        const uint64_t now = std::stoull(
            Call(server, { { "cmd", "reach" }, { "items", items } })["mask"].get<std::string>(), nullptr, 16);
        CHECK((before & ~now) == 0);
        before = now;
    }
}

void TestFinalizeNeedsEveryReachableLocation() {
    OracleServer server(FakeBackend());
    Call(server, { { "cmd", "open" }, { "masterSeed", 1 } });
    auto place = [](const char* game, const char* check, const char* itemGame, const char* item) {
        return json{ { "game", game }, { "check", check }, { "item", { { "game", itemGame }, { "name", item } } } };
    };
    json placements = json::array({ place("oot", "Start Chest", "oot", "Small Key") });
    CHECK(!Call(server, { { "cmd", "finalize" }, { "placements", placements } })["ok"].get<bool>());
    placements.push_back(place("oot", "Key Chest", "mm", "Odolwa's Remains"));
    placements.push_back(place("mm", "MM Chest", "oot", "Master Sword"));
    placements.push_back({ { "game", "mm" },
                           { "check", "Remains Chest" },
                           { "other",
                             { { "player", "Zelda" },
                               { "name", "Hookshot (OoT)" },
                               { "advancement", false },
                               { "class", "useful" } } } });
    CHECK(!Call(server, { { "cmd", "finalize" }, { "placements", placements } })["ok"].get<bool>());
    const json done = Call(server, { { "cmd", "finalize" },
                                     { "placements", placements },
                                     { "archipelago", { { "slotName", "Link" }, { "seedName", "AP_123" } } } });
    CHECK(done["ok"].get<bool>());
    CHECK(done["seed"]["placements"] == 5); // the never-reachable Never Chest is padded with junk
    CHECK(done["seed"]["fileType"] == kApSeedFileType);
    CHECK(done["seed"]["archipelago"][kApOthersKey].size() == 1);
    CHECK(done["seed"]["foreign"].size() == 1);
    CHECK(done["seed"]["foreign"][0]["displayName"] == "Zelda's Hookshot");
    CHECK(done["seed"]["foreign"][0]["ap"] == true);
    CHECK(done["seed"]["archipelago"][kApOthersKey][0]["class"] == "useful"); // passed through as the apworld sent it
    CHECK(done["seed"]["foreign"][0][kApForeignClassKey] == "useful" &&
          done["seed"]["foreign"][0]["category"] == "lesser");
    CHECK(done["seed"]["file_hash"].size() == 5);
    placements.back()["other"].erase("class"); // an apworld that sends no class: the look follows the importance
    placements.back()["other"]["advancement"] = true;
    const json classless = Call(server, { { "cmd", "finalize" },
                                          { "placements", placements },
                                          { "archipelago", { { "slotName", "Link" }, { "seedName", "AP_123" } } } });
    CHECK(classless["ok"].get<bool>());
    CHECK(!classless["seed"]["archipelago"][kApOthersKey][0].contains("class"));
    CHECK(classless["seed"]["foreign"][0][kApForeignClassKey] == "progression");
    CHECK(!Call(server, { { "cmd", "finalize" },
                          { "placements", placements },
                          { "archipelago", { { "slotName", "Link" } } } })["ok"]
               .get<bool>());
    CHECK(!Call(server, { { "cmd", "finalize" },
                          { "placements", json::array({ place("oot", "Nope", "oot", "Small Key") }) } })["ok"]
               .get<bool>());
    CHECK(!Call(server, { { "cmd", "finalize" },
                          { "placements", json::array({ place("oot", "Start Chest", "oot", "Not An Item") }) } })["ok"]
               .get<bool>());
}

void TestServeReadsLinesUntilInputCloses() {
    OracleServer server(FakeBackend());
    std::istringstream in("{\"cmd\":\"hello\"}\nnot json\n\n{\"cmd\":\"close\"}\n");
    std::vector<std::string> lines;
    server.Serve(in, [&](const std::string& line) { lines.push_back(line); });
    CHECK(lines.size() == 3);
    CHECK(json::parse(lines[0])["ok"].get<bool>());
    CHECK(!json::parse(lines[1])["ok"].get<bool>());
    CHECK(json::parse(lines[2])["ok"].get<bool>());
}

} // namespace

int main() {
    TestMaskHex();
    TestCallsNeedAnOpenSeed();
    TestOpenDescribesTheSeed();
    TestReachAnswersTheClosedQuestion();
    TestMoreItemsNeverShrinkTheMask();
    TestFinalizeNeedsEveryReachableLocation();
    TestServeReadsLinesUntilInputCloses();
    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
