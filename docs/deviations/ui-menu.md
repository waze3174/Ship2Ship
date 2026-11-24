# ComboShip deviations — Menu & UI

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

## UIWidgets empty-combobox UB + combo-rendered MM rando menu (2026-06-09)

**soh side RETIRED 2026-09-28:** upstream now initializes `longest` in every soh overload; the MM
fix below stands.

**`mm/2s2h/BenGui/UIWidgets.hpp` (and `soh/soh/SohGui/UIWidgets.hpp`) — fix to a vendored upstream
bug:** every `UIWidgets::Combobox` template overload declared `const char* longest;` **uninitialized**,
then assigned it only inside the loop that scans the options for the widest entry. If the options
container is empty, `longest` stays garbage and the immediately-following
`CalcComboWidth(longest, ...)` → `ImGui::CalcTextSize` dereferences it → crash. Changed to
`const char* longest = "";` in all overloads, with a `// ComboShip:` comment explaining why. This is a
genuine upstream bug; OOT's `std::vector<std::string>` overload happened to already have the
initializer, but its three other overloads (map / `vector<const char*>` / fixed array) did not — those
were fixed too. WHY it surfaced: MM's rando "Seed" combobox reads `Rando::Spoiler::spoilerOptions`,
which is empty when the combo layer renders MM's always-available rando menu while MM is backgrounded.

**`mm/2s2h/BenPort.cpp` (`Combo_EnsureBenMenu()`):** belt-and-suspenders for the same symptom —
`spoilerOptions` is populated by `Rando::Init()` → `RefreshOptions()` at boot, but in the
combo/backgrounded render path that vector can be empty. `Combo_EnsureBenMenu()` (called by every MM
menu export — ExportMenu / InvokeCallback / EvalDisabled / DrawCustom) now calls
`Rando::Spoiler::RefreshOptions()` when `spoilerOptions.empty()`, so the menu has real options to draw.
`RefreshOptions` is idempotent (clears + repopulates) and only runs when empty. No new include needed
(`2s2h/Rando/Spoiler/Spoiler.h` was already included).

**Note:** other MM rando tabs (Logic / Items / etc.) may have their own backgrounded-live-state crashes;
out of scope here, handled as they surface.

## Menu code extraction to combo-owned headers (2026-06-10)

The ComboShip-written menu glue that lived inside the vendored trees was consolidated into
combo-owned, header-only units under `combo/menu/` (already on both games' include paths, so no
game CMake changes). The headers compile INTO each game DLL — only the SOURCE ownership moved —
so future upstream pulls conflict at most on small per-game glue blocks, never on the algorithms:

- **`combo/menu/ComboMenuExport.h`** — the two-pass count/reserve/fill CwMenu serializer that was
  copy-pasted in `soh/soh/SohGui/SohMenu.cpp` and `mm/2s2h/BenGui/BenMenu.cpp` (~260 lines each).
  Each game now keeps only its `WidgetTypeToCwKind` mapping + a Policy struct
  (`SohExportPolicy` / `BenExportPolicy`) + a one-line delegate. Pointer-stability invariant
  (reserve == fill) is now assert-checked and unit-tested
  (`libultraship/tests/combo_menu_export_tests.cpp`, mock-based, runs in `lus_tests`).
- **`combo/menu/ComboMenuDrawContent.h`** — the shared `Menu::DrawContent` ImGui body (~230 lines
  each) that mirrored the upstream `DrawElement` layout inside comboui's window. Each game keeps a
  Hooks shim (`SohDrawHooks` / `BenDrawHooks`) + a ~30-line wrapper. Upstream `DrawElement` is
  untouched. WHY a TU-glue header (`#error` guard, no own ImGui include): it must resolve each
  game's OWN UIWidgets/draw functions and per-module compile context.
- **`combo/menu/ComboMenuSharedContext.h`** — `ComboMenuContext::UseSharedImGuiContext()`, replacing
  2 static + 3 inline duplicated copies in `soh/soh/OTRGlobals.cpp` / `mm/2s2h/BenPort.cpp`.

Intentional micro-deviations from the pre-extraction bodies (do not "fix" back): empty tooltips are
no longer copied into owned-string storage (output still `""`); MM's redundant
`GetVectorIndexOf(sidebarOrder, …)` fallback clause was dropped (subsumed by the `std::find` over
`visibleSidebars`); OOT's widget label width now uses the clamped column count like MM (visible
only below 800px window width).

Net effect: vendored menu diff vs upstream shrank from ~1,630 to ~907 added lines across soh/ + mm/.

## hook_handlers.h re-added (combo-owned) (2026-06-11)

**`soh/soh/Enhancements/randomizer/hook_handlers.h`** — upstream has NO such header at our vendor
tip (functions in hook_handlers.cpp are static/self-registered). ComboShip re-added it to expose
`OOT_LookupForeign` (the per-slot foreign-item map lookup) to `Messages/MerchantMessages.cpp` and
the check tracker for cross-game item display names. The file contains only `#ifdef COMBO_BUILD`
declarations and MUST be preserved on future upstream merges — an upstream pull that "removes the
deleted file" silently breaks the foreign shop/tracker name builds.

## Dual-game title screen logos (2026-06-12)

**`soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c`:** the OOT title screen now shows BOTH games'
logos — OOT's shrunk and shifted left, MM's title logo (mask + Zelda logo + subtitle, ROM-extracted
textures resolved against MM's ResourceManager via the G_COMBO_RM_PUSH/POP interpreter bracket) on
the right. Both games' flame-effect mask grids (OOT 3x3 `gTitleEffectMask*`, MM 2x3
`gTitleScreenDisplayEffectMask*` + per-cell `gTitleScreenFlame0-3`) draw behind their logos, scaled
and repositioned with them; MM's oscillating effect colors are replicated in the combo header
(OOT's EnMag doesn't carry MM state). The early-out therefore sits BEFORE the original effect-grid
combiner setup, so the skipped vendored region spans from `gDPSetCycleType` through the MQ
subtitle. All draw code
is combo-owned (`combo/title/ComboTitleLogos.h`, included after the LOGO_* macros so it can reuse
them and the EnMag_Draw* helpers). The vendored logo block in `EnMag_DrawInner` is left BYTE-INTACT
for upstream merges; a `#ifdef COMBO_BUILD`-guarded early-out (`if (ComboTitle_DrawLogos(...))
goto ...;` + a guarded label after the block) skips it at runtime, so non-combo builds compile the
file unchanged. On future merges, keep the include + the goto wrapper and re-check
that the duplicated OOT-logo draw sequence in ComboTitleLogos.h still matches upstream's (combiner
setup, texture names, MQ subtitle branch).

**`soh/CMakeLists.txt`:** added `${CMAKE_SOURCE_DIR}/combo/title` to the include dirs (next to the
existing `combo/menu` entry).

## File-select seed-hash icons for combo seeds (issue #32, 2026-06-23)

**Why:** stock SoH draws five item icons on the file-select as a visual seed hash so players can
match seeds/spoiler logs. The combo build showed five Deku Nuts (or an identical set every seed):
the combo generator owns generation and never runs OOT's `Playthrough_Init`, which is where vanilla
calls `GenerateHash()` to fill `Rando::Context::hashIconIndexes` — so the array stayed all-zero.
Scope is OOT only: combo boots into OOT's file-select and enters MM via transition, so MM's
file-select is never shown (MM's `finalSeed = 0` gap in `MM_InitRandoSaveFile` is a known follow-up).

**Change (vendored `soh`, minimal):** new export `SOH_SetComboSeedHash(uint32_t)` in
`OTRGlobals.cpp` (just after `SOH_ApplyRandoPlacements`) calls `ctx->SetHash(...)` + `GenerateHash()`
— mirrors stock `playthrough.cpp:64-73`. Added `#include ".../3drando/spoiler_log.hpp"` for the
declaration. Persistence/render are stock-wired (`SaveManager` copies into `fileMetaInfo.seedHash`;
`z_file_choose.c` renders it).

**Change (combo-owned, `ComboShip.cpp`):** `RunComboFill` computes a settings-aware display hash
`ComboHash(inputSeed + sohDump + mmDump)` and passes it to the new export *after*
`SOH_ApplyRandoPlacements` (which `ItemReset`s). Folding both static dumps in makes the icons
identify seed **and** settings: each dump is built from its game's live CVars and carries only the
settings-scoped pool, so OOT *and* MM shuffle/starting-item settings both vary the icons. Accepted
limitation (symmetric): a logic-only setting that doesn't change the shuffled pool (e.g. tricks)
won't change the dump, so won't change the icons.

**On future merges:** if upstream restructures `GenerateHash`/`SetHash` or the file-select hash
render, re-point the new export; the combo-side hash derivation is independent.

## File-select quest menu locked to Randomizer (2026-06-27)

**Why:** the combo build drives a randomizer through OOT's normal save flow; showing the Normal /
Master Quest / Boss Rush quest options on file-select is confusing since they're never the intent.

**Change (vendored `soh`, one COMBO_BUILD-guarded block):** in
`soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c`, the `MIN_QUEST`/`MAX_QUEST` macros
(which seed `questType` at init and bound the L/R carousel wrap) are redefined to `QUEST_RANDOMIZER`
under `COMBO_BUILD`. The carousel therefore starts on Randomizer and every L/R wrap lands back on it,
so the other quests are unreachable — no draw/branch code was deleted (the non-combo build is
byte-intact via the `#else`).

**On future merges:** if upstream changes the quest enum or the carousel wrap logic, re-check that
the locked `MIN_QUEST == MAX_QUEST == QUEST_RANDOMIZER` still resolves to Randomizer on every L/R
path (incl. the Master-Quest-absent skip loop).

**Hide-quest options (2026-10-09):** soh's six File Select "Hide ..." checkboxes and their
`HideQuestPreFunc` are compiled out under `COMBO_BUILD` (`SohMenuEnhancements.cpp`); they could not
change anything with the lock above. `SohFileSelect_IsQuestHidden` (`FileSelectEnhancements.cpp`) hides
every quest but Randomizer, so `CountVisibleQuests() == 1`: the one-entry carousel is skipped (no dead
L/R arrows) and stale Hide CVars in an old config can no longer change the flow. B from name entry now
returns to the main menu, as single-quest soh does.

## Live-apply settings changed from the combo menu (2026-06-28)

**Why:** the games' native UIWidgets call `ShipInit::Init(cvar)` after a widget change to re-run the
enhancement registered for that CVar; comboui only set the CVar, so combo-menu changes didn't apply
until the next `ShipInit::InitAll` (game boot / new save). New exports `SOH_MenuApplyCVarChange` /
`MM_MenuApplyCVarChange` (OTRGlobals.cpp / BenPort.cpp) run `ShipInit::Init(cvar)`; the comboui menu
model + `ComboWidgetRender` call them after each change. Fixes #27. **On future merges:** if the
native UIWidgets stop using `ShipInit::Init`-on-change, revisit.

## Shared-settings consolidation + dev-tool window fixes (issue #22, 2026-06-28)

**Why:** three related issues in the shared menu/window subsystem. (a) #22 — MM ignored the Shared
tab's Graphics/Audio/Controls and the per-game tabs redundantly exposed them. (b) Opening MM's "Save
Editor" showed OOT's: the shared `Gui::AddGuiWindow` keys by display name and rejects duplicates, so
MM's identically-named dev windows were dropped (OOT boots first). (c) Dev-tool windows only offered a
"popout" button, never rendering inline.

**Vendored (additive, `COMBO_BUILD`-guarded):**
- `mm/2s2h/BenGui/BenGui.cpp` — the 9 MM dev/debug windows whose names collide with OOT's (Save Editor,
  Actor/Collision/Message Viewer, Audio Editor, Mod Menu, Hook Debugger, Input Viewer (+Settings)) now
  register with `COMBO_MM_TRACKER_SUFFIX` (`"##MM"`), same mechanism as the trackers — map-key/ID only,
  visible title unchanged, empty in standalone. (Cosmetic Editor / DL Viewer differ from OOT's strings
  already, so they don't collide; Time Splits now does collide, see the Time Splits section.)
- `mm/2s2h/BenGui/BenMenu.cpp` — matching `WindowName(...)` popout refs carry the same suffix
  (`COMBO_MM_WINDOW_SUFFIX`). New `#ifdef COMBO_BUILD` inline `WIDGET_CUSTOM` entries render each dev
  window's `DrawElement()` inline (skipped when popped out, so no double-draw); live-world viewers
  (Actor/Collision/Message/DL/Event Log) gate on `Combo_MmIsForeground()` so they don't read MM's
  dormant/swapped play state when the MM tab is opened while OOT is foreground.
- `soh/soh/SohGui/SohMenuDevTools.cpp` + `OTRGlobals.cpp` — the same inline `WIDGET_CUSTOM` treatment for
  OOT's dev tools, gating live viewers on `Combo_OotIsForeground()`. Both `Combo_*IsForeground` helpers
  resolve comboui's `ComboUI_GetForegroundGame` once.
- `mm/2s2h/BenPort.cpp` — exports `MM_ApplyAudioVolume(seqPlayer, vol)` (→ `AudioSeq_SetPortVolumeScale`,
  the apply path MM uses instead of ShipInit) and `MM_ReloadControls()` (reloads MM's ControlDeck ports
  from the shared `gSettings.Controllers.*` CVars); `Combo_MmIsForeground()` queries comboui's
  foreground game. All inside the existing `COMBO_BUILD` export block.

**Combo-owned:**
- `combo/gui/ComboAudioBridge.{h,cpp}` — one-way mirror of OOT's canonical `gSettings.Volume.*` (int
  0-100) into MM's `gSettings.Audio.*` (float 0-1). `MirrorIfVolumeCVar` fires from the
  `ComboWidgetRender` apply-step; `SyncAllToMM` runs on MM entry. Graphics needs no bridge (one shared
  window) and Controls' data is already shared CVars.
- `combo/gui/ComboForeground.h` + `ComboTrackerVisibility.cpp` — cache the foreground game from the
  existing `ComboUI_OnForegroundGame` callback; expose `ComboUI::GetForegroundGame()` (C++) and
  `ComboUI_GetForegroundGame()` (C ABI, for MM's gating). On MM entry the callback also runs
  `ComboAudio::SyncAllToMM()` + `MM_ReloadControls` so changes made while MM was dormant take effect.
- `combo/gui/ComboMenu.cpp` — the per-game tab sidebar filter now also hides MM's
  Graphics/Audio/Controls/General (they live only in Shared, on shared CVars). **On future merges:** if
  MM's audio CVar names/scale change, update `kMap` in `ComboAudioBridge.cpp` (note: the OOT `Volume.*`
  defaults — Master 40, rest 100 — are duplicated in `kMap`'s `defaultPct` and must match
  SohMenuSettings.cpp, else an untouched slider reads as 0 and silences MM).

## Combo-side Advanced Resolution editor (issue #26 #3, 2026-06-29)

**Why:** SoH's Advanced Resolution editor (`soh/soh/SohGui/ResolutionEditor.cpp`) is built from
machinery the flat C-ABI menu snapshot can't carry — dynamic `WIDGET_TEXT` names set per-frame by a
`PreFunc`, an aspect-ratio combobox bound to a C++ static via `ValuePointer` (no CVar), two
`WIDGET_CUSTOM` draw lambdas, and a per-page `UpdateResolutionVars` MenuUpdate func. In the overlay
those widgets render broken (empty `{} x {}` readouts, an empty-CVar combobox, placeholder customs,
dead aspect/enable controls). User chose a combo-side reimplementation (works regardless of which
game is foreground) over a game-side custom-draw dependency.

**Combo-owned (no vendored edits):**
- `combo/gui/ComboResolutionEditor.{h,cpp}` — `TryRenderResolutionWidget(w)`, called at the top of
  `ComboWidgetRender::RenderWidget`. Intercepts the resolution widgets **by name/cvar** and renders
  combo controls over the effective `gSettings.AdvancedResolution.*` CVars (libultraship's shared
  `Fast3dGui::ApplyResolutionChanges` reads them live each frame). Stateless per-frame: the CVars are
  the source of truth. Live dims read from `Fast::Interpreter` via `Fast3dWindow::GetInterpreterWeak`.
  Owns the Enable checkbox + calls `SaveConsoleVariablesNextFrame` after writes so changes persist.
  Scope = core controls; niche extras (horizontal-res-field alt, NeverExceedBounds/ExceedBoundsBy,
  IgnoreAspectCorrection) intentionally omitted.

**On future merges (coupling — re-verify):** this **shadows specific SoH widget names** —
`"Aspect Ratio"` (empty-cvar combo), `"AspectRatioCustom"`, `"MoreResolutionSettings"`, the
`"Viewport dimensions"`/`"Internal resolution"` readout prefixes, the `"...is overriding these
settings"` / `"Click to disable N64 mode"` advisories — and **transcribes SoH's preset tables**
(aspect labels + X/Y, pixel-count labels + values, clamps). If SoH renames those widgets or changes
the tables, re-sync `ComboResolutionEditor.cpp` (else the widgets silently fall back to the broken
generic render). CVar prefix `gSettings.AdvancedResolution` comes from `CMake/lus-cvars.cmake`.

## Inline controller bindings on Shared → Controls (issue #26 #1, 2026-06-29)

**Why:** SoH's Controls page is popout-only (no inline bindings widget), so the combo overlay showed
just a "Popout Bindings Window" button — the user had to detach a floating window to rebind.

**Vendored (additive, `COMBO_BUILD`-guarded):**
- `soh/soh/SohGui/SohMenuSettings.cpp` — a new "Controller Bindings Inline" `WIDGET_CUSTOM` in the
  Controls section draws the "Configure Controller" (`SohInputEditorWindow`) inline, reusing the
  issue #22 inline-window mechanism (get the registered `GuiWindow`; skip when `IsVisible()`/popped
  out to avoid double-draw; `Update()` then `DrawElement()`). No foreground gate — the input editor
  only touches the shared `ControlDeck`, not OOT play state.

One inline editor (OOT's) covers both games: controls are shared `gSettings.Controllers.*` CVars and
`MM_ReloadControls` reloads MM from them, and MM's Controls sidebar is hidden in the overlay (above),
so no MM-side change is needed. **On future merges:** if SoH renames the "Configure Controller"
window, update the name string here.

## Link's Voice Pitch Multiplier CVar collision (2026-08-04)

**Why:** SoH stores its pitch slider as a float **leaf** `gAudioEditor.LinkVoiceFreqMultiplier`
while 2Ship stores **children** of that exact path (`.Enable`/`.Scale`). Upstream never meet; in the
shared ComboShip CVar table + single config both coexist, and `Config::Save`'s `unflatten()` throws
(json can't make one key both scalar and object) — an uncaught crash that also truncated the config
file (the `ofstream` opened before `unflatten`).

**Vendored (`COMBO_BUILD`-guarded):**
- `soh/soh/cvar_prefixes.h` — `CVAR_LINK_VOICE_FREQ_MULTIPLIER` resolves to the `.Scale` child in
  combo builds (upstream leaf otherwise); used at the 3 SoH sites (widget, reset button, z_actor.c
  read). Side effect: both games share one pitch value; MM still gates behind `.Enable`.
- `soh/soh/OTRGlobals.cpp` (`Combo_FinishInit`, after `RunVersionUpdates()`) — one-shot migration of
  the old leaf to `.Scale` (covers old combo configs, the legacy `gLinkVoiceFreqMultiplier` updater
  target, and launcher-imported standalone SoH configs, which all still produce the leaf). Gated on
  table presence (not float type — a legacy int leaf must still be cleared) and followed by
  `CVarSave()` so a mid-session `ConsoleVariable::Load` can't resurrect the leaf from disk.
- `libultraship/src/ship/config/Config.{h,cpp}` — all four `unflatten()` sites (`Save`, `Nested`,
  `SetBlock`, `EraseBlock`) route through a `TryUnflatten` helper that logs instead of throwing
  (the exception would unwind across the game-DLL boundary); `Save()` unflattens **before**
  opening/truncating the file; `Nested()` falls back to the last-good nested state on failure.
  Since 2026-09-28 `Save()` sits on upstream's atomic temp-file write (#1183); only the guard is ours.

**On future merges:** if upstream SoH renames the CVar or grows its own `.Enable`, drop the macro
seam and re-check the migration. Audited 2026-08-04: this was the only cross-game leaf-vs-subtree
CVar pair (other candidates were Color-CVar bases, never stored as leaves).

Follow-up (same day): the checkbox still didn't enable the slider — MM's `disabledMap` per-frame
refresh lives only in `Menu::DrawElement`, which never runs under comboui, so popout windows
(Audio Editor, Mod Menu) that draw via `MenuDrawItem` read a frozen disable flag.
`mm/2s2h/BenGui/Menu.cpp` (`Menu::MenuDrawItem`, `COMBO_BUILD`-guarded) now refreshes the map once
per ImGui frame. SoH needs no parity fix: its popout-drawn widgets have no disable-map preFuncs.

## Config::SetBlock drops writes when intermediate keys are missing (2026-08-10)

**Why:** `SetBlock`'s dot-walk only descended into keys that already existed — a missing
intermediate (e.g. `CVars.gRando` on a config that never saved a gRando key) made the whole write a
silent no-op. Surfaced as `MM_RestoreRandoSettings` failing to restore an empty
`gRando.StartingItems: []` kit: `SetStartingItemsInConfig` never landed, exact-seed repro fell back
to the default kit. (Related pre-existing quirk, handled by callers: nlohmann `flatten()` turns an
empty array into null; `GetStartingItemsFromConfig` treats null as empty.)

**Vendored:** `libultraship/src/ship/config/Config.cpp` (`SetBlock`) — create missing intermediate
objects and descend; a non-object in the path keeps the old silent no-op (`ComboShip:` comment at
site).

## Mod ordering never survived a restart (2026-08-24)

**Why:** two independent defects, both combo-only.

(a) Both mod menus stored the user's mod order in the SAME CVar, `gSettings.EnabledMods` — soh via
`CVAR_SETTING("EnabledMods")`, MM via a hardcoded literal, and `CVAR_PREFIX_SETTING` is `gSettings`
in both games. In combo MM reuses OOT's Context and skips `InitConsoleVariables()`, so
ConsoleVariables is shared. `UpdateModFiles(init=true)` erases every listed name it can't find in
*its own* mods dir and rewrites the key; OOT scans `./mods/soh`, MM scans `./mods/2ship`, so each
wiped the other's names. OOT boots first and MM writes last, so after every launch the key held only
MM's mods (or `""`, since `./mods/2ship` is auto-created even when empty) and the order was rebuilt
path-sorted on the next boot. Same shared key also crashed OOT's Mod Menu: Edit -> Cancel re-reads
the CVar without pruning (`init == false`), then `DrawMods` hit `filePaths.at()` on an MM name.

(b) MM's `BenModalWindow` registered as `"Modal Window"`, which OOT already owns, so
`Gui::AddGuiWindow` rejected it and it was never drawn. MM's Mod Menu "Clear List" and "Apply &
Close" queue into a `BenModals` global vector that only that window drains — both buttons did nothing
at all, so an MM order could never be saved.

**Vendored (additive, `COMBO_BUILD`-guarded unless noted):**
- `mm/2s2h/Enhancements/ModMenu/ModMenu.cpp` — MM's key becomes `gSettings.EnabledModsMM`. A SIBLING
  leaf, never a child: `gSettings.EnabledMods.MM` would put a leaf and a subtree at one path, the
  failure behind `CVAR_LINK_VOICE_FREQ_MULTIPLIER`. Since `Config::TryUnflatten` now logs instead of
  throwing, that would silently drop every config write rather than crash.
- `mm/2s2h/BenGui/BenGui.cpp` — `"Modal Window" COMBO_MM_TRACKER_SUFFIX`. Nothing resolves MM's modal
  by name (MM uses the `BenGui::mModalWindow` static), and the visibility CVars already differed
  (`gWindows.ModalWindow` vs OOT's `gOpenWindows.ModalWindow`).
- `mm/2s2h/BenGui/Menu.cpp` — seed `menuThemeIndex` in the ctor (mirroring
  `soh/soh/SohGui/Menu.cpp`) **and** clamp it in `GetMenuThemeColor()`. Required: registering the
  modal makes it draw every frame from the Gui loop, and `THEME_COLOR` resolves to a member that only
  `UpdateElement()` sets — which under comboui runs only once MM's tab is drawn — so `ColorValues.at()`
  would throw out of 2ship.dll across the DLL boundary. The ctor seed alone is not enough:
  `MM_MenuDrawCustom` calls `Update()`, which re-reads the CVar unclamped, so the clamp belongs at the
  `GetMenuThemeColor()` funnel where every `THEME_COLOR` read passes. Also fixes a live crash for
  anyone with `gWindows.InputViewerSettings=1` (`InputViewerSettingsWindow::DrawElement` uses
  `THEME_COLOR`; `InputViewer::DrawElement` does not).
- `mm/2s2h/BenGui/BenModals.cpp` — `ImGuiPopupFlags_NoOpenOverExistingPopup` plus a `PushID("MM")`
  scope. Neither modal window calls `Begin()`, so both open at popup level 0 and hash their popup ids
  against the same window. Both games use the exact titles "Clear List" and "Apply & Close", so
  without the `PushID` MM would draw its message and buttons into OOT's popup with colliding button
  ids — one click could run the other game's callback. The flag is the level tie-break: it is
  one-way (OOT's `OpenPopup` stays unflagged), so OOT wins and MM retries next frame.
- `soh/soh/Enhancements/mod_menu.cpp` + `mm/.../ModMenu.cpp` — a plain "Apply" that writes the CVar
  and leaves edit mode without closing. `SetEnabledModsCVarValue` already calls
  `SaveConsoleVariablesNextFrame()`, so no explicit `Save()`; "Apply & Close" needs its own only
  because it closes before the next frame. Upstream's "Apply & Close" is unchanged, and in combo it
  takes both games down.
- Both mod menus, **not** guarded — a `filePaths.find()` skip at the top of `DrawMods`' loop. A
  genuine upstream bug (a file deleted mid-session throws out of the inline-menu draw path, and
  neither `SOH_MenuDrawCustom` nor `MM_MenuDrawCustom` has a try/catch); correct in standalone too,
  and an `#ifdef` around one `continue` would be worse than the line. The doc entry is the only
  record, since the line carries no `ComboShip:` marker — re-check it after upstream merges.

**Residuals, accepted:** OOT's Presets "Settings" block bulk-clears the whole `gSettings` subtree, so
an OOT settings-preset apply clears both mod lists — benign, they rebuild from disk. The
`GetMenuThemeColor()` clamp covers every `THEME_COLOR` read, but a dozen MM sites read
`gSettings.Menu.Theme` directly with `CVarGetInteger` and stay unclamped; soh's `Menu` is unclamped
throughout, unchanged here because OOT's modal was always registered. If a game's mods dir doesn't
exist, `UpdateModFiles` skips its whole body and leaves the list unpruned and unwritten. When a mod
file vanishes mid-session the skipped row leaves the up/down arrows working on raw vector indices, so
a swap with a skipped neighbour changes the list without changing what is drawn (and a shift-range
selection can pick up the invisible name) — cosmetic, in an already-degraded state.

**On future merges:** the theme seeding relies on soh's and MM's `UIWidgets::Colors` enums and
`ColorValues` maps being identical (`soh/soh/SohGui/UIWidgetOptions.hpp` vs
`mm/2s2h/BenGui/UIWidgets.hpp`) because `gSettings.Menu.Theme` is shared. If a merge diverges them,
the shared key becomes unsafe. Also re-check the `Modal Window` suffix and the "Apply" button if
upstream reworks the mod menu.

## Combo-owned overlay timers (issue #173, 2026-08-24)

**Why:** OOT's "Additional Timers" and MM's "Display Overlay" each showed only their own game's play
time, so a combo run never had a number covering the whole run. MM also had a live bug: its play time
is wall clock between flushes (`filePlaytime += now - lastTimeLog`) and `lastTimeLog` was never
advanced when combo swapped away, so hours spent in OOT were folded into MM's save at its next flush.

**Combo-owned:** `combo/gui/ComboTimersWindow.{h,cpp}` — window `Timers##Combo`, CVars `gCombo.Timers.*`,
settings under ComboShip Settings → Timers. The total is `OOT playTimer/2 + pauseTimer/3` (deciseconds)
plus MM's `filePlaytime/100`; both already live in the `.combosav`, and exactly one advances at a time.
Time of day / Navi / conditional draw only while OOT is foreground. `Combo_SetForegroundGame` pauses
and resumes MM's accumulator across every swap. The total tints green once both games are beaten
(`ComboUI_SetComboComplete`, pushed from the existing `combo.completion` flags — no new save key).

**No real-time (RTA) row, deliberately.** MM's `GetUnixTimestamp` (`mm/2s2h/BenPort.cpp`) assigns
`millis.count()` to a `long`, which is 32-bit on Windows, so every MM timestamp is truncated modulo
2^32 — `fileCreatedAt` lands around 8.6e8 where soh's untruncated ms is around 1.79e12. MM's own
timestamps stay self-consistent (differences survive the truncation until it wraps, every ~49.7 days),
so this is invisible inside 2Ship, but it makes MM and OOT timestamps incomparable. A first attempt at
an RTA row took `min(OOT firstInput, MM fileCreatedAt)` and displayed 496307 hours. **Never compare a
soh timestamp with an MM one** without fixing the truncation first.

**Vendored (all `COMBO_BUILD`-guarded):**
- `soh/soh/OTRGlobals.cpp` — `SOH_GetPlaytimeDeciseconds`, `SOH_GetOverlayTimers` (classification
  stays in soh so comboui hardcodes no vanilla enum values).
- `mm/2s2h/BenPort.cpp` — `MM_GetPlaytimeMs`, `MM_ComboPausePlaytime`,
  `MM_ComboResumePlaytime` + a running latch. The advance is implemented locally rather than calling
  `SavingEnhancements_AdvancePlaytime` so `SavingEnhancements.cpp` stays untouched. The
  `lastTimeLog != 0` guard matters because `z_sram_NES.c` zeroes it on a new file — treating 0 as a
  timestamp would add a whole Unix epoch (~57 years).
- `soh/soh/SohGui/SohMenuEnhancements.cpp` and `mm/2s2h/BenGui/BenMenu.cpp` — the native timer menu
  entries are `#ifndef COMBO_BUILD`. A sidebar allow-list is not enough: `ComboMenu::DrawSearchResults`
  walks both games' menu models unfiltered, so a search for "timer" would re-open the native window.

**Do not remove** the `TimeDisplayWindow` registration at `soh/soh/SohGui/SohGui.cpp:203` — its
`InitElement` is what loads the digit and icon textures the combo overlay draws with. Only its draw
is suppressed.

**Residuals:** the pause only flushes into memory — nothing persists MM's save on the way out — so
time played in MM without an owl/auto save is lost when the slot's MM blob is re-read (reset or
owl-save quit, `g_MmSaveInMemorySlot = -1`), and the total steps back to MM's last saved value. Same
as vanilla 2ship.

**Separate bug found while playtesting this, NOT fixed here:** MM's play time reads 0 after an owl
save because `SaveManager_LoadSaveFile` (`mm/2s2h/SaveManager/SaveManager.cpp`) reads only the
`newCycleSave` key and never `owlSave`. ComboShip's resume shortcut (`mm/src/code/title_setup.c`,
`COMBO_BUILD` block) goes through that function instead of vanilla's `Sram_OpenSave`, which picks the
owl page when `isOwlSave` is set and then deletes it on continue (`VB_DELETE_OWL_SAVE`). So an owl
save's whole state — not just play time — is discarded on a combo resume. Note that a new-cycle save
deliberately preserves `owlSave`, so a read-priority fix alone would let a stale owl save shadow a
newer cycle save; the delete-on-continue half is required too.

## Time Splits window + CVar clash (2026-09-28)

soh ported 2Ship's Time Splits, so both games registered the same window names and
`ImGui::Begin("Timesplits")`. MM itself registers both the leaf `gWindows.Timesplits` and
`gWindows.Timesplits.Settings` (leaf-vs-subtree: config stops saving); soh reusing that settings key
made the clash reachable from OOT's menu. soh's button CVar also never matched its own window's CVar
upstream, so the fix repairs that too.

Fix: soh's button uses `CVAR_WINDOW("TimeSplitSettings")` + an inline settings shim; MM's two windows
get `##MM`, its overlay ID `Timesplits##MM`, its settings CVar `gWindows.TimesplitsSettings` (old key
cleared on init), and `ComboTrackerVisibility` keeps only the foreground game's overlay in the Gui map
(same Remove/Re-add as notifications, see tracker.md).

## Merged pause menus: hexagonal prisms (2026-11-12)

Owner ask: "the entire Masks panel of the MM pause menu in the OOT pause panels, both item panels from both games in
each other's pause menus, every item equipable, and my dpad/cbutton equips line up in both games". Owner design
(same day): "make the pause cubes into hexagonal prisms with 6 tiles to scroll between". Owner hard rule: everything
from the other game looks and behaves 1:1 like its home game.

**Faces (R / stick right = +1; Z / L / stick left = -1; vanilla's page-turn animation, 60° per turn):**
- OOT: Hyrule items (OOT's item page) → Termina items (MM's item page) → Masks (MM's mask page) → Map → Quest →
  Equipment.
- MM: Termina items (MM's item page) → Hyrule items (OOT's item page) → Map → Quest → Masks → Hyrule equipment
  (OOT's equipment page).

**Geometry.** Vanilla's cube: pages 93.55 (OOT, `WREG(3)`) / 93 (MM) from the centre, camera on the opposite wall
(64 back), 157.55 / 157 from the page it shows. The prism keeps the page size: apothem = page width /
(2 tan 30°) = 187.2 / 1.1547 = 162.1, camera 4.55 (OOT) / 5.1 (MM) in front of the centre looking out, so the face
shown is exactly vanilla's distance (and size). Each face's transform is vanilla's per-page
Translate/Scale/flip/RotateY generalised to `face × 60°`. Every face is drawn every frame (MM now allocates all
four page vertex sets, `COMBO_PRISM_ALL_PAGES`), the active face last. MM's active 3D world-map plane keeps its
camera distance (208) the same way. Save / game-over prompt pages swing down in front of the active face.

**Page state.** The game page enums are untouched: the extra faces are `PAUSE_ITEM` pages with a combo "face" that
says whose page it is (`Combo_PauseFace` / `Combo_MmPauseFace`). `KaleidoScope_SwitchPage` starts a prism turn, the
turn's end (`func_808237B4` / `KaleidoScope_UpdateSwitchPage`) lands on the prism's face, the opening spin
(`func_808265BC` / `KaleidoScope_UpdateOpening`) reopens on the last face, and the camera
(`KaleidoScope_ComboSetPrismView`) replaces `KaleidoScope_SetView(eye)`. A side face is drawn with `pageIndex`
temporarily set to another page so it is drawn but not driven. The "To <page>" labels name the prism's neighbours.

**Foreign pages (1:1).** A foreign face is the owning game's page: its page tiles (MM: flat 180,180,120 prim colour;
OOT: per-column vertex gradients), icons, name plates and ammo digits, all from the owning game's ResourceManager
(`G_COMBO_RM_PUSH/POP`), and its cursor rules (MM: the cursor rests on any slot, white on empty; OOT: only on owned
items). Inventory is read live from the dormant save through `MM_ComboPause_*` / `SOH_ComboPause_*`
(`combo/menu/ComboPauseABI.h`). The Hyrule equipment face shows OOT's 4×4 equipment grid (upgrade column + three
pieces per row, equipped outline; no Link preview).

**Foreign items on buttons.** Encoded in the host's own button-item byte (no save-format change), naming the owning
game's inventory slot: OOT host 0x9E + MM slot, skipping 0xBF–0xC1 (→ 0x9E–0xD0); MM host 0xD0 + OOT slot
(→ 0xD0–0xE7), and with SoH's "Assignable Tunics and Boots" on, OOT's tunics and boots as the button slots OOT
stores for them (`SLOT_TUNIC_KOKIRI` 24 – `SLOT_BOOTS_HOVER` 29 → 0xE8–0xED). The host's slot byte for a foreign
button is the foreign id itself (unique, never a real slot), so both games' "same slot on another button → swap" rules work unchanged. A shared item the host holds equips as the
host's own copy. HUD / equip-animation icons come from the owning game's RM. A foreign ammo item's button digits are
the owning game's count as a use spends it (MM host: `VB_DRAW_HUD_AMMO_COUNT` in `ComboPause.cpp`; OOT host: a fenced
`Interface_ComboDrawForeignAmmoCount` in `z_parameter.c`), green at the owning game's full bag, grey at 0; with shared
consumables on, that is the shared count (`ComboHealth::DormantAmmo`), and the foreign face shows the same number.
Use: OOT host — MM transformation
masks → the form system (any mask while transformed changes back), other MM masks → worn with MM's model
(`D_801C0B20`, routed `@mm:` DL on the head limb), MM's Deku Sticks, Deku Nuts, bombs, bombchus and bow → OOT's own code
for the same item on MM's bag (`Combo_PauseForeignHostItem`), the items in the "MM items in Hyrule" table below →
their ports, anything else → OOT's error sound. MM host — OOT's Keaton / Bunny / Truth / Goron / Zora masks → MM's own
masks, OOT's Deku Sticks, Deku Nuts, bombs, bombchus and bow → MM's own code for the same item on OOT's bag
(`Combo_MmPauseForeignHostItem`), the items in the "OOT items in Termina" table → their ports, anything else → MM's
error sound.

**Shared layout.** `ComboEquipShare` now carries each button's origin (`"cx"`, `"dx"`: owning game + slot); the
receiver puts its own item there when it has one, otherwise the other game's item as a foreign id, and empties a
button the sender had empty.

**Seams:** `soh/src/overlays/misc/ovl_kaleido_scope/{z_kaleido_scope_PAL.c,z_kaleido_item.c,z_kaleido_combo.c}`,
`soh/src/code/{z_parameter.c,z_player_lib.c}`, `soh/src/overlays/actors/ovl_player_actor/z_player.c`,
`mm/src/overlays/kaleido_scope/ovl_kaleido_scope/{z_kaleido_scope_NES.c,z_kaleido_item.c,z_kaleido_combo.c}`,
`mm/src/code/z_parameter.c`, `mm/src/overlays/actors/ovl_player_actor/z_player.c`; logic in
`soh/soh/Enhancements/randomizer/ComboPause.cpp` and `mm/2s2h/Rando/MiscBehavior/ComboPause.cpp`. Test harness:
`COMBO_DEBUG_PAUSE` / `COMBO_DEBUG_MM_PAUSE` (`combo/menu/ComboPauseHarness.h`).
## MM items in Hyrule (2026-11-13, branch feat/mm-items-in-hyrule)

Owner bar: "Every item has to work from both games in both games ... correct models, correct sounds, animations, all
of it, no sloppy analogs". Rule applied: each MM item used from OOT's pause (a foreign id on a C button / D-pad) runs
a PORT of MM's own code (MM's player item action, MM's actor, MM's models / animations / sounds through the "@mm:"
routing and MM's native sfx player); only damage and puzzle credit map to OOT (OOT damage flags, OOT bombable walls,
OOT's counterpart actors). Where Hyrule has nothing that reacts, the item still behaves as in MM (worn with MM's
model, MM's sounds) and the table says so.

Shared pieces added:
- `soh/soh/Enhancements/combo/ComboMmText.cpp`: MM's own messages in OOT's textbox. Reads MM's message table from
  MM's archive (`text/message_data_static/message_data_static`), translates MM control codes to OOT's (colours,
  newline, box breaks, quick text, two/three-choice, button glyphs; MM-only timers/delays/sfx dropped).
  `Combo_MmTextPrepare(mmTextId)` -> OOT text id `TEXT_COMBO_MM_TEXT` (0x9218).
- `Randomizer_DrawMmItemByName` (draw.cpp / combo/menu/ComboForeignDrawOOT.h): MM's GI model of an MM item by its MM
  rando name, for items Link holds up.
- `MM_ComboItems_SetSlot` (mm/2s2h/Rando/MiscBehavior/ComboHyruleItems.cpp): an MM bottle used in Hyrule writes its
  new content into MM's own slot (the button keeps its foreign id, which now names the new content).
- `Combo_MmPlayFanfare` / `Combo_MmStopFanfare` (ComboMmAudio.cpp, code_800EC960.c, audio_load.c): MM music sequences
  (Bremen March 0x53, Kamaro's Dance 0x71) played natively by OOT's engine on the fanfare player in MM script mode.

| item | works | what was ported | techniques verified | known gaps (reason) |
|---|---|---|---|---|
| Great Fairy's Sword | yes (child + adult) | MM EnMThunder GFS look (spin scale 4 / great 6, MM dust, MM charge aura), MM swing / spin sounds (MM sfx player), MM reach 5500 = OOT Biggoron row; 2S2H "Great Fairy Sword B-Button Attack" and "Two-Handed Sword Spin Attack" CVars honoured (shared CVars) | draw + swing from C (child, adult), charged spin by holding C with TwoHandedSwordSpinAttack on (screenshots gfsA / gfsC) | damage credit = OOT Biggoron flags; MM's instant great spin (Swordsman's flag) is not read in Hyrule |
| Powder Keg | yes (Goron form) | MM EnBom keg branch (z_en_bom_combo_keg.c) + MM EnClearTag large explosion; MM keg ammo (its 0/1 on OOT's button since 2026-11-24, as MM's HUD shows it); Goron-only use and lift (MM rules); fuse 200; MM sounds; MM barrel / skull / fuse models | Goron takes the keg out overhead, sets it down, fuse burns, MM explosion with rays and smoke (keg-48..68, log "MM keg exploded") | breaks OOT bombable walls / boulders through OOT's explosive flag (radius 200, damage 4); fuse sound doesn't speed up (MM channel IO not in the native sfx player); a held keg isn't respawned across a scene change; non-Gorons get the error sound (button not greyed) |
| Pictograph Box | yes | MM picto lens, MM HUD focus frame, shutter, capture -> I8 -> I5 in MM's save, MM 0xF8 "Keep this picture? Yes/No" (MM's text), photo shown again on reuse, ColorPictograph CVar | lens, shutter, prompt, Yes, exit, reuse shows the photo (picto-48..88) | no Hyrule photo subjects (snap flags cleared); the prompt box is OOT's; MM's save isn't written to disk while in Hyrule (the colour PNG is) |
| MM bottles (potions, milk, fairy, fish, bug, Poe, blue fire) | yes | OOT's bottle actions (MM's are the same code) writing the MM bottle's own slot (MM_ComboItems_SetSlot); MM content colours | Chateau drunk: MM slot 18 0x25 -> 0x12 (log) | MM bottles can't catch blue fire (MM's catch table has none); forms can't use MM bottles yet |
| Chateau Romani | yes | MM drink (heal + magic) + infinite magic (MAGIC_STATE_CONSUME and lens drain skipped) | log "infinite magic until dawn" | adapted: lasts until the next OOT dawn (06:00) instead of MM's cycle reset; not saved |
| Deku Stick, Deku Nut (when OOT has none of its own) | yes (2026-11-24, untested) | OOT's stick and nut actions (MM's are the same code: take-out ammo check, break, burn-out, nut throw), checking and spending MM's bag (`Combo_PlayerAmmo` in z_player.c; with shared consumables on, the shared count via `Combo_SharedMmAmmoSpend`, otherwise MM's resident save, as for the Powder Keg); before this, the error sound | code only | OOT's stick model in hand; OOT's age rule for its own sticks (child; Timeless Equipment and Unified Inventory lift it, `Combo_PauseForeignAgeOk`; until 2026-11-24 none); forms can't use them (OOT's rule for its own sticks and nuts); a stick keeps the bag it was taken out with |
| Hero's Bow and arrows (when OOT has none of its own) | yes (2026-11-24, code only) | OOT's bow action (MM's is the same code: the arrow count in `func_80834380`, the shot in `func_808350A4`), OOT's EnArrow with OOT's bow model, checking and spending MM's quiver (`Combo_PlayerAmmo`, as for the sticks; the bow keeps the bag it was taken out with) | code only | adult only (OOT's rule; Timeless Equipment and Unified Inventory lift it, and a child then shoots OOT's seeds, as with OOT's own bow); forms can't use it; minigames (horseback archery, the gallery) spend their own count; MM's fire / ice / light arrows not ported (MM's magic arrows are separate items with MM's magic costs; OOT's arrow cycling needs OOT's own bow); OOT's arrow drops need OOT's own bow (inference, not checked in game) |
| Bomb, Bombchu (when OOT has none of its own) | yes (2026-11-24, code only) | OOT's explosive take-out (MM's is the same code: `Player_InitExplosiveIA`, the ammo and 3-on-screen checks in `Player_UseItem`), OOT's EnBom / EnBomChu with OOT's models, fuse and blast, checking and spending MM's bag (`Combo_PlayerAmmo`, as for the sticks); before this, the error sound | code only | forms can't use them (OOT's rule; MM's too); bombchu bowling spends the alley's count, as for OOT's own |
| Kotake's red potion | yes | MM maps it to the Poe drink (random -1 / +3 hearts) | code | |
| Spring / Hot Spring Water | yes | MM pour (release action) spawning MM's ObjAqua (new actor); hot water melts OOT's Obj_Ice_Poly (MM's rule) | code | no frozen Goron / watered bean in Hyrule |
| Gold Dust, Mushroom, Seahorse, Zora Egg, Hylian Loach, Deku Princess, Big Poe | yes | MM show route: bottle held up, MM 0xFE "This item doesn't seem to work here..." | same path as the Moon's Tear | no takers in Hyrule |
| Moon's Tear, Title Deeds, Room Key, Letters, Pendant | yes | MM show route with MM's GI model held up, MM 0xFE text, NA_SE_PL_PUT_OUT_ITEM | Moon's Tear held up + textbox (bottle-80) | |
| Bunny Hood | yes | MM Player_Action_13 1.5x run; MM air steering (no clamp to the run limit) so jumps keep the speed | code | not measured headless |
| Blast Mask | yes | B through MM's item path (ITEM_F0): an OOT bomb at the head with its fuse at 0 and its blast sphere put on it at spawn (MM EnBom_Init; OOT's EnBom sets it only in its body draw, which a fuse-0 bomb never reaches), so it reveals grottos, cuts grass, breaks bomb walls and boulders and hits switches and enemies as an OOT bomb does; cooldown 310 frames or 2S2H's "Blast Mask Cooldown" (`gEnhancements.Masks.BlastMaskCooldown`, shared CVar store), ticked every frame and reset with each new Link (MM Player field); MM's "Explode" B label (MM's do_action_static texture), B greyed out during the cooldown (MM Interface_UpdateButtonsPart2); MM scorched-mask fade | explosion on B, a second B during the cooldown does nothing (blast-44..64); world hits, label and cooldown setting: code only (2026-11-24) | self-damage = the OOT bomb hitting Link (AT_TYPE_ALL), as in MM; B is the mask's only while B holds a sword or nothing (MM: sword, nothing or Deku Nuts), so the bow on Epona or a minigame item stays on B; the grey-out applies only with a sword on B (OOT's `buttonStatus[0]` stashes the B item otherwise); blast size and damage are OOT's bomb's |
| Bremen Mask | yes | MM Player_Action_11 march (MM anims, stride bob) + MM's Bremen March fanfare natively; OOT dogs / chicks follow (MM En_Dg / En_Nwc); MM's "March" B label | march + fanfare start, no crash (log) | chicks don't grow into cuccos; the dog doesn't wait for the beat; B is the mask's only while B holds a sword or nothing |
| Kamaro's Mask | yes | MM Player_Action_12 dance + MM Kamaro's Dance fanfare; MM's "Dance" B label | code (same path as Bremen) | no Rosa sisters in Hyrule; B is the mask's only while B holds a sword or nothing |
| Stone Mask | yes | MM's per-actor checks mirrored in the OOT counterparts: Keese, Guay, Bubbles, Mad Scrub, Dodongo, purple Gerudo guards, Leever, Like Like, Skulltula, Tektite, Beamos | code review | OOT-only enemies (Stalfos, Moblins, ...) ignore it, like MM's non-checking enemies |
| Captain's Hat | yes | MM En_Skb: Stalchildren stop attacking, turn friendly, MM idle / salute, MM lines 0x13F6-0x13F8 | code review | ReDeads: MM reacts only with its dancing types (none in Hyrule) |
| Garo's / Gibdo Mask | worn | MM reactions are Ikana-only (Garo encounters, dancing ReDeads, Gibdo NPCs) | | nothing in Hyrule reacts (MM rule) |
| Mask of Scents | yes | MM voice bank 7, MM sniffing idle (cl_msbowait), no fidgets | code | MM has no speed effect; no MM mushrooms in Hyrule |
| Mask of Truth | yes | Gossip Stones read as with Truth | code | MM dogs' thoughts: OOT dogs can't be picked up |
| Great Fairy's Mask | yes | MM hair sim incl. its stray-fairy reaction (strands float + sparkle); MM stray fairies at OOT checks are MM's EnElforg (drift, fly to Link's waist with the mask, chime, collect) — see "MM stray fairies in Hyrule" below | see below | see below |
| Couple's / Circus Leader's Mask | worn | MM animated material / tears + bubbles | code | NPC reactions are Termina-only |
| Giant's Mask | disabled | MM rule: only in Twinmold's arena; button disabled, error sound | | by design |
| All-Night, Postman's Hat, Kafei's, Romani's, Don Gero's, Keaton | worn | MM model + MM on/off sounds | | only Termina NPCs / objects react |
| MM swords / shields | n/a | no MM equipment face on OOT's pause | | skipped |

### MM stray fairies in Hyrule (2026-11-13, branch feat/stray-fairies-hyrule)

Owner: "randomizer can have great fairy pieces in oot levels so the mask should react to that." An MM stray fairy at an
OOT check (freestanding, or released from a pot / crate / beehive / grass / rock / tree / wonder spot) is MM's EnElforg,
ported line for line (`soh/src/code/z_en_item00_combo_strayfairy.c`): the check's EnItem00 (ITEM00_SOH_DUMMY whose item
is RG_COMBO_FOREIGN drawn by MM as gStrayFairySkel) adopts the fairy on its first update and swaps its update / draw /
destroy. MM's own skeleton, flying animation and per-area colour texanim (per-actor SkelAnime, head flipped by facing:
`ComboForeignAnim_InitInstance` / `_DrawInstance`), MM's drift around home (speed 1, deliberate orbit), MM's sparkles,
MM's collect ranges (xz < 30, y in (-68, 12)), +3 hearts, 80 frames circling Link with NA_SE_PL_CHIBI_FAIRY_HEAL. A
container release appears 40 units up with NA_SE_EV_BUTTERFRY_TO_FAIRY (MM Item_DropCollectible).
- Great Fairy's Mask worn (from OOT's pause, human Link): the fairy flies to Link's waist at speed 5 and chimes
  NA_SE_SY_FAIRY_MASK_SUCCESS once per fairy each time the mask goes on (MM sounds through the native MM sfx player).
- Hair: MM's ACTORCTX_FLAG_3 is `gComboStrayFairyNear`, raised by a free-floating fairy and by a pot / crate / beehive
  still holding one (MM's ObjTsubo / ObjKibako2 / ObjTaru / ObjComb do the same, culling off); the mask's hair sim
  (z_player_lib.c) floats the strands and sheds MM's Player_DrawStrayFairyParticles; cleared at the end of Player_Draw.
- The check: collection sets the pickup's own flag (randomizer inf / collectible), exactly what touching the EnItem00
  did, so OOT's queue presents and OOT_DeliverForeign grants the fairy into MM's save (MM's randomizer does the same:
  VB_GIVE_ITEM_FROM_ELFORG false, flag -> rando queue).
- Chests: MM's randomizer gives a chest's stray fairy through the get-item path (EnBox VB_GIVE_ITEM_FROM_CHEST), so OOT's
  chest give is kept, and chests do not raise the hair flag (MM's rando chests hold GI_RECOVERY_HEART). NPC rewards
  unchanged.
- Verified headless (file2 seed, Hyrule Field): drift (sfA), touch collect + delivery "OOT delivered foreign item
  'Stone Tower Stray Fairy' to MM (from check 'LH Grass 9')" (sfB), mask on -> FAIRY_MASK_SUCCESS, target speed 5,
  fairy flew 150 units to Link and was collected, hair floating (sfC). Harness:
  `COMBO_DEBUG_SPAWN_STRAYFAIRY=<check name, '+' = space | auto>[,freestanding][,dist=N][,delay=N]` (spawns the real
  check's pickup in front of Link), trace `COMBO_DEBUG_STRAYFAIRY`.
- Owed / not ported: OOT fairy-shuffle checks (EnElf healing fairies carrying a stray fairy) stay OOT fairies; MM's
  bubble / enemy-held / collider stray-fairy types have no OOT counterpart; MM's CosmeticEditor stray-fairy colours not
  read; container hair flag not verified in a run (code path only); sounds not checked by ear (headless); `auto` may pick
  a chest check whose randomizer inf does not queue it (use a name).


## OOT items in Termina (2026-11-13, branch `feat/oot-items-in-termina`, IN PROGRESS)

Owner bar: every OOT item works in Termina as a PORT of OOT's own code (player item action + OOT actor + OOT
models/animations/sounds); only damage / puzzle credit maps to MM.

**Design.** OOT items without a Termina twin get their own MM item actions (`PLAYER_IA_COMBO_OOT_*`, appended after
MM's so no MM range check sees them) and run OOT's item code ported into MM's player
(`mm/src/overlays/actors/ovl_player_actor/z_player_combo_oot.inc`, included at the end of z_player.c; each function
names its OOT original). OOT actors are combo files in MM's overlay dirs: OOT's EnBoom as MM EnBoom params
`COMBO_OOT_BOOMERANG` (`ovl_En_Boom/z_en_boom_combo_oot.c`), OOT's Magic_Dark/Fire/Wind as new actors
`ACTOR_COMBO_MAGIC_DARK/FIRE/WIND` (`ovl_Combo_Magic_*`). Models are OOT's (`__OTR__@oot:` routed DLs; hand models
swapped in `Combo_MmOotHeldDL`), animations come from OOT's archive (new `__OTR__@oot:` routing in MM's
`ResourceMgr_LoadAnimByName`), raw OOT resources via `Combo_MmOotRawResource`. OOT's save stays the owner of ammo
(`SOH_ComboAmmoBag`, soh `ComboSharedHealth.cpp`) and age rules (`SOH_ComboTermina_*`, soh `ComboTerminaItems.cpp`,
additive). An OOT ammo item's count in Termina (button digits, MM's Hyrule face, what a shot spends) is one number,
`Combo_MmOotAmmo`: with shared consumables on, the shared count as OOT will show it on arrival (the shot spends from
it); off, OOT's own save, which a shot changes in memory and `SOH_ResumeGame` writes before OOT loads it again.
OOT's age rule (`gItemAgeReqs`, SoH
Timeless Equipment / Adult Masks honoured) greys and blocks items like OOT's pause does. Logic:
`mm/2s2h/Rando/MiscBehavior/ComboOotItems.cpp`. Harness trace: `COMBO_DEBUG_OOTITEMS=1`.

**Sound.** Measured (sequence emulator over both archives, tool kept outside the repo): every sound these items use
is byte/note-identical in MM at the same id (boomerang throw/fly/catch, hammer swing/hit, slingshot shot/flick/
reflect, hookshot, Nayru's Love, FW vanish, spell voices) EXCEPT `NA_SE_PL_MAGIC_FIRE` 0x879 (MM's flamethrower
sample is a lower-rate copy, corr 0.79), `NA_SE_PL_MAGIC_WIND_NORMAL` 0x87A (MM envelope tail half volume),
`NA_SE_PL_MAGIC_WIND_WARP` 0x87B (MM layer turns vibrato off), `NA_SE_IT_SLING_DRAW` 0x1821 (same recording,
re-encoded 2-bit ADPCM), and for later items the four iron-boots sounds and the hookshot-hang voice. No full OOT audio
port is needed; those ids need a targeted carry of OOT's script + font entry + sample (OWED).

| item | works | ported | verified (headless) | known gaps / owed |
|---|---|---|---|---|
| Boomerang (child) | yes | OOT func_80835800 chain + OOT EnBoom (speed 12, homing, 20-frame return, wall bounce, EnItem00/Gold Skulltula fetch, catch r=40), OOT anims, OOT child hand + boomerang model, OOT aim camera (CAM_MODE_BOOMERANG); dmg 0x10 = MM Zora-boomerang bit | throw + catch x3 (log `boomerang thrown/caught`) | flight not yet captured on screenshot (camera angle); item fetch / stun not exercised |
| Megaton Hammer (adult) | yes | OOT melee path: forward/side choice, OOT hammer anims + hit windows, no combo/lunge, OOT wall hit (hit-stop, quake, rebound), frame-7 / jump-attack frame-2 shockwave; credit DMG_GORON_PUNCH+POUND, impact = Goron ground pound | 4 swings, OOT anims, shockwave each forward swing; OOT hammer model in adult hand (screenshots) | side swing / jump attack / switch+rock credit not exercised in harness |
| Slingshot (child) | yes (owner, Deck, 2026-11-24: seeds run out) | OOT bow/slingshot chain (unk_B28 2), OOT seed (MM kept ARROW_SEED), OOT bullet bag ammo, OOT sling draw/flick sfx, OOT child hand + rubber band + first-person arm; seed count on the C button / D-pad (`VB_DRAW_HUD_AMMO_COUNT`, MM's places and colours) and on MM's Hyrule face | — | the count on button and pause face is owed a Deck check (2026-11-24 fix: none was drawn, the face showed OOT's stale save); OOT's rando infinite bullet bag isn't honoured in Termina; draw sfx re-encoded in MM |
| Din's Fire | builds, untested | OOT cast action (Player_Action_808507F4) + Magic_Fire port (OOT sphere/texture/tint/vertex alpha), credit DMG_FIRE_ARROW | — | test owed; 0x879 sample degraded in MM |
| Farore's Wind | builds, untested | Magic_Wind port (OOT curve skeleton), warp point set/return/dispel, point saved in MM save (`shipSaveInfo.comboFaroresWind`), OOT arrival | — | test owed; choice box is Termina's message box with OOT's 0x3B wording; 0x87A/0x87B differ |
| Nayru's Love | builds, untested | Magic_Dark port (orb, diamond, invincibility -100, 1200 frames, song kills it, carries across scenes via MM's nayrusLoveTimer remnant) | — | test owed |
| Hookshot / Longshot | builds, untested | no longer twins: OOT lengths (13 / 26 frames), OOT reticle reach, OOT tip + chain models | — | test owed; MM's own Hookshot stays on the Termina face |
| Skull / Spooky / Gerudo Mask | builds, untested | worn instantly (OOT), OOT model on head limb, one mask at a time | — | test owed |
| Deku Stick, Deku Nut (when MM has none of its own) | yes (2026-11-24, code only) | MM's own stick and nut actions (OOT's are the same code: take-out ammo check, break, burn-out, nut throw), checking and spending OOT's bag (`Combo_PlayerAmmo` in MM's z_player.c, `Combo_MmOotAmmo` / `Combo_MmOotChangeAmmo`: the shared count with shared consumables on, otherwise OOT's save); before this, the error sound | — | MM's stick model in hand; MM's form rule (sticks: human only; nuts: human and Deku), no OOT age gate (MM has none); a stick keeps the bag it was taken out with; Deku Link's B-button nut drop in flight stays MM's own bag |
| Bomb, Bombchu (when MM has none of its own) | yes (2026-11-24, code only) | MM's own explosive take-out (OOT's is the same code: `Player_InitExplosiveIA`, the ammo and on-screen-limit checks in `Player_UseItem`), MM's EnBom / EnBomChu with MM's models, fuse and blast, checking and spending OOT's bag (`Combo_PlayerAmmo`, as for the sticks) | — | human only (MM's form rule); a minigame's own bomb / bombchu count (MM `play->unk_1887E` / `unk_1887D`) is spent instead, as for MM's own |
| Hero's Bow and arrows (when MM has none of its own) | yes (2026-11-24, code only) | MM's own bow action (OOT's is the same code: the arrow count in `func_808305BC`, the shot in `func_80831194`), MM's EnArrow with MM's bow model, checking and spending OOT's quiver (`Combo_PlayerAmmo`, as for the sticks; the bow keeps the bag it was taken out with) | — | human only (MM's form rule), no OOT age gate (MM has none); minigames spend their own count; OOT's fire / ice / light arrows not ported |
| Lens, beans, ocarina, bottles | twin rule kept (MM's own copy when MM holds it) | — | — | decision: shared items; OOT-identical behaviour not separately verified |
| Ruto's Letter, OOT trade items | no (MM error sound) | — | — | OWED: hold up / show as OOT does |
| Hyrule Equipment face: tunics and boots | A puts them on (OOT's save); with SoH's "Assignable Tunics and Boots" a C button / D-pad direction takes them, and a press puts the piece on or back to Kokiri (SoH's `UseTunicBoots`) | `ComboPause.cpp` `UseGearOnPress`, `SOH_ComboTermina_ToggleEquipValue`; Goron Tunic heat and Zora Tunic water (`Interface_Update` hazard), Iron / Hover Boots (`Player_SetEquipmentData`) read OOT's save | — | Deck check owed (2026-11-24); human form only; MM's scene restrictions still grey the button (SoH keeps equipment enabled); no heavy-boots sound in MM |
| Hyrule Equipment face: swords, shields, strength, scale | no (MM's own in Termina) | — | — | OOT shields on a button don't carry to Termina |

## Named presets and the ComboShip settings import (combo-owned)

**Why:** neither port has a settings preset that is safe here. OOT's preset apply replaces whole CVar blocks with
`Config::SetBlock`, and `gSettings` / `gEnhancements` are shared, so it wipes both games' mod lists and every key MM owns
(see the residuals under the mod menu above). Players also keep one install per play style only because of that.

**What:** Settings > Presets and Settings > Import from Ship2Ship or ComboShip in the combo menu (`combo/gui/ComboPresetsPanel.cpp`,
`ComboConfigImportPanel.cpp`).
- **Storage:** `presets/<name>.json` beside `ship2ship.json` (`ComboRando::DataDir()`: the working directory on
  Windows, `$SHIP_HOME` on Linux, the AppImage and macOS). Format `{"format":"Ship2ShipPreset","version":1,"settings":{"<dotted CVar>": value}}`;
  colours are 3 or 4 number arrays. Names are ASCII letters, digits, space and `- _ . ( )`, so the file name is the same on every OS.
- **Scope:** `combo/ComboPresetScope.h` is the one allowlist, used by save, apply, the menu import and the first-launch
  import. Roots found in the trees: soh `gRandoSettings gRandoEnhancements gCosmetics gEnhancements gSettings gTrackers
  gCheats gAudioEditor gAddTraps gGameplayStats gRemote gGeneral gOpenWindows gDeveloperTools`; 2S2H `gRando gCosmetic
  gEnhancements gSettings gModes gFixes gNotifications gHudEditor gDisplayOverlay gCheats gAudioEditor gRemote
  gDeveloperTools gWindows`. `gSettings` and `gRemote` are listed key group by key group; graphics, window, LED, debug,
  per-run state and the transient `Seeded` / `MmPlaced` markers are never carried. Mod lists
  (`gSettings.EnabledMods*`) and controls (`gSettings.Controllers.*`, mouse and nav keys) are import-only, behind
  "Also import mods list and controls".
- **Apply:** `ComboPresets::WriteLeaves` (`ComboPlayPreset.cpp`, the leaf writer that came with WomboComboShip's control
  presets, which are gone) writes only the preset's keys with the typed CVar setters, skips keys already at the
  requested value, hands each changed key to both games' `*_MenuApplyCVarChange` (a key one game does not know has no
  init func there, so the call is a no-op), reloads controllers when bindings changed, and saves. Never `SetBlock`.
- **Loaded save:** keys the randomizer reads at generation or slot bind (`gRandoSettings.`, `gRando.`, `gCombo.Rando.`) are
  held back unless the OoT file-select screen is up and MM is not foreground (`SeedBoundChangesAllowed`); the status
  line says how many were skipped. Everything else applies live.
- **File check:** `ComboConfigTree::ReadConfigFile` is the one check for both imports. It returns a reason
  (`ComboConfigCheck` in `combo/ComboSettingsImport.h`): can't open, not JSON, no `CVars`, no `gCombo` (ComboShip files
  only), or this install's own `ship2ship.json`, which is never a source. Both screens show the reason in plain words
  (`ComboPresets::DescribeConfigCheck`), and a file that fails is not imported.
- **Menu import:** checks the file as above, copies the current config to `ship2ship.json.bak-<timestamp>`, then applies
  through the same writer. Only the `CVars` block is read, never `Window`.
- **First launch:** the import screen has a third slot for a `ship2ship.json` or `comboship.json` (it auto-detects both; `ComboSettingsImportResult::comboPath`).
  The launcher runs the same check on Import that the screen showed, for every slot. It filters the ComboShip file's
  CVars with the same scope and merges it as the lowest-priority base under 2Ship and SoH. `ConfigVersion` comes from
  the SoH file, else from the ComboShip file; without one, `RunVersionUpdates` would re-run every OoT updater over
  current settings (v2 wipes the Audio Editor's replaced sequences, v7 remaps bridge, Ganon's Boss Key and Triforce
  Hunt settings).
- **Not changed:** the stock OOT / MM Presets sidebars still exist in the per-game tabs.
