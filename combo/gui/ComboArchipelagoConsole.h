// combo/gui/ComboArchipelagoConsole.h
//
// The Archipelago console: everything the server prints and the client says, in colour, and a line to type into (chat,
// "!" server commands, "/" client commands). It is drawn inside Settings > Online > Archipelago, or in a window of its
// own that stays on screen while playing. The text lives in the launcher's client (combo/net/ComboApConsole.h); this
// side keeps a copy of the scrollback to draw, fed through ComboApMenuApi.
#pragma once

#include "ComboApMenuApi.h"

namespace ComboRando {

// The console for the panel: the log, the input line and the buttons. While it is popped out, a note and a button to
// dock it back.
void DrawArchipelagoConsoleSection(const ComboApMenuApi& api, bool connected);

// Adds the pop-out window to the Gui; called once at startup with the other combo windows.
void RegisterArchipelagoConsoleWindow();

} // namespace ComboRando
