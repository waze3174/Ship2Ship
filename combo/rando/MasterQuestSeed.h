// combo/rando/MasterQuestSeed.h
// ComboShip: the Master Quest dungeon set stored in a combo seed. Header-only, shared by the game launcher and the
// comborando validator. The key and format are native SoH's spoiler `masterQuestDungeons` (an array of dungeon
// names). A seed without the key was made before the set was stored, so it pushes nothing and OOT rolls as before.
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace ComboRando {

inline constexpr const char* kMasterQuestKey = "masterQuestDungeons";

// The set to store in a seed, from SOH_DumpComboMasterQuestDungeons' text. Null when the export is missing or its
// text isn't an array, so the key is left out rather than written wrong.
inline nlohmann::json MasterQuestFromDump(const char* dump) {
    if (dump == nullptr)
        return nullptr;
    nlohmann::json names = nlohmann::json::parse(dump, nullptr, false);
    return names.is_array() ? names : nlohmann::json(nullptr);
}

// The seed's stored set as the text SOH_SetComboMasterQuestDungeons takes; empty when the seed has none.
inline std::string StoredMasterQuestText(const nlohmann::json& seed) {
    const auto it = seed.find(kMasterQuestKey);
    return (it != seed.end() && it->is_array()) ? it->dump() : std::string();
}

} // namespace ComboRando
