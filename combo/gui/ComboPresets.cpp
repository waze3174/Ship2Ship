// combo/gui/ComboPresets.cpp
//
// ComboShip: the preset files. See ComboPresets.h.
#include "ComboPresets.h"

#include "ComboPlayPreset.h"
#include "ComboPresetScope.h"
#include "rando/CrossForeign.h" // ComboRando::DataDir: the folder ship2ship.json lives in

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace ComboPresets {

namespace {

constexpr const char* kPresetDirName = "presets";
constexpr const char* kPresetExtension = ".json";
constexpr const char* kFormatKey = "format";
constexpr const char* kFormatName = "Ship2ShipPreset";
// The format name of a ComboShip preset, accepted on read.
constexpr const char* kLegacyFormatName = "ComboShipPreset";
constexpr const char* kVersionKey = "version";
constexpr const char* kSettingsKey = "settings";
constexpr int kFormatVersion = 1;
constexpr int kJsonIndent = 2;

constexpr const char* kMsgNameEmpty = "Enter a name for the preset.";
constexpr const char* kMsgNameInvalid = "Use only letters, numbers, spaces and - _ . ( ) in the name";
constexpr const char* kMsgNameReserved = "Windows reserves that name; pick another one.";
constexpr const char* kMsgNotPreset = "That file is not a Ship2Ship preset.";
constexpr const char* kMsgPresetNewer = "That preset was saved by a newer Ship2Ship.";
constexpr const char* kMsgCannotOpen = "Could not open that preset.";
constexpr const char* kMsgCannotWrite = "Could not write the preset file.";
constexpr const char* kMsgCannotDelete = "Could not delete the preset file.";

constexpr const char* kReservedDeviceNames[] = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
};
// COM1..COM9 and LPT1..LPT9
constexpr const char* kReservedPortPrefixes[] = {
    "COM",
    "LPT",
};
constexpr size_t kPortPrefixLength = 3;
constexpr size_t kReservedPortNameLength = kPortPrefixLength + 1;

// Settings the 0.2.0 merge renamed; a preset saved before it still holds the old key.
struct RenamedKey {
    const char* oldKey;
    const char* newKey;
};
constexpr RenamedKey kRenamedKeys[] = {
    { "gRandoSettings.SleepingWaterfall", "gRandoSettings.SleeplessWaterfall" },
    { "gEnhancements.TimeSavers.SleepingWaterfall", "gEnhancements.TimeSavers.SleeplessWaterfall" },
    { "gEnhancements.Equipment.GreatFairySwordBButton", "gEnhancements.Equipment.ActiveItemOnB" },
};

// ASCII only on purpose: std::filesystem::path from a narrow string is the ANSI code page on Windows.
bool IsAllowedNameChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')';
}

std::string ToUpperAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

bool IsReservedPortName(const std::string& stem) {
    if (stem.size() != kReservedPortNameLength || stem.back() < '1' || stem.back() > '9') {
        return false;
    }
    return std::any_of(std::begin(kReservedPortPrefixes), std::end(kReservedPortPrefixes),
                       [&](const char* prefix) { return stem.compare(0, kPortPrefixLength, prefix) == 0; });
}

// Windows treats "con.txt" like "con", so compare the part before the first dot.
bool IsReservedDeviceName(const std::string& name) {
    const std::string stem = ToUpperAscii(name.substr(0, name.find('.')));
    const bool isDevice = std::find(std::begin(kReservedDeviceNames), std::end(kReservedDeviceNames), stem) !=
                          std::end(kReservedDeviceNames);
    return isDevice || IsReservedPortName(stem);
}

// The key a preset entry is applied under: the new name for a renamed setting, the key itself otherwise.
std::string CurrentKey(const std::string& key) {
    const auto renamed = std::find_if(std::begin(kRenamedKeys), std::end(kRenamedKeys),
                                      [&](const RenamedKey& entry) { return key == entry.oldKey; });
    return renamed != std::end(kRenamedKeys) ? renamed->newKey : key;
}

std::filesystem::path PresetPath(const std::string& name) {
    return PresetDir() / (name + kPresetExtension);
}

} // namespace

std::filesystem::path PresetDir() {
    return ComboRando::DataDir() / kPresetDirName;
}

std::string ValidateName(const std::string& name) {
    if (name.empty()) {
        return kMsgNameEmpty;
    }
    const bool edgeBlank = name.front() == ' ' || name.back() == ' ' || name.back() == '.';
    if (name.size() > kMaxNameLength || edgeBlank || !std::all_of(name.begin(), name.end(), IsAllowedNameChar)) {
        return std::string(kMsgNameInvalid) + " (up to " + std::to_string(kMaxNameLength) + " characters).";
    }
    return IsReservedDeviceName(name) ? kMsgNameReserved : "";
}

bool Exists(const std::string& name) {
    std::error_code ec;
    return std::filesystem::exists(PresetPath(name), ec);
}

std::vector<std::string> ListNames() {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(PresetDir(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::filesystem::path& file = it->path();
        const std::string stem = file.stem().string();
        if (file.extension() == kPresetExtension && ValidateName(stem).empty()) {
            names.push_back(stem);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string Save(const std::string& name) {
    if (std::string problem = ValidateName(name); !problem.empty()) {
        return problem;
    }
    nlohmann::json settings = nlohmann::json::object();
    for (const auto& [key, value] : ReadAllLeaves()) {
        if (ComboPresetScope::IsPresetKey(key)) {
            settings[key] = value;
        }
    }
    nlohmann::json doc = nlohmann::json::object();
    doc[kFormatKey] = kFormatName;
    doc[kVersionKey] = kFormatVersion;
    doc[kSettingsKey] = std::move(settings);

    std::error_code ec;
    std::filesystem::create_directories(PresetDir(), ec);
    std::ofstream out(PresetPath(name), std::ios::trunc);
    out << doc.dump(kJsonIndent, ' ', false, nlohmann::json::error_handler_t::replace);
    return out.good() ? "" : kMsgCannotWrite;
}

std::string Load(const std::string& name, Leaves& out) {
    if (std::string problem = ValidateName(name); !problem.empty()) {
        return problem;
    }
    std::ifstream in(PresetPath(name));
    if (!in) {
        return kMsgCannotOpen;
    }
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    const bool isPreset = doc.is_object() && doc.contains(kFormatKey) &&
                          (doc[kFormatKey] == kFormatName || doc[kFormatKey] == kLegacyFormatName) &&
                          doc.contains(kSettingsKey) && doc[kSettingsKey].is_object();
    if (!isPreset) {
        return kMsgNotPreset;
    }
    if (doc.contains(kVersionKey) && doc[kVersionKey].is_number_integer() && doc[kVersionKey] > kFormatVersion) {
        return kMsgPresetNewer;
    }
    out.clear();
    const nlohmann::json& settings = doc[kSettingsKey];
    for (auto& [key, value] : settings.items()) {
        const std::string current = CurrentKey(key);
        const bool newKeyPresent = current != key && settings.contains(current);
        if (!newKeyPresent && ComboPresetScope::IsPresetKey(current)) {
            out[current] = value;
        }
    }
    return "";
}

std::string Remove(const std::string& name) {
    if (std::string problem = ValidateName(name); !problem.empty()) {
        return problem;
    }
    std::error_code ec;
    const bool removed = std::filesystem::remove(PresetPath(name), ec);
    return (removed && !ec) ? "" : kMsgCannotDelete;
}

} // namespace ComboPresets
