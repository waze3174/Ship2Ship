// combo/gui/ComboPresetPanels.h
//
// ComboShip: the combo menu's Settings > Presets and Settings > Import from ComboShip panels.
#pragma once

#include <string>

namespace ComboRando {

// Settings > Presets: save the current settings under a name, apply or delete saved ones.
// ComboPresetsPanel.cpp.
void DrawPresetsSharedPanel();

// Settings > Import from Ship2Ship or ComboShip: pull another install's ship2ship.json or comboship.json into this one.
// ComboConfigImportPanel.cpp.
void DrawConfigImportSharedPanel();

// One line of result text, green for success and red for an error; nothing when `text` is empty.
void DrawStatusLine(const std::string& text, bool isError);

// Wrapped text in the disabled colour (ImGui::TextDisabled does not wrap).
void DrawHintText(const std::string& text);

} // namespace ComboRando
