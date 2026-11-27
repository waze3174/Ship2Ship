// combo/gui/ComboArchipelagoPanel.h
//
// The Settings > Online > Archipelago panel (combo-owned): server, slot and password for the loaded Archipelago file,
// connect and disconnect, and the connection's status. The client itself lives in the launcher (combo/net/); the panel
// reaches it through ComboApMenuApi (ComboApMenuApi.h), which the launcher registers with ComboUI_SetArchipelagoApi.
#pragma once

#include "ComboApMenuApi.h"

namespace ComboRando {

void DrawArchipelagoSharedPanel();

// The launcher's client, as it registered itself; null before that.
const ComboApMenuApi* ArchipelagoApi();

// True while the loaded file is an Archipelago file: Anchor co-op is off then.
bool ArchipelagoFileLoaded();

} // namespace ComboRando
