// combo/rando/ComboApSeed.h
// Ship2Ship + Archipelago: the seed an Archipelago generation writes for one Ship2Ship slot, and what Ship2Ship does
// with it. Header-only and pure (no game module), so the oracle server, the launcher and the tests share it.
//
// The seed is the consolidated seed of ComboSeedFileType.h with three differences:
//   - fileType is kApSeedFileType, which a build without this file refuses as "not a Ship2Ship seed".
//   - "archipelago" holds the format version, the slot and room names, and "otherPlayersItems": the checks that hold
//     another player's item ({game, check, player, item, advancement}).
//   - "foreign" carries one more entry per such check, flagged "ap": itemName is the item's Archipelago name and
//     displayName is "<Player>'s <Item>". The check itself stays native junk in the placements; the loaders put the
//     foreign sentinel there, as for the cross-game items. Nothing is ever granted for an "ap" entry.
//
// Which checks were sent is not stored: the obtained-check lists of both games say it (ApSentLocations).
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

#include "ComboSeedFileType.h"
#include "CrossForeign.h"

namespace ComboRando {

inline constexpr int kApSeedFormat = 1;

// The key of the seed's block and of the save's record (combo.archipelago in the slot's container).
inline constexpr const char* kApKey = "archipelago";
inline constexpr const char* kApFormatKey = "formatVersion";
inline constexpr const char* kApSlotNameKey = "slotName";
inline constexpr const char* kApSeedNameKey = "seedName";
inline constexpr const char* kApOthersKey = "otherPlayersItems";
inline constexpr const char* kApReceivedIndexKey = "receivedIndex";

// Category names the games' foreign-item code already reads (CrossForeign.h): they decide the container look and
// whether skipping get-item animations treats the item as important.
inline constexpr const char* kApMajorCategory = "major";
inline constexpr const char* kApJunkCategory = "junk";

// Archipelago names carry the game as " (OoT)" or " (MM)"; an item of any other game has no tag.
inline constexpr const char* kApOotTag = " (OoT)";
inline constexpr const char* kApMmTag = " (MM)";

inline std::string StripApGameTag(std::string name) {
    const size_t n = std::strlen(kApOotTag);
    if (name.size() > n && name.compare(name.size() - n, n, kApOotTag) == 0) {
        name.resize(name.size() - n);
        return name;
    }
    return StripGameSuffix(std::move(name));
}

inline std::string ApShownName(const std::string& player, const std::string& item) {
    return player + "'s " + StripApGameTag(item);
}

inline std::string ApLocationName(GameId game, const std::string& check) {
    return check + (game == GAME_OOT ? kApOotTag : kApMmTag);
}

// The "foreign" entry for one of otherPlayersItems.
inline nlohmann::json ApForeignEntry(const nlohmann::json& other) {
    const std::string game = other.value("game", std::string());
    const std::string player = other.value("player", std::string());
    const std::string item = other.value("item", std::string());
    const bool advancement = other.value("advancement", false);
    nlohmann::json entry = { { "checkGame", game },
                             { "checkName", other.value("check", std::string()) },
                             { "itemGame", game },
                             { "itemName", item },
                             { "displayName", ApShownName(player, item) },
                             { "ap", true },
                             { "apPlayer", player },
                             { "category", advancement ? kApMajorCategory : kApJunkCategory } };
    if (advancement) {
        entry["advancement"] = true;
    }
    return entry;
}

// The five hash icons a file shows, derived from the display seed as OOT's own hash is (ten decimal digits, five
// pairs).
inline nlohmann::json ApFileHash(uint32_t displaySeed) {
    std::string digits = std::to_string(displaySeed);
    digits.insert(0, 10 - digits.size(), '0');
    nlohmann::json icons = nlohmann::json::array();
    for (size_t pair = 0; pair < 5; ++pair) {
        icons.push_back(std::stoi(digits.substr(pair * 2, 2)));
    }
    return icons;
}

// Turns a finished consolidated seed into the slot's Archipelago seed. `info` is the generation's own: the slot name
// and the room's seed name. Returns "" or why the seed cannot be one.
inline std::string StampApSeed(nlohmann::json& seed, const nlohmann::json& info, const nlohmann::json& others,
                               uint32_t displaySeed) {
    const std::string slotName = info.is_object() ? info.value(kApSlotNameKey, std::string()) : std::string();
    const std::string seedName = info.is_object() ? info.value(kApSeedNameKey, std::string()) : std::string();
    if (slotName.empty() || seedName.empty()) {
        return std::string("an Archipelago seed needs \"") + kApKey + "\" with a " + kApSlotNameKey + " and a " +
               kApSeedNameKey;
    }
    seed["fileType"] = kApSeedFileType;
    seed["displaySeed"] = displaySeed;
    seed["file_hash"] = ApFileHash(displaySeed);
    seed[kApKey] = { { kApFormatKey, kApSeedFormat },
                     { kApSlotNameKey, slotName },
                     { kApSeedNameKey, seedName },
                     { kApOthersKey, others } };
    nlohmann::json& foreign = seed["foreign"];
    if (!foreign.is_array()) {
        foreign = nlohmann::json::array();
    }
    for (const auto& other : others) {
        foreign.push_back(ApForeignEntry(other));
    }
    return "";
}

inline bool IsApSeed(const nlohmann::json& seed) {
    return seed.is_object() && IsApSeedFileType(seed.value("fileType", std::string()));
}

namespace ApSeedDetail {

inline const char* GameKeyIfValid(const std::string& game) {
    return game == "oot" ? "oot" : game == "mm" ? "mm" : nullptr;
}

inline bool HasPlacement(const nlohmann::json& seed, const std::string& game, const std::string& check) {
    const auto gameBlock = seed.find(game);
    if (gameBlock == seed.end() || !gameBlock->is_object()) {
        return false;
    }
    const auto placements = gameBlock->find("placements");
    return placements != gameBlock->end() && placements->is_object() && placements->contains(check);
}

inline std::string CheckKey(const std::string& game, const std::string& check) {
    return game + "|" + check;
}

} // namespace ApSeedDetail

// Why this Archipelago seed cannot be played, or "" when it can. Refuses what a build of another format wrote, a seed
// without its slot, and any item of another player that is not at a real check of its own game.
inline std::string ApSeedProblem(const nlohmann::json& seed) {
    if (!IsApSeed(seed)) {
        return "not an Archipelago seed";
    }
    const auto block = seed.find(kApKey);
    if (block == seed.end() || !block->is_object()) {
        return "the seed has no Archipelago block";
    }
    const auto format = block->find(kApFormatKey);
    if (format == block->end() || !format->is_number_integer()) {
        return "the seed has no Archipelago format version";
    }
    if (format->get<int>() != kApSeedFormat) {
        return "the seed is Archipelago format " + std::to_string(format->get<int>()) +
               " and this build reads format " + std::to_string(kApSeedFormat);
    }
    if (block->value(kApSlotNameKey, std::string()).empty() || block->value(kApSeedNameKey, std::string()).empty()) {
        return "the seed does not name its slot and room";
    }
    const auto others = block->find(kApOthersKey);
    if (others == block->end() || !others->is_array()) {
        return std::string("the seed has no ") + kApOthersKey + " list";
    }
    std::set<std::string> listed;
    for (const auto& other : *others) {
        const std::string game = other.value("game", std::string());
        const std::string check = other.value("check", std::string());
        if (ApSeedDetail::GameKeyIfValid(game) == nullptr || check.empty()) {
            return "another player's item names no game or check";
        }
        if (!ApSeedDetail::HasPlacement(seed, game, check)) {
            return "another player's item is at '" + check + "', which is not a " + game + " check of this seed";
        }
        if (!listed.insert(ApSeedDetail::CheckKey(game, check)).second) {
            return "'" + check + "' holds two of another player's items";
        }
    }
    std::set<std::string> flagged;
    for (const auto& entry : seed.value("foreign", nlohmann::json::array())) {
        if (!entry.value("ap", false)) {
            continue;
        }
        const std::string game = entry.value("checkGame", std::string());
        const std::string check = entry.value("checkName", std::string());
        if (entry.value("itemGame", std::string()) != game || entry.value("displayName", std::string()).empty() ||
            !flagged.insert(ApSeedDetail::CheckKey(game, check)).second) {
            return "the foreign entry for '" + check + "' is not one of another player's items";
        }
    }
    if (flagged != listed) {
        return std::string("the seed's foreign entries and its ") + kApOthersKey + " list disagree";
    }
    return "";
}

// The save's record of an Archipelago seed (combo.archipelago). The received index counts the server's items already
// given; the client advances it with the grant.
inline nlohmann::json ApSaveRecord(const nlohmann::json& seed) {
    const nlohmann::json block = seed.value(kApKey, nlohmann::json::object());
    return { { kApSlotNameKey, block.value(kApSlotNameKey, std::string()) },
             { kApSeedNameKey, block.value(kApSeedNameKey, std::string()) },
             { kApReceivedIndexKey, 0 } };
}

// Every obtained check of both games under its Archipelago location name (the obtained lists are JSON arrays of check
// names, as Combo_SOH_GetObtainedChecks and Combo_MM_GetObtainedChecks return them). A check holding another player's
// item is obtained like any other, so this is the set the client has to report.
inline std::vector<std::string> ApSentLocations(const std::string& ootObtainedJson, const std::string& mmObtainedJson) {
    std::vector<std::string> sent;
    const std::pair<GameId, const std::string*> lists[] = { { GAME_OOT, &ootObtainedJson },
                                                            { GAME_MM, &mmObtainedJson } };
    for (const auto& [game, text] : lists) {
        const nlohmann::json names = nlohmann::json::parse(*text, nullptr, false);
        if (!names.is_array()) {
            continue;
        }
        for (const auto& name : names) {
            if (name.is_string()) {
                sent.push_back(ApLocationName(game, name.get<std::string>()));
            }
        }
    }
    return sent;
}

} // namespace ComboRando
