# Back-to-back mode (ComboShip Ultimate)

A back-to-back slot plays **vanilla Ocarina of Time, then vanilla Majora's Mask**, in one save slot. It has no seed and no
randomizer. The file select shows it as the Ship2Ship logo alone (the randomizer's choice has a "Randomizer" subtitle)
and labels the slot `S2S`. Ganon's defeat ends OoT with its full ending and opens the Lost Woods path into Termina for
child Link; Majora's defeat opens the way back. Save compatibility: no `COMBO_RELEASE_VERSION` change and no change to
an existing field; the only new keys are `sequential` and `options` inside `combo.mode`, and OoT's save section
`comboBackToBack` (`endingReached`; an older build skips the section with a warning). An older build reads such a slot as a combo
randomizer slot with no seed, so downgrading is not covered.

This page records stage A (the plumbing and the story gates, and Master Quest for Ocarina of Time's half) and stages B to E
(MM's real opening on the first crossing, the settings page with the item locks in Termina, carrying capacities over,
and everything usable after both games). Not done: adult Link in Termina (see "Not done yet").

## The mode

`combo.mode` for a back-to-back slot (`combo/rando/ComboMode.h`):

```json
"mode": { "game": "ootmm", "kind": "vanilla", "quest": "normal", "sequential": true, "options": 8172, "optionsLayout": 3 }
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

The carousel offers the mode unless the player hides it: `gCombo.BackToBack.Show` (`kBackToBackShowCVar`,
`combo/rando/ComboBackToBackSettings.h`; unset = `kBackToBackShowDefault`, shown since 0.2.1, when stages B to E landed;
0.2.0 hid it while only stage A existed), a checkbox at the top of Settings > Back to back.
`SohFileSelect_IsQuestHidden` returns hidden for `QUEST_COMBO_BACK2BACK` while it is off, so the carousel's own skipping
(`NextVisibleQuest`) and its fallback for a selection that becomes hidden apply as for a missing Master Quest archive.
Only the new-file choice is hidden: a slot that already is a back-to-back one loads, plays and shows its `S2S` badge
whatever the switch says.

**Co-op is not supported in this mode.** A back-to-back slot is `IsVanilla()`, so the co-op dormant gate stays closed and
the boss release (`Combo_OnFinalBossDefeated` queues it only for a non-vanilla slot) refuses it.

## The settings page and Master Quest

Settings > Back to back > New files (`combo/gui/ComboBackToBackPanel.cpp`) is the mode's own page. Its header says that
the choices apply to new back-to-back files when they're created. Every setting on it is read once, when a file is
created and baked into it; the rest of the mode's settings are on the same page (stage C). The page has two views, "New
files" and "This file". "New files" is the CVars above, with three quick-set buttons: Purist (every choice off), Default
(`defaultOn`) and Everything (every choice on; `QuickSetOptions`). "This file" shows the loaded back-to-back file's
choices (the launcher answers `Combo_GetLoadedBackToBackOptions`; with no such file loaded it says so). The item
switches (the OoT-only items and the early-use ones) can be changed there while the file is loaded: the setter
(`Combo_EditLoadedBackToBackOptions`, handed to comboui with `ComboUI_SetBackToBackStore`) applies
`BackToBack::ApplyEditedOptions`, which changes only `EditableOptionsMask()` bits, writes the slot's `combo.mode`
(`Combo_SetSlotMode`, the container's normal flush) and pushes the new value to both DLLs at once, so the locks follow
it on the next frame. The carries (hearts, double magic, bottles, wallet, songs) and the Magic early switch were applied
when Termina was first entered and stay greyed. The CVars live under `gCombo.BackToBack.` (`combo/rando/ComboBackToBackSettings.h`); each is a leaf, and no
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

OoT (`soh/soh/Enhancements/combo/ComboBackToBackEnding.cpp`, with seams in `soh/soh/SaveManager.cpp` and
`soh/src/code/z_sram.c`):

1. **The final blow saves the slot.** The Master Sword strike on Ganon (`z_boss_ganon2.c`, `csState 6`) fires
   `OnBossDefeat`. At the end of that frame, once every hook on it has run (the Ganon timestamp is one), the slot calls
   `gComboFinalBossDefeated(0, fileNum)`: the launcher records Ganon as beaten, flushes the completion record and pushes
   the gate, which opens the Lost Woods path for child Link. With Ganon beaten and Majora not, the slot is then saved as it
   will be once the ending is over: child Link (his worn gear swapped for the child's, as the pedestal does), in the
   Temple of Time (`savedSceneNum`), arriving at `ENTR_TEMPLE_OF_TIME_2` (the pedestal arrival a child Link gets when he
   returns through the Master Sword), with no cutscene queued. Only the saved copy is changed: `SaveManager::SaveSection`
   copies `gSaveContext` before it returns, and the live state is put back at once, so the ending plays on as adult Link.
2. **The ending plays as vanilla.** `csState 20` asks the same callback again; the repeat writes nothing and answers 1 for
   a back-to-back slot, so the combo-seed skip of the credits is not taken: the Chamber of the Sages, Zelda's farewell
   and the credits play. Until the ending is over, every other base save of the slot is held (`SaveSection` returns
   early, with a `[ComboBackToBack] save ... held` log line): the 3-minute autosave and the soft-reset autosave
   included. A section-only save (the tracker's data) still writes its section and keeps that base. Every scene of the
   ending plays one of its own cutscenes (`cutsceneIndex` 0xFFF0 or above) or the credits; a scene in normal play means the
   ending was left some other way, and the hold ends there.
3. **"The End" moves on.** The ending stops in the castle courtyard (`ENTR_CASTLE_COURTYARD_ZELDA_0`, cutscene 0xFFF0)
   with "The End" (`End_Title`); no cutscene exit follows, so vanilla stays there until the console is reset. Once "The
   End" has fully faded in (its alpha has reached 200) and stayed up for 5 seconds, or at once on A, B or Start, the
   courtyard fades white to `ENTR_TEMPLE_OF_TIME_2` with child Link, as the pedestal does (`z_demo.c`, terminator 8). When
   the courtyard is torn down (`OnPlayDestroy`), the credits' muted sound banks are unmuted, the game mode returns to
   normal, no cutscene is queued and the hold ends.
4. **The arrival is saved once,** when the Temple of Time has settled (no transition, no cutscene): `Play_PerformSave`.

**Loading.** `Sram_OpenSave` sends a child save made outside a dungeon to Link's house. Once OoT's ending has been
reached in the slot (the save section `comboBackToBack`, key `endingReached`, set at the final blow), a save of child Link
in the Temple of Time at `ENTR_TEMPLE_OF_TIME_2` loads there instead (`Combo_BackToBack_KeepsSavedArrival`). So a reset or
quit at any point after the final blow loads that arrival, with Ganon recorded: every OoT load reads the completion record
(`OnLoadGame` → `Combo_OnOOTSaveLoad` → `LoadComboCompletion` → the gate), so the Lost Woods path is open. The key stays
set for the slot: a later child return through the pedestal, saved before leaving the temple, also loads there.

The player then walks to the Lost Woods and takes the path.

MM (`mm/2s2h/Rando/MiscBehavior/ComboBackToBackEnding.cpp`, with a seam in `mm/src/code/z_demo.c`; read from the code,
not run):

1. **Majora's death records him.** Majora's last cutscene (`z_boss_07.c`) asks the same callback, which saves Majora as
   beaten in the completion record (opening the way back, and both ways once Ganon is beaten too) and answers 1:
   Majora's real ending and credits play. The same cutscene fires `OnGameCompletion`, which marks MM's ending as playing
   in a back-to-back slot. Each scene of the ending is logged (`[ComboBackToBack] MM's ending: scene ...`).
2. **MM's ending never ends by itself.** The credits go from scene to scene (`CutsceneCmd_ChooseCreditsScenes`, on the
   credits music's cues) and never reach the title: their last shot holds its cutscene on one frame for good. MM has two
   commands that hold a cutscene that way, `CS_MISC_FINALE` and `CS_MISC_LONG_SCARECROW_SONG` (`z_demo.c`), and none of
   MM's code leaves either. No code in MM makes the invalid entrance (-1) on which `Play_Init` would load MM's own title,
   so the `Play_Init` seam (`z_play.c`) is never reached; it stays as a harmless fallback. Which of the two commands the
   last shot uses is in the scene data, not the code: the log line at the hold names it, with the scene, layer, script,
   frame, whether the credits' stump (`Obj_Ending`) is in the scene, and the music playing.
3. **The last shot moves on.** While MM's ending plays, once a hold has lasted 5 seconds, or at once on A, B or Start, and
   no transition is running, `Combo_MM_CreditsEnded` asks for the owl-save quit: the slot returns to OoT's title (MM's
   own title and file select are unreachable in the launcher: MM's boot would wipe the save through MM's file select).
   The slot is not saved again, so MM resumes from its last save, with the gate open from the completion record. Every
   other slot keeps MM's vanilla hold.

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

The options are one 64-bit value (`BackToBack::OptionBits`, a `uint64_t`; nlohmann stores it as an unsigned number, and
the launcher and both DLLs pass it as 64 bits): bits 0-12 the carry and OoT-only switches, 13-31 the early-use switches
(Goron Mask is 29, Zora Mask 30, Magic 31). `ComboMode::ToJson` writes `"optionsLayout": 3` (`kOptionsLayout`) beside
`"options"`. Layout 2 (0.2.2) is the same bits in a 31-bit int without Magic; `MigrateStoredOptions` returns a layout-2
or layout-3 value untouched, so a layout-2 file has Magic off. A mode without the key holds the first layout (bits 0-12 only); `MigrateStoredOptions` (called by `ComboMode::FromJson`) leaves
every old choice as it was and, when bit 4 was on (it used to also mean "the masks both games have are usable at once,
and the Goron and Zora masks transform at once"), sets all five mask early-use bits (Keaton, Bunny, Truth, Goron, Zora).
A mode without the key but with bit 30 set was made by 0.2.2, which marked its layout with that bit
(`kMarkedLayoutBit`): bits 0-28 mean what they mean now, bit 4 only the songs, and it had no Goron or Zora switch (both
were worn plain until MM's own), so `MigrateStoredOptions` only clears bit 30. Before 2026-11-25 such a file read bit
30 as the Zora Mask's switch, and bit 4 as all five mask switches. So an older file's locks and masks behave as they
did. A slot with no `options` key at all takes `DefaultOptions()`.

The carries (default in brackets):

| Option | What it does |
|---|---|
| Hearts (off) | Termina starts with OoT's max hearts and heart pieces (MM's start is three hearts) |
| Double magic meter (off) | OoT's double magic meter carries once MM's Great Fairy has given magic (tick in `ComboBackToBack.cpp`) |
| Bottles (on) | OoT's bottles are usable in Termina; off: they stay in Hyrule |
| Wallet and rupees (on) | the wallet tier (capped at MM's 500) and the rupees (capped to the wallet) go in as the opening starts |
| Songs (off) | the songs (Time, Epona's, Storms) are learned once Link has an ocarina he can play in Termina (MM's, or OoT's with its early switch on) |

"Ocarina of Time items usable in Termina": one switch each for Din's Fire, Farore's Wind, Nayru's Love, Boomerang,
Slingshot, Megaton Hammer, Iron and Hover Boots, and the Goron and Zora tunics. All on by default; off, the item stays in
Hyrule.

"Usable in Termina before Majora's Mask's own is found" (`CVars gCombo.BackToBack.Early.*`): one switch per item MM has
too, all off by default (the item waits). Deku Sticks, Deku Nuts, Bombs, Bow, Fire, Ice and Light Arrows, Ocarina (both
of OoT's), Bombchus, Hookshot (with the Longshot), Lens of Truth, Magic Beans, Bottles (with the Bottles carry on: before
MM's first bottle), Keaton Mask, Bunny Hood, Mask of Truth, Goron Mask and Zora Mask (those two mean something else, see
below) and Magic (see below).

**Song of Time and Ocarina of Time's consumables** (`gCombo.BackToBack.ResetOotConsumables`, bit 32, off by default, the
last section of the page; editable on a loaded file). MM's cycle reset (`Sram_SaveEndOfCycle`) zeroes every ammo count
and empties every filled bottle, but only in MM's save, so Ocarina of Time's carried ammo and bottles survive it. With
the switch on, `ResetOotConsumablesAtEndOfCycle` (`ComboBackToBack.cpp`, on the `BeforeEndOfCycleSave` hook, back-to-back
slots only) calls `SOH_ComboTermina_ResetConsumables` (`ComboTerminaItems.cpp`): OoT's sticks, nuts, bombs, bow, slingshot,
bombchus and beans go to 0 and every filled bottle becomes an empty one (Ruto's Letter stays), and OoT's dormant save is
marked changed so the write at the return to Hyrule keeps it. Rupees, hearts and the wallet are not consumables of this
rule: MM resets its own rupees and the carried wallet is only a start value.

**Magic in Termina.** With the Magic switch on, `CarryEarlyMagic` (`ComboBackToBack.cpp`, run as the opening starts with
the hearts and the wallet) gives Link Ocarina of Time's meter at once: `isMagicAcquired` set, `magic` full, `magicLevel`
0, which `Interface_Update` grows and fills (`z_parameter.c`, the same path MM's own gift uses). It is double when the
Double magic meter carry is on and OoT's meter was double, otherwise single, and none if OoT had none. Off: no meter until
MM's Great Fairy, then the carry decides single or double. MM's Great Fairy flow tolerates the meter being there already:
her gift (`z_bg_dy_yoseizo.c`, `GREAT_FAIRY_TYPE_MAGIC` and `_WISDOM`) only writes `isMagicAcquired` and the fill target
when the flag is unset. The Clock Town fairy's Deku Mask and Great Fairy's Mask branches (`z_en_elfgrp.c`) do not read
the magic flag; with it set she plays her heal cutscene in the one branch that tests it. A double meter from the start
also makes the Snowhead (Wisdom) fairy heal instead of giving double magic (she tests `isDoubleMagicAcquired`), which no
progression needs. Spells and magic arrows go through
`Magic_Consume`, which only needs `isMagicAcquired`. Read from the code, not played.

The locks (`combo/rando/ComboBackToBackLocks.h`, pure rules with `combo/tests/ComboBackToBackTest.cpp`):

| OoT item | Locked in Termina until |
|---|---|
| Sticks, nuts, bombs, bow, the three arrows, ocarinas, bombchus, lens, beans, Hookshot and Longshot, Keaton, Bunny and Truth masks | its early-use switch is on, or MM holds its own copy (the Hookshot and Longshot: MM's Hookshot) |
| Bottles and what is in them | the Bottles carry is on, and the early-use switch is on or MM holds any bottle |
| Goron and Zora masks | never locked (see below) |
| Din's, Farore's, Nayru's, Boomerang, Slingshot, Hammer | its switch is on |
| Tunics and boots | the Tunics / Boots switch is on |
| Everything else OoT alone has (trade items, Ruto's letter, Skull / Spooky / Gerudo masks, Roc's Feather) | never locked: they are in Termina from the start. The three masks are worn as plain masks (`Combo_MmOotMaskIsPlain`); the other items are there to carry and trade, and "can't use" on a button |

**Waits that are not settings.** Each has a technical reason.
- The quiver, bomb bag, Deku stick and Deku nut levels wait for MM's bow, bombs, sticks and nuts (stage D).
- Without the Magic switch, the magic meter waits for MM's Great Fairy: MM reads `isMagicAcquired` as "magic not given
  yet" (the Great Fairy gift in `SkipGreatFairyCutscene.cpp`, Tatl's hint in `z_elf_message.c`). The Magic switch above
  sets it from the start.
- The songs wait for an ocarina Link can play in Termina (`Combo_MM_LinkHasUsableOcarina`, `ComboBackToBack.cpp`):
  MM's, or OoT's once the slot lets it in (the ocarina's early-use switch). MM's own lessons (Song of Time, Epona's
  Song, Song of Storms) are not skipped by an early flag, and no MM code branches on the Song of Time bit but the
  ocarina's song list (`z_message.c`), so MM's flashback grant just sets the bit again. Before 2026-11-25 (bug #32)
  they waited for MM's own ocarina only, so with OoT's ocarina in early they never carried.

**The Goron and Zora masks in Termina.** In a slot with the locks in force, OoT's Goron and Zora masks are worn as plain
masks (OoT's model, no transformation) until Link holds MM's own Goron Mask (Darmani) or Zora Mask (Mikau), unless the
slot's Goron Mask or Zora Mask early-use switch is on, which lets that mask transform Link from the start. Once Link
holds MM's own, they transform him as MM's masks (`Combo_MmOotMaskIsPlain`, `ComboOotItems.cpp`; they take the same path as
the Skull, Spooky and Gerudo masks, `PLAYER_IA_COMBO_OOT_MASK`). In every other slot they transform at once, as before
(a randomizer slot's Goron Mask is MM's). In Hyrule OoT's own Goron and Zora masks are plain masks unless the form
setting is on (`Combo_FormsAllowed`: randomizer slots, or "Transformations in vanilla OoT" for a vanilla one), which this
mode does not change.

Where they apply: `Snapshot()` (`mm/2s2h/Rando/MiscBehavior/ComboPause.cpp`), which feeds the Hyrule face of MM's pause
menu, the buttons' foreign items, `Combo_MmPauseOotItemAt` and with it every use of an OoT item in Termina; `EquipSnapshot()`
for the tunic and boots rows of the Hyrule equipment face; and `SOH_ComboTermina_EquipValue` / `SetEquipValue` /
`ToggleEquipValue` (`ComboTerminaItems.cpp`), which make Termina's boots and tunic physics read the Kokiri pieces while
locked. They are in force only while `LocksActive(mask)`: a back-to-back slot that has not beaten both games. Every other
mode has `kAllOpen`.

### OoT's items in MM's story and pages (bugs #31 to #37, 2026-11-25)

**OoT's age rule does not apply in Termina** (`TerminaAgeRuleApplies` in `ComboBackToBackLocks.h`, read by
`SOH_ComboTermina_ItemAgeOk`, soh `ComboTerminaItems.cpp`). Only child Link goes to Termina until both games are beaten,
and OoT's rule would have greyed every adult item the page lets in: the Megaton Hammer, the Hookshot and Longshot, the
Goron and Zora tunics and the Iron and Hover Boots (bugs #36 and #37: greyed on the buttons, refused on the equipment
face). The rule is lifted for the whole slot, so nothing goes grey again once both games are beaten. Hyrule keeps
OoT's own rule. Every other slot keeps it in Termina too (randomizer: Unified Inventory and the other lifts).

**OoT's ocarina in MM's story** (bug #31). `Combo_MM_LinkHasUsableOcarina` (`ComboBackToBack.cpp`): MM's own
ocarina, or in a back-to-back slot OoT's while Termina may use it (`Combo_MmPauseOotHolds`, which applies the locks).
Changed to read it:
- the Happy Mask Salesman (`ovl_En_Osn`, five checks behind `OSN_LINK_HAS_OCARINA()`): his talk offer, his
  "you have the ocarina" lines and the cutscene picked at the Song of Time's first return, so with OoT's ocarina he
  teaches the Song of Healing at once (the lesson's cutscene plays the ocarina without an item check). The
  randomizer's own override (`EnOsn.cpp`, `VB_OSN_CONSIDER_ELIGIBLE_FOR_SONG_OF_HEALING`) still runs after;
- the songs carried over (above).

Left reading MM's ocarina slot, because they mean "the Skull Kid still has MM's ocarina" (the first cycle's story),
not "Link can play": the Clock Tower opening's first-cycle rule (`z_en_test4.c`), the faster first cycle (`z_scene.c`,
`z_scene_2SH.cpp`), the clock tower (`z_obj_tokeidai.c`), the Skull Kid's ocarina and hands (`z_dm_stk.c`), the
falling ocarina (`z_dm_char02.c`), the empty Termina Field (`z_play.c`), Tatl's first-cycle hints (`z_elf_message.c`,
`z_elf_msg6.c`); also 2S2H's song items (`SongItems.cpp`), the pause owl warp (Soaring is not carried), the Curiosity
Shop refills and the randomizer's own checks (`BenPort.cpp`). So the Skull Kid still has MM's ocarina, the retrieval
and the Song of Time flashback play as in MM, and MM's own ocarina is never given early. With the Songs switch on as
well, a carried Song of Time can end the first cycle early: the first return then has the Mask Salesman teach the Song
of Healing, and the next cycle still reads as the first until Link takes MM's ocarina back at the top of the Clock
Tower (Termina Field empty, the faster clock). Not tested in game.

**MM's own slots show OoT's same item** (bug #33). On the Termina item and mask pages, MM's empty ocarina slot and its
empty Keaton Mask, Bunny Hood, Mask of Truth, Goron Mask and Zora Mask slots show OoT's same item while Termina may use
it (the Goron and Zora masks only while they transform, not while worn plain), drawn as MM's item with MM's name and
description (`Combo_MmPauseSlotShownItem`, through `ITEM_SLOT_ITEM` / `MASK_SLOT_ITEM` in `z_kaleido_item.c` /
`z_kaleido_mask.c`). Equipping it there puts OoT's item on the button as its foreign id, as the Hyrule face does
(`Combo_MmPauseStandInEquip`). MM's item is never given and no get-item flag is set, so Grog, the Curiosity Shop, Kafei
and the Swamp Spider House still give MM's own, and once found MM's shows in its slot. Back-to-back slots only.

**OoT's magic arrows** (bug #34). OoT's Fire, Ice and Light Arrows run in Termina as MM's bow with that arrow
(`Combo_MmPauseForeignHostItem`: `ITEM_BOW_FIRE` / `_ICE` / `_LIGHT`) when Link has a bow Termina may use (MM's own, or
OoT's), so MM's own code shoots them and spends MM's magic, or shoots a plain arrow without enough, as for MM's own
magic arrows. The arrows come from OoT's quiver while OoT has a bow Termina may use, else MM's
(`Combo_MmPauseForeignAmmoItem`, `Combo_ChooseAmmoBag`), and the button shows OoT's bow-with-arrow icon and that
quiver's count. Without a usable bow the Hyrule face refuses to put them on a button. Their early-use switches still
gate them (the locks), and the same rule holds in every slot (a randomizer seed's OoT arrows in Termina too). Before
2026-11-25 they ran as nothing (the error sound) and OoT's age rule greyed them.

**The Hyrule equipment face greys what Termina doesn't use** (bug #37): OoT's swords and shields (MM's own are worn
in Termina, by design in every mode) and a tunic or boots Link's age may not wear (`Combo_MmPauseEquipUsable`). A
locked item is not greyed: it isn't shown at all (the locks above).

**The slot's options in the log.** At every load of a back-to-back slot, MM logs the options the file was made with:
`[ComboBackToBack] slot options 0x... (cross gate 0x...), on: ...` (`MM_SetComboBackToBackOptions`, `BenPort.cpp`).
The page itself shows what the next new file would get.

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

## Shared health is off in this mode

"Shared health, magic and ammo" (`gCombo.Shared.HealthMagicAmmo`) does nothing in a back-to-back slot, whatever the menu
says, before or after both games are beaten: `ComboHealth::Enabled` reads `Combo_BackToBackSlot()` (each DLL, from the
cross gate's `kBackToBack` bit). Each game keeps its own hearts, magic meter and ammo, and the page's carry settings
decide what Termina takes from Hyrule, once. Before this, with the switch on, the shared bar overwrote the hearts the
page chose, and a new file could take over the shared hearts of the file erased before it (#30, 0.2.2: 20 heart
containers, 3 filled, on a new file).

## Not done yet

- Adult Link in Termina with OoT's adult gear (the adult path stays closed until both games are beaten).
- Co-op: the mode is vanilla, so the dormant co-op gate stays closed.
