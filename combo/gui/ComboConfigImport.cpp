// combo/gui/ComboConfigImport.cpp
//
// ComboShip: see ComboConfigImport.h.
#include "ComboConfigImport.h"

#include "ComboConfigTree.h"
#include "ComboPresetScope.h"
#include "rando/CrossForeign.h" // ComboRando::DataDir: the folder comboship.json lives in

#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h> // CVarSave

#include <ctime>
#include <filesystem>
#include <fstream>

namespace ComboPresets {

namespace {

constexpr const char* kConfigFileName = "comboship.json";
constexpr const char* kBackupSuffix = ".bak-";
constexpr const char* kTimestampFormat = "%Y%m%d-%H%M%S";
constexpr size_t kTimestampBufferSize = 32;

constexpr const char* kMsgNotJson = "That file could not be read as JSON.";
constexpr const char* kMsgNotComboShip = "That is not a ComboShip config; choose a ComboShip install's comboship.json.";
constexpr const char* kMsgNothingToImport = "That file has no settings this version can import.";
constexpr const char* kMsgBackupFailed = "Could not back up the current comboship.json, so nothing was imported.";

std::string TimestampNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[kTimestampBufferSize] = {};
    std::strftime(text, sizeof(text), kTimestampFormat, &local);
    return text;
}

// Copies the live config to comboship.json.bak-<timestamp>. Returns that file's name, or "" on failure.
std::string BackUpCurrentConfig() {
    CVarSave(); // flush the live CVars so the copy is what the player sees now
    const std::filesystem::path source = ComboRando::DataDir() / kConfigFileName;
    const std::string backupName = std::string(kConfigFileName) + kBackupSuffix + TimestampNow();
    std::error_code ec;
    std::filesystem::copy_file(source, source.parent_path() / backupName, ec);
    return ec ? "" : backupName;
}

} // namespace

ImportOutcome ImportComboShipConfig(const std::string& path, const ImportOptions& options) {
    ImportOutcome outcome;
    std::ifstream in(path);
    const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    if (!root.is_object()) {
        outcome.error = kMsgNotJson;
        return outcome;
    }
    if (!ComboConfigTree::LooksLikeComboShipConfig(root)) {
        outcome.error = kMsgNotComboShip;
        return outcome;
    }

    ComboConfigTree::Leaves leaves;
    for (const auto& [key, value] : ComboConfigTree::Flatten(root[ComboConfigTree::kCVarsBlock])) {
        if (ComboPresetScope::IsImportKey(key, options.includeModsAndControls)) {
            leaves[key] = value;
        }
    }
    if (leaves.empty()) {
        outcome.error = kMsgNothingToImport;
        return outcome;
    }

    outcome.backupName = BackUpCurrentConfig();
    if (outcome.backupName.empty()) {
        outcome.error = kMsgBackupFailed;
        return outcome;
    }
    outcome.applied = WriteLeaves(leaves, ApplyOptions{ options.allowSeedBound });
    return outcome;
}

} // namespace ComboPresets
