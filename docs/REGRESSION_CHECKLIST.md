# Regression checklist

What a person plays before a release. CI never boots the game: it compiles, runs the unit tests and generates seeds
(see [`ARCHITECTURE.md`](ARCHITECTURE.md), "Checks before a release"), and it has not caught any of the bugs below, which
were all found by playing. Each item names the bug it guards (`#n` are the test-round numbers; the history is in the
project notes, not in this repo). Run it on a **copy** of the previous release's folder wherever an item says upgrade.

Mark an item off only after seeing it happen. After the run, open `logs/Ship2Ship.log` and check:

- the session has `[Launcher]` lines (the launcher's own messages)
- no `[error]` or `[critical]` lines that the run did not provoke
- no `Texture is null (<path>)` line, or each one names a texture you can explain (#35's class)
- for a back-to-back file, one `[ComboBackToBack] slot options 0x...` line per load

## 1. Boot, ROM Setup and upgrade

- [ ] **Clean install:** ROM Setup asks for both ROMs and extracts them once. The relaunch does not ask again.
- [ ] **Upgrade, on a copy of the last release's folder:** one ROM Setup for both games, then the title screen with no crash
      (#28, #38). If an outdated `oot.o2r` or `mm.o2r` cannot be deleted, a message box says so and the game does not
      boot on the old archive.
- [ ] **ROM Setup errors:** an OoT ROM dropped in the MM slot, a misplaced ROM and several files dropped at once each give
      a clear message (#38).
- [ ] **A game library that will not load** (rename `soh.dll` / `libsoh.so`): a message box names it; on Windows too, where
      there is no console.
- [ ] **First launch:** the settings import offers SoH, 2S2H, ComboShip and Ship2Ship installs and can be skipped.
- [ ] **Old saves:** a save from an earlier release shows the outdated-save popup, is renamed to `.bak` and is not
      loaded.
- [ ] **Quit:** closing the window ends the process (no process left holding the save lock).

## 2. Menus and settings

- [ ] Every tab opens (Settings, Randomizer, OoT, MM, Both games) and the search finds an option.
- [ ] A setting changed in the shared groups reads the same in both games' tabs.
- [ ] Presets: save, apply and delete one.
- [ ] Discord: on by default; Settings > Discord turns it off, and the status shows the game and area while it is on.

## 3. File select and modes

- [ ] The five modes appear in order: Back to back, Randomizer, Normal, Master Quest, Majora's Mask. Master Quest is
      skipped without its archive.
- [ ] Each new file carries its badge: `S2S`, `COMBO`, `OOTR`, `MMR`, `MQ`, `MM`.
- [ ] Erase and copy a slot of each mode. A new file made in an erased slot starts clean: **3 hearts, empty bottles,
      default magic** (#30).
- [ ] A vanilla MM file shows MM's own hearts on the file select.

## 4. Seeds

Automated by CI for beatability; play to check what the generator cannot see.

- [ ] A cross-game seed, an OoT-only seed and an MM-only seed each generate and start.
- [ ] Reload a seed from the Generate page and from a spoiler file.
- [ ] Triforce Hunt, with the pieces counted across both games.
- [ ] Unified Inventory, Shared Items and cross-game entrances each on, one short run.

## 5. Crossing between the games (randomizer)

- [ ] **Mask Shop to Clock Tower and back** with the Lost Woods link off.
- [ ] **Lost Woods link on** (#2-#5): the stump marker is visible and not inside the geometry; a wrong turn in Hyrule's
      Lost Woods enters Termina's; the Clock Tower corridor's far end returns to Hyrule; the Happy Mask Salesman is
      talkable in Termina, and the Mask Shop in Hyrule works as a shop.
- [ ] **Foreign item pickup:** a check in one game that holds the other game's item gives that item and shows its model and
      texture (#35's class: no `Texture is null` for it).
- [ ] **Items of the other game on the buttons** (#1): MM's Deku Sticks and Nuts, bombs, bow and beans work in Hyrule;
      OoT's work in Termina.
- [ ] **Forms in Hyrule:** Deku, Goron, Zora and Fierce Deity Link move and attack; the Blast Mask's blast can be
      shielded (#8); each game's enhancements follow (Goron roll needing the shield button, for one).
- [ ] **Shared Bottles:** one bottle pickup gives one bottle (#7), and a pickup is not laggy (#6).
- [ ] **Boss release** (setting, off by default): beating Ganon or Majora hands over that game's remaining checks, with
      one message and no traps.

## 6. Back to back

Use a fresh file for each pass, and keep an OoT-100% test file for passes that start in Termina.

### OoT half and the first crossing

- [ ] **The Ganon blow saves** at once (#29): quit during the credits, load the file, and Link is a child at the Temple of
      Time with the Lost Woods path open into Termina.
- [ ] **"The End" ends:** the credits return to the Temple of Time without a reset (#29).
- [ ] **Majora's credits** return to OoT's title (the MM ending seam, for 0.2.3: watch which shot holds and that the quit
      does not cut the credits short).
- [ ] **MM's opening** plays on the first crossing; Skip Intro still skips it.
- [ ] **New file starts with 3 hearts** whatever other files hold (#30).

### Carries (the Back to back page, "New files")

- [ ] Hearts (with heart pieces), wallet, double magic (off by default) and bottles arrive as the page says, and the
      magic meter does not snap back in Termina.
- [ ] Songs arrive once any ocarina is usable in Termina (#32). Epona's Song plays.
- [ ] Capacity levels (quiver, bomb bag, sticks, nuts) rise only once MM holds the base item.
- [ ] **Magic early switch** (#40, 0.2.3): on, OoT's meter is there from the start and MM's Great Fairy still works.

### Locks and early-use switches in Termina

- [ ] Locked OoT items are hidden (0.2.3: greyed with a hint) until MM's own copy is found or its switch is on.
- [ ] The early ocarina: the Happy Mask Salesman sees it and teaches the Song of Healing (#31).
- [ ] Bunny Hood, Keaton, Truth: the MM mask slot shows OoT's mask as a stand-in without granting MM's item (#33).
- [ ] Goron and Zora masks are worn as plain masks until MM's own, then transform.
- [ ] Hammer, hookshots, bow, arrows, tunics, boots and swords are not greyed by OoT's adult-only rule (#36, #37).
- [ ] OoT's fire, ice and light arrows map to MM's arrows on the bow (#34).
- [ ] Din's Fire (South Clock Town and on a torch), Farore's Wind (set and return) and Nayru's Love do not crash (#35).
- [ ] **The pause screen:** the Masks page with an OoT item on a C button shows no stray cursor boxes (#41).
- [ ] Both games beaten: every lock ends.

### Shared health

- [ ] Two rando files, shared health on: raise file 1's hearts, restart, load file 2 (3 hearts), load file 1 (its own)
      (#30).
- [ ] Owl-quit in Termina, then load file 2: its own hearts.
- [ ] Pick up a heart container: the bar grows by the right amount (#39; the log has one line per heart counted).
- [ ] Shared magic and ammo in Termina follow the same rules.

## 7. Co-op (Anchor)

- [ ] Two clients in one room: presence, puppets and item sync across a game switch.
- [ ] A client that joins late resyncs.
- [ ] Kill the network mid-session: held packets survive (the outbox) and send on reconnect.
- [ ] A vanilla or back-to-back file does not try to sync.

## 8. Archipelago

- [ ] A slot's seed loads (drop or Load seed file), a new Randomizer file starts, and Settings > Online > Archipelago
      connects to a room on `localhost` (`ws://`): items arrive in both games, opened checks are sent.
- [ ] **A room on archipelago.gg** (`archipelago.gg:<port>`, no scheme) connects over `wss://`, on Windows, Linux and macOS.
- [ ] `Ship2Ship --ap-tls-probe archipelago.gg:443` prints `TLS OK` and exits 0; with `--verify-host example.org` it prints
      `TLS REFUSED` and exits 1.
- [ ] A server whose certificate is not trusted (a self-signed one on another machine's address) is refused, once, with
      "certificate could not be verified" and the reason on the status line and in the console, and no `ws://` retry.
- [ ] The console shows the room's messages, `!hint` works, and the pop-out window opens.

## 9. Platforms

- [ ] **Windows:** the zip runs from any folder; the log is written next to the exe.
- [ ] **Linux / Steam Deck:** the AppImage runs after `chmod +x`; data goes to `Ship2Ship-data/`; ROM Setup works
      with a gamepad alone in Game Mode.
- [ ] **macOS:** untested so far. Boot, ROM Setup, one cross-game seed.
