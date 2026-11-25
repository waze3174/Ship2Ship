# Back-to-back mode (ComboShip Ultimate)

A back-to-back slot plays **vanilla Ocarina of Time, then vanilla Majora's Mask**, in one save slot. It has no seed and no
randomizer. The file select shows it as the Ship2Ship logo alone (the randomizer's choice has a "Randomizer" subtitle)
and labels the slot `S2S`. Ganon's defeat ends OoT with its full ending and opens the Lost Woods path into Termina for
child Link; Majora's defeat opens the way back. Save compatibility: no `COMBO_RELEASE_VERSION` change and no change to
an existing field; the only new key is `sequential` inside `combo.mode`. An older build reads such a slot as a combo
randomizer slot with no seed, so downgrading is not covered.

This page records stage A: the plumbing and the story gates, and the first setting of the mode's own page (Master Quest
for Ocarina of Time's half). Not done yet: MM's real opening on first entry, item locks in Termina, carrying capacities
over, the rest of the settings page, adult Link in Termina and "everything usable" after both games (see "Not done
yet").

## The mode

`combo.mode` for a back-to-back slot (`combo/rando/ComboMode.h`):

```json
"mode": { "game": "ootmm", "kind": "vanilla", "quest": "normal", "sequential": true }
```

- `ComboMode::BackToBack(quest)`, `IsBackToBack()`. `sequential` is written only for this mode. `FromJson` reads a vanilla
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
created, and is fixed for that file from then on; later stages add the rest of the mode's settings to the same page.
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

## What happens now on the first crossing into Termina

Read from the code, not run. Stage B replaces this.

- The MM save was made at slot creation by MM's new-file routine (`Sram_InitNewSave`): an empty inventory (no Ocarina, no
  Deku Mask), three hearts, the opening not played (`isFirstCycle` false).
- Entering MM, `Setup_InitImpl` loads that save, then `Combo_StartVanillaOpening` (a vanilla save whose opening hasn't
  played) selects the opening, which the Lost Woods link's arrival entrance overrides (`gComboTargetEntrance`): Link
  arrives at MM's Lost Woods spawn 0 as a human, with the entrance cutscene skipped
  (`Combo_MM_LostWoodsLinkSkipsEntranceCs`) and the Happy Mask Salesman's flag marked done (`MarkIntroDone`). No Skull
  Kid, no stolen Ocarina, no Deku curse.
- The opening is not marked as played, so a later load of the slot into MM runs `Combo_StartVanillaOpening` again (or, with
  2S2H's Skip Intro Sequence, starts in South Clock Town).
- Age and tunic come from OoT on the crossing (child Link; the age and tunic hand-off is the combo one).

## Not done yet

- MM's real opening on first entry (stage B).
- Item locks in Termina and the pause menu's Hyrule face: while not solo, OoT's items are visible and usable from Termina
  as in a combo slot. Shared health (an opt-in CVar) also applies if the player has it on.
- Carry-over of capacities (hearts, magic, quiver, bomb bag, wallet, bottles), the rest of the settings page, adult Link in Termina
  (the adult path stays closed), and everything usable after both games.
