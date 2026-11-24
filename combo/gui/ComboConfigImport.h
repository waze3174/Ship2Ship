// combo/gui/ComboConfigImport.h
//
// ComboShip: import the settings of another ComboShip install from its comboship.json, from the
// combo menu. The first-launch screen does the same job for a fresh install (ComboSettingsImport.h).
#pragma once

#include "ComboPlayPreset.h"

#include <string>

namespace ComboPresets {

struct ImportOptions {
    // The mod lists and controller input. They belong to the other install's player, so the owner of
    // that install usually wants them; ComboPresetScope keeps them out of presets.
    bool includeModsAndControls = true;
    bool allowSeedBound = false;
};

struct ImportOutcome {
    std::string error;      // non-empty: nothing was changed
    std::string backupName; // file name of the copy of the current comboship.json
    ApplyResult applied;
};

// Applies the chosen file's allowlisted CVars through the leaf writer. Never touches the file's Window
// block or anything else outside its CVars. Backs up the current comboship.json first.
ImportOutcome ImportComboShipConfig(const std::string& path, const ImportOptions& options);

} // namespace ComboPresets
