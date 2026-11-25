# ComboShip deviations — Discord Rich Presence

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

Ship2Ship shows what the player is doing on their Discord profile. It is presence only: no Social SDK, no OAuth, no
client secret, no Discord binary, no Ask to Join. The launcher talks to the Discord desktop app over its local IPC
endpoint, with a small client of its own. The application id (`1575091654120050811`) is public and sits in
`combo/discord/DiscordActivity.h`.

## What is sent

Only these, and only to the Discord desktop app on the same PC, which shows them on the profile:

- **Always, while presence is on:** the game (Ocarina of Time or Majora's Mask) and the mode (Randomizer, Ocarina of
  Time or Majora's Mask), the button "Get Ship2Ship" (the GitHub releases page) and the large image `ship2ship` with the
  hover text "Ship2Ship v<version>".
- **Each behind its own switch (all on by default):**
  - the area name (also hidden whenever an Anchor room does not show everyone's location)
  - checks collected / total, randomizer saves only; a combo run adds both games
  - Link's form, age and hearts
  - Majora's Mask's day and time, in Termina only, to the hour
  - "Co-op (N players)" while in an Anchor room
  - the session timer: Discord's own elapsed counter, from when the program started
  - the save's total play time, to the minute, both games added up in a combo run; it goes in the state line, or in
    the image's hover text when the line is full
- Outside play (title, file select) the status is "In the menus".
- Plus the process id and the application id, which the protocol needs.

Never sent: paths, seed hashes, names, Anchor room names or ids, save contents. Whatever is sent is public on the
profile, which the menu's tooltip says.

## Switches

Settings > Discord (`combo/gui/ComboDiscordPanel.cpp`). CVars `gCombo.Discord.Enabled`, `.Area`, `.Checks`, `.Link`,
`.Clock`, `.Coop`, `.SessionTimer`, `.PlayTime`; `gCombo.Discord` itself is not a CVar. They are excluded from
presets and settings imports (`combo/ComboPresetScope.h`). Off means the launcher never connects, and a connection that
exists is closed, which clears the presence.

## Platforms

- **Windows:** the named pipe `\.\pipe\discord-ipc-0` to `-9`.
- **Linux:** the socket `discord-ipc-0` to `-9` in `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`, `$TEMP` and `/tmp`, each plain
  and in the Flatpak (`app/com.discordapp.Discord/`) and Snap (`snap.discord/`) subfolders.
- **macOS:** the same sockets, in `$TMPDIR`.

It does not work with web Discord (a browser tab has no IPC endpoint) or in the Steam Deck's Game Mode, where the
desktop app is not running next to the game. Without Discord the launcher retries every 30 s without a message.

## How it fits together

- `combo/discord/DiscordGameState.h`: the plain C struct the two games fill. `SOH_DiscordGetState`
  (`soh/soh/Enhancements/randomizer/ComboDiscordState.cpp`) and `MM_DiscordGetState`
  (`mm/2s2h/Rando/MiscBehavior/ComboDiscordState.cpp`) are the only game-side code; both are combo-owned files. The one
  edit to an existing game file is `Combo_MM_LinkIsAdult` in `ComboAdultBody.cpp/.h`.
- `combo/ComboShip.cpp`: `Combo_DiscordTick`, called from `Combo_SharedTick`, takes a snapshot at most once a second
  (at once after a game switch) and hands it to the presence thread. The settings come from comboui
  (`ComboUI_GetDiscordSettings`), because the launcher has no CVar API.
- `combo/discord/DiscordPresence.*`: the thread. It sends only when the formatted activity changed, never more than
  once every 15 s, and answers Discord's PING. A lost connection is retried every 30 s. It logs "connected",
  "disconnected" and the first failure, once each, to the console like the launcher's other messages.
- `combo/discord/DiscordActivity.h`: state to activity, pure. `ModeLabel` is the one place a save mode becomes a label.
- `combo/discord/DiscordIpc*`: the transport. Every read and write has a timeout (overlapped I/O on Windows, `poll` on
  Unix), and sends raise no SIGPIPE.
- Shutdown: `g_discord.Stop()` runs right after the shutdown watchdog starts, before the saves are written. The worst
  case is one 250 ms write timeout.

## Tests

`combo/discord/tests/DiscordActivityTest.cpp`: the formatter (what each switch drops, trimming, the hover overflow, the
Anchor room), the framing and the socket paths. It has no Discord and no sockets in it. Target `comboDiscordTest`
(excluded from the default build): build it, then run the exe.
