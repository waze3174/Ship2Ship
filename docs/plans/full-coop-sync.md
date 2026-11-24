# Plan: ComboShip "One World": full co-op sync + full cross-play randomizer

- **Status:** plan, not started. Written 2026-11-11 against `a6abbb06` (junkx420/ComboShip).
- **Audience:** Claude Code, executing this end to end. The repo owner reviews milestones.
- **Part A: co-op world sync.** sm64coopdx-level co-op for OoT and MM. Everyone in the room plays one
  world: same seed and settings, same pots, crates, grass, rocks and drops, and one shared copy of every enemy
  and boss. That copy has the same position, AI state, animation, health, death and drops for everyone, and
  enemy HP scales with the number of players.
- **Part B: cross-play randomizer.** Hyrule and Termina become one randomized world:
  - MM entrance randomizer;
  - entrances, warps and spawns that cross between the games;
  - child Link and MM Link sharing one inventory;
  - every item from either game usable in both, in tiers up to MM transformations in OoT;
  - one logic model and fill over all of it;
  - opt-in "insane" extras.
- **Shared plumbing.** Part B's settings travel through Part A's room-seed sync, so do Part A's first
  milestone first (§16).

---

## 0. Operating rules for the executing agent

Read before touching code: `docs/ARCHITECTURE.md`, `docs/deviations/anchor.md` (all of it — it is the
history of every co-op bug already fixed here), `docs/deviations/README.md`, `docs/UPSTREAM_MERGES.md`.

1. **Branch.** Work on `feat/full-coop-sync`. Never commit to the default branch. One commit per task
   (or per tight group of tasks), using this repo's prefixes: `feat(anchor):`, `fix(anchor):`,
   `docs(fork):`, `ci(fork):`, `style(anchor):`.
2. **Vendored code rules (HM64 principle).** Any edit under `soh/src`, `mm/src`, `soh/soh`, `mm/2s2h`,
   `libultraship` is fenced with `#ifdef COMBO_BUILD`, kept minimal, and deletes no original lines. Prefer
   GameInteractor hooks, VB hooks and `ObjectExtension` over editing decomp source. Non-combo builds
   (standalone soh/2ship) must still compile. Keep the original paths under `#else`.
3. **Document every vendored change** as a new dated section, in the existing style: **Why**, then the
   fixes per file, then "On future merges".
   - Networking and sync changes go in `docs/deviations/anchor.md`.
   - Randomizer, logic, entrance, item and save changes (Part B) go in `docs/deviations/rando.md`.
   - Tracker changes go in `docs/deviations/tracker.md`.
4. **Transport stays launcher-owned.** `namespace ComboAnchor` in `combo/ComboShip.cpp` owns the socket.
   Every inbound packet goes to *both* DLLs (`ReceiveLoop`). Each game filters on `originGame`. New packet
   types are combo-private strings prefixed `COMBO_`. The public relay forwards unknown types; Phase 0
   confirms the details.
5. **Shared code goes header-only under `combo/`.** Both DLLs already include headers from there
   (precedent: `combo/rando/ComboAnchorToast.h`). New shared code goes in `combo/coop/`.
6. **Nothing throws across a DLL boundary.** Every new `extern "C"` export wraps its body in try/catch
   (precedent: `SOH_Anchor_RequestResync`).
7. **Packet hygiene.** Transient packets never set `addToQueue`. High-rate packets set `"quiet": true`.
   Everything respects `roomState.syncItemsAndFlags` and team scoping (`targetTeamId`). Directed replies
   use `targetClientId`, as `PlayerSfx.cpp` and `DamagePlayer.cpp` already do.
8. **Threading.** The launcher receive thread only enqueues. Game state is touched on the game thread.
   Launcher-side work runs on the main thread through the existing per-frame pump (precedent:
   `sResyncPending` drained in `PumpDormant`).
9. **Build and format.** Install the deps and use the commands in `docs/BUILDING_LINUX.md`, adding
   `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. Targets are `libultraship soh 2ship comboui ComboShip`. Use about one
   job per 4 GB of RAM. Run `run-clang-format.sh` before each commit. Windows CI builds with MSVC `/WX` (see
   `a6abbb06` for the narrowing-warning trap), so keep conversions explicit.
10. **Phase gate.** A phase is done when the Linux build is clean, CI is green, the two-instance playtest
    table for that phase passes (§13) and the deviation doc is updated.
11. **Wrong premise?** If a file, hook or line referenced here moved or behaves differently, stop that task.
    Append what you found to the **Findings log** at the bottom, then choose the smallest alternative that
    still follows these rules. Don't improvise a different architecture silently.

---

# Part A: co-op world sync

## 1. Definition of done (Part A)

Two to four players in one Anchor room:

1. **Room seed.** Joining the room automatically gets the host's exact seed plus every setting that shapes
   the world. That covers both games' rando settings and the non-rando enhancement settings (enemy
   randomizer, drop tweaks, and so on). A save on a different seed is blocked from syncing instead of being
   corrupted. The joiner's own config is untouched after they leave.
2. **Same world.** Pots, crates, barrels, grass, rocks, rolling boulders and beehives break once for everyone
   and drop the same thing. Each drop can only be picked up once.
3. **Same enemies.** Every enemy is one shared entity: position, AI state, animation, health, stun or freeze,
   death and drops. Whoever is fighting it drives it. Late joiners see the current state, including what is
   already dead.
4. **Same bosses.** Same as enemies, plus intro cutscenes, multi-part bosses, arena state and death or clear.
5. **HP scaling.** Enemy and boss HP scales with player count. The scaling is configurable per room.
6. **Both games.** OoT and MM both work. Only players in the same game, scene and room share actors. A
   player in the other game is unaffected.
7. **Solo is unchanged.** Solo play, and play with Anchor off, behaves exactly as it does today. Every
   feature is a no-op with no peers.

**Non-goals for this plan:**
- a shared MM 3-day clock (decision point §11);
- NPC dialogue and cutscene state sync;
- push-block physics (block puzzles already resolve through flag sync);
- PvP rework;
- changes to the Anchor server.

---

## 2. Findings: why it desyncs today (verified by reading code at `a6abbb06`)

### 2.1 Seed and settings

- **The seed file carries a lot already.** The consolidated seed JSON (`combo/ComboShip.cpp` ~2078–2139) holds:
  - both games' rando settings (`SOH_DumpRandoSettings`, `MM_DumpRandoSettings`);
  - placements, prices, hints, entrances, goal and shared items.

  Dropping it at file select goes through `Combo_OnReloadRequest` (~2533), which restores settings and
  re-applies placements. So **rando-level** content (shuffled shop items, crate, pot and grass checks)
  matches when that path is used.
- **What the seed file does not carry:**
  - **Non-rando enhancement CVars that change the world.**
    - OoT: `RandomizedEnemies` + `RandomizedEnemyList.*`, `RandomizedEnemySizes`,
      `EnemySizeScalesHealth`, `HyperEnemies`, `NewDrops`, `NoRandomDrops`, `NoHeartDrops`, `FixFlexDrops`,
      `BushDropFix`, `TreesDropSticks`, `DampeDropRate`, `EnableBombchuDrops`, `EnemySpawnsOverWaterboxes`,
      `EnemyRandomizerAirWalkVoidouts`, …
    - MM: `gEnhancements.DifficultyOptions.{NoRandomDrops,NoHeartDrops,HyperEnemies,AlwaysFindRockSirloin}`,
      `gEnhancements.Equipment.ChuDrops`, `gEnhancements.Minigames.RandomizeShootingGalleryOctoroks`, …
    - Task 1.2 does the full audit.
  - **MM Saria-priority list.** `Rando::GetSariaPriorityItemsFromConfig()` is read at slot-bind
    (`mm/2s2h/BenPort.cpp` ~2923) and never dumped.
  - **Absent keys in MM restores.** `MM_RestoreRandoSettings` (`BenPort.cpp` ~3213) doesn't pre-clear keys
    that are missing from the snapshot. OoT's restore does. So partial or older snapshots inherit local values.
- **Unshuffled drops roll the global LCG.** `Item_DropCollectibleRandom` calls `Rand_ZeroOne`:
  - `soh/src/code/z_en_item00.c` ~1652, with the RNG in `soh/src/code/code_800FD970.c`;
  - `mm/src/code/z_en_item00.c` ~1339, with the RNG in `mm/src/boot/O2/rand.c`.

  Every actor's AI advances the same LCG, so drops diverge immediately even with identical settings. This is
  the "crate drops desync" whenever crates, pots or grass aren't shuffled.
- **The enemy randomizer is already seeded** from the rando seed (`soh/soh/Enhancements/ExtraModes/EnemyRandomizer.cpp` ~410).
  It matches only if both players have the same enemy-randomizer CVars, which nothing enforces today.
- **The handoff is manual and unguarded.**
  - The seed must be dropped at file select before the slot is created.
  - A slot already bound to another seed silently keeps it.
  - Nothing stops two different seeds from syncing items and flags into each other's saves.
- **The seed cross-check doesn't work.**
  - MM reports `seed = 0` in client state (`mm/2s2h/Network/Anchor/MMAnchor.cpp` ~367).
  - OoT reports its own ctx seed.
  - The room window only compares peers in the same game (`combo/gui/ComboAnchorRoomWindow.cpp` ~217).
- **Different builds silently stop syncing.** `clientVersion` is the git hash
  (`soh/soh/Network/Anchor/Anchor.cpp`, `OnIncomingJson`). Friends on a different CI artifact lose item and
  flag sync, and the only sign is a log line.

### 2.2 Actors

- **Nothing syncs world actors.**
  - Anchor syncs player puppets, items, flags (switch, clear, treasure, collectible), check status, dungeon
    items, beans, entrances and team-state resync.
  - There's also a set of "break this wall if its switch flag is set" hooks
    (`soh/soh/Network/Anchor/HookHandlers.cpp`).
  - There is **no** enemy, boss or prop sync. Each client simulates its own copy.
- **Team puppets don't collide.** `DummyPlayer.cpp` ~184 returns before `SetAC`/`SetOC` for teammates, so
  enemies only ever interact with the local Link. Keep that property: enemy→player damage stays
  victim-local.
- **Building blocks that already exist:**
  - Stable per-room spawn index `GetActorListIndex()` in both games
    (`soh/soh/ObjectExtension/ActorListIndex.h`, `mm/2s2h/ObjectExtension/ActorListIndex.h`).
  - Actor hooks in both games: `ShouldActorInit`, `OnActorInit`, `ShouldActorUpdate`, `OnActorUpdate`,
    `OnActorKill`, `OnActorDestroy`. MM also has `OnRoomInit`, `OnSceneInit` and `OnBossDefeated`.
  - `ObjectExtension` for per-actor side data, and `ActorMaximumHealth` (soh).
- **Gotcha: OoT spawn index timing.** `Actor_Spawn` sets the index to `-1` and calls `Actor_Init`
  (`soh/src/code/z_actor.c` ~3373/3412) **before** `Actor_UpdateAll` stamps the real index (~2602). The init
  hooks therefore see `-1` for setup actors. Resolve sync keys lazily, on first update. MM sets
  `currentActorListIndex` before spawning, so it is fine.
- **Room-unload kills.** These look like real deaths through `OnActorKill`:
  - OoT `func_80031B14` kills actors whose room is neither the current nor the previous room;
  - MM's analogue is `func_800BA798` (verify).

  Classify those as unloads, not deaths.
- **Damage.** `colChkInfo.health` is `u8` in both games. `Actor_ApplyDamage` (soh `z_actor.c` ~4776, mm
  ~5218) is used by about 40 OoT and 49 MM overlays. About 50 OoT and 83 MM overlays write
  `colChkInfo.health` directly.

---

## 3. Architecture

```
          ┌────────────── Anchor relay (unchanged; local or anchor.hm64.org) ──────────────┐
          │ NUL-delimited JSON frames · room-scoped · targetClientId / targetTeamId          │
          └──────────────────────▲──────────────────────────────▲──────────────────────────┘
                                 │                              │
   ComboShip launcher ─ ComboAnchor (socket, roster)  +  SeedShare (NEW: room seed, chunks, world-CVar overlay)
                                 │ forwards every packet to both DLLs
          ┌──────────────────────┴──────────────┐   ┌──────────────┴───────────────────────┐
          │ soh.dll: Anchor + CoopSync(OoT)     │   │ 2ship.dll: MMAnchor + CoopSync(MM)    │
          │  SessionGuard · WorldRng · Actors   │   │  SessionGuard · WorldRng · Actors     │
          └──────────────────────┬──────────────┘   └──────────────┬───────────────────────┘
                                 └──── combo/coop/*.h (header-only shared core) ───┘
            CoopProtocol · CoopWire (delta codec) · CoopKeys · CoopOwnership · CoopRng · CoopScaling
```

### New packet types

| Packet | Route | When | Payload (`blob` = base64 binary) |
|---|---|---|---|
| *(extension)* `UPDATE_CLIENT_STATE.state.combo` | each client → room | existing triggers | `protocol`, `buildHash`, `layoutHash{oot,mm}`, `seedId`, `settingsHash`, `worldCvarsHash`, `game`, `sceneId`, `roomNum`, `timeBucket` (OoT isNight / MM halfDay), `desyncHash` |
| `COMBO_SEED_OFFER` | host → room | a joiner appears; host seed changes; "Share" button | `seedId`, `masterSeed`, `displaySeed`, `file_hash`, `bytes`, `chunks` |
| `COMBO_SEED_REQUEST` | joiner → host (`targetClientId`) | offer with an unknown `seedId` | `seedId` |
| `COMBO_SEED_CHUNK` | host → joiner (`targetClientId`) | reply | `seedId`, `i`, `n`, `blob` (zlib) |
| `COMBO_WORLD_CVARS` | host → room | joiner appears; host changes a world CVar | `{oot:{k:v}, mm:{k:v}}`, `hash` |
| `COMBO_SYNC_CONFIG` | room owner → room | join; change | toggles, boss mode, HP-scale factors |
| `COMBO_ACTOR_SNAP` | owner → room (`quiet`) | 1–20 Hz per actor | `game`, `sceneId`, `roomNum`, `frame`, `blob` (batched entries) |
| `COMBO_ACTOR_EVENT` | any → room | kill / despawn / spawn / break / lift / throw / pickup / light | `game`, `sceneId`, `roomNum`, `events[]` |
| `COMBO_ROOM_REQUEST` | entering client → room | room enter | `game`, `sceneId`, `roomNum`, `timeBucket` |
| `COMBO_ROOM_STATE` | room authority → requester (`targetClientId`) | reply | dead keys, dynamic spawns, keyframes, owners, temp-clear |

Extending `UPDATE_CLIENT_STATE` is safe: its own header comment requires `contains` checks for added fields.

---

## 4. Phase 0: groundwork and test harness

- [ ] **0.1 Local relay + server facts.**
  - Run the Anchor server locally. Upstream is `github.com/garrettjoecox/anchor` (Go): `go run .`, or
    `docker run -p 43383:43383 ghcr.io/garrettjoecox/anchor:latest`.
  - Read its source and record in the Findings log:
    - (a) whether unknown types are relayed;
    - (b) max frame size and buffer limits;
    - (c) `targetClientId` / `targetTeamId` semantics (server-side routing or client-side filter?);
    - (d) `addToQueue` semantics;
    - (e) rate limits;
    - (f) whether fields are rewritten or stripped.
  - Size seed chunks (§5) and snapshot batches (§7) from (b) and (e).
- [ ] **0.2 Two-instance harness.** Write `scripts/coop/run-two-instances.sh` (and a `.ps1` for Windows). It
  launches two ComboShip instances with separate `SHIP_HOME` dirs (see `docs/BUILDING_LINUX.md`), Anchor host
  `127.0.0.1`, distinct names and colors, and the same room and team.
- [ ] **0.3 Protocol header.** Add `combo/coop/CoopProtocol.h` with `kCoopProtocol = 1`, the packet-name
  constants and helpers that stamp the `combo` metadata.
- [ ] **0.4 Byte counters.** Add per-packet-type byte and packet counters to ComboAnchor send/recv. Expose them
  through an export.
- [ ] **0.5 "Co-op Sync" debug window** in each DLL's ImGui (actor data lives there), behind CVar
  `gRemote.Anchor.Coop.Debug`. It shows:
  - each synced actor: key, actor id, owner, epoch, mode, snapshot age, HP and scale k;
  - bytes/s in and out by type;
  - peer table with seed, protocol and layout status;
  - desync-hash mismatches.

**Accept:** two local instances join one room, see each other's puppets, and the debug window shows live
byte counts.

---

## 5. Phase 1: room seed and world-settings sync over Anchor

- [ ] **1.1 Refactor the reload path.** Split `Combo_OnReloadRequest(path)` into file resolution plus
  `Combo_ApplySeedJson(const nlohmann::json&, SeedApplyMode)`, with three modes:
  - `SilentAutoLoad`: today's null path;
  - `ExplicitDrop`: today's non-null path;
  - `RoomShare`: new. It behaves like ExplicitDrop for the seed, but world CVars are a session overlay (1.3).
    The file is written with `WriteComboSpoiler`/`RememberComboSpoiler`.

  Behavior-preserving refactor. Keep every existing comment and ordering rule (goal before pool prep, prices,
  the user-settings snapshot and restore). It stays main-thread-only.
- [ ] **1.2 World-CVar audit and registry.** Create `combo/coop/WorldCvars.h` with `kWorldCvarsOot[]` and
  `kWorldCvarsMm[]`. Each entry has key, type, default and a one-line note.
  - **Method:** enumerate every `CVarGet*` read (`CVAR_ENHANCEMENT(...)`, `CVAR_CHEAT(...)`,
    `"gEnhancements.*"`, `"gCheats.*"`) under `soh/src`, `soh/soh/Enhancements`, `mm/src` and `mm/2s2h`.
  - **Classify** each read:
    - **WORLD**: changes what spawns, drops, how actors behave, damage, timers or puzzle state. Synced.
    - **LOCAL**: cosmetics, audio, UI, controls, camera, QoL that affects only the local player. Not synced.
  - Cheats default to LOCAL. Seed the list with §2.1. Add a table of the registry to the deviation doc.
- [ ] **1.3 Bake world CVars and identity into the seed.**
  - At generation add these fields to the consolidated JSON:
    - `consolidated["worldCvars"] = {oot:{...}, mm:{...}}`;
    - `settingsHash`, `worldCvarsHash`;
    - `seedId`: a 64-bit FNV-1a or xxHash of a canonical dump of masterSeed, placements, settings,
      worldCvars, entrances, prices and hints.
  - **Applying `RoomShare`:** snapshot the user's values for every registry key, apply the host's values, and
    restore them on disconnect, room leave or return to title.
  - **Applying `ExplicitDrop`:** keep the current semantics (the dropped seed becomes the baseline) and add
    world CVars to it.
  - **Config persistence.** Find where libultraship persists CVars to `comboship.json` and make sure overlay
    values are never written there. For example: restore the user snapshot, save, then re-apply the overlay;
    or keep overlay keys out of the save. Verify the mechanism and record it in the Findings log.
  - **Old seeds** without `worldCvars` apply nothing for that block. Log it once.
- [ ] **1.4 Settings gaps.**
  - Make `MM_RestoreRandoSettings` pre-clear, mirroring `SOH_RestoreRandoSettings`.
  - Dump and restore the MM Saria-priority list.
  - Confirm OoT `enabledTricks` round-trips. It is in the seed per `rando.md`; verify restore order.
- [ ] **1.5 Seed identity in client state.**
  - Add the `combo` object (§3) to `Anchor::PrepClientState` (`Packets/UpdateClientState.cpp`) and to
    MMAnchor's client-state builder (~367). MM's `seed` becomes the combo `masterSeed`.
  - Parse it in the launcher roster (`ParseClient`, `combo/ComboShip.cpp` ~993) and in each game's client map.
- [ ] **1.6 SeedShare (launcher).** Add new `combo/coop/SeedShare.{h,cpp}`, compiled into `ComboShip`.
  - **Host** = `roomState.ownerClientId`, read through the existing `sSohAnchorGetOwnerInfo` export.
  - **Only the host offers.** A host with no bound or loaded seed doesn't offer, and the room shows
    "no room seed".
  - **Offer triggers:** a new team peer appears in ALL/UPDATE_CLIENT_STATE; the host generates or loads a seed;
    the "Share my seed" button.
  - **Seed index.** Index local seed files in `ComboRando::ConsolidatedDir()` by `seedId`. A joiner that
    already has the seed skips the transfer.
  - **Transfer.** Otherwise: REQUEST, then chunks (zlib; chunk size from Phase 0, default ≤ 32 KiB base64).
    Validate indices, reassemble, verify the `seedId` hash, write the file.
  - **Compression.** zlib is likely already linked through libzip/libpng; confirm. Fall back to miniz if
    vendored, else send uncompressed.
  - **Threading.** Receive-thread code only enqueues. Apply on the main thread via the per-frame pump.
- [ ] **1.7 Joiner state machine and UX** (comboui toast and modal):

  | Joiner state | What happens |
  |---|---|
  | Title or file select, no save loaded | Auto-apply `RoomShare`. Toast "Loaded \<host\>'s room seed" with the hash icons. Fill the Generate panel (reuse the `g_ComboProgress` block at the end of `Combo_OnReloadRequest`). |
  | Creates a new file | Binds the room seed through the existing `Combo_OnOOTSaveInit`. |
  | Loads a save with the same `seedId` | Normal play. |
  | Save loaded (or loading) with a different `seedId` | SessionGuard blocks it. Modal: "This save is on a different seed than the room. Return to file select to start a file on the room seed, or disconnect." Never rewrite a bound slot automatically. |
  | Host switches seed mid-session | New offer. Joiners in-game get the modal. |

- [ ] **1.8 SessionGuard (both DLLs, shared helper in `combo/coop/`).**
  - **Drop these packets** from peers whose `seedId` or `protocol` differs from ours:
    - `GIVE_ITEM`, `SET_FLAG`, `UNSET_FLAG`, `SET_CHECK_STATUS`;
    - `UPDATE_TEAM_STATE` and `REQUEST_TEAM_STATE` replies;
    - `COMBO_CROSS_ITEM`, `UPDATE_DUNGEON_ITEMS`, `UPDATE_BEANS_COUNT`, `ENTRANCE_DISCOVERED`;
    - all `COMBO_ACTOR_*` and `COMBO_ROOM_*`.

    Check next to the existing `clientVersion` check in `Anchor::OnIncomingJson`, and in
    `MMAnchor::OnIncomingJson`.
  - **Actor and room packets also require a matching `layoutHash`** for that game.
  - **Peers without `combo` metadata** (old builds): treat them as mismatched for actor and world packets and
    keep legacy behavior for the rest. Document this.
  - **Make mismatches visible.** Build, seed and layout mismatches show as a warning in the room window, not
    just a log line.
- [ ] **1.9 Room window** (`combo/gui/ComboAnchorRoomWindow.cpp`): room seed (hash icons and displaySeed),
  per-peer seed, protocol and layout status, "Share my seed" for the host, "Use room seed" for joiners at
  file select.

**Accept:**
- **Setup.** The host has crates unshuffled, `NewDrops` on and the enemy randomizer on. The joiner has
  defaults and no seed.
- **Expected:**
  - the joiner receives the seed automatically and creates a file;
  - both clients show identical `settingsHash`/`worldCvarsHash` and identical enemy-randomizer picks in three
    sample rooms;
  - after disconnecting and restarting, the joiner's `comboship.json` is byte-identical to before;
  - a different-seed save is blocked by the modal, and nothing is written into it.

---

## 6. Phase 2: deterministic world RNG

Goal: make all event-driven randomness (drops, break scatter) identical on every client without streaming it.

- [ ] **2.1 RNG state seam (vendored, COMBO_BUILD, about 2 lines each).**
  - Add `u32 Rand_GetState(void)` next to `Rand_Seed` in `soh/src/code/code_800FD970.c` and
    `mm/src/boot/O2/rand.c`.
  - Add prototypes in each game's `functions.h`.
- [ ] **2.2 Scoped RNG.** In `combo/coop/CoopRng.h`, add `CoopRngScope`. It:
  - saves the LCG state;
  - seeds `Derive(seedId, game, sceneId, roomNum, syncKey, eventCounter)` (splitmix64 → u32);
  - **restores** the LCG on destruction, so the local AI's RNG stream is not perturbed.
- [ ] **2.3 Drop scopes.**
  - **Seam.** Wrap `Item_DropCollectibleRandom`, `Item_DropCollectible` and `Item_DropCollectible2` in both
    games with a fenced entry/exit seam: `Combo_RngEnter(fromActor, spawnPos)` / `Combo_RngLeave()`.
    - Key off `fromActor`'s sync key, or a quantized `spawnPos` when it is NULL.
    - Active only while connected with world sync on.
  - **What this covers:** enemy death drops, pot, crate and grass scatter, and the EnItem00 velocity and
    rotation rolls inside those calls (OoT `z_en_item00.c` ~706/~1320/~1761 and the MM equivalents).
  - **Other prop RNG.** Any prop that draws random contents *outside* those functions gets its own scope at its
    break site. Find them while building the Phase 4 prop list.
- [ ] **2.4 Event counter.** Count drop events per sync key per room visit. Synced kill and break events carry
  the counter, so a late applier derives the same drop.
- [ ] **2.5 Verify the enemy randomizer** is identical across clients once Phase 1 lands. It already seeds from
  the rando seed.

**Accept:** each client breaks its *own* copy of the same unshuffled crate and pot (Phase 4 adds real
break sync). Drop type, count and scatter are identical across clients and across repeated runs. The local AI
RNG is unaffected: an enemy-behavior regression run with sync off matches baseline.

---

## 7. Phase 3: actor sync core (enemies)

### 7.1 Registry and eligibility
- **Eligible:** `ACTORCAT_ENEMY`, then `ACTORCAT_BOSS` (Phase 5) and the prop list (Phase 4).
- **Never eligible:**
  - the player and puppets: OoT `ACTOR_EN_OE2` with `DummyPlayer_Update`; MM's re-tagged dummy;
  - NPCs;
  - `EnItem00` (handled as drops in Phase 4).
- **Gold Skulltulas** keep their existing flag-kill hooks and also get transform sync.
- **Per-actor state.** An `ObjectExtension` record `CoopSyncState` holds: key, mode, owner, epoch, claim-lock
  deadline, last applied frame, the pending snapshot, dynamic/matched/dead flags, eventCounter, HP-scale k and
  the damage carry.

### 7.2 Sync keys (`combo/coop/CoopKeys.h`)
- **Setup actors:** `hash(game, sceneId, actor->room, GetActorListIndex(actor), actor->id, spawnParams)`.
  - Capture `spawnParams` in `ShouldActorInit`, before Init can change `params`.
  - Resolve the key **lazily on first update** (see the OoT index-timing gotcha in §2.2).
- **Dynamic actors** (index −1): `hash(parentKey, id, params, ordinal)`.
  - `parentKey` is the key of `actor->parent` if that is synced. Otherwise use the key of the actor whose
    update spawned it, tracked by pushing and popping `gCoopUpdatingActor` around updates in
    `ShouldActorUpdate`/`OnActorUpdate`. Otherwise use a room-level spawner key.
  - `ordinal` counts `(parentKey,id,params)` per room visit.
- **Divergence rule.** RNG-dependent spawns can still diverge. The **room authority's** dynamic actors are
  canonical:
  - the authority emits `spawn` events;
  - other clients bind an unbound local candidate (same id and params, same parent, within 300 units) or
    spawn one (`Actor_Spawn` / `Actor_SpawnAsChild`);
  - non-authority dynamic enemies left unbound for more than 1 s are removed silently.

### 7.3 Ownership (`combo/coop/CoopOwnership.h`: pure functions, unit tested)
- **Candidates:** peers in the same game, scene, room and `timeBucket` (§11).
- **Default owner:** the nearest candidate, using the local Link or each puppet's PLAYER_UPDATE pose.
  - Switch only when the challenger is at least 30% closer for at least 10 frames (hysteresis).
  - Because enemies chase whoever owns them, this gives "chase the nearest player" behavior for free.
- **Combat claim.** A local hit takes ownership immediately:
  - detection: `colChkInfo.damage > 0` before update, or health dropped across the local update;
  - epoch + 1, with a claim lock of 1.5 s (3 s for bosses) refreshed on each hit.
- **Interaction claim.** Local hookshot, boomerang, carry or grab involving the local player also claims:
  `player->heldActor`, `actor->parent == local player`, and similar.
- **Conflicts:** higher epoch wins. On a tie, the lower `clientId` wins. Every snapshot carries
  `(epoch, owner)`. Receiving a higher epoch for an actor you own demotes you.
- **Owner leaves** the room or scene, disconnects or goes idle for more than 1 s: each client re-evaluates.
  The winner takes over at epoch + 1.
- **Room authority:** the room owner if present, otherwise the lowest `clientId` in the room. It answers
  `COMBO_ROOM_REQUEST` and is canonical for dynamic spawns.

### 7.4 What gets replicated: generated per-actor field tables
- **Generator.** `scripts/coop/gen_actor_sync_fields.py`, using libclang (`pip install libclang`):
  - Parse every `soh/src/overlays/actors/ovl_*/z_*.h` and `mm/src/overlays/actors/ovl_*/z_*.h`, with
    include flags from `build-cmake/compile_commands.json` for that overlay's `.c`.
  - Find the instance struct: its first member is `Actor actor`, `DynaPolyActor dyna` or similar.
  - Map it to its `ACTOR_*` id by reading the overlay `.c`'s `ActorInit`/`ActorProfile` `.id` and
    `.instanceSize`. That is 428 OoT and 574 MM overlay dirs.
- **Field kinds:**

  | Kind | Rule | Wire |
  |---|---|---|
  | `RAW` | integers, floats, enums, `Vec3f/Vec3s/Vec3i`, arrays and pointer-free structs of these | bytes |
  | `FUNC` | function pointers (`*ActionFunc` typedefs, any fn ptr) | u32 offset from the owning DLL's image base. Receiver rebases and checks the result is inside its own `.text`; otherwise skip and log once per actor id. |
  | `ASSET` | `AnimationHeader*`, `Gfx*`, `TexturePtr`, `const char*` asset pointers | module-relative if inside the DLL image, else skip |
  | `SKEL` | `SkelAnime` (and MM variants) | `animation` (ASSET), `curFrame`, `playSpeed`, `startFrame`, `endFrame`, `animLength`, `mode`, `morphWeight`, `morphRate`, `moveFlags`. Skip skeleton, jointTable and morphTable pointers and `limbCount`. |
  | `SKIP` | `Collider*` structs and element arrays, `Actor*` and other heap pointers, `LightNode*`/`LightInfo`, `BodyBreak`, effect/particle buffers holding pointers, any union with a pointer | not sent |

- **Name heuristics that force `SKIP`.** These are client-local indices that crash if copied. Case-insensitive
  regex:
  `cam(era)?(id|idx|index)|subcam|cscam|effect(index|idx|id)|blure|objslot|objbank|bankindex|objectslot|objectindex|requiredobj|lightnode`.
  Embedded `jointTable`/`morphTable` arrays are `RAW` but sent only in keyframes.
- **Overrides** live in `scripts/coop/actor_sync_overrides.json` (checked in). Per actor:
  - `mode`: `full | transform | events | off`;
  - extra `skip` and `force` field lists;
  - `hpScale`: `damage | refund | off`;
  - notes.

  Initial values:
  - every `ACTORCAT_BOSS` = `transform` until its Phase 5 card is green;
  - the grabbers in §7.8 = `transform`;
  - everything else = `full`.
- **Output.** Write `soh/soh/Network/Anchor/Sync/ActorSyncFields.gen.cpp` and
  `mm/2s2h/Network/Anchor/Sync/ActorSyncFields.gen.cpp`.
  - Entries are `offsetof`-based, so each compiler computes the layout and it is correct on MSVC, clang and gcc.
  - At startup, check the struct size against the overlay's `instanceSize`. On mismatch, disable that actor
    and log.
  - Emit a `kActorSyncLayoutHash`.
  - If headers collide in one TU, split into chunks of about 40 headers.
  - Generated files are **checked in**; CI doesn't need Python. Add `scripts/coop/regen.sh` and an optional
    CI "generated files up to date" check.
- **Base `Actor` fields** are hand-written per game in `CoopBaseFields.inl`:
  - **Sync:** `world.pos/rot`, `shape.rot`, `shape.yOffset`, `shape.shadowAlpha`, `velocity`, `speedXZ`
    (MM: `speed`), `gravity`, `minVelocityY`, `scale`, `focus`, `colChkInfo.health`, color-filter
    timer and params (the hit flash), `freezeTimer`, and `flags & kCoopActorFlagMask` (targetable, hostile,
    lens and invisible bits; define per game). `home` only in keyframes.
  - **Never sync:** list links, `parent`/`child`, the init/update/draw/destroy pointers, `overlayEntry`,
    object slot or bank index, `room`, `category`, `isDrawn`, `projectedPos`, distance-to-player fields,
    culling fields.

### 7.5 Wire format (`combo/coop/CoopWire.h`)
- **Batching.** One `COMBO_ACTOR_SNAP` per (game, scene, room) per send tick, `quiet: true`.
- **Blob layout:** a varint stream of entries `{keyHash u64, epoch u16, flags(keyframe|delta|dead), fieldBitmap, bytes}`.
- **Deltas** are against the last state sent. TCP is reliable and ordered, so deltas are safe.
- **Keyframes** go out on owner change, when a new peer enters the room (seen via client state or ROOM_REQUEST),
  and every 2 s.
- **Rates:**
  - 20 Hz (every logic frame) when within 600 units of any non-owner or claim-locked;
  - 5 Hz otherwise;
  - 1 Hz keyframe-only when more than 2000 units from every peer.
- **Budget:** at most 25 KB/s up per client in a room with eight enemies. Measure with the Phase 0 counters and
  tune rates before widening coverage.

### 7.6 Applying snapshots on receivers
- **When.** Keep the latest snapshot per key and apply it in `ShouldActorUpdate` (pre-update), so the local AI
  continues from the authoritative state. Never apply when `init != NULL` or `update == NULL`.
- **Smoothing.**
  - Position: under 150 units of error, blend 50% per frame for 3 frames; at or above it, snap.
  - Rotation: shortest-arc lerp.
  - Everything else: overwrite.
- **Retarget trick.** `Actor_UpdateAll` fills `xzDistToPlayer`, `yDistToPlayer` (MM: `playerHeightRel`),
  `xyzDistToPlayerSq` and `yawTowardsPlayer` against the **local** Link. For non-owned synced actors,
  recompute those against the **owner's puppet** in `ShouldActorUpdate`. Between snapshots the local AI then
  chases the same player the owner's AI chases.
  - Code that reads `GET_PLAYER(play)` directly still sees the local Link; snapshots correct that.
  - Document this limitation.
- **Damage to players stays victim-local.** The enemy's attack collider on *your* client hits *your* Link.
  Puppets never take or forward enemy damage. Keep `DummyPlayer`'s teammate early-return. For cross-team PvP,
  filter puppet AC hits to player-sourced AT only.

### 7.7 Lifecycle events (`COMBO_ACTOR_EVENT`)
- **`kill`.**
  - **When it fires:** `OnActorKill` on the owner, or on any client whose own hit took health to 0. Ignore it
    during scene or room transitions (flag set on scene init / room change / `OnPlayDestroy`) and when the
    actor's room is being unloaded (§2.2).
  - **Payload:** `{key, eventCounter}`.
  - **Receivers:** if the snapshot has already driven the actor to death, let the local AI play it out (drops
    come from `CoopRng`). Otherwise force `Actor_Kill` after 15 frames with no drops. Either way, add the key
    to the room's dead-set.
- **Kill-linked rando checks are granted only on the killer.** Examples: MM `RO_SHUFFLE_ENEMY_DROPS` and
  `RO_SHUFFLE_ENEMY_SOULS` (see `mm/2s2h/Rando/ActorBehavior/*`), plus any OoT equivalent. Receiver-side
  deaths must not grant: expose `Coop_IsRemoteDeath(actor)` and gate the grant on it. A double grant would
  desync the check tracker.
- **`despawn`** is a non-death removal (Leever burrow, a flee off-screen). Same path, without the dead-set.
- **`spawn`** is a dynamic actor from the room authority (§7.2).
- **Dead-set lifetime.** It persists while at least one teammate is in the room and clears once the room is
  empty of teammates. That matches vanilla respawn-on-re-entry.

### 7.8 Special cases (use overrides, plus small per-actor handlers only where needed)
- **Grabbers and holders.** OoT: Like Like `EnRr`, Wallmaster `EnWallmas`, Floormaster `EnFloormas`,
  ReDead/Gibdo `EnRd`, Dead Hand `EnDh`/`EnDha`. MM: the same families plus the item thief `EnThiefbird`.
  - Derive the full list by grepping overlays for player-grab and player-state writes.
  - Grab states run only on the owner, against the owner's own Link. Receivers render the grab on the owner's
    puppet, or hold the idle pose.
  - Item theft affects only the victim.
- **Enemies that read the player's input or equipment:** Dark Link `EnTorch2` (mirrors input), shield and sword
  checks in Stalfos and Lizalfos. Use `transform` mode on receivers.
- **Spawners.** Encounter actors, night Stalchildren, Keese and Tektite spawners, Leevers: authority-only.
  If generic matching (§7.2) proves noisy, block spawner children on non-authority clients in
  `ShouldActorInit`.
- **Stun and freeze** (Deku nut, ice arrow) ride on `freezeTimer`, color filter and `RAW` damage-effect fields.
  No special code expected; verify.

**Accept (enemy core):**
- **Enemies to test:**
  - OoT: Deku Babas (Deku Tree), Stalfos (Forest Temple), Lizalfos (Dodongo's Cavern), Peahat
    (Hyrule Field), ReDeads (Kakariko well).
  - MM: Deku Babas (Woodfall), Snapper, ReDead/Gibdo (Ikana), Dodongo.
- **Pass criteria:**
  - steady-state position error under 30 units;
  - HP matches after hits from either player;
  - a kill on one client produces the same death and drop on the other;
  - a late joiner sees the dead-set;
  - owner handoff mid-fight with no visible teleport (at most 1 frame).

---

## 8. Phase 4: props and world objects

- [ ] **Build the authoritative prop list.** Combine three sources:
  - (a) `Item_DropCollectibleRandom` callers in each game that are not ENEMY or BOSS;
  - (b) actors named in the rando shuffle files: `soh/soh/Enhancements/randomizer/ShufflePots.cpp`,
    `ShuffleRocks.cpp`, `ShuffleFreestanding.cpp`, `ShuffleFairies.cpp`, `ShuffleIcicles.cpp`, the crate and
    grass shuffles, and `mm/2s2h/Rando/ActorBehavior/*` for MM;
  - (c) known world movers.

  Starting set (verify names): pots `ObjTsubo` (MM: + `ObjFlowerpot`), crates `ObjKibako`/`ObjKibako2`, MM
  barrels `ObjTaru`, grass and bushes (OoT `EnKusa`; MM grass actors), rocks `EnIshi`, bomb rocks
  `ObjBombiwa`/`ObjHamishi`, **rolling boulders `EnGoroiwa`**, beehives `ObjComb`, MM trees `ObjTree`, signs
  `EnKanban`, torches `ObjSyokudai`, MM snowballs.
- [ ] **Events:** `break {key, eventCounter, by}`, `lift {key, by}`, `throw {key, by, pos, vel}`,
  `light {key}`, `pickup {dropKey}`.
- [ ] **Applying a remote break.** Use a per-actor adapter table that drives the actor's *own* break path.
  - Preferred: inject an AC hit, which is the proven pattern in `HookHandlers.cpp`
    (`collider.base.acFlags |= AC_HIT`).
  - Otherwise call the known break function.
  - Last resort: `Actor_Kill` plus the break effect.
  - The drop comes from Phase 2 RNG keyed by the event, so it is identical without being sent.
- [ ] **Pickups.**
  - Collectible-flagged drops already vanish through the existing `EnItem00` flag hook.
  - Unflagged drops get `dropKey = hash(sourceKey, eventCounter, dropIndex)`. A `pickup` event kills the
    matching drop elsewhere. Accept a rare double-pickup race: first event received wins locally.
  - Rando-shuffled pot, crate and grass items grant only to the picker. Other copies vanish through the
    existing check-obtained / `VB_ITEM00_DESPAWN` paths; verify with a test.
- [ ] **Carried and thrown props.** The carrier owns the prop and streams its transform while held and in
  flight.
- [ ] **Rolling boulders** (`EnGoroiwa`): the room authority streams `full` (path index and progress are `RAW`).

**Accept:**
- OoT: Kakariko pots, Lon Lon crates, Hyrule Field grass, Goron City rolling boulders, Kokiri rocks.
- MM: Clock Town pots and crates, Termina Field grass, barrels.
- Pass criteria: each prop breaks once for both players, drops are identical, and a pickup removes the drop
  for the other player.

---

## 9. Phase 5: bosses and mini-bosses

Turn bosses on one at a time. Each boss gets a **playtest card** in `docs/deviations/anchor.md` with the mode
shipped, what was verified and known issues. A boss flips from `transform` to `full` only when its card is
green.

**OoT bosses:**
- Gohma `BossGoma` (+ larvae `EnGoma`)
- King Dodongo `BossDodongo`
- Barinade `BossVa` (many parts)
- Phantom Ganon `BossGanondrof` (+ horse and projectiles)
- Volvagia `BossFd`/`BossFd2`
- Morpha `BossMo` (+ water level)
- Bongo Bongo `BossSst` (+ hands)
- Twinrova `BossTw`
- Ganondorf `BossGanon`
- Ganon `BossGanon2`

**OoT mini-bosses:** Dark Link `EnTorch2`, Iron Knuckle `EnIk`, Dead Hand `EnDh`, Lizalfos/Dinolfos `EnZf`,
Big Octo `EnBigokuta`, Flare Dancer `EnFd`, Stalfos.

**MM bosses:**
- Odolwa `Boss_01`
- Twinmold `Boss_02`
- Gyorg `Boss_03`
- Goht `Boss_Hakugin`
- Majora `Boss_07`
- `Boss_04/05/06`: identify them from source.

**MM mini-bosses:** verify names. Gekko, Wizrobe, Igos du Ikana and knights, Iron Knuckle, Garo Master,
Captain Keeta, Dinolfos, Big Poe, Eyegore, Takkuri.

**Generic boss rules:**
1. **Intro cutscenes.**
   - Each client plays its own intro on entry.
   - While a client's intro cutscene or boss sub-camera is active, it neither applies snapshots for that boss
     nor claims it. When the intro ends it requests a keyframe and joins the fight.
   - If the room state says the fight has already started, skip the joiner's intro through the game's own
     "intro watched" flags.
2. **Multi-part bosses.** Each part is a dynamic child keyed by parentKey + ordinal. Cross-part pointers stay
   local (`SKIP`), and state flows through each part's `RAW`/`FUNC` fields.
3. **Cameras.** Camera and cutscene fields are always `SKIP`. If a camera index hides in an `unk_` field (it
   shows up as a crash or a garbage camera in playtests), add it to the overrides.
4. **Arena actors** must be on that scene's sync list: Morpha water, Volvagia platforms, Gyorg water, Twinmold
   sand, Bongo drum.
5. **Ownership.** Use a longer claim lock (3 s) and longer hysteresis for bosses.
6. **Death.**
   - Receivers reach the death state through snapshots and run their own death cutscene.
   - Clear flags, heart container and blue warp come through the existing flag and item sync.
   - Verify that `GAME_COMPLETE` (OoT `OnBossDefeat` for Ganon2) and MM `OnBossDefeated` feed the combo
     completion latch exactly once per client, with no double fire.
7. **Room setting** `bossMode`: `full | healthAndDeath | off`. Untested bosses run `healthAndDeath`
   regardless.

**Order:**
- OoT: Gohma → King Dodongo → Phantom Ganon → Morpha → Volvagia → Barinade → Bongo Bongo → Twinrova →
  Ganondorf → Ganon.
- MM: Odolwa → Goht → Gyorg → Twinmold → Majora.

---

## 10. Phase 6: enemy and boss HP scaling by player count

- **Scale incoming damage, not health.** Health is `u8` and boss phases trigger on health thresholds, so
  raising health breaks both. For an actor with factor *k*, applied damage = ⌊(damage + carry) / k⌋, and the
  fractional carry is kept on the actor so every hit makes progress over time.
- **Seam.** In `ShouldActorUpdate` (pre-update), rewrite `colChkInfo.damage`.
  - Collision check fills that field before the actor's update.
  - Both `Actor_ApplyDamage` callers and direct `health -= colChkInfo.damage` writers read it.
  - So one generic seam covers most actors.
- **Constant-damage actors.**
  - Sweep the overlays that subtract constants or bypass `colChkInfo.damage`: about 50 OoT and 83 MM files
    write `colChkInfo.health` directly, and bosses often do this.
  - For those, use `hpScale: refund` in the overrides: after the update, give back Δ·(1 − 1/k) using the
    carry, but never when the hit already triggered death. Mark kills with `off` when refund is unsafe.
- **Factor.**
  - k = 1 + s·(n − 1), where n = number of team peers in the same game and scene.
  - Enemies: freeze *n* on first sync. Bosses: freeze it when the fight starts (after the intro).
  - *s* comes from the room config. Defaults: enemies 0.5, bosses 0.75. Range 0–2, with an off switch for
    each.
  - The owner's *k* rides in snapshots and receivers use it, so local hits scale identically everywhere.
- **Leave alone:** instakill and finisher mechanics such as the Ganon finishing blow. Handle these per boss in
  the overrides.
- **Health bars** (OoT `EnemyHealthBar`, MM `gEnhancements.Graphics.EnemyHealthBars`) still show the right
  fraction.

**Accept:**
- With 2 players and s = 1, a Stalfos takes twice the hits.
- Boss phase transitions still fire at the right points.
- Solo play and Anchor-off play are exactly unchanged.

---

## 11. Phase 7: time of day (decision point)

- **Why it matters.**
  - OoT night/day changes spawns: Stalchildren, guards.
  - MM setup actors are filtered by half-day (`Actor_SpawnSetupActors`, `halfDaysBit`), and each player runs
    their own 3-day clock.
- **Option A (this plan's default).** Pair-gating only: actors are shared only between peers with the same
  `timeBucket`. The room window shows a "different time of day" badge.
- **Option B (future, separate plan).** A room clock authority while players share a scene: OoT `dayTime`, MM
  time and day. This touches MM cycle mechanics heavily. Don't attempt it here.

---

## 12. Phase 8: settings, UI, docs

- [ ] **Room-level config.** `COMBO_SYNC_CONFIG` comes from the room owner. Fields: world sync master switch,
  enemies, props, `bossMode`, deterministic drops, HP-scale on/off and factors for enemies and bosses,
  seed auto-share. Joiners show these values read-only.
- [ ] **Local-only settings:** debug window, partner-feed toggles (already exist), and auto-accept of room
  seeds.
- [ ] **UI placement.** New "World Sync" section in the combo **Shared > Settings > Network** panel
  (`combo/gui/ComboMenu.cpp`), owner-gated like the existing room-admin controls.
- [ ] **Docs.**
  - `docs/deviations/anchor.md`: dated sections per phase and the boss cards.
  - `README.md`: update the "What this fork changes" list and the "Both players need a build of this fork"
    note (now enforced by the protocol and layout check).
  - `docs/UPSTREAM_MERGES.md`: notes for every vendored seam (`rand.c`, the `z_en_item00.c` drop seam, damage
    seam, any `z_actor.c` touch).

---

## 13. Testing and verification

- **Unit tests.** New headless target `coop_tests`, linking no game DLL. It covers:
  - wire codec round-trip and fuzz;
  - ownership scenarios: handoff, ties, epochs, disconnect, room leave;
  - key derivation stability;
  - RNG scope save and restore;
  - damage-scaling carry math;
  - SeedShare chunk reassembly with dropped, duplicated and out-of-order chunks.
- **Desync detector.** Every 2 s each client hashes `(key, round(pos/10), health, FUNC offsets)` for synced
  actors in its room and publishes `desyncHash` in client state. Mismatches show in the debug window with the
  first differing key.
- **Two-instance playtest matrix,** one table per phase, run with the Phase 0 harness:

  | Area | OoT | MM |
  |---|---|---|
  | Seed/settings | fresh join, mismatched save, host re-seed, restart persistence | same, with an MM-start seed |
  | Props/drops | Kakariko pots, LLR crates, HF grass, GC boulders | Clock Town pots/crates, TF grass, barrels |
  | Enemies | Deku Tree, DC Lizalfos, Forest Stalfos, HF Peahat (day) / Stalchildren (night), Well ReDeads | Woodfall, Swamp, Ikana, Snowhead |
  | Bosses | per §9 order | per §9 order |
  | Split games | one player in each game: no interference, game-switch toast still works | — |
- **Soak.** Two instances in a dense room for 30 min. Track memory growth, bytes/s and desync count.
- **Regression.** Solo play, Anchor off and same-room item/flag/cross-item/resync behavior all unchanged. Re-run
  the playtest notes recorded in earlier `anchor.md` sections.
- **CI.** Windows MSVC `/WX`, Linux and macOS all green.

---

## 14. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Copying a field that is really a client-local index or pointer crashes the receiver | `SKIP` heuristics, `FUNC`/`ASSET` range validation, per-actor overrides, `transform` default for bosses, desync detector, start with 5 enemies |
| Relay frame-size or rate limits | Phase 0 measurement, chunking, batching, adaptive rates |
| Overlay world CVars leak into the user's config | snapshot and restore around config saves (1.3), restart-persistence acceptance test |
| Double rando grants on synced deaths or pickups | `Coop_IsRemoteDeath` gate, picker-only grants, tests in §8 |
| Header collisions in the generated TU | chunked TUs |
| MSVC/gcc layout differences | `offsetof` in compiled code, plus a runtime `instanceSize` check |
| MM half-day mismatch spawns different actor sets | `timeBucket` pair-gating (§11) |
| Upstream merges touching seams | fenced, minimal seams documented in `UPSTREAM_MERGES.md` |

---

## 15. Decisions taken by default (owner can override)

1. **A save on a different seed is blocked and prompted,** never auto-switched.
2. **Room world-CVar overlay is session-only.** The joiner's config is restored on leave.
3. **HP scaling defaults** are s = 0.5 for enemies and s = 0.75 for bosses. n counts teammates in the same
   game and scene.
4. **Time of day** uses pair-gating; a shared clock is out of scope.
5. **Boss default** is `healthAndDeath` until a boss's card is green, then `full`.
6. **Enemy targeting** is "chase the owner" (the nearest player, via ownership) plus the retarget trick. There
   is no per-actor rewrite to target arbitrary players.

---

## 16. Milestones, both parts (each a PR-sized, CI-green chunk with its playtest table filled in)

**Part A milestones:**
- **A-M1:** Phases 0 + 1, room seed and world settings. Shippable alone. It fixes the reported shop, crate and
  boulder desync, and it is the transport for every Part B setting.
- **A-M2:** Phase 2, deterministic drops.
- **A-M3:** Phase 3 core, with the generator and five enemies on `full`, everything else on `transform`.
- **A-M4:** widen enemy coverage through overrides; Phase 4 props.
- **A-M5:** Phase 6, HP scaling.
- **A-M6:** Phase 5, bosses one by one.
- **A-M7:** Phase 8 polish, UI and docs.

**Part B milestones** (§B8 has the detail):
- **B-M1:** MM entrance randomizer.
- **B-M2:** unified entrance shuffler + cross-game entrances.
- **B-M3:** Child Body.
- **B-M4:** Item bridge Tier A + B.
- **B-M5:** extras.
- **B-M6:** Tier C transformations.

**Recommended order:** A-M1 → B-M1 → B-M2 → A-M2 → A-M3 → B-M3 → A-M4 → B-M4 → A-M5 → A-M6 → B-M5 → B-M6
→ A-M7.

- Randomizer milestones can be developed and validated solo with `comborando`.
- Co-op milestones need the two-instance harness.
- Alternating the two keeps each PR reviewable and puts the most-wanted features in players' hands early.

---

## 17. Co-op combat (built 2026-11-13, branch `feat/anchor-enemy-sync`)

Owner ask (2026-11-13): "sync all enemies over Anchor and multiply enemy health by player number; eventually
enemies follow players across rooms." Rulings carried over (2026-11-12): co-op combat is a **synced room
setting**; HP and spawn scaling by player count; **no boss exclusions**; later: enemies follow you between
rooms, BotW Blood Moon respawns, cross-game enemy/boss randomizer, wandering bosses, player-triggered "wrath"
waves.

This is a deliberately smaller first cut of Phases 3 and 6 above (§7, §10): it syncs every enemy and boss
**generically** (base `Actor` state, no per-overlay field tables yet) so it covers all ~400 enemy/boss
overlays in both games on day one, and leaves the libclang generator (§7.4) as the fidelity upgrade.

### 17.1 What is built

**Code layout (both games, one protocol):**

| File | Role |
|---|---|
| `combo/coop/CoopEnemySync.h` | pure core, unit tested: sync keys, the 72-byte wire `Entry`, `ClaimBeats` ownership rule, proximity rule, HP math (`PlayerFactor`, `ScaleHealth`, `RescaleHealth`, `HpRefund`), `LayoutHash` |
| `combo/coop/CoopEnemySyncGame.inl` | the glue, written once and compiled into each DLL against that game's types (hooks, ownership pass, snapshot send/apply, events, HP scaling, harness) |
| `soh/soh/Network/Anchor/CoopEnemySync.cpp` / `mm/2s2h/Network/Anchor/CoopEnemySync.cpp` | 40-line binders: field-name macros (`speedXZ`/`speed`, `yDistToPlayer`/`playerHeightRel`, `sceneNum`/`sceneId`), "connected", own client id, send |
| hooks into shared files | one dispatch branch + one init call in each Anchor (`Anchor.cpp`/`HookHandlers.cpp`, `MMAnchor.cpp`), `MMAnchor::SendCoopJson`, a remote-kill gate in MM `EnemyDrops.cpp`, layout-hash stamp in `ComboShip.cpp`, the both-unseeded rule in `SessionGuard.h`, three registry lines in `WorldCvarsOot.inl`, the menu block in `ComboMenu.cpp` |

**Who is synced:** every actor in `ACTORCAT_ENEMY` or `ACTORCAT_BOSS` in the same game and scene as a teammate
(same Anchor team). Puppets, NPCs and props are never touched. Bosses are included (owner ruling).

**Identity (keys).** Setup actors: `SetupKey(game, scene, room, actor-list index, id, spawn params)`, resolved
lazily on the first update (OOT stamps the list index after `Init`, §2.2). Runtime spawns:
`DynamicKey(game, scene, parentKey, id, params, ordinal)`, where `parentKey` is the synced actor whose update
spawned it (or the explicit `parent`), and `ordinal` counts identical spawns this scene visit. Spawn params are
captured in `ShouldActorInit`, before `Init` can rewrite `params`.

**Authority model: one owner per actor, claims with epochs.**
- The owner runs the actor's real AI and streams it; everyone else applies the stream to their copy.
- First owner: the **scene authority** (lowest Anchor clientId in the scene) claims every unowned actor at once;
  others wait 1 s + 0.5 s per rank so the authority's entry normally arrives first.
- **Combat claim:** a hit on *your* copy (`colChkInfo.damage > 0` before the update, or HP dropped across it)
  makes you the owner immediately, so the hit reaction, HP loss, death and drop all run on the hitter's copy.
  The owner sets a 1.5 s lock (3 s for bosses) that suppresses proximity hand-offs mid-fight.
- **Proximity hand-off:** a non-owner whose Link has been < 70% of the owner's distance (and < 1500 units) for
  ~1 s takes over, so enemies go after whoever is nearest.
- **Owner leaves / goes silent** (left the scene, disconnected, paused, or unloaded the actor's room): after
  1.5 s the remaining players claim, staggered by clientId rank.
- **Conflicts:** higher epoch wins; tie → lower clientId (`ClaimBeats`, identical on every client). A demoted
  owner that hit the actor inside the same round trip sends the damage the winner never saw (`dmg` event).

**What travels (`COMBO_ACTOR_SNAP`, quiet, team-scoped):** a header with the sender's scene, room and Link
position (presence: drives player count, ownership distances, retargeting) and a base64 blob of `Entry`s for
the actors the sender owns: key, epoch, id, flags (locked / boss / dying), HP, `world.pos/rot`, `shape.rot`,
velocity, speed, `freezeTimer` (stun/freeze), color filter (hit flash, fire/ice tint), base HP and the factor.
10 Hz while a teammate shares the scene (actors > 2000 units from every teammate at 2 Hz), 2 Hz heartbeat
otherwise; every 2 s (or on change) the scene's **dead set**.

**Applying (receiver, `ShouldActorUpdate`, pre-update):** position blends 50% per frame toward the
velocity-extrapolated target (snap at ≥ 150 units), rotations take the shortest arc, velocity/speed/HP are
overwritten, freeze and color filter are raised to the owner's. Then the **retarget trick**: the actor's
`xzDistToPlayer`/`yawTowardsPlayer`/height fields are recomputed against the **owner's Link**, so the local AI
between snapshots chases the same player. Enemy attacks still hit only the local Link (damage to players stays
victim-local; puppets don't collide). When the owner's copy is dying (HP > 0 → 0), receivers stop running the
non-boss AI and just follow the owner's positions until the kill arrives.

**Events (`COMBO_ACTOR_EVENT`):** `kill` (setup actors join the dead set: late joiners and re-entering players
get it removed), `despawn` (non-death removal), `dmg` (claim-race merge). Kills are ignored during scene
teardown and for room unloads (`actor->room` neither the current nor the previous room). A receiver removes
its copy with `Actor_Kill` under a remote-kill flag; MM's rando enemy-drop hook (`EnemyDrops.cpp`) checks
`CoopEnemySync_IsRemoteKill()` so the drop and its check exist only in the killer's world (OOT's
kill-linked rewards fire from the overlays' own death paths, which never run on a remote kill).

**Drops: once, on the killer.** The killer's copy runs the real death, so its drop table rolls exactly once.
Teammates don't see that drop (A-M2 deterministic drops + Phase 4 pickup sync make it shared later).

**HP scaling (room setting, default ON ×N, bosses included).**
- n = 1 + teammates whose presence puts them in your scene; k = 1 + (n − 1) × pct/100 (pct default 100 → ×N).
- **HP multiply** (the owner's wording): at keying, `health = round(base × k)`. Live: when n changes mid-fight
  the current HP keeps the same fraction of the new max (`RescaleHealth`, never revives or kills).
- **Overflow rule.** `colChkInfo.health` is u8, and several bosses read it as `(s8)` (Ganon, Gohma, Volvagia,
  Phantom Ganon, Twinrova, Morpha), where anything above 127 reads as negative = dead. So scaled HP is capped at
  **127**, and the rest of the factor (`base × k / 127`) is applied as a **damage refund** after each
  non-lethal hit (`HpRefund`, fractional carry, every hit still costs ≥ 1), so total effort still scales by k.
  An actor whose base HP is already ≥ 127 is never raised; its whole k is refunded.
- Why HP and not damage division: dividing `colChkInfo.damage` turns small hits into 0-damage hits, which many
  enemies treat as "immune/stun only" — the hit stops landing. Multiplying HP keeps every hit a real hit.
- Phase thresholds keep their absolute values, so with k > 1 a boss spends longer in its first phase(s). Death
  still triggers at 0.
- The owner's HP rides in every entry, so receivers show (and health bars read) the owner's value.

**Room settings (WORLD CVars, so the room owner's values are overlaid on joiners for the session and restored on
leave):** `gCombo.Coop.EnemySync` (1), `gCombo.Coop.EnemyHpScale` (1), `gCombo.Coop.EnemyHpPercent` (100).
Menu: Shared > Settings > Network, "Co-op Combat" (editable before joining and by the room owner; read-only
for joiners).

**SessionGuard change:** actor packets now also pass between two **unseeded** (vanilla) saves; a seeded save
still never pairs with an unseeded one, and both sides must stamp the same `Coop::Enemy::LayoutHash`.

**Solo / Anchor off:** nothing registers until Anchor connects, nothing is sent without another online client,
and k = 1 with no teammate in the scene, so solo play is unchanged.

**Harness (env at launch only):** `COMBO_DEBUG_COOP_TRACE` (per-3 s dump of every synced actor: key, owner,
epoch, HP/max/base, position; plus claim/hit/kill/remote-kill lines), `COMBO_DEBUG_COOP_NEAR=<frame>` (stand
next to the nearest synced enemy), `COMBO_DEBUG_COOP_HIT=<frame>` (one 1-damage hit through the real
post-update path), `COMBO_DEBUG_COOP_KILL=<frame>` (kill the nearest synced enemy as its death would: claim,
0 HP, its random drop, `Actor_Kill`). Two-client runner: `scripts/wombo/run2.sh` (local relay container on a
private docker network).

### 17.2 Verified (2026-11-13) and owed

**Verified, OOT, two headless clients on a local relay** (`scripts/wombo/run2.sh oot2 …`, both
`COMBO_DEBUG_BOOT_PLAY=0x0000,child`, Deku Tree lobby, trace on; client B adds `COMBO_DEBUG_COOP_HIT=200
COMBO_DEBUG_COOP_KILL=260`):
- both clients derive the same 5 keys; A (clientId 1, scene authority) owns all at epoch 1, B applies;
  every traced position identical on both (e.g. `0x002B key 7844d984… pos (321,981,285)` on A and B);
- HP doubled with 2 players on both: Skulltulas `0x002B` base 2 → 4, Deku Babas/others base 1 → 2;
- B's hit: `claim key 16d8f472… epoch 2 (hp drop)`, `hp 4 -> 3` on B; A's trace then shows `owner 2 ep 2
  hp 3/4`;
- B's kill: `kill actor 0x2B key 16d8f472…` (drop rolled once, on B) → A: `remote kill actor 0x2B key
  16d8f472…`; the actor stays gone on both for the rest of the run, the others keep their owner.
- Finding: software-GL clients run at ~3 fps, so wall-clock staleness of 2.5 s flipped peers in/out (HP
  rescaled 2→1→2, spurious "owner silent" claims). Raised `kPeerStaleMs` to 6 s and `kOrphanMs` to 4 s.

**Built but NOT yet verified in a run (owner asked to scope down 2026-11-13):** the MM side (same glue,
compiled and linked into lib2ship), bosses (code path identical, `ACTORCAT_BOSS`), proximity hand-off,
the claim-race `dmg` merge, real sword combat (the harness hit/kill drive the same post-update path but not
the collision → `colChkInfo.damage` claim), and the menu block. Next test: MM Termina Field
(`COMBO_DEBUG_CROSS=0x5400`) and Gohma's room.

**Owed (and why):**

- **Per-overlay AI state (fidelity).** Receivers run their own AI between snapshots, corrected toward the
  owner's transform; animation phase and attack timing can differ from the owner's view. Fix = §7.4's generated
  field tables (`actionFunc` rebasing, `SkelAnime`), which needs the libclang generator; the wire `Entry` and
  ownership model are designed to carry those extra fields as an appended block.
- **Boss deaths on receivers.** The killer's boss runs the real death cutscene, warp and heart container; on a
  teammate's client the boss is removed on the kill event, and the warp/heart appear when the clear flag
  (already synced) is seen on re-entry. Per-boss receiver death handling (spawn warp + heart in place) is the
  Phase 5 boss-card work.
- **Grabbers and input readers** (Like Like, Wallmaster, Dead Hand, Dark Link…, §7.8): grabs run only on the
  owner against its own Link; a receiver may see the enemy hold still. Per-actor overrides owed.
- **Shared drops** (A-M2 deterministic drops + Phase 4 pickups): today the drop exists only for the killer.
- **Dynamic spawns that diverge by RNG** (spawner Stalchildren, Leevers, Keese): keyed by spawner + ordinal;
  when two clients spawn different counts, the extras run unsynced locally. The plan's "authority-canonical
  dynamic spawns" (§7.2) is owed.
- **MM time of day:** setup actors filtered by half-day differ between players on different days; their keys
  simply don't match (no cross-talk), but those players don't share them either (§11).
- **Spawn-count scaling** (owner ruling: spawns scale with players) is not built; see 17.3.

### 17.3 Designed for later: follow, Blood Moon, wrath

The pieces above are the substrate. The extensions slot in as follows:

- **Enemies follow players across rooms/scenes.** The owner of an actor that is chasing a player who leaves the
  room emits a `follow` event `{key, id, params, hp, epoch, toScene, toRoom, entrance}` instead of letting the
  room unload kill it. The arriving client spawns that actor at the destination entrance as a **dynamic actor
  with a carried key** (key stays the same, so ownership/epoch/HP continue), and other clients in the
  destination adopt it through the normal snapshot path. The source room's dead set gets the key so it doesn't
  respawn behind them. Needs: object dependency loading at the destination (`Object_Spawn` for the actor's
  object), a per-actor "can follow" table (fliers and walkers yes, wall/ceiling/stationary no), and a cap.
- **Blood Moon respawns.** The dead set is already per scene and is the one thing that keeps a killed enemy
  dead while any teammate remains. A Blood Moon is a room-owner-triggered `COMBO_ACTOR_EVENT {e:"bloodmoon"}`
  that clears every client's dead sets (and the OOT/MM room-clear flags the scene logic uses), plus a cutscene
  /tint hook; scheduling it (in-game night count, real-time timer) is a room setting.
- **Wrath waves.** A player-triggered event `{e:"wrath", by, scene, room, table, size}` that the **scene
  authority** answers by spawning enemies from a per-scene table as dynamic actors keyed `DynamicKey(…, parent =
  hash("wrath", by, waveNo))`. Because keys derive from the event, every client gets the same keys and the
  existing ownership/snapshot path syncs them; the Anchor feed shows "<name> called a wrath wave". The same
  spawn path gives **spawn-count scaling**: the authority adds `round((n − 1) × pct)` extra copies of a room's
  enemies on room entry (owner ruling), keyed off the originals.
- **Wandering bosses / cross-game enemy randomizer.** Both are spawn-table changes on top of the same dynamic
  key + authority-spawn path; cross-game actors additionally need the guest-actor work from Part B Tier C.

---

# Part B: cross-play randomizer ("One World")

## B0. Definition of done (Part B)

Every item below is a **seed setting**:
- off by default;
- in the consolidated seed;
- in `seedId`;
- synced to joiners by Part A Phase 1.

1. **MM entrance randomizer.** Within Termina, with the same pools SoH offers: dungeons, boss rooms,
   overworld, interiors, grottos, owl warps, spawn. Plus decoupled and mixed pools.
2. **Cross-game entrances.** Any pool can be "mixed across games", so an OoT door, grotto, dungeon or
   overworld exit can lead into Termina and the other way round. The Mask Shop ↔ Clock Tower portal becomes
   one more shuffleable pair (fixed by default).
3. **Cross-game warps and spawns.** OoT warp songs, owl drops, blue warps and child/adult spawns may land in
   Termina, and MM owls, the Song of Soaring and spawns may land in Hyrule. This extends the existing
   teleport songs.
4. **Child Body.** OoT child Link and MM Link (every form) are one body with one inventory, the MM save's.
   Adult Link keeps the OoT inventory. Vitals (hearts, magic, rupees) can be shared across both bodies.
5. **Item bridge.** Every item from either game exists and is usable in both:
   - **Tier A:** equivalents, just mapped and enabled.
   - **Tier B:** abilities ported into the other engine.
   - **Tier C:** MM transformations in OoT (moonshot).
6. **Logic.** One fill reasons over both games' graphs joined by portals, the Child Body and the bridge.
   `comborando --playthrough` proves seeds beatable. Logic never credits an ability that isn't ported and
   enabled.
7. **Extras ("insane" knobs).** Cross-game dungeon rewards and requirements, boss rooms shuffled across games,
   random start anywhere at any age, all songs playable in both games, and an opt-in "glass cannon" Fierce
   Deity mode for adult Link in Termina.
8. **Balance.** Cross-play can't make Link overpowered. Damage mapping, travelling costs and a pooled-heart
   cap keep the difficulty curve of a single game (§B4, "Balance rules").

### Building blocks that already exist (verified at `a6abbb06`)

- **Cross-world fill.**
  - Two oracles (`OracleFns` in `combo/rando/CrossWorldRando.h`: `Reset`, `SetOwnedItems`,
    `GetReachableChecks`, `PlaceItem`, `GetPortalOpen`, `GetCrossOut`) with a fixpoint between the games.
  - Cross-game abilities travel as **pseudo-owned names** (`@combo:mmWarp:N`, `@combo:ootSoaring`, … in
    `combo/rando/CrossWarpLogic.h`).
  - The OoT→MM portal is modeled as *region* access (`RR_MARKET_MASK_SHOP`, see "Gate MM on the OOT→MM portal
    region" in `rando.md`).
  - **Generalizing this pattern is the backbone of Part B logic.**
- **OoT entrance shuffle.** SoH's full shuffler runs headless (`SOH_ShuffleEntrancesForCombo`,
  `soh/soh/OTRGlobals.cpp` ~4274), re-derived from `masterSeed` on reload.
  - Options: `RSK_SHUFFLE_{DUNGEON,BOSS,OVERWORLD,INTERIOR,GROTTO,THIEVES_HIDEOUT,BLUEWARP,GANONS_TOWER}_ENTRANCES`,
    `RSK_SHUFFLE_OWL_DROPS`, `RSK_SHUFFLE_WARP_SONGS`, `RSK_SHUFFLE_OVERWORLD_SPAWNS`,
    `RSK_DECOUPLED_ENTRANCES`, `RSK_MIXED_ENTRANCE_POOLS` + `RSK_MIX_*`.
  - Combo addition `RSK_EXCLUDE_MASK_SHOP_ENTRANCE` keeps the portal in place.
- **Cross-game arrival at any entrance.** `Combo_RequestCrossSwitch(entrance)` stages a target and the launcher
  drains it (`SOH_/MM_GetPendingCrossTarget`) and pushes it (`SOH_/MM_SetTargetEntrance`, `gComboTargetEntrance`)
  before the other game boots or resumes (teleport songs; debug `combo_warp_mm` / `combo_warp_oot`).
- **MM logic is an explicit region graph.** Exits are entrance pairs, e.g.
  `EXIT(ENTRANCE(DEKU_PALACE, 3), ENTRANCE(DEKU_KINGS_CHAMBER, 1), cond)` in
  `mm/2s2h/Rando/Logic/Regions/*.cpp`, with half-day `TimeLogic` (`Logic.h` `RegionTimeState`). There is **no
  MM entrance shuffle**. `VB_SETUP_TRANSITION` exists as a likely runtime override point.
- **Shared Items.** 17 families + 7 songs (`combo/rando/SharedItems.h`) with a tier-reconcile ABI:
  `SOH_/MM_GetSharedTier`, `SOH_/MM_RaiseSharedTier`, plus changed/tick callbacks.
- **Cross-game asset routing.** Either game resolves `__OTR__@oot:` / `__OTR__@mm:` paths against the other
  game's resident ResourceManager. Item models, and skeletal-animated items via `combo/menu/ComboForeignAnim.h`,
  already render cross-game.
  - ABI: `combo/menu/ComboItemDrawABI.h`, `ComboForeignDrawMM.h`, and OoT's `GetItem_GetDrawTableEntry` seam.
  - **This is what makes porting abilities, effects and forms across engines feasible.**
- **Saves.** Two saves linked by slot (OoT slot N → MM file N+1). There is no unified save format, and none
  is needed (§B3 uses translation, not merging).

## B1. MM entrance randomizer (inside Termina)

- [ ] **Entrance table.** Generate `mm/2s2h/Rando/Entrances/EntranceTable.cpp` from the logic graph's
  `EXIT(...)` pairs (script: `scripts/rando/gen_mm_entrances.py`).
  - **Each record:** forward and reverse entrance, type (`overworld | interior | grotto | dungeon | boss | owl
    | spawn | special`), the region on each side, and the logic condition.
  - **Never shuffle:**
    - one-way and cutscene transitions: Moon, Clock Tower rooftop, Majora;
    - ride and minigame transitions: boat cruise, Goron race, Beaver race;
    - dungeon-clear "raise" transitions;
    - time-locked event transitions.

    Seed this exclusion list from 2S2H's own `Enhancements/Cutscenes/*` and `Timesavers/*`, which set
    `nextEntrance` directly.
  - **Scene variants:** Southern Swamp poisoned/clear, Woodfall, Snowhead spring/winter, Ikana Canyon
    cleared, Termina Field moon variants.
    - An entrance target is a **logical place**, resolved at runtime to the variant that matches world
      flags.
    - Encode this as a variant resolver function, not separate table rows.
- [ ] **MM option set.** Add `RO_SHUFFLE_*_ENTRANCES`, `RO_DECOUPLED_ENTRANCES` and `RO_MIXED_ENTRANCE_POOLS`
  + mix toggles to `mm/2s2h/Rando/Types.h` and `StaticData` options, mirroring SoH names. They are CVar-backed
  and so flow through `MM_DumpRandoSettings` automatically.
- [ ] **Runtime override.**
  - Find MM's single transition choke point where `play->nextEntrance` becomes the destination (candidate:
    the `VB_SETUP_TRANSITION` path in `Play` transition setup; verify).
  - Map through an override table stored in the MM save's rando block, or the combo container, so it
    survives reloads.
  - Also apply it to void-out and respawn, the grotto return position (`VB_ROTATE_GROTTO_ENTRANCE` handling)
    and owl and Soaring destinations when owl warps are shuffled.
- [ ] **Logic.** The MM oracle gets `MM_Rando_SetEntranceOverrides(json)`, which rewires the region graph's
  exits before reachability. Conditions and `TimeLogic` stay attached to the **exit** (the doorway's own
  requirement), and the destination region inherits time state as it does today.
- [ ] **MM-specific rules.**
  - The Song of Time cycle reset still returns to South Clock Town Day 1, which acts as a fixed respawn root.
  - Form-gated entrances (Deku flowers, the Zora Hall water entrance, Goron-only doors) keep their condition.
  - A "shuffle cycle-reset spawn" option is an extra.
- [ ] **Entrance tracker.** A combo-owned window covering both games (`combo/gui/`, modeled on the unified
  Hint Tracker #164), plus `ENTRANCE_DISCOVERED` sync for MM in MMAnchor.

**Accept:** 20 seeds with every MM pool shuffled (coupled and decoupled) generate. `comborando --playthrough`
traces each to completion. A manual run checks all four temples, an owl warp, a grotto, a scene-variant entrance
and a void-out.

## B2. Cross-game entrances, warps and spawns

- [ ] **Unified entrance model.** `combo/rando/CrossEntrance.h` defines
  `CwEntrance{game, index, type, region, reverse, conditionTag, flags}`.
  - Each game's static dump gains an `entrances` array: OoT from SoH's entrance table, MM from §B1's table.
- [ ] **One combo-owned shuffler** for all entrances of both games whenever any MM or cross option is on.
  - It replaces `SOH_ShuffleEntrancesForCombo` in those seeds, which keeps running alone for OoT-only ER
    seeds, so existing seeds don't change.
  - Write overrides down with `SOH_SetEntranceOverrides(json)` / `MM_SetEntranceOverrides(json)`.
  - **Store the result in the seed** (`entrances.oot`, `entrances.mm`, `entrances.cross`) and apply it from
    the seed on reload instead of re-deriving. This makes cross-machine reproduction exact and removes any
    dependence on the shuffler staying deterministic across builds.
- [ ] **Pools.**
  - Each type's pool may be "mixed across games". A Hyrule interior door may open into a Termina interior.
  - Overworld↔overworld pairs are the headline feature.
  - Dungeon↔dungeon and boss↔boss across games are allowed (boss rooms are covered in §B6).
  - Grottos too.
  - The Mask Shop ↔ Clock Tower pair stays fixed unless "Shuffle the Termina portal" is on.
- [ ] **Logic generalization** (`CrossWorldRando.h`).
  - **Add to `OracleFns`:**
    - `SetEntranceOverrides(json)`;
    - `SetArrivalRoots(json)`: a list of `{entrance, age|timeContext}` this game is entered through from the
      other game;
    - `GetReachableExits()`: which local exits that lead cross-game are reachable after the last
      `GetReachableChecks`, with OoT age and MM time context. Same contract as `GetPortalOpen`.
  - **Migrate the existing special cases onto the generic API.** `GetPortalOpen` becomes the fixed Mask Shop
    portal. `GetCrossOut` becomes warp songs and Soaring. Keep both exports until the generic path passes
    every existing seed bit-identically.
  - **Fixpoint.** The fill alternates OoT ↔ MM, feeding newly reachable cross exits to the other side as
    arrival roots, until nothing changes. Latch and memoize like `CrossWarpLatch`.
  - **Arrival age in Hyrule.** Setting **Portal Age**:
    - `Keep`: the age you last had in Hyrule. Adult Link then enters Termina according to the **Adult in
      Termina** setting (§B4);
    - `Child`: Termina is child-bodied, so you always come back child. This makes Termina an age-swap route.

    Default: `Child` when Child Body is on, else `Keep`. The OoT oracle must model the chosen rule exactly.
  - **Arrival time in Termina.** Entering through a portal **resumes the saved MM clock**. Logic can't know
    what time that is, so it applies a sound rule:
    - a portal arrival root grants only *time-agnostic* reachability (exits and checks available in every
      half-day);
    - time-specific content is reachable only from the cycle-start root (Song of Time reset → South Clock
      Town Day 1, or the first-ever arrival).

    Implement this in `TimeLogic` as an "unknown time" region state. The alternative **"Portal resets the MM
    cycle"** option (arrival behaves like a Song of Time reset to Day 1 06:00, landing at the portal) is
    simpler and sound but changes MM's feel. Offer it as an option.
- [ ] **Runtime crossing.**
  - **OoT side.** At SoH's entrance override (`Entrance_OverrideNextIndex`, `soh/soh/Enhancements/randomizer/entrance.cpp`),
    a cross target → `Combo_RequestCrossSwitch(mmEntrance)`, then the same switch path the Mask Shop uses.
    Generalize `gComboSceneSwitchCallback`, which today fires only for the Mask Shop.
  - **MM side.** The mirror, through the MM→OoT return path with a target entrance (teleport songs already do
    this).
  - **The handoff.** Fade out → switch → `SOH_/MM_SetTargetEntrance` → fade in.
  - **Respawn rules.** Void-out or death after a crossing respawns at the last entrance *in that game*. Game
    over in MM is a Song of Time-style reset.
  - **Discovery.** Crossing marks both sides discovered, in both trackers.
- [ ] **Warps and spawns across games.**
  - Warp-song, owl, blue-warp and spawn pools join the cross shuffler as one-way targets.
  - Blue warps send you back to the dungeon's *entrance side*, which may be in the other game.
  - Starting spawn anywhere is covered in §B6.
- [ ] **Co-op interplay** (Part A).
  - Actor sync keys already include `game`, so nothing changes there.
  - The client-state game-switch toast also fires on portal crossings.
  - `ENTRANCE_DISCOVERED` grows `game` and cross variants.

**Accept:** with cross mixing on for overworld, interior and grotto pools, 30 seeds generate within the
existing attempt budget (`kFillAttempts`) and the validator passes them. A manual run crosses at least six
different portals both ways, at both ages, including a void-out right after crossing and a Song of Time
reset after arriving by portal.

## B3. Child Body (child Link uses MM Link's inventory)

**Model.**
- **Bodies.** There are two bodies.
  - **CHILD:** OoT child plus MM in every form. Its source of truth is the **MM save** plus a combo-owned
    **extension block** in the container (`combo.childExt`) for OoT-only child items that have no MM slot:
    Slingshot and seeds, Boomerang, the OoT child masks MM lacks, Deku Shield, the Kokiri tunic flag.
  - **ADULT:** OoT adult, using the OoT save's inventory as today.
- **Setting `Child Body`:** `Off` (today) / `Shared child inventory` / `Shared child inventory + shared vitals`.
  - **Shared vitals:** one pool of heart containers, pieces, magic, rupees and wallet across both bodies and
    games.
  - Default when enabled: shared vitals, because hearts that differ between ages feel broken.
- **Engine strategy: translate on body switch; don't redirect reads.** OoT code reads
  `gSaveContext.inventory`/`equips` everywhere, so swap contents at well-defined points instead.
  - **Entering CHILD in OoT** (file load as child, Master Sword placement, portal arrival as child, resume
    from MM):
    - stash the adult-only state;
    - translate the CHILD inventory (MM save + `childExt`) into OoT inventory, equipment and vitals using the
      mapping table;
    - set `childViewActive`.
  - **Leaving CHILD in OoT** (age change, switch to MM, any save, the Anchor team-state serialize):
    - translate the OoT deltas back into the MM save + `childExt`;
    - restore the adult stash.
  - **Persistence.** Write through on every transition. Persist `childViewActive` in the combo container so a
    crash mid-session is repaired on next load.
  - **MM side.** The MM save already is the truth. `childExt` items appear in MM only through the Item bridge
    (§B4); until then they are shown as unusable.
- [ ] **Mapping table** `combo/rando/BodyInventoryMap.h`: OoT item/slot/tier ↔ MM item/slot/tier for every
  family. Each row needs a decision, recorded in the doc:
  - **Ammo and capacities:** bow and quiver, bomb bag, bombchus, sticks, nuts, magic, wallet tiers. Units
    match across games for hearts and magic; verify each capacity table.
  - **Bottles and contents:** contents that exist in only one game become a "foreign bottle" that keeps its
    identity and has a defined effect when drunk or used in the other game.
  - **Swords and shields:**
    - MM Kokiri / Razor / Gilded ↔ OoT child sword slot, with damage tiers 1/2/3 and the model routed
      through `@mm:`;
    - MM Hero's Shield ↔ OoT child shield slot;
    - MM Mirror Shield ↔ OoT Mirror Shield behavior, child-usable;
    - Great Fairy Sword is a Tier B item.
  - **Songs:** the union of both games' songs, playable in both (effects in §B4).
  - **Quest items:** spiritual stones, medallions, remains and skulltula tokens stay per game unless §B6
    rewards mixing is on.
- [ ] **Age restrictions.** Child-body items that are adult-only in OoT (bow, hookshot, …) must work for child:
  - reuse SoH's existing timeless-equipment behavior where present;
  - otherwise add a `COMBO_BUILD` VB-hook bypass;
  - draw child-appropriate models (MM models via `@mm:` where OoT has none).
- [ ] **Interplay with Shared Items.** Child Body subsumes Shared Items for every child-usable family, and
  Shared Items keeps working for adult-only families.
  - Keep the reconcile ABI. Child Body is just a wider family set with a different authority (the MM save).
  - Update the trim rules at generation so the pool has the right copy counts. The pool math must match the
    runtime or seeds go wrong; add a gentest assertion.
- [ ] **Anchor interplay.**
  - `GIVE_ITEM` for a child-body item received in OoT is delivered to the MM save through the existing
    cross-delivery path.
  - Team-state resync merges both saves (it already does), plus `childExt`.
  - The stash must be written back before `UPDATE_TEAM_STATE` serializes, via the `OnSaveFile` hook order.
- [ ] **Logic.**
  - The OoT oracle's **child** item checks see MM-owned child-body items, fed as `@combo:child:<item>`
    pseudo-names that OoT's logic helpers accept for child age only.
  - Adult checks see only OoT and shared items.
  - The MM oracle sees `childExt` items only when their Tier B port is enabled.

**Accept:**
- Getting a bow in MM makes it usable by child Link in Hyrule.
- Bomb counts, rupees and hearts stay identical across a full lap: child OoT → MM → child OoT → adult →
  child.
- Killing the game mid-lap and reloading loses nothing.
- A two-player Anchor session keeps both players consistent.

## B4. Item bridge (every item usable in both games)

Each bridged item ships as a self-contained module:
- OoT side: `soh/soh/Enhancements/combo/bridge/<Item>.cpp`;
- MM side: `mm/2s2h/Enhancements/Combo/Bridge/<Item>.cpp`.

Each module provides HUD icon, C-button equip, the player action, effect actors, logic ability flag and a
playtest card. Models and effects come from the other game through `@oot:`/`@mm:` routing. Ported actors
are compiled into the *host* DLL against the host's APIs and are never called cross-DLL per frame.

- **Pause menu.** Both games get a **Bridge** inventory page in the pause screen listing foreign items that
  can be equipped to C. Reuse the combo menu ABI in `combo/menu/*` where it fits.
- **Tier A (equivalent semantics: map, enable, test).**
  - Bow and elemental arrows, bombs, bombchus, hookshot, sticks, nuts, lens, ocarina, bottles.
  - Bunny Hood, Keaton Mask, Mask of Truth.
  - OoT's trade-sequence Goron and Zora masks: cosmetic in OoT, and in MM they *are* the transformation
    masks through Shared Items.
- **Tier B, OoT items in MM:**
  - **Boots:** Hover Boots, Iron Boots. MM's player already carries a boots table, including Zora underwater
    walking; extend it.
  - **Tunics and scales:** Goron Tunic (heat immunity where MM applies heat), Zora Tunic (human-form water
    breathing), Silver and Gold Scale.
  - **Strength:** Goron Bracelet and Silver/Golden Gauntlets let human Link lift heavy objects.
  - **Weapons:** Megaton Hammer (port the player hammer action, and give MM rocks, boulders and switches a
    hammer collider response), Slingshot, Boomerang, Longshot (range on MM's hookshot).
  - **Spells:** Din's Fire (port `ovl_Magic_Fire`), Farore's Wind (a per-scene warp point), Nayru's Love.
- **Tier B, MM items in OoT:**
  - **Weapons and gear:** Powder Keg (port the actor and add OoT boulder responses), Great Fairy Sword (child
    B-alternative), Razor and Gilded swords, MM Mirror Shield, Pictograph Box (cosmetic).
  - **Masks with effects in Hyrule:**
    - Stone Mask: enemies ignore you; one generic player-visibility hook;
    - Blast Mask;
    - Captain's Hat: Stalchildren and Stalfos become passive;
    - Gibdo Mask: ReDeads and Gibdos become passive;
    - Garo's Mask;
    - Bremen Mask: cuccos and dogs follow;
    - Kamaro's Mask: dance;
    - All-Night Mask;
    - Giant's Mask: arena-gated.
- **Song effects across games:**
  - Song of Double Time ↔ Sun's Song (time skip);
  - Inverted Song of Time slows OoT time;
  - Song of Healing heals in OoT;
  - Elegy of Emptiness spawns a shell statue in OoT, so statue-weight switches become cross-logic;
  - Sonata, Lullaby, New Wave and Oath are cosmetic in OoT;
  - OoT's Saria's Song, Zelda's Lullaby and Sun's Song in MM: Sun's Song acts as Double Time, the others are
    cosmetic unless a mapping is defined.

  Every effect sits behind an ability flag.
- **Tier C (moonshot): MM transformations in OoT.**
  - Port the form player behaviors into OoT's Player, render MM form skeletons and animations through `@mm:`
    routing, and add OoT interactions:
    - **Goron:** roll, punch, pound. Breaks boulders and rocks, presses switches.
    - **Zora:** fast swim, fin boomerang, barrier, walk on lake floors.
    - **Deku:** spin, water hop. OoT has no Deku flowers, so add an optional "Deku flowers in Hyrule"
      placement set.
    - **Fierce Deity:** sword beams.
  - **Order:** Goron → Zora → Deku → Fierce Deity, one playtest card each. Each form's abilities enter OoT
    logic as `@combo:ability:<form>_<move>` only when that form's Tier C flag is on.
  - Tier C Fierce Deity is about FD **in Hyrule** only. In Termina, FD is MM's own native form; see below.
- **Logic rule for the whole bridge.**
  - Each ported ability has one feature flag (a seed setting).
  - The host game's logic helpers accept the ability **only if** the flag is on: OoT's helpers in
    `soh/soh/Enhancements/randomizer/` logic, MM's `CAN_*` macros in `mm/2s2h/Rando/Logic/`, both fenced.
  - Abilities cross oracles as `@combo:ability:*` pseudo-names, like the teleport songs.
  - Add a gentest that fails if any `@combo:ability:*` name is accepted by logic without a registered,
    enabled runtime module.

### Adult Link in Termina (applies when Portal Age = `Keep`)

Setting **Adult in Termina**: `Adult Link` (default) / `Fierce Deity` / `Forbidden`.

The guiding rule: **two bodies, two toolkits, no free lunch.**
- The child body carries the MM inventory, including the transformation masks, in both games.
- The adult body carries the OoT inventory in both games.
- Choosing which body to bring through a portal is the strategic decision, and neither body is strictly
  better.

**`Adult Link` (default: fair, and the most cohesive).**
- **Model.** The adult body uses OoT's adult skeleton through `@oot:` routing, with **MM human-form movement**.
  - That means MM's per-form age properties for ledge grab, climb, jump and crawl heights, not OoT adult's.
  - With OoT adult reach, adult Link would skip MM obstacles the level designers built for child height.
  - With MM's numbers, every existing MM logic condition stays valid and only item ownership changes.
- **Animations.** OoT already drives child and adult from one animation set with two skeletons, and MM's
  human animations descend from OoT's. Expect the OoT adult skeleton to play MM human animations.
  - **Verify limb count and order first.**
  - Fallback: route the core OoT adult animations through `@oot:` as well.
- **Items.** OoT adult items work in Termina only as Tier B ports (hookshot/longshot, hammer, boots,
  tunics, spells). Unported items show greyed out.
- **No transformations.** The masks live in the child body's inventory, so adult Link in Termina can't become
  Deku, Goron or Zora (unless those mask families are shared). This is the trade-off: adult brings the OoT
  toolkit, child brings the forms.
- **Logic.** MM logic has no age dimension, and bodies can't change inside Termina; age changes happen only in
  Hyrule or through the portal rule. So:
  - run MM reachability **per body**: once with the child-body inventory and once with the adult-body
    inventory, over the same exits (same movement envelope);
  - arrival roots carry their body.

**`Fierce Deity` (opt-in "glass cannon" for players who want a harder, flashier adult).**
- **Cost.** FD is MM's native form, so in Termina this is cheap to build. Lift the boss-room restriction and
  apply these balance rules; no OoT port is needed.
- **Sword.** Hits one host damage tier above your current OoT adult sword (see damage mapping below), not
  vanilla FD's flat high damage. A better sword in Hyrule means a stronger deity.
- **Beams.** Fire only at **full health** (the classic Zelda 1 rule) and cost magic per beam.
- **No C-items and no masks,** as in vanilla FD. FD is a combat body; traversal comes from movement alone.
- **Takes double damage.**
- **Logic.** The FD body has only its movement plus sword and beams. Most Termina checks are unreachable as
  adult, so the fill routes the adult body through fights (mini-bosses, boss rooms, combat-gated checks).
  That is the challenge this mode is for.
- **Fallback.** Can't decide on numbers? Make damage tier, beam rule and damage taken three sub-knobs with
  the values above as defaults.

**`Forbidden` (the most vanilla).**
- Portals refuse adult Link: a closed-door message, and you stay in Hyrule.
- Logic treats Termina as child-only.
- Use this for players who want cross-play without any body weirdness.

### Balance rules (all of cross-play)

1. **Damage mapping, not damage import.**
   - Every foreign weapon hits as a **host-game damage type**, from one table:
     `combo/rando/CrossDamageMap.h`.
   - Illustrative MM-side mapping: Master Sword → Razor-tier slash, Biggoron Sword → Gilded-tier, Megaton
     Hammer → Goron-pound class, Din's Fire → fire-arrow class.
   - Illustrative OoT-side mapping: Gilded Sword → Master Sword tier, Great Fairy Sword → Biggoron tier,
     Powder Keg → bomb class.
   - Fill in and verify the real tiers from each game's player damage and actor damage tables (`DMG_*`)
     before shipping.
   - Effect: enemies and bosses always take the damage their own game's designers tuned for, whichever
     game's weapon you hold. A Biggoron Sword must not two-shot Odolwa.
2. **Costs travel with abilities.** Ported items keep their home costs, converted to host units: Din's and
   Nayru's magic, Nayru's duration, Hover Boots' hover window, ammo.
   - Powder Kegs stay Goron-only, as in MM, unless "Unrestricted kegs" is on. In OoT that means Tier C Goron.
   - Bombchus and kegs keep their per-game carry limits.
3. **Pooled-heart cap.** With shared vitals, heart containers and pieces from both games count into one pool,
   which would roughly double max health.
   - The fill **trims surplus heart items** so the combined pool tops out at the 20-heart cap both HUDs
     already use (verify each game's count of containers and pieces).
   - Trimmed hearts become junk or filler, the same way Shared Items trims MM's duplicate copies.
   - Magic and wallet tiers are already single families.
4. **Logic stays honest.**
   - No cross-game trick is assumed unless a **Cross tricks** list enables it. Examples: Hover Boots across
     MM gaps, hookshot to MM targets, Goron roll across OoT gaps.
   - Each trick is a named entry, like SoH tricks, so seeds stay fair by default.
5. **One difficulty for both games.**
   - Damage-taken multiplier, no-heart-drops and the other difficulty CVars (SoH and 2S2H each have their
     own) are linked: one combo setting writes both games.
   - That setting goes in the Part A world-CVar registry, so the room shares one difficulty.
   - Part A's HP scaling stacks on top for co-op.

**Accept:** for each bridged item, its playtest card is green: it equips from C, its HUD icon and model are
right, it does what it should in three representative rooms, and seeds that rely on it pass the validator.

## B5. Unified logic, generation and spoiler

- [ ] **Fixpoint.** Extend `CrossWorldCombinedFill` with:
  - arrival roots and exits (§B2);
  - Child Body pseudo-items (§B3);
  - abilities (§B4).

  Memo keys must include them. Keep `kFillAttempts` and the other budgets `constexpr` in `CrossWorldRando.h`.
  Measure generation time on cross-ER seeds and add a progress phase for the entrance shuffle.
- [ ] **Entrance + item interplay.** Use assumed-fill validation for entrances, as SoH does, run against the
  combined oracle. If the entrance shuffle's retry rate makes generation too slow, cap the number of
  cross-mixed pairs per pool (setting: `Cross-game connections: Few / Many / All`).
- [ ] **Validator.** `comborando --playthrough` traces cross entrances, age, time context, Child Body and
  abilities, and prints the full route for failures.
- [ ] **Spoiler.**
  - New blocks: `entrances.{oot,mm,cross}`, `childBody`, `bridge`, `extras`.
  - The playthrough lists entrance steps.
  - Hints may name entrances, extending SoH entrance hints to MM and cross.
- [ ] **Seed sync.** Every new option goes through the dump/restore exports and the Part A world-settings
  registry. Add a gentest asserting that each new CVar appears in exactly one of: rando settings dump, world
  CVar registry, explicitly local.

## B6. Extras ("insane" knobs, each opt-in)

- **Cross-game dungeon rewards.**
  - Stones, medallions and remains shuffle anywhere in either game.
  - Rainbow Bridge, Ganon's Boss Key, Moon access and Majora requirements may count **combined** rewards
    ("N of 13").
  - Both oracles see rewards through pseudo-names.
- **Boss rooms across games.** A dungeon's boss door can lead to any of OoT's 9 dungeon bosses or MM's 4.
  - Blue warp returns to the source dungeon's entrance side.
  - Boss souls (OoT `RSK_SHUFFLE_BOSS_SOULS`, MM `RO_SHUFFLE_BOSS_SOULS`) and remains rules apply to the boss
    you meet.
  - MM bosses' arena assumptions (Gyorg's water, Twinmold's Giant's Mask) become logic requirements on that
    door.
- **Start anywhere.** Random starting entrance in either game, at a random or chosen age. Depends on §B2 and
  the arrival rules.
- **Songs everywhere.** Every song from both games is learnable and playable in both, with the effects from
  §B4.
- **Cross-game enemy shuffle (moonshot+).** MM enemies in Hyrule and the reverse.
  - Out of scope until the Tier C asset-routing work proves skeletal actor ports are stable.
  - It needs Part A's actor sync to know the ported actors: generator entries for the host DLL.
- **Unified clock (moonshot).** OoT day/night follows the MM clock. This ties into Part A §11 Option B; it
  needs its own plan.

## B7. Part B risks

| Risk | Mitigation |
|---|---|
| Porting is many mini-projects (Tier B/C) | Each item or form is its own module with a card, so ship incrementally. Logic only credits enabled ports, enforced by gentest. |
| MM time-logic soundness with portal arrivals | "Unknown time" arrival state (time-agnostic only). Optional cycle-reset-on-arrival. Validator traces time context. |
| Scene variants (swamp, Woodfall, Snowhead, Ikana, moon) | Logical-place targets with a runtime variant resolver |
| Child Body save integrity | Write-through on every body switch, `childViewActive` crash repair, lap test in §B3 |
| Generation time with cross ER | Budgets in `CrossWorldRando.h`, the "Few/Many/All" cap, measurement |
| Shuffler determinism across builds | Overrides are stored in the seed, never re-derived for cross/MM seeds |
| Upstream merge footprint (entrance.cpp, logic helpers, MM logic macros, Player) | `COMBO_BUILD` fences, bridge code in separate files, Tier C behind its own fences; documented in `UPSTREAM_MERGES.md` |
| Existing seeds changing | All new options default off. OoT-only ER seeds keep `SOH_ShuffleEntrancesForCombo`. Gentest compares old seeds bit-identically. |
| Cross-play power creep (two games' items, pooled hearts, adult toolkit in Termina) | Damage mapping, costs that travel, the 20-heart cap trim, adult has no forms in Termina, cross tricks opt-in, one linked difficulty setting |
| Adult skeleton can't play MM human animations | Verify limb layout before committing. Fallback: route OoT adult animations. Last resort: ship `Fierce Deity`/`Forbidden` first. |

## B8. Part B defaults and milestones

**Defaults (owner can override):**
1. **Portal Age** = `Child` when Child Body is on, else `Keep`.
2. **MM time on arrival:** resume the saved clock, with logic using the time-agnostic arrival rule.
   "Portal resets cycle" is an option.
3. **Termina portal:** Mask Shop ↔ Clock Tower stays fixed unless "Shuffle the Termina portal" is on.
4. **Child Body:** default mode, once enabled, is shared inventory + shared vitals.
5. **Tier C:** forms in Hyrule are off until each form's card is green.
6. **Cross-game connections:** default `Many` once cross mixing is on.
7. **Adult in Termina** = `Adult Link`: OoT adult inventory, MM human movement, no transformations.
   - `Fierce Deity` (glass cannon) and `Forbidden` are options.
   - `Fierce Deity` needs no Tier C work because FD is native to MM.
8. **Balance:** damage mapping on, costs travel, the pooled-heart cap at 20 when vitals are shared, cross
   tricks off.

**Milestones:**
- **B-M1:** §B1 (MM ER). Shippable alone.
- **B-M2:** §B2 + the oracle API generalization (§B5). Existing seeds must still reproduce bit-identically.
- **B-M3:** §B3, Child Body.
- **B-M4:** §B4 Tier A, then Tier B in batches:
  - boots, tunics, scales, strength;
  - hammer, slingshot, boomerang, longshot;
  - spells;
  - MM masks;
  - Powder Keg and swords;
  - songs.
- **B-M5:** §B6 extras: rewards, boss rooms, start anywhere, songs everywhere.
- **B-M6:** Tier C forms, one milestone per form.

**Part B playtest matrix additions** (two-instance harness for co-op, solo for rando):
- cross-portal lap at both ages;
- void-out after a crossing;
- Song of Time after a portal arrival;
- Child Body lap with a crash and reload;
- each bridge item's card;
- a co-op session where the two players cross different portals and meet in a third scene.

---

## Findings log (append while executing)

| Date | Task | Finding | Decision |
|---|---|---|---|
| 2026-11-11 | 0.1 (a) | Relay (`garrettjoecox/anchor` @ `bf7b43c`) forwards ANY `type` it doesn't special-case: to the whole room, or to one team with `targetTeamId`. Verified live with raw clients (`COMBO_PROBE` reached the teammate and the other team without `targetTeamId`, only the team with it). | `COMBO_*` packets need no server change. Every launcher co-op packet sets `targetTeamId` except directed replies. |
| 2026-11-11 | 0.1 (b) | `MAX_PACKET_SIZE` = 8 MiB per NUL-framed packet (bufio scanner limit); over it the server **disconnects** the sender. 4 MiB delivered fine live. | Seed chunks are 256 KiB of base64 (≤ 64 chunks ⇒ 16 MiB cap); snapshot batches stay well under 64 KiB. |
| 2026-11-11 | 0.1 (c) | `targetClientId` is **server-routed** (only that client receives it; checked before team routing, no team check). `targetTeamId` is server-routed to team members. The server excludes the sender **only if the packet carries `clientId`**. | Launcher-originated packets stamp `clientId` (own id). Directed replies use `targetClientId`. |
| 2026-11-11 | 0.1 (d) | `addToQueue: true` (with `targetTeamId`) appends to a per-team replay queue (max 512, oldest dropped) that rides the next server-answered `REQUEST_TEAM_STATE`. The server also stores the team's last `UPDATE_TEAM_STATE.state` and replays it **without a sender id**. | Transient co-op packets never set `addToQueue`. The replayed team state can't be judged by sender, so the launcher stamps `state.comboSeedId` into outgoing team state and drops a replay from another seed. |
| 2026-11-11 | 0.1 (e) | No rate limiting, but each client has a **256-packet send queue; when it overflows the server DISCONNECTS that (receiving) client**. Live: 2000 back-to-back small packets → the slow-reading receiver got 826, then was disconnected. | Phase 3 must batch: one `COMBO_ACTOR_SNAP` per (game, scene, room) per send tick, never one packet per actor. The launcher's receive loop now reads 64 KiB at a time on a 1 ms wait (was 512 B on a busy 0 ms poll). |
| 2026-11-11 | 0.1 (f) | Fields are never rewritten or stripped, except `UPDATE_CLIENT_STATE.state` (cached, `clientId` set) and the HANDSHAKE `roomState.ownerClientId` (the first client to create the room becomes its owner). | `state.combo` survives in the cached client state, so late joiners see every peer's identity in `ALL_CLIENT_STATE`. |
| 2026-11-11 | 1.5 / 1.8 | Every inbound packet already passes through the launcher's `ComboAnchor::ReceiveLoop`, and every outbound client state through `ComboAnchor::Send`, from either game. The launcher is the only place that knows the bound seed for both games and keeps the always-live roster. | **Placement change (smallest alternative, same behavior):** `state.combo` is stamped in `ComboAnchor::Send`, and SessionGuard runs in the launcher before forwarding to the DLLs, instead of in `PrepClientState` / `OnIncomingJson` of each game. No vendored Anchor code changed for 1.5/1.8. |
| 2026-11-11 | 1.3 | libultraship persists CVars only through `ConsoleVariable::Save()` (every `CVarSave`), which writes every live variable into the flattened config and then `Config::Save()`s it. | Added a fenced persist **shadow** (`CVarShadowPersist`): a shadowed key keeps its captured on-disk entry (or stays absent) on every save. The world-CVar overlay and the RoomShare rando settings are shadowed for the session, so nothing a room applies ever reaches `comboship.json`. |
| 2026-11-11 | 1.1 / 1.3 | The plan's `RoomShare` says "written with `WriteComboSpoiler`/`RememberComboSpoiler`", but `RememberComboSpoiler` saves the remembered-path CVar to `comboship.json`, which would break §5's "byte-identical after leaving" acceptance. | `RoomShare` writes the seed file but does **not** remember it. A slot started on the room seed carries the seed in its own container, so nothing is lost on restart. |
| 2026-11-11 | 0.5 | The debug window's peer/seed/traffic data lives in the launcher (roster, counters); per-actor rows will come from the DLLs in A-M3. | The Co-op Sync window lives in comboui (`combo/gui/ComboCoopWindow.cpp`), fed through `ComboCoopHooks`. A-M3 adds a DLL export for the actor rows. |
| 2026-11-11 | build | With CMake 3.31 + ninja 1.12, the yaml-cpp C++20 module dyndep scan can hit a ninja assertion (`Plan::RefreshDyndepDependents`) on incremental builds. | Configure with `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF` (nothing uses modules). |
| 2026-11-11 | B1 entrance table | Deriving the table at runtime from MM's `Regions` graph (`MM_DumpEntranceGraph`) gives every exit pair with its condition, so a generated `EntranceTable.cpp` would only duplicate it and drift. | No generator script: the launcher shuffler reads the live graph. Classification is a scene-id table in `CrossEntrance.h`. |
| 2026-11-11 | B1 options | New `RO_*` entries would resize `RANDO_SAVE_OPTIONS` inside the MM save struct (save layout + team-state JSON). | Combo-owned CVars `gCombo.Rando.Entrances.MM.*`, read via `SOH_ReadComboEntranceCVars`; the seed stores the settings + layout. |
| 2026-11-11 | B1 runtime | MM sets `nextEntrance` in many places (void-out, respawn, warps, cutscene skips). Overriding at `VB_SETUP_TRANSITION` would also redirect a void-out whose respawn entrance is a shuffled destination. | Redirect only at the three walk-through exit sites (`z_player.c` `func_808354A4`, `z_door_ana.c`, `z_en_holl.c`). |
| 2026-11-11 | B1 grottos | Most MM grottos enter via one generic `ENTRANCE(GROTTOS, 4)` plus respawn data (see `GetRegionIdFromEntrance`). | Grotto pool deferred to a follow-up with its own redirect. Boss rooms deferred to §B6. |
| 2026-11-11 | owner ruling | Owner: child Link is ALWAYS MM Link (Child Body on by default), masks usable by adult too, every entrance random incl. cross-game, still beatable. | Overrides §B3 "off by default" and §B4 "adult has no transformations". Recorded here; B-M2/B-M3 follow it. |
| 2026-11-12 | B2 pairing | The MD sketches one combined door pool. Both games already shuffle their own doors (B-M1 + SoH ER), and a combined pool would have to re-validate SoH's own placement rules. | Cross pass runs AFTER each game's own shuffle and swaps a percentage of same-kind two-way connections across games, on each game's current layout. Layout stored in the seed (`entrances.cross`), never re-derived. |
| 2026-11-12 | B2 age (owner ruling) | Owner: Link keeps his age across games ("if you came into termina as an adult, you're an adult"). The MD's §B2 forced child arrivals in Termina. | Logic is age-tagged both ways: crossed doors report `D:c`/`D:a`, arrivals root that age only. The runtime switch keeps OOT's save age. Adult body in MM is its own milestone. |
| 2026-11-12 | B2 verify | Generation passed while `--playthrough` failed the same seed. Cause: with OOT ER off, `SOH_ShuffleEntrancesForCombo` returns before burning the oracle's lazy init, so its `RegionTable_Init` ran on the fill's first query and reconnected every crossed OOT door (0 arrival roots in 1436 searches). | `SOH_SetCrossEntrances` / `SOH_DumpEntranceGraph` burn the init first; door-index map liveness check; tripwire log if a crossed door is connected again. |
| 2026-11-12 | B2 verify | Seed batches with OOT ER on: replay lost 16 of 150 OOT crossings (SoH's override apply never sets `replacement`), and 1 seed in 10 crossed 0 doors (stale door-index map after the per-attempt `RegionTable_Init`; layout-dependent, vanished under verbose logging). | One fenced line in `entrance.cpp` (`SetReplacement`), dedupe by door before pairing, index-checked liveness, and an attempt that pairs nothing now fails. |
| 2026-11-13 | §17 HP scaling | `colChkInfo.health` is u8 in both games, and OOT bosses (`Boss_Ganon`, `Boss_Goma`, `Boss_Fd`/`Fd2`, `Boss_Ganondrof`, `Boss_Tw`, `Boss_Mo`, `En_Clear_Tag`) read it as `(s8)`: > 127 reads as dead. Dividing `colChkInfo.damage` instead (§10) turns 1-damage hits into 0, which many enemies treat as "no damage". | Owner asked for HP x N: multiply HP, cap at 127, refund the overflow factor per hit (`HpRefund`). §10's damage-division design is superseded. |
| 2026-11-13 | §17 guard | SessionGuard refused `COMBO_ACTOR_*` unless both saves are seeded, so vanilla co-op could never sync enemies. | Both-unseeded pairs are accepted; seeded vs unseeded still refused. |
| 2026-11-13 | §17 settings | `COMBO_SYNC_CONFIG` (Phase 8) is unimplemented, but the A-M1 world-CVar relay already overlays the room owner's registry values on joiners for the session. | Co-op combat settings are WORLD CVars (`gCombo.Coop.*`) instead of a new packet. |
