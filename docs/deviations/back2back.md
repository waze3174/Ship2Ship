# Back-to-back mode (ComboShip Ultimate)

A back-to-back slot plays **vanilla Ocarina of Time, then vanilla Majora's Mask**, in one save slot. It has no seed and no
randomizer. The file select shows it as the Ship2Ship logo alone (the randomizer's choice has a "Randomizer" subtitle)
and labels the slot `S2S`. Ganon's defeat ends OoT with its full ending and opens the Lost Woods path into Termina for
child Link; Majora's defeat opens the way back. Save compatibility: no `COMBO_RELEASE_VERSION` change and no change to
an existing field; the only new keys are `sequential` and `options` inside `combo.mode`. An older build reads such a slot as a combo
randomizer slot with no seed, so downgrading is not covered.

This page records stage A (the plumbing and the story gates, and Master Quest for Ocarina of Time's half) and stages B to E
(MM's real opening on the first crossing, the settings page with the item locks in Termina, carrying capacities over,
and everything usable after both games). Not done: adult Link in Termina (see "Not done yet").

## The mode

`combo.mode` for a back-to-back slot (`combo/rando/ComboMode.h`):

```json
"mode": { "game": "ootmm", "kind": "vanilla", "quest": "normal", "sequential": true, "options": 14 }
```

- `ComboMode::BackToBack(quest, options)`, `IsBackToBack()`. `sequential` and `options` (stage C) are written only for this mode;
  a slot without `options` takes the page's defaults. `FromJson` reads a vanilla
  `ootmm` as a back-to-back slot only when `sequential` is true; every older mode reads exactly as before (a vanilla
  `ootmm` without it is still a randomizer slot).
- `IsVanilla()` is true, so the launcher's vanilla paths (no seed bound, no combo rando pushed) apply. `game` is
  `ootmm`, so `Combo_GamesSolo()` is false in both DLLs and the cross-game code is not refused as in a one-game slot.
- `HasVanillaMmSave()` (vanilla MM slot or back-to-back) is what `LoadComboCompletion` pushes to `MM_SetComboVanillaSlot`,
  so `Combo_LoadMMSaveFile` expects, and loads, a `SAVETYPE_VANILLA` save for the MM half.
- `ComboModeLabel` returns `S2S`, or `S2S MQ` for a Master Quest slot (see "Master Quest"); the file select draws it where it draws the other mode labels
  (`FileChoose_GetQuestLabelText`, `Combo_SlotModeLabel`).
- Creation: `Combo_InitBackToBackSlot` (`combo/ComboShip.cpp`) makes the vanilla slot (no seed), then
  `MM_InitVanillaSaveFile`, as `Combo_InitVanillaMmSlot` does, but the slot's last game stays OoT. Nothing is shared:
  `g_sharedMask` is 0 as in every vanilla slot.
- Loading a slot last saved in MM resumes MM (`Combo_ResumeMMIfLastSavedThere`), as for a vanilla MM slot.

### The carousel entry

`QUEST_COMBO_BACK2BACK` (7, `soh/include/z64save.h`) follows `QUEST_COMBO_MM` (6). Like it, it exists only in the file
select's `questType[]` and `gComboNewSaveQuest`; `QUEST_SAVE_ID()` stores the OoT half as a Normal Quest file, or a Master Quest one when the setting asks for it (see
"Master Quest"). Touched:
`MAX_QUEST` and the draw case (`z_file_choose.c`, the Ship2Ship logo 128x128 centred at 160,135 where the other logos are
centred), `SohFileSelect_IsQuestHidden` and `SohFileSelect_CountVisibleQuests` (`FileSelectEnhancements.cpp`), and the
static_assert in `OTRGlobals.cpp` that ties the value to `kSohQuestComboBackToBack` in `ComboMode.h`.

### The hide switch

The mode is unfinished, so the carousel hides it until the player asks for it: `gCombo.BackToBack.Show`
(`kBackToBackShowCVar`, `combo/rando/ComboBackToBackSettings.h`; 0 or unset = hidden), a checkbox at the top of Settings >
Back to back. `SohFileSelect_IsQuestHidden` returns hidden for `QUEST_COMBO_BACK2BACK` while it is off, so the
carousel's own skipping (`NextVisibleQuest`) and its fallback for a selection that becomes hidden apply as for a missing
Master Quest archive. Only the new-file choice is hidden: a slot that already is a back-to-back one loads, plays and
shows its `S2S` badge whatever the switch says. A slot made while it was on may not match the finished mode.

**Co-op is not supported in this mode.** A back-to-back slot is `IsVanilla()`, so the co-op dormant gate stays closed and
the boss release (`Combo_OnFinalBossDefeated` queues it only for a non-vanilla slot) refuses it.

## The settings page and Master Quest

Settings > Back to back > New files (`combo/gui/ComboBackToBackPanel.cpp`) is the mode's own page. Its header says that
the choices apply to new back-to-back files when they're created. Every setting on it is read once, when a file is
created, and is fixed for that file from then on; the rest of the mode's settings are on the same page (stage C).
The CVars live under `gCombo.BackToBack.` (`combo/rando/ComboBackToBackSettings.h`); each is a leaf, and no
`gCombo.BackToBack` leaf exists to collide with that subtree. Presets carry them like any other `gCombo.` preference.

**Master Quest** (`gCombo.BackToBack.MasterQuest`, 0 or unset = Normal Quest, 1 = Master Quest). The file's quest is
decided where OoT stores it:

1. Choosing the back-to-back entry on the file select runs `QUEST_SAVE_ID` (`soh/include/z64save.h`), which for the
   back-to-back entry calls `Combo_BackToBackSaveQuest` (`soh/soh/OTRGlobals.cpp`): `QUEST_MASTER` when the CVar is on
   and `oot-mq.o2r` is loaded (`OTRGlobals::HasMasterQuest`, the check the carousel's own Master Quest entry uses),
   else `QUEST_NORMAL`. It runs again when the file is created (`Sram_InitSave`, `z_sram.c`), and that is the value
   saved: `gSaveContext.ship.quest.id`, exactly as for a vanilla Master Quest file.
2. The new-save callback runs after OoT has written the save, so `Combo_InitBackToBackSlot` reads the stored quest
   (`SOH_GetNewSaveStoredQuest`) and writes it into the slot's `combo.mode` as `"quest": "mq"`. The mode records what the
   save plays; the setting is never read again.
3. Every later load plays the stored quest: OoT's `IS_MASTER_QUEST` and `ResourceMgr_IsGameMasterQuest` follow
   `quest.id`, whatever the CVar says by then. A slot made before this setting has no `quest` key and reads as Normal
   Quest.

Without `oot-mq.o2r` the page shows the setting greyed out with the reason ("Needs the Master Quest ROM (oot-mq.o2r)...",
asked of `SOH_GameHasMasterQuest`), and a new back-to-back file is a Normal Quest one even if the CVar is on, so a file
that cannot load is never made.

The file select marks such a slot `S2S MQ` in the quest label box (it shows `S2S` for a Normal Quest one). Normal and
Master Quest vanilla OoT slots show `MQ` through that same box; a back-to-back slot has only the one label, so its
mode's label carries both. The label drawer (`FileChoose_DrawQuestLabelText`) draws letters from the loaded font as
before and everything else (digits, which `S2S` needs) from the port's character table; a space is skipped.

## The cross gate

`combo/rando/ComboCrossGate.h` is a bit mask the launcher pushes to both DLLs (`SOH_SetComboCrossGate`,
`MM_SetComboCrossGate`) when a slot loads (`LoadComboCompletion`) and each time a final boss is recorded
(`SaveComboCompletion`). The completion record is the slot's `combo.completion.oot` / `.mm`, written by
`Combo_OnFinalBossDefeated`. Every other slot has `kAllOpen`, which gates nothing.

**"The link exists" is separate from "the link is open".** A back-to-back slot has the Lost Woods link switched on from
the start (`g_lostWoodsLink`), so `Combo_LostWoodsLinkOn()` is true throughout. That keeps OoT's Happy Mask Shop an
ordinary shop (`Combo_OOT_LostWoodsLinkKeepsMaskShop`), so its door never reverts to the portal into Termina, and keeps
the Clock Tower's South Clock Town door in MM (`Combo_MM_ClockTowerEntryStaysInMm`). What is open is the mask.

| State | Hyrule to Termina | Termina to Hyrule |
|---|---|---|
| Ganon not beaten | nothing | nothing |
| Ganon beaten, Majora not | the Lost Woods path, child Link only | nothing |
| Both beaten | everything, either age | everything |

Each path, and what blocks it:

| Path | Blocked by |
|---|---|
| OoT Lost Woods north wrong turn (collision poly 48) | `Combo_OOT_LostWoodsLinkExit` tests `PathOpen()` (link exists and `Combo_LostWoodsToTerminaOpen()`): closed, the poly is not intercepted and the wrong turn loops back to Kokiri Forest as in vanilla. Open, it calls `Combo_RequestLostWoodsCrossSwitch`, which re-checks. Child only until both bosses are beaten |
| The stump in front of the Lost Woods tunnel | `SpawnMarkerInRoomZero` tests `PathOpen()`: no stump while the path is closed for the Link on screen (so none for adult Link before both bosses) |
| Every other OoT crossing: warp songs, the owl statue song, cross entrances, the debug console and the pause menu's test-harness crossing | `Combo_RequestCrossSwitch` (`OTRGlobals.cpp`) refuses unless `Combo_CrossToTerminaOpen()`; the owl song refuses with its "can't warp here" text first (`ComboOwlWarp.cpp`) |
| OoT's Happy Mask Shop door | never crosses in this mode: the link exists, so `Combo_OOT_LostWoodsLinkKeepsMaskShop()` is true |
| MM Clock Tower corridor's far end back to the Lost Woods | `Combo_MM_LostWoodsLinkExit` crosses only if `Combo_CrossToHyruleOpen()`; closed, the exit leads where vanilla's does (Deku Butler's area, OPENINGDAN spawn 1) |
| MM Clock Tower hall's stone doors | `HandleStoneDoorInit` removes them only if the way into Hyrule is open: closed, the hall's door locks up as in vanilla and there is no corridor back |
| MM Clock Tower South Clock Town door (spawn 1) | the portal return never runs (the link exists) and `Combo_RequestPortalReturn` refuses unless `Combo_CrossToHyruleOpen()` |
| Every other MM crossing: warp songs, cross entrances, the adult body entrances, the console | `Combo_RequestCrossSwitch` (`BenPort.cpp`) refuses unless `Combo_CrossToHyruleOpen()`; MM's warp-song refusal runs first (`WarpSongs.cpp`) |

In Termina, before Majora is beaten, the arrival from OoT and the walk from MM's Lost Woods to the Clock Tower corridor's
far end and up to the hall work as in a combo slot's link; only the way back is closed. Ganon beaten with Majora not is
the only state where adult Link and child Link differ: adult Link finds the Lost Woods as vanilla.

## The ending

OoT (`ComboBackToBackEnding.cpp`, `soh/src/code/z_play.c`):

1. Ganon's last cutscene (`z_boss_ganon2.c`, `csState 20`) asks `gComboFinalBossDefeated(0, fileNum)`. The launcher
   records Ganon as beaten (opening the gate) and, for a back-to-back slot, answers 1: the combo-seed skip of the credits
   is not taken, and the vanilla ending plays: the Chamber of the Sages, Zelda's farewell (`z_demo.c` terminator 117) and
   the credits.
2. The credits end the way the game ends: `Play_Init` receives `ENTR_LOAD_OPENING` (the invalid entrance that exits to the
   title). It first asks `Combo_BackToBack_ResumeAfterCredits`. In a back-to-back slot with Ganon beaten and Majora not,
   that call unmutes the credits' muted sound banks, returns the game mode to normal, swaps in the child's equipment if
   Link is adult, sets his age to child and the entrance to `ENTR_TEMPLE_OF_TIME_2` (the pedestal arrival a child Link
   gets when he returns through the Master Sword), and `Play_Init` carries on loading that scene.
3. Once the Temple of Time has settled (no transition, no cutscene), the slot is saved (`Play_PerformSave`).

The player then walks to the Lost Woods and takes the path. If the game is closed during the credits, the slot loads with
adult Link and Ganon recorded; the Master Sword pedestal still makes him a child.

MM (`BenPort.cpp`, `mm/src/code/z_play.c`): Majora's last cutscene (`z_boss_07.c`) asks the same callback, which records
Majora as beaten (opening the way back, and both ways once Ganon is beaten too) and answers 1: Majora's real credits
play. MM then reaches `Play_Init` with entrance -1, which in vanilla loads MM's own title. In the launcher that is
unreachable (MM's boot would wipe the save through MM's file select), so for a back-to-back slot `Combo_MM_CreditsEnded`
asks for the owl-save quit: the slot returns to OoT's title. The slot is not saved again there, so MM resumes from its last
save, with the gate open from the completion record.

## Stage B: Majora's Mask's real opening on the first crossing

Read from the code, not run. The opening under the launcher is untested, as it is for a vanilla Majora's Mask slot.

- The MM save made at slot creation is a vanilla one with the opening unplayed (`isFirstCycle` false; it is set when Link
  first enters South Clock Town and never cleared). Entering MM, `Setup_InitImpl` (`mm/src/code/title_setup.c`) loads that
  save, and `Combo_StartVanillaOpening` selects the opening (`ENTRANCE(CUTSCENE, 0)`, human Link). In a back-to-back slot
  the first crossing also names the Lost Woods spawn as the arrival (`gComboTargetEntrance`); that branch now drops it,
  so the opening stands: Skull Kid, Epona and the Ocarina stolen, the Deku curse. With 2S2H's Skip Intro Sequence on, the
  opening is replaced by South Clock Town as for any file.
- `Combo_MM_BackToBackOpeningPending` (`BenPort.cpp`) is true for a back-to-back slot whose `isFirstCycle` is not set. While
  it is true:
  - the Lost Woods link's hooks stand down (`Active()` in `ComboLostWoodsLink.cpp`): the east-exit redirect would send the
    intro's fall to the Clock Tower corridor, and the Salesman's flag would skip his cutscene;
  - the carried mask and button layout are dropped instead of applied on the first frame (`ComboAdultBody.cpp`,
    `ComboEquipShare.cpp`), so the opening starts with nothing worn or assigned, as in a vanilla file.
  The Clock Tower's South Clock Town door still "stays in MM" (`Combo_MM_ClockTowerEntryStaysInMm` follows the link, not
  `Active()`).
- Later crossings (the opening played, so `isFirstCycle` set) behave as before: the target entrance applies.
- Quitting during the opening writes nothing, so the next load of the slot plays it again.

## Stage C: the settings and the Termina item locks

The Back to back page lists the choices (`gCombo.BackToBack.*`, the table `kOptions` in
`combo/rando/ComboBackToBackSettings.h`). They are read once when a file is created (`SOH_GetNewSaveBackToBackOptions`,
called from `Combo_InitBackToBackSlot`), stored as one int in the file's `combo.mode` (`"options"`; a slot without it takes
the defaults) and pushed to both DLLs with the cross gate (`SOH_SetComboBackToBackOptions`,
`MM_SetComboBackToBackOptions`; 0 for every other slot).

| Option | Default | What it does |
|---|---|---|
| Hearts | off | Termina starts with OoT's max hearts and heart pieces (MM's start is three hearts) |
| Magic meter | on | Double magic carries once MM's Great Fairy has given magic (tick in `ComboBackToBack.cpp`) |
| Bottles | on | OoT's bottles are usable in Termina once MM's first bottle is found; off: they stay in Hyrule |
| Wallet and rupees | on | the wallet tier (capped at MM's 500) and the rupees (capped to the wallet) go in as the opening starts |
| Songs and masks | off | the masks MM has too are unlocked from the start; the songs (Time, Epona's, Storms) are learned once Link has MM's Ocarina |
| Din's, Farore's, Nayru's, Boomerang, Slingshot, Hammer, Boots, Tunics | off | one switch each: off, the item stays in Hyrule |

The locks (`combo/rando/ComboBackToBackLocks.h`, pure rules with `combo/tests/ComboBackToBackTest.cpp`):

| OoT item | Locked in Termina until |
|---|---|
| Sticks, nuts, bombs, bow, the three arrows, ocarinas, bombchus, lens, beans, Hookshot and Longshot | MM holds its own copy (the Hookshot and Longshot: MM's Hookshot) |
| Bottles and what is in them | the Bottles setting is on and MM holds any bottle |
| Keaton, Bunny, Goron, Zora, Truth masks | MM holds its own, or Songs and masks is on |
| Din's, Farore's, Nayru's, Boomerang, Slingshot, Hammer | its switch is on |
| Tunics and boots | the Tunics / Boots switch is on |
| Everything else OoT alone has (trade items, Ruto's letter, Skull / Spooky / Gerudo masks, Roc's Feather) | both games are beaten |

Where they apply: `Snapshot()` (`mm/2s2h/Rando/MiscBehavior/ComboPause.cpp`), which feeds the Hyrule face of MM's pause
menu, the buttons' foreign items, `Combo_MmPauseOotItemAt` and with it every use of an OoT item in Termina; `EquipSnapshot()`
for the tunic and boots rows of the Hyrule equipment face; and `SOH_ComboTermina_EquipValue` / `SetEquipValue` /
`ToggleEquipValue` (`ComboTerminaItems.cpp`), which make Termina's boots and tunic physics read the Kokiri pieces while
locked. They are in force only while `LocksActive(mask)`: a back-to-back slot that has not beaten both games. Every other
mode has `kAllOpen`.

## Stage D: capacities carry over once MM has the item

`ComboBackToBack.cpp` holds a tick (`TickCarryOnceFound`, in a back-to-back slot while the locks are in force) that raises
MM's quiver, bomb bag, Deku stick and Deku nut levels to OoT's (`SOH_ComboCarry_Get`,
`soh/soh/Enhancements/combo/ComboBackToBackCarry.cpp`) once MM's `INV_CONTENT` holds the bow, the bombs, the sticks or the
nuts. MM's give code reads a nonzero level as "already owned" (`mm/src/code/z_parameter.c`, `Item_Give`: the first bow adds
an arrow only when the quiver level is not 0), so a level is never written earlier. MM's own upgrade items set a level
outright, so a lower one is raised again on the next frame.

## Stage E: after both games are beaten

Majora's defeat with Ganon's already recorded sets `kToTermina` in the cross gate, which ends the locks, the gear lock and
the carry tick at once and at every later load: everything OoT has is usable in Termina through the Hyrule face, and
everything MM has in Hyrule through OoT's Termina faces, as in a randomizer file. No new mechanism: `LocksActive` is the one
gate. Both ways are open for either age, as in stage A.

## Not done yet

- Adult Link in Termina with OoT's adult gear (the adult path stays closed until both games are beaten).
- Co-op: the mode is vanilla, so the dormant co-op gate stays closed.
- Shared health (the opt-in `gCombo.Shared.HealthMagicAmmo`) is not aware of the mode: with it on, it can overwrite the
  hearts the page chose.
