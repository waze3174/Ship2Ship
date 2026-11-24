// combo/gui/ComboPresets.h
//
// ComboShip: named settings presets. A preset is presets/<name>.json next to ship2ship.json (same
// data folder on Windows, Linux and the AppImage), holding the allowlisted leaf CVars of
// ComboPresetScope.h at the moment it was saved. Applying one goes through ComboPlayPreset's leaf
// writer, so keys the preset does not mention are left alone.
#pragma once

#include "ComboConfigTree.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ComboPresets {

using ComboConfigTree::Leaves;

constexpr size_t kMaxNameLength = 48;

std::filesystem::path PresetDir();

// Saved preset names, sorted.
std::vector<std::string> ListNames();

bool Exists(const std::string& name);

// The functions below return "" on success, or a message for the player.

// Whether `name` can be a preset file name on every OS.
std::string ValidateName(const std::string& name);

// Snapshots the current CVars into presets/<name>.json, replacing a preset of that name.
std::string Save(const std::string& name);

// Reads a preset's leaves, dropping any key outside the preset scope (a hand-edited or shared file
// cannot carry mod lists, controls or per-machine state).
std::string Load(const std::string& name, Leaves& out);

std::string Remove(const std::string& name);

} // namespace ComboPresets
