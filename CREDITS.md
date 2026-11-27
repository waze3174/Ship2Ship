# Credits

Ship2Ship is an unofficial fan project. It is not made by, affiliated with or endorsed by any project listed here,
and nearly all of the work in it is theirs. Ship2Ship was called ComboShip Ultimate until 2026-11-24.

## Lineage

Ship2Ship is built from these projects:
- [Varuuna's ComboShip](https://github.com/Varuuna/ComboShip)
- [junkx420's ComboShip co-op work](https://github.com/junkx420/ComboShip)
- the cross-game forms, masks and items of
  [Captain-Backwash's WomboComboShip](https://github.com/Captain-Backwash/WomboComboShip) (main, commit `35ae4d55`)

All of it sits on Ship of Harkinian and 2 Ship 2 Harkinian, which in turn are built on the Zelda decompilation
projects.

## Projects and people

- **ComboShip** by [Varuuna](https://github.com/Varuuna) and contributors: the cross-game randomizer that
  everything here runs on, including the game switching, the combined fill and the trackers. The Clock Shuffle
  guards come from ComboShip pull request #251.
- **ComboShip co-op fork** by [junkx420](https://github.com/junkx420/ComboShip): background world sync, the live
  partner feed and the game-switch notices, as WomboComboShip's README credits them.
- **WomboComboShip** by [Captain-Backwash](https://github.com/Captain-Backwash/WomboComboShip): Majora's Mask forms,
  masks and items in Hyrule, Ocarina of Time items in Termina, and the merged pause menus. The exact split of
  authorship between WomboComboShip and junkx420's fork has not been checked.
- **Ship of Harkinian** by the [HarbourMasters](https://github.com/HarbourMasters/Shipwright) team, including its
  randomizer and the Anchor co-op mode.
- **2 Ship 2 Harkinian** by the [2 Ship 2 Harkinian](https://github.com/2ship2harkinian/2ship2harkinian) team,
  including its randomizer.
- **libultraship** by [Kenix3](https://github.com/Kenix3/libultraship) and contributors.
- **The Ocarina of Time and Majora's Mask decompilations** by [zeldaret](https://github.com/zeldaret).
- **[OoTMM](https://github.com/OoTMM/OoTMM)**: the original Ocarina of Time + Majora's Mask combo randomizer.
  Ship2Ship's goal and its "Games" setting (both games, or one) follow OoTMM's.
- **Archipelago item models** by aMannus, from the Archipelago client for Ship of Harkinian
  ([jeromkiller/Shipwright_archipellago](https://github.com/jeromkiller/Shipwright_archipellago), `30fe440dd`): the
  three display lists another player's item is drawn as. They are used under Ship of Harkinian's `ASSETS-LICENSE.md`
  permission for Ship projects.
- **The Archipelago online client** ([`combo/net/ComboArchipelago.cpp`](combo/net/ComboArchipelago.cpp)) is built on
  [apclientpp](https://github.com/black-sliver/apclientpp) and [wswrap](https://github.com/black-sliver/wswrap) by
  black-sliver and contributors (MIT), [websocketpp](https://github.com/zaphoyd/websocketpp) by Peter Thorson
  (BSD-3-Clause), [asio](https://github.com/chriskohlhoff/asio) by Christopher Kohlhoff (Boost Software License 1.0)
  and [valijson](https://github.com/tristanpenman/valijson) by Tristan Penman (BSD-2-Clause). The way each game's
  client receives items and sends checks follows junkx420's Archipelago clients,
  [Updated-2S2H-Archipelago](https://github.com/junkx420/Updated-2S2H-Archipelago) (`client` branch, `69cc318`, CC0) and
  [Updated-SoH-Archipelago](https://github.com/junkx420/Updated-SoH-Archipelago) (`client` branch, `c0e2cc3`, no license
  file, so only read for the protocol: none of its code is included). The same versions of the libraries are pinned.
- **The Archipelago console** ([`combo/net/ComboApConsole.h`](combo/net/ComboApConsole.h),
  [`combo/net/ComboApConsoleInput.h`](combo/net/ComboApConsoleInput.h)) follows the text client of
  [Archipelago](https://github.com/ArchipelagoMW/Archipelago) (MIT; `NetUtils.py`, `kvui.py` and `CommonClient.py` at the
  pinned core `2c64ef4`): the message colours and their palette, and the `/` commands. No code is copied.
- **[MMRecompRando](https://github.com/RecompRando/MMRecompRando)**: the idea of walking back out of the Clock Tower
  to the Lost Woods. Only the idea is used; no code from it is included.

Ship2Ship's own changes are put together by [waze3174](https://github.com/waze3174), with AI assistance.

## License

Nothing is relicensed here. Each part keeps its own license: see the repositories linked above and
[`libultraship/LICENSE`](libultraship/LICENSE). Changes made in Ship2Ship are offered under the same terms as the
files they modify.
