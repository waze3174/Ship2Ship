// soh/soh/Enhancements/randomizer/ComboMasterQuest.cpp — ComboShip: the Master Quest dungeon set of a combo seed.
// Combo-owned file (soh GLOB_RECURSE).
//
// FinalizeSettings rolls the MQ dungeons from whatever the shared rando RNG holds, and it runs on every prep
// (generation attempt, oracle lazy init, reload), so the set would differ between the logic that filled the seed
// and the layout played. The set is therefore pinned: the first roll of a generation is kept for the rest of that
// attempt, the launcher stores it in the seed as `masterQuestDungeons` (native SoH's spoiler key and format), and a
// reload pushes it back before the prep. A seed without the key pins nothing and rolls as it always did.

#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ComboExport.h"
#include "ComboMasterQuest.h"
#include "rando/MasterQuestSeed.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/dungeon.h"
#include "soh/OTRGlobals.h"

namespace {
// The dungeon names, as an array. nullopt = nothing pinned, FinalizeSettings keeps its own roll.
std::optional<nlohmann::json> sPinned;

nlohmann::json CurrentSet(Rando::Dungeons& dungeons) {
    nlohmann::json names = nlohmann::json::array();
    for (const Rando::DungeonInfo* dungeon : dungeons.GetDungeonList()) {
        if (dungeon->IsMQ()) {
            names.push_back(dungeon->GetName());
        }
    }
    return names;
}

// With only one of the two archives loaded FinalizeSettings forces every dungeon to that quest, and the play-time
// layout (IsSceneMasterQuest) follows the archives, so no stored set may override it. Headless picks the archives
// from the files on disk.
bool ArchivesAllowAnySet() {
    return OTRGlobals::Instance->HasMasterQuest() == OTRGlobals::Instance->HasOriginal();
}
} // namespace

bool ComboMasterQuest_ApplyPinned(Rando::Dungeons& dungeons) {
    if (!sPinned) {
        return false;
    }
    if (!ArchivesAllowAnySet()) {
        if (!sPinned->empty()) {
            SPDLOG_WARN("[ComboShip] the seed's Master Quest dungeons need both oot.o2r and oot-mq.o2r; "
                        "playing the quest the loaded archive holds");
        }
        return false;
    }
    nlohmann::json spoiler = nlohmann::json::object();
    spoiler[ComboRando::kMasterQuestKey] = *sPinned;
    dungeons.ParseJson(spoiler);
    return true;
}

void ComboMasterQuest_PinCurrent() {
    if (!sPinned) {
        sPinned = CurrentSet(*OTRGlobals::Instance->gRandoContext->GetDungeons());
    }
}

void ComboMasterQuest_Unpin() {
    sPinned.reset();
}

// Pins the set stored in a seed: a JSON array of dungeon names. Anything else (null, empty, not an array) unpins.
// Call after SOH_SetComboRandoSeed, which unpins.
extern "C" COMBO_EXPORT void SOH_SetComboMasterQuestDungeons(const char* json) {
    sPinned.reset();
    if (json == nullptr) {
        return;
    }
    nlohmann::json names = nlohmann::json::parse(json, nullptr, false);
    if (!names.is_array()) {
        SPDLOG_ERROR("[ComboShip] SOH_SetComboMasterQuestDungeons: not a JSON array of dungeon names");
        return;
    }
    sPinned = std::move(names);
}

// The pinned set, else the live context's. Written into the seed after a generation.
extern "C" COMBO_EXPORT const char* SOH_DumpComboMasterQuestDungeons(void) {
    static std::string cached;
    cached = (sPinned ? *sPinned : CurrentSet(*OTRGlobals::Instance->gRandoContext->GetDungeons())).dump();
    return cached.c_str();
}
