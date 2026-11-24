// combo/ComboSettingsImport.h
// ComboShip: ABI for the first-launch settings-import screen (issue 24; mirrors ComboExtract.h).
// comboui renders the screen + returns chosen paths; the launcher merges (SoH wins, then 2Ship, then a
// ComboShip config as the base) and applies via soh's SOH_ApplyImportedConfig. Pure C ABI — no STL across the
// boundary; fixed-size path buffers.
#pragma once

extern "C" {

// What a settings file turned out to be. Anything but COMBO_CONFIG_OK is not imported.
typedef enum ComboConfigCheck {
    COMBO_CONFIG_OK = 0,
    COMBO_CONFIG_UNREADABLE, // the file could not be opened
    COMBO_CONFIG_NOT_JSON,   // not a JSON object
    COMBO_CONFIG_NO_CVARS,   // no CVars block, so not a Ship config
    COMBO_CONFIG_NO_COMBO,   // a Ship config without ComboShip's gCombo section (ComboShip slot only)
    COMBO_CONFIG_OWN_FILE,   // this install's own comboship.json
} ComboConfigCheck;

// Checks the file at `path` for one slot. The screen shows the result, and the launcher runs the same
// check again on Import and takes the file only when it is COMBO_CONFIG_OK. May be null (no check).
typedef ComboConfigCheck (*ComboFnCheckConfig)(const char* path);

typedef struct ComboSettingsImportCallbacks {
    ComboFnCheckConfig sohCheck;
    ComboFnCheckConfig mmCheck;
    ComboFnCheckConfig comboCheck; // a comboship.json from another ComboShip install
} ComboSettingsImportCallbacks;

// Filled by the screen. Paths are "" when that slot was left empty.
typedef struct ComboSettingsImportResult {
    int action; // 0 = Skip, 1 = Import
    char sohPath[1024];
    char mmPath[1024];
    char comboPath[1024];
    int comboIncludeModsAndControls; // nonzero: also take that file's mod lists and controller input
} ComboSettingsImportResult;

// Implemented in comboui.dll. Renders the screen and blocks until Import or Skip. Returns 1 if the
// user made a decision (action valid), 0 if the window was closed (launcher treats as Skip).
typedef int (*ComboFnRunSettingsImport)(const ComboSettingsImportCallbacks* cb, ComboSettingsImportResult* out);

// Implemented in soh.dll. Applies a merged config (JSON object, UTF-8) to the live Ship::Config and
// reloads CVars + controller mappings. Caller excludes the Window block. Returns nonzero on success.
typedef int (*ComboFnApplyImportedConfig)(const char* mergedJsonUtf8);

} // extern "C"
