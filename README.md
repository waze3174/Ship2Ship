# ComboShip Ultimate

**Ocarina of Time and Majora's Mask as one randomizer, with Majora's Mask forms, masks and items usable in Hyrule.**

> [!IMPORTANT]
> **This is an unofficial fan build.** It is not made by, affiliated with or endorsed by HarbourMasters, Varuuna
> (ComboShip) or Captain-Backwash (WomboComboShip). Please report problems from this build here, never upstream.
> Parts of it were written with AI assistance. See [`CREDITS.md`](CREDITS.md) for whose work this is built on.

ComboShip Ultimate is [Varuuna's ComboShip](https://github.com/Varuuna/ComboShip), the cross-game randomizer for
[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/2ship2harkinian/2ship2harkinian), plus the cross-game forms, masks and items
from [Captain-Backwash's WomboComboShip](https://github.com/Captain-Backwash/WomboComboShip).

> [!WARNING]
> **Early test build.** Expect bugs. It is not ready for races or leaderboards.

## What this build adds to ComboShip

- **Majora's Mask in Hyrule:** Deku, Goron, Zora and Fierce Deity Link with their move sets, MM's masks and items
  (Powder Keg, Pictograph Box, Blast Mask and more) and stray fairies.
- **Ocarina of Time in Termina:** OoT items such as the Boomerang and Megaton Hammer, and adult Link. Some items are
  still in progress (Slingshot, the magic spells, Hookshot and Longshot).
- **Hexagonal pause menus** showing both inventories, and MM's sounds through OoT's audio engine.
- **Co-op extras on the base Anchor mode:** background world sync, a partner feed and game-switch notices.

Off unless you turn them on in the combo menu: shared health, magic and ammo; logic that credits Zora and Goron
form abilities (a seed setting); and the link that mirrors camera and ocarina settings between the two games.

Not included: WomboComboShip's synced co-op enemies and its seed and world-setting sharing.

## Builds

Windows, Linux (AppImage) and macOS builds come from this repository's Actions tab (the **Build Artifacts**
workflow). There are no tagged releases yet.

**You need your own legally dumped Ocarina of Time and Majora's Mask ROMs. No game data is included or shared
here.**

## Building it yourself

The build works like the original project. See [`docs/BUILDING_LINUX.md`](docs/BUILDING_LINUX.md) for Linux, and
the [original ComboShip README](https://github.com/Varuuna/ComboShip#readme) for Windows and macOS. Contributor
docs live in `docs/combo/` and `docs/deviations/`.

## Credits

See [`CREDITS.md`](CREDITS.md).

## License

Nothing is relicensed here. Each bundled project keeps its own license: see the upstream repositories linked above
and [`libultraship/LICENSE`](libultraship/LICENSE). This fork's changes are offered under the same terms as the
files they modify.
