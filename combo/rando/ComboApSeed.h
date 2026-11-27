// combo/rando/ComboApSeed.h
// Ship2Ship + Archipelago: the seed an Archipelago generation writes for one Ship2Ship slot, and what Ship2Ship does
// with it. Header-only and pure (no game module), so the oracle server, the launcher and the tests share it.
//
// The seed is the consolidated seed of ComboSeedFileType.h with three differences:
//   - fileType is kApSeedFileType, which a build without this file refuses as "not a Ship2Ship seed".
//   - "archipelago" holds the format version, the slot and room names, and "otherPlayersItems": the checks that hold
//     another player's item ({game, check, player, item, advancement, class}). "class" is the item's Archipelago
//     classification: progression, useful, filler or trap. A seed without it falls back to "advancement".
//   - "foreign" carries one more entry per such check, flagged "ap": itemName is the item's Archipelago name,
//     displayName is "<Player>'s <Item>" and apClass the item's class. The check itself stays native junk in the
//     placements; the loaders put the foreign sentinel there, as for the cross-game items. Nothing is ever granted
//     for an "ap" entry, and it is drawn as the Archipelago item model (ApLook in CrossForeign.h).
//   - displayName and apPlayer are safe to put into either game's message text (SafeApText): the seed loader
//     rewrites them (ApSanitizeSeed), whatever file it was given.
//
// Which checks were sent is not stored: the obtained-check lists of both games say it (ApSentLocations).
#pragma once

#include <algorithm>
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
inline constexpr const char* kApClassKey = "class";
inline constexpr const char* kApForeignClassKey = "apClass";

// Category names the games' foreign-item code already reads (CrossForeign.h): they decide the container look and
// whether skipping get-item animations treats the item as important.
inline constexpr const char* kApMajorCategory = "major";
inline constexpr const char* kApMinorCategory = "lesser";
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

// What the games' message text systems read as a code instead of a letter: OoT's colour (%), box break (^), newline
// (&), player name (@), colour pair (#), altar icon ($) and singular/plural (|) marks, the [[d]] number slot of both
// games' templates, and MM's {{item}} slot.
inline constexpr const char* kApMessageCodeChars = "%^&@#$|{}[]";
inline constexpr size_t kApTextMaxLength = 64;
inline constexpr const char* kApUnknownPlayer = "Someone";
inline constexpr const char* kApUnknownItem = "an item";
inline constexpr char kApUnrenderableChar = '?';

inline bool IsApMessageCode(unsigned char c) {
    return c < 0x20 || c == 0x7F || std::strchr(kApMessageCodeChars, c) != nullptr;
}

// True when every character of `text` is printable ASCII and none is a message code: what SafeApText returns, and
// what a name must hold to go into a message as it is.
inline bool IsApTextSafe(const std::string& text) {
    return std::none_of(text.begin(), text.end(),
                        [](char c) { return static_cast<unsigned char>(c) >= 0x80 || IsApMessageCode(c); });
}

// A name that is safe to put into either game's message text: printable ASCII only, no message code, no run of
// spaces, at most kApTextMaxLength characters. Control characters and the codes above become spaces; any other
// character (neither game's font draws it, and MM reads a byte of one as a code) becomes one '?'. An empty result
// is `fallback`.
inline std::string SafeApText(const std::string& raw, const char* fallback) {
    std::string out;
    bool inForeignChar = false;
    for (const char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 0x80) {
            const bool continuation = (c & 0xC0) == 0x80;
            if (!(continuation && inForeignChar)) {
                out.push_back(kApUnrenderableChar);
            }
            inForeignChar = true;
            continue;
        }
        inForeignChar = false;
        const char safe = IsApMessageCode(c) ? ' ' : ch;
        if (safe != ' ' || (!out.empty() && out.back() != ' ')) {
            out.push_back(safe);
        }
    }
    if (out.size() > kApTextMaxLength) {
        out.resize(kApTextMaxLength);
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out.empty() ? std::string(fallback) : out;
}

inline std::string ApShownName(const std::string& player, const std::string& item) {
    return SafeApText(player, kApUnknownPlayer) + "'s " + SafeApText(StripApGameTag(item), kApUnknownItem);
}

inline bool IsApClassName(const std::string& name) {
    return name == kApClassProgression || name == kApClassUseful || name == kApClassFiller || name == kApClassTrap;
}

// The class of one of otherPlayersItems: its own, or for a seed without one the item's importance.
inline std::string ApClassOf(const nlohmann::json& other) {
    const std::string given = other.value(kApClassKey, std::string());
    if (IsApClassName(given)) {
        return given;
    }
    return other.value("advancement", false) ? kApClassProgression : kApClassFiller;
}

// The container look of an item of this look: progression reads as a major item, useful as a lesser one.
inline const char* ApCategoryForLook(ApLook look) {
    switch (look) {
        case ApLook::Progression:
            return kApMajorCategory;
        case ApLook::Useful:
            return kApMinorCategory;
        case ApLook::Junk:
            break;
    }
    return kApJunkCategory;
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
    const std::string apClass = ApClassOf(other);
    nlohmann::json entry = { { "checkGame", game },
                             { "checkName", other.value("check", std::string()) },
                             { "itemGame", game },
                             { "itemName", item },
                             { "displayName", ApShownName(player, item) },
                             { "ap", true },
                             { "apPlayer", player },
                             { kApForeignClassKey, apClass },
                             { "category", ApCategoryForLook(ApLookFromClass(apClass, advancement)) } };
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
        if (other.contains(kApClassKey) && !(other[kApClassKey].is_string() && IsApClassName(other[kApClassKey]))) {
            return "another player's item at '" + check + "' has an unknown class";
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
        if (entry.contains(kApForeignClassKey) &&
            !(entry[kApForeignClassKey].is_string() && IsApClassName(entry[kApForeignClassKey]))) {
            return "the foreign entry for '" + check + "' has an unknown class";
        }
    }
    if (flagged != listed) {
        return std::string("the seed's foreign entries and its ") + kApOthersKey + " list disagree";
    }
    return "";
}

// Rewrites the text the games show for another player's item so it is safe in message text, whoever wrote the file:
// the foreign entries' "<Player>'s <Item>" and the player. itemName stays the item's Archipelago name, which no game
// shows; the rest of the seed, including otherPlayersItems, is untouched.
inline void ApSanitizeSeed(nlohmann::json& seed) {
    const auto foreign = seed.find("foreign");
    if (foreign == seed.end() || !foreign->is_array()) {
        return;
    }
    for (auto& entry : *foreign) {
        if (!entry.is_object() || !entry.value("ap", false)) {
            continue;
        }
        const std::string player = entry.value("apPlayer", std::string());
        entry["displayName"] = ApShownName(player, entry.value("itemName", std::string()));
        entry["apPlayer"] = SafeApText(player, kApUnknownPlayer);
    }
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
