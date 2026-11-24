#pragma once
// ComboShip co-op (plan task 1.3): the identity of a consolidated seed file.
//
// seedId        = hash of the whole consolidated document minus its own identity fields and the purely
//                 derived/human-readable blocks. It covers masterSeed, both games' settings + tricks,
//                 placements, prices, foreign, shared items, goal, starting game, entrances, hints and
//                 worldCvars, so two clients agree on seedId iff they would build the same world.
// settingsHash  = both games' rando settings + OOT enabled tricks.
// worldCvarsHash= the worldCvars block (non-rando enhancement CVars that shape the world, WorldCvars.h).
//
// Old seeds (no worldCvars / no stored seedId) still get a seedId computed from their content; nothing
// is ever trusted from the file's own "seedId" field (it is recomputed and only compared).

#include <cstdint>

#include <nlohmann/json.hpp>

#include "CoopHash.h"

namespace Coop {

struct SeedIdentity {
    uint64_t seedId = 0;
    uint64_t settingsHash = 0;
    uint64_t worldCvarsHash = 0;
};

// Keys excluded from seedId: the identity fields themselves, and blocks derived from the rest
// (playthrough is generated from placements; displaySeed/file_hash from seed+settings; seed is the
// human-entered string that produced masterSeed).
inline constexpr const char* kSeedIdExcludedKeys[] = { "seedId",      "settingsHash", "worldCvarsHash",
                                                       "playthrough", "displaySeed",  "file_hash" };

inline SeedIdentity ComputeSeedIdentity(const nlohmann::json& seed) {
    SeedIdentity id;
    if (!seed.is_object())
        return id;
    nlohmann::json core = seed;
    for (const char* k : kSeedIdExcludedKeys)
        core.erase(k);
    id.seedId = HashJson(core);
    if (id.seedId == 0)
        id.seedId = 1; // 0 is the "no seed" sentinel on the wire

    nlohmann::json settings = nlohmann::json::object();
    if (auto oot = seed.find("oot"); oot != seed.end() && oot->is_object()) {
        settings["oot"] = oot->value("settings", nlohmann::json::object());
        settings["ootTricks"] = oot->value("enabledTricks", nlohmann::json::array());
    }
    if (auto mm = seed.find("mm"); mm != seed.end() && mm->is_object()) {
        settings["mm"] = mm->value("settings", nlohmann::json::object()); // includes gRando.SariaPriority
    }
    id.settingsHash = HashJson(settings);
    id.worldCvarsHash = HashJson(seed.value("worldCvars", nlohmann::json::object()));
    return id;
}

// Stamps the identity into the document (generation / on first load of an old seed). Returns it.
inline SeedIdentity StampSeedIdentity(nlohmann::json& seed) {
    SeedIdentity id = ComputeSeedIdentity(seed);
    seed["seedId"] = HexU64(id.seedId);
    seed["settingsHash"] = HexU64(id.settingsHash);
    seed["worldCvarsHash"] = HexU64(id.worldCvarsHash);
    return id;
}

} // namespace Coop
