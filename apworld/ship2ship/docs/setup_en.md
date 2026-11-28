# Ship2Ship setup

## What you need to generate

- Archipelago 0.6.9 or newer, from source or installed.
- `ship2ship.apworld`, which every Ship2Ship release has, in the `custom_worlds` folder (or `worlds` for a source
  checkout). To build it from the source tree: `python -I scripts/ap/pack_apworld.py --out ship2ship.apworld`.
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

Start Ship2Ship, go to the file select and either drop `AP_<seed>_P<n>_<name>.ship2ship.json` on the game window or open
Combo > Randomizer and press "Load seed file...". Then start a new Randomizer file as for any seed. A check that holds
another player's item shows "<Player>'s <Item>" and gives nothing; opening it sends the check to the server and shows
"Sent <Item> to <Player>".

## Connecting

Load the file, then open Settings > Online > Archipelago. Enter the server (`host:port`, for example `localhost:38281`),
the slot name (the file's own slot is filled in) and the password if the room has one, and press Connect. The server and
slot are saved with the file, so it reconnects when it loads; the password is never saved.

- The checks you open are sent as you open them. Ones you opened while offline are sent when you connect.
- Items other players find for you, and your start inventory, are given in the order the server lists them, in whichever
  game they belong to. Closing the game, losing the connection or restarting the server never gives an item twice or
  loses one: the file counts how many of the server's items it has been given.
- A file only syncs with the room it was generated for, as its own slot.
- The goal (both final bosses, or the Triforce Piece count) is reported to the server once it is reached.
- Anchor co-op is off in an Archipelago file.
- `ws://` and `wss://` both work. A server on `localhost` or `127.0.0.1` is trusted as it is: a room of your own
  usually has a self-signed certificate. For anything else, such as an archipelago.gg room (`archipelago.gg:12345`), the
  certificate has to chain to a trusted root and be valid for the host you typed. If it is not, the status line and the
  console say "certificate could not be verified" and the reason (the certificate has expired, is self-signed, is
  not for that host, or no trusted root was found), and the client stops there: it does not retry, and it does not fall back to a plain `ws://`
  connection, which it would try next for an address typed without `ws://` or `wss://`. Press Connect to try again; type
  `ws://` before the address to connect to a server that has no TLS. The panel says when a build has no TLS
  support (one made with `-DSHIP2SHIP_AP_TLS=OFF`).
- **Where the trusted roots come from.** Windows: the system's root store. Linux: the system's bundle
  (`/etc/ssl/certs/ca-certificates.crt` and the usual places of other distributions), or `SSL_CERT_FILE` /
  `SSL_CERT_DIR` when they are set. macOS: the roots in `cacert.pem` only. On every system `cacert.pem` beside the game
  (Mozilla's CA list, as published by curl) is trusted too, so a machine with missing roots still reaches archipelago.gg.
- **Testing it.** `Ship2Ship --ap-tls-probe archipelago.gg:443` makes one TLS handshake with these checks and exits with 0
  (verified), 1 (certificate refused) or 2 (no answer). From the AppImage: `Ship2Ship.AppImage --ap-tls-probe
  archipelago.gg:443`. Add `--verify-host example.org` to see the refusal for a wrong host name.
