"""A stand-in for `comborando --oracle` over a four-check toy world, for the apworld's unit tests.

L0 is always reachable, L1 needs a Key, L2 needs two Keys, and M0 (Majora's Mask) needs a Mask. Ganon is L2 and Majora is
M0. Options after --oracle: --die-on-reach exits when asked to reach, --protocol N changes what hello says.
"""
import json
import sys

LOCATIONS = [["oot", "L0"], ["oot", "L1"], ["oot", "L2"], ["mm", "M0"]]
POOL = [
    {"game": "oot", "name": "Key", "count": 2, "class": "progression", "category": "major", "advancement": True},
    {"game": "mm", "name": "Mask", "count": 1, "class": "progression", "category": "mask", "advancement": True},
    {"game": "oot", "name": "Blue Rupee", "count": 1, "class": "filler", "category": "junk", "advancement": False},
]


def reached_bits(items):
    keys = items.get("oot", {}).get("Key", 0)
    mask = items.get("mm", {}).get("Mask", 0)
    return [True, keys >= 1, keys >= 2, mask >= 1]


def mask_hex(bits):
    return format(sum(1 << i for i, b in enumerate(bits) if b), "x")


def handle(request, protocol):
    command = request["cmd"]
    if command == "hello":
        return {"protocol": protocol, "games": ["oot", "mm"], "library": {"ship2ship": "fake"}}
    if command == "open":
        return {"masterSeed": request["masterSeed"], "locations": LOCATIONS, "neverReachable": [], "items": POOL,
                "fixed": [], "forced": [], "startingOwned": {"oot": [], "mm": []},
                "effective": {"startingGame": "OOT", "echo": request},
                "win": {"condition": "ganon+majora", "requiredPieces": 0}}
    if command == "reach":
        bits = reached_bits(request["items"])
        return {"mask": mask_hex(bits), "fixedMask": "0", "unknown": [],
                "win": {"ganon": bits[2], "majora": bits[3], "pieces": 0, "won": bits[2] and bits[3]}}
    if command == "finalize":
        others = [p for p in request["placements"] if "other" in p]
        return {"seed": {"placements": request["placements"], "archipelago": {"otherPlayersItems": others}}}
    if command == "fail":
        return {"error": "asked to fail"}
    return {}


def main(argv):
    protocol = int(argv[argv.index("--protocol") + 1]) if "--protocol" in argv else 1
    for line in sys.stdin:
        request = json.loads(line)
        if request["cmd"] == "reach" and "--die-on-reach" in argv:
            print("fake oracle: dying on reach", file=sys.stderr)
            return 3
        response = handle(request, protocol)
        response["ok"] = "error" not in response
        sys.stdout.write(json.dumps(response) + "\n")
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
