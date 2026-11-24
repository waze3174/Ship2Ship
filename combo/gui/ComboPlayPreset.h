// combo/gui/ComboPlayPreset.h
//
// ComboShip: the leaf-CVar writer behind presets and the ComboShip settings import. See
// ComboPlayPreset.cpp.
#pragma once

#include "ComboConfigTree.h"

#include <string>

namespace ComboPresets {

using ComboConfigTree::Leaves;

struct ApplyOptions {
    // False while a save is loaded: the randomizer options in the batch are held back (see
    // ComboPresetScope::IsSeedBoundKey).
    bool allowSeedBound = false;
};

struct ApplyResult {
    int written = 0;           // leaves whose value differed, written and live-applied
    int unchanged = 0;         // leaves already at the requested value
    int heldBackSeedBound = 0; // randomizer options skipped because a save is loaded
    int rejected = 0;          // leaves whose value is not a CVar type
    bool needsRestart = false; // the batch changed the mod list, which the games read at boot
};

// Every CVar currently set, as flat leaves (colours as 3 or 4 number arrays). Saves the CVars first
// so the config tree is current.
Leaves ReadAllLeaves();

// Writes leaves one by one with the typed CVar setters and hands each changed key to both games'
// live-apply export. Never replaces a block, so keys outside `leaves` are untouched. The caller has
// already filtered `leaves` to the scope it allows.
ApplyResult WriteLeaves(const Leaves& leaves, const ApplyOptions& options);

// True when randomizer options may be written now: the OoT file-select screen is showing and MM is
// not the foreground game.
bool SeedBoundChangesAllowed();

// One-sentence summary of an apply for the status line.
std::string DescribeApplyResult(const ApplyResult& result);

} // namespace ComboPresets
