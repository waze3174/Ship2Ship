# Ship2Ship setup

## What you need to generate

- Archipelago 0.6.9 or newer, from source or installed.
- `ship2ship.apworld` in the `custom_worlds` folder (or `worlds` for a source checkout).
- The generator pack or a Ship2Ship install. It contains `comborando` next to the two game libraries. No ROMs are
  needed to generate.

## Telling Archipelago where comborando is

Open `host.yaml` and set the path in the Ship2Ship section:

```yaml
ship2ship_options:
  comborando_path: C:/Ship2Ship/comborando.exe
```

`comborando` must stay next to the game libraries it shipped with; generating starts it from that folder.

## Your YAML

Create a template from the Archipelago launcher ("Generate Template Options") and edit `Ship2Ship.yaml`. The
cross-game choices are real options. Each game's own settings go in `oot_settings` and `mm_settings`, as the setting
names Ship2Ship's seed files use. OoT No Logic is not supported, and neither is starting in Majora's Mask.

## Generating

Generate as usual. Each Ship2Ship slot starts one `comborando` process, which Archipelago asks about 2,000 to 3,500
times (each answer takes tens of milliseconds), so a solo slot takes two to three minutes and each extra Ship2Ship slot
adds about as much. The output holds the slot's seed as `AP_<seed>_P<n>_<name>.ship2ship.json` next to the usual
`.archipelago` file.

Entrance shuffles can produce layouts nothing can be played in, such as one with nothing reachable at the start.
Like Ship2Ship's own generator, the world then rolls a new layout, up to 30 times, and the log says which attempt it
kept.

## Playing your slot's seed

Until the Archipelago client arrives, a slot's seed plays offline. Start Ship2Ship, go to the file select and either drop
`AP_<seed>_P<n>_<name>.ship2ship.json` on the game window or open Combo > Randomizer and press "Load seed file...". Then
start a new Randomizer file as for any seed. A check that holds another player's item shows "<Player>'s <Item>" and
gives nothing; the other player's items, and your own items found by them, are not delivered yet.
