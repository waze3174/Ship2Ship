# WomboComboShip

**Ocarina of Time and Majora's Mask as one game.** Every item, mask and transformation from either game works in
both, and co-op randomizer runs share their enemies, health and supplies.

> [!WARNING]
> **Version 0.1 is an early public test build, not a finished release.**
> - Expect bugs, crashes and things that don't yet behave exactly like the original games.
> - **Please report inaccuracies.** The goal is 1:1 fidelity with each home game: if a move, item, sound, animation
>   or enemy behaves differently from the original, that's a bug. Open an issue with what you did, what you expected
>   (the original game's behaviour) and what happened.
> - **Not competition-ready yet.** Randomizer logic for cross-game items, seed checking and timing aren't stable
>   enough for races or leaderboards. Don't use 0.1 for competitive runs.

> [!IMPORTANT]
> **This is not the official ComboShip.** It's a fan project, a fork of a fork:
>
> **WomboComboShip** ← [junkx420's ComboShip co-op fork](https://github.com/junkx420/ComboShip) ← [**ComboShip** by Varuuna](https://github.com/Varuuna/ComboShip) ← [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) + [2 Ship 2 Harkinian](https://github.com/2ship2harkinian/2ship2harkinian)
>
> - Nearly all of the credit belongs to those projects (see [Credits](#credits)).
> - This fork is not affiliated with, endorsed by, or supported by any of them. Please report problems from this
>   fork here, never upstream.
> - Much of this fork was written with AI assistance.

## What WomboComboShip adds

### 1. True cross-game item usage
Items, masks and forms from either game work in the other, ported from their home game's own code, models,
animations and sounds. They aren't stand-ins.

- **Majora's Mask in Hyrule**
  - Deku, Goron, Zora and Fierce Deity Link, with their real move sets, MM's transformation sequence, voices,
    footsteps and water rules:
    - Deku: spin (with the hat trail), bubbles, pipes
    - Goron: punches, ground pound, spiked rolling, craters, drums
    - Zora: jabs, kicks, boomerang fins, barrier, swimming, walking the bottom, guitar
    - Fierce Deity: the Double Helix sword and its beam
  - Each form attack hits every enemy the way MM's version of that enemy reacts, from MM's own damage tables. For
    example, the Deku spin stuns a Wolfos, hurts a Keese and passes straight through a Skulltula.
  - The Great Fairy's Sword, wielded by child and adult Link.
  - MM masks and items, such as the Powder Keg, Pictograph Box and Blast Mask.
  - MM stray fairies placed at Hyrule checks behave as they do in Termina, and the Great Fairy's Mask draws them in.
- **Ocarina of Time in Termina**
  - OOT items running OOT's own item code, such as the Boomerang and Megaton Hammer.
  - Adult Link with OOT's tunics and Iron / Hover Boots.
  - The Great Fairy's Sword, wielded by adult Link.
  - *Still in progress:* the Slingshot, Din's Fire / Farore's Wind / Nayru's Love, OOT's Hookshot/Longshot, OOT
    masks, and the rest of the Hyrule equipment.
- **Hexagonal pause menus**: both games' pause screens are six-sided prisms showing both inventories, so any item
  can go on any button in either game.
- **One sound engine**: MM's sounds play natively through OOT's audio engine.

### 2. Synced enemies for co-op *(new, experimental)*
- **One enemy, one owner:** each enemy is run by one player in its room. Everyone else sees that exact copy, with
  the same animation, sounds, spawned projectiles, effects and appearance, updated 20 times a second like Anchor's
  player movement.
- **Hits count once:** your hits on an enemy go to its owner, and its attacks reach every player caught in them
  through the game's own damage code. Kills and drops happen once.
- **Health scales with the players in the room**, bosses included, with no health cap. It's a room setting under
  Shared > Settings > Network > Co-op Combat.
- **Every player needs the same build** for enemy sync; different builds won't pair.
- **Planned:** enemies that follow players between rooms, Blood Moon respawns, wandering bosses, and a cross-game
  enemy and boss randomizer.

### 3. Shared health and supplies across both games
- **Current health** carries over exactly between rooms and between Hyrule and Termina.
- **Max health** is one pool: heart pieces and containers from either game count for half, so a full run of both
  games lands on the normal 20-heart maximum.
- **Double Defense** from either game's Great Fairy applies in both.
- **Shared magic and consumables:** arrows, bombs, bombchus, Deku sticks, Deku nuts and magic beans are one count
  across both games.

### 4. Easier co-op setup *(planned)*
- **Share my seed** sending every Randomizer-tab setting exactly, and **Share my save** sending your whole combo
  save to a teammate, are being built and aren't in 0.1.

### Also included
- One settings menu for both games, and shipped control presets.
- From junkx420's co-op fork: background world sync, the partner feed and game-switch notices.
- An optional Anchor tunic color that applies to every tunic and transformation.

## Downloads

Windows, Linux (AppImage) and macOS builds are on the [Releases page](../../releases).

**You need your own legally dumped Ocarina of Time and Majora's Mask ROMs. No game data is included or shared
here.**

## Building it yourself

The build works like the original project. See [`docs/BUILDING_LINUX.md`](docs/BUILDING_LINUX.md) for Linux, and
the [original ComboShip README](https://github.com/Varuuna/ComboShip#readme) for Windows and macOS. Contributor
docs live in `docs/combo/` and `docs/deviations/`.

## Credits

- **ComboShip** by [Varuuna](https://github.com/Varuuna) and contributors: the foundation everything here runs on.
- **ComboShip co-op fork** by [junkx420](https://github.com/junkx420/ComboShip): background world sync and the
  partner feed.
- **Ship of Harkinian** by the [HarbourMasters](https://github.com/HarbourMasters/Shipwright) team, including the
  Anchor co-op mode.
- **2 Ship 2 Harkinian** by the [2 Ship 2 Harkinian](https://github.com/2ship2harkinian/2ship2harkinian) team.
- **libultraship** by [Kenix3](https://github.com/Kenix3/libultraship) and contributors.
- Inspired by [OoTMM](https://ootmm.com/).

## License

Nothing is relicensed here. Each bundled project keeps its own license: see the upstream repositories linked above
and [`libultraship/LICENSE`](libultraship/LICENSE). This fork's changes are offered under the same terms as the
files they modify.
