// combo/tests/ComboApSeedCheck.cpp
//
// Loads an Archipelago slot seed (the .ship2ship.json a generation writes) through the code the game loads it with,
// with no game module: the launcher's validation, then both games' foreign loaders and apply payloads. Every item of
// another player in the seed must come out as a foreign entry of the check's own game, and the sentinel must stand at
// its check. Build and run: `cmake --build <dir> --target comboApSeedCheck`, then `comboApSeedCheck <seed.json>...`; it
// exits 0 when every seed loads.
#include <fstream>
#include <iostream>
#include <string>

#include "rando/ComboApSeed.h"

namespace {

using nlohmann::json;
using namespace ComboRando;

constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;

struct SeedCounts {
    size_t others = 0;
    size_t progression = 0;
    size_t useful = 0;
};

// "" or why the loaders do not give the seed's other-player items back.
std::string LoaderProblem(const json& seed, SeedCounts& counts) {
    Combo_SetForeignJson(seed.dump().c_str());
    const std::unordered_map<std::string, ForeignItem> loaded[] = { LoadForeignForGame(0, GAME_OOT),
                                                                    LoadForeignForGame(0, GAME_MM) };
    const json payloads[] = { ApplyPayloadFromConsolidated(seed, GAME_OOT),
                              ApplyPayloadFromConsolidated(seed, GAME_MM) };
    const char* sentinels[] = { kForeignSentinelNameOOT, kForeignSentinelNameMM };
    std::string problem;
    for (const auto& other : seed[kApKey][kApOthersKey]) {
        const GameId game = KeyToGameId(other.value("game", std::string()));
        const std::string check = other.value("check", std::string());
        const auto found = loaded[game].find(check);
        if (found == loaded[game].end() || !found->second.ap) {
            problem = "'" + check + "' does not load as another player's item";
        } else if (found->second.displayName !=
                   ApShownName(other.value("player", std::string()), other.value("item", std::string()))) {
            problem = "'" + check + "' shows '" + found->second.displayName + "'";
        } else if (found->second.advancement != other.value("advancement", false)) {
            problem = "'" + check + "' loses its progression flag";
        } else if (found->second.apLook != ApLookFromClass(ApClassOf(other), other.value("advancement", false))) {
            problem = "'" + check + "' loses its class";
        } else if (!IsApTextSafe(found->second.displayName)) {
            problem = "'" + check + "' shows text that is not safe in a message";
        } else if (found->second.itemGame != game) {
            problem = "'" + check + "' belongs to the other game";
        } else if (payloads[game].value(check, std::string()) != sentinels[game]) {
            problem = "'" + check + "' does not hold the sentinel when applied";
        }
        if (!problem.empty()) {
            break;
        }
        ++counts.others;
        counts.progression += found->second.apLook == ApLook::Progression ? 1 : 0;
        counts.useful += found->second.apLook == ApLook::Useful ? 1 : 0;
    }
    Combo_SetForeignJson(nullptr);
    return problem;
}

bool CheckSeedFile(const std::string& path) {
    std::ifstream file(path);
    const json seed = json::parse(file, nullptr, false);
    if (!seed.is_object() || !IsSeedFileType(seed.value("fileType", std::string()))) {
        std::cerr << path << ": not a Ship2Ship seed\n";
        return false;
    }
    std::string problem = ApSeedProblem(seed);
    SeedCounts counts;
    if (problem.empty()) {
        problem = LoaderProblem(seed, counts);
    }
    if (!problem.empty()) {
        std::cerr << path << ": REFUSED: " << problem << "\n";
        return false;
    }
    const json record = ApSaveRecord(seed);
    std::cout << path << ": ok, slot '" << record[kApSlotNameKey].get<std::string>() << "', "
              << seed.value("foreign", json::array()).size() << " foreign entries, " << counts.others
              << " other players' items (" << counts.progression << " progression, " << counts.useful << " useful)\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: comboApSeedCheck <seed.json>...\n";
        return kExitUsage;
    }
    bool allLoad = true;
    for (int i = 1; i < argc; ++i) {
        allLoad = CheckSeedFile(argv[i]) && allLoad;
    }
    return allLoad ? 0 : kExitFailed;
}
