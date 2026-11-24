// combo/gui/ComboSettingsLink.h
//
// ComboShip: one control for settings both games own separately (camera speed, invert, first-person
// aim, ocarina stick, D-pad equips...). See ComboSettingsLink.cpp for the pairing table.
#pragma once

#include "ComboExport.h"

extern "C" {

// Master toggle CVar: gCombo.LinkSettings (1 = linked, the default).
COMBO_EXPORT int ComboUI_SettingsLinkEnabled(void);

// Called once per frame by the combo menu. Mirrors whichever side the user just changed onto the
// other, live-applies it in the owning game and persists. No-op when the link is off.
COMBO_EXPORT void ComboUI_SettingsLinkPoll(void);

// Push OOT's current values onto MM for every linked row, ignoring change detection. Used when the
// link is switched on (OOT is the side the Shared tab shows) and by the play preset.
COMBO_EXPORT void ComboUI_SettingsLinkPushOotToMm(void);

// How many settings the link covers — for the menu's explanatory line.
COMBO_EXPORT int ComboUI_SettingsLinkCount(void);
}
