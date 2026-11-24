// combo/gui/ComboPlayPreset.h
//
// ComboShip: shipped control presets — one click gives a player someone else's bindings, button
// colours and control preferences, in BOTH games. See ComboPlayPreset.cpp.
#pragma once

#include "ComboExport.h"

extern "C" {

// How many presets ship with this build.
COMBO_EXPORT int ComboUI_PlayPresetCount(void);
// Display name / one-line description for preset i (nullptr when i is out of range).
COMBO_EXPORT const char* ComboUI_PlayPresetName(int i);
COMBO_EXPORT const char* ComboUI_PlayPresetDescription(int i);
// Apply preset i. parts is a bitmask: 1 = controller bindings, 2 = button/HUD colours,
// 4 = control preferences (camera, ocarina, D-pad). Returns the number of CVars written, -1 on error.
COMBO_EXPORT int ComboUI_PlayPresetApply(int i, int parts);
}

#define COMBO_PRESET_PART_BINDINGS 1
#define COMBO_PRESET_PART_COLORS 2
#define COMBO_PRESET_PART_PREFS 4
#define COMBO_PRESET_PART_ALL 7
