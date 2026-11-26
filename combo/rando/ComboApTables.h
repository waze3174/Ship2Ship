// combo/rando/ComboApTables.h
// ComboShip: every item and check of both libraries under the names the Archipelago world uses. `comborando
// --ap-tables` writes this; scripts/ap/update_tables.py merges the names into apworld/ship2ship/data/ids.json.
//
// A name is the native name plus the game, "Progressive Hookshot (OoT)", so both games' items can live in one table.
// The dumps list every item the library has (not only the ones a seed's settings put in the pool) and every check
// its hint data knows, whatever the settings shuffle.
#pragma once

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ComboRando {

inline constexpr int kApTablesVersion = 1;
inline constexpr const char* kApTagOot = " (OoT)";
inline constexpr const char* kApTagMm = " (MM)";

// The table's entries by AP name. The same name twice with the same data is one entry (two native ids that read the
// same to the oracle, which finds items by name); with different data it is a conflict the script fails on.
class ApTableBuilder {
  public:
    void Add(const nlohmann::json& entry) {
        const std::string apName = entry.at("apName").get<std::string>();
        const auto [at, inserted] = entries_.emplace(apName, entry);
        if (!inserted && at->second != entry)
            conflicts_.push_back(apName);
    }
    nlohmann::json Entries() const {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& [name, entry] : entries_)
            out.push_back(entry);
        return out;
    }
    const std::vector<std::string>& Conflicts() const {
        return conflicts_;
    }

  private:
    std::map<std::string, nlohmann::json> entries_;
    std::vector<std::string> conflicts_;
};

inline void AddApItems(ApTableBuilder& table, const char* game, const char* tag, const nlohmann::json& dump) {
    for (const auto& it : dump.value("items", nlohmann::json::array())) {
        const std::string name = it.value("name", std::string());
        if (name.empty())
            continue;
        table.Add({ { "game", game },
                    { "name", name },
                    { "apName", name + tag },
                    { "category", it.value("category", std::string("unknown")) },
                    { "advancement", it.value("advancement", false) },
                    { "trap", it.value("trap", false) } });
    }
}

inline void AddApLocation(ApTableBuilder& table, const char* game, const char* tag, const std::string& name,
                          const std::string& area) {
    if (name.empty())
        return;
    table.Add({ { "game", game }, { "name", name }, { "apName", name + tag }, { "area", area } });
}

// sohDump / mmDump: each game's static data dump; sohHintDump: OOT's hint data (every static check with its area).
inline nlohmann::json BuildApTables(const std::string& sohDump, const std::string& sohHintDump,
                                    const std::string& mmDump) {
    const nlohmann::json soh = nlohmann::json::parse(sohDump);
    const nlohmann::json hints = nlohmann::json::parse(sohHintDump);
    const nlohmann::json mm = nlohmann::json::parse(mmDump);

    ApTableBuilder items, locations;
    AddApItems(items, "oot", kApTagOot, soh);
    AddApItems(items, "mm", kApTagMm, mm);
    for (const auto& c : hints.value("checks", nlohmann::json::array()))
        AddApLocation(locations, "oot", kApTagOot, c.value("name", std::string()), c.value("area", std::string()));
    for (const auto& [name, area] : mm.value("locationHints", nlohmann::json::object()).items())
        AddApLocation(locations, "mm", kApTagMm, name, area.is_string() ? area.get<std::string>() : std::string());

    nlohmann::json conflicts = nlohmann::json::array();
    for (const auto& n : items.Conflicts())
        conflicts.push_back({ { "table", "items" }, { "apName", n } });
    for (const auto& n : locations.Conflicts())
        conflicts.push_back({ { "table", "locations" }, { "apName", n } });
    return { { "version", kApTablesVersion },
             { "items", items.Entries() },
             { "locations", locations.Entries() },
             { "conflicts", std::move(conflicts) } };
}

} // namespace ComboRando
