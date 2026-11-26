"""The finished slot: AP's placements as the oracle's `finalize` request, and the consolidated seed it returns.

The seed is the same file Ship2Ship's own generator writes. It goes into slot_data compressed, because it is large
(every check of both games) and slot_data travels as JSON.
"""
import base64
import json
import zlib
from typing import Any, Dict, Iterable, List, Mapping

SEED_ENCODING = "zlib+base64"
COMPRESSION_LEVEL = 9
SEED_FILE_EXTENSION = ".ship2ship.json"


def own_placement(game: str, check: str, item_game: str, item: str) -> Dict[str, Any]:
    """One of this slot's own items, by the game's own names."""
    return {"game": game, "check": check, "item": {"game": item_game, "name": item}}


def other_placement(game: str, check: str, player: str, item: str, advancement: bool) -> Dict[str, Any]:
    """Another player's item (or one the oracle's pool does not hold): the game shows native junk and the client
    overlays the real item from the server."""
    return {"game": game, "check": check, "other": {"player": player, "name": item, "advancement": advancement}}


def finalize_request(placements: Iterable[Mapping[str, Any]]) -> Dict[str, Any]:
    return {"cmd": "finalize", "placements": list(placements)}


def encode_seed(seed: Mapping[str, Any]) -> str:
    raw = json.dumps(seed, separators=(",", ":")).encode("utf-8")
    return base64.b64encode(zlib.compress(raw, COMPRESSION_LEVEL)).decode("ascii")


def decode_seed(text: str) -> Dict[str, Any]:
    return json.loads(zlib.decompress(base64.b64decode(text)).decode("utf-8"))


def slot_data_for(seed: Mapping[str, Any], opened_effective: Mapping[str, Any], library: Mapping[str, Any],
                  master_seed: int, protocol: int) -> Dict[str, Any]:
    """What the client receives on connecting."""
    text = encode_seed(seed)
    return {
        "seed": text,
        "seed_encoding": SEED_ENCODING,
        "seed_json_bytes": len(json.dumps(seed, separators=(",", ":"))),
        "master_seed": master_seed,
        "effective": dict(opened_effective),
        "oracle_protocol": protocol,
        "library": dict(library),
    }


def other_players_items(seed: Mapping[str, Any]) -> List[Mapping[str, Any]]:
    return list(seed.get("archipelago", {}).get("otherPlayersItems", []))
