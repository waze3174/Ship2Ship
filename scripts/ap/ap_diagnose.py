"""Explain an Archipelago accessibility failure from a Ship2Ship generation log.

When Archipelago's final check says a location cannot be reached, this replays the failed slot against a fresh oracle:
it reads the open request and the placements out of generate.log, sweeps the way Archipelago does (collect the
progression items at reachable locations until nothing new opens), and reports

  - which locations the sweep cannot reach, and whether owning every placed item (junk too) would reach them;
  - which single non-progression item names open them, i.e. logic that depends on an item the apworld calls junk;
  - whether the oracle answers the same question the same way twice, and again after a finalize.

  ap_diagnose.py --comborando <comborando> --log <generate.log> [--player Solo]
"""
import argparse
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from world_modules import load_world_package  # noqa: E402

MODULES = load_world_package()
oracle, reach, seedout, tables = (MODULES[name] for name in ("oracle", "reach", "seedout", "tables"))

PLACEMENT_PATTERN = re.compile(r"\((.+?\((?:OoT|MM)\)), (.+?\((?:OoT|MM)\))\)")
REPORT_LIMIT = 12


def read_log(path):
    return path.read_text(encoding="utf-8", errors="replace")


def open_request_of(log, player):
    match = re.search(rf"^{re.escape(player)}: open request (\{{.*\}})$", log, re.M)
    if not match:
        sys.exit(f"no '{player}: open request' line in the log")
    return json.loads(match.group(1))


def placements_of(log):
    start = log.index("All Placements:")
    line = log[log.index("[(", start):].splitlines()[0]
    return dict(PLACEMENT_PATTERN.findall(line))


def counts_of(ap_names):
    owned = {"oot": {}, "mm": {}}
    for name in ap_names:
        game, native = tables.split_ap_name(name)
        owned[game][native] = owned[game].get(native, 0) + 1
    return owned


class Replay:
    def __init__(self, client, opened, placed):
        self.client = client
        self.opened = opened
        self.placed = placed
        self.bit_of = {tables.ap_name(g, n): bit for bit, (g, n) in enumerate(opened.locations)}
        self.progression = {tables.ap_name(e.game, e.name) for e in opened.pool if e.item_class == "progression"}

    def mask(self, ap_names):
        answer = self.client.call(oracle.reach_request(counts_of(ap_names)))
        return int(answer["mask"], 16)

    def reachable(self, mask):
        return {name for name, bit in self.bit_of.items() if mask >> bit & 1}

    def sweep(self, extra=()):
        """Archipelago's sweep: own what progression the reachable locations hold, repeat."""
        owned, seen = list(extra), set()
        while True:
            reached = self.reachable(self.mask(owned))
            fresh = [n for n in reached - seen if self.placed.get(n) in self.progression]
            seen |= reached
            if not fresh:
                return reached, owned
            owned.extend(self.placed[n] for n in fresh)


def report(replay, client):
    reached, owned = replay.sweep()
    everything = [item for location, item in replay.placed.items() if location in replay.bit_of]
    with_all = replay.reachable(replay.mask(everything))
    missing = sorted(set(replay.bit_of) - reached)
    print(f"sweep reaches {len(reached)} of {len(replay.bit_of)} locations; {len(missing)} not reached")
    for name in missing[:REPORT_LIMIT]:
        print(f"  not reached: {name} (holds {replay.placed.get(name)}); reachable with every placed item owned: "
              f"{name in with_all}")
    junk_names = sorted({item for item in everything if item not in replay.progression})
    for name in missing[:REPORT_LIMIT]:
        opening = [j for j in junk_names if name in replay.reachable(replay.mask(owned + [j]))]
        if opening:
            print(f"  {name} opens when this non-progression item is owned: {opening[:REPORT_LIMIT]}")
    first = replay.mask(owned)
    print(f"same question twice gives the same answer: {replay.mask(owned) == first}")
    placements = [seedout.own_placement(g, n, *tables.split_ap_name(replay.placed[tables.ap_name(g, n)]))
                  for g, n in replay.opened.locations if tables.ap_name(g, n) in replay.placed]
    finalized = client.call(seedout.finalize_request(placements))
    print(f"finalize ok: {bool(finalized.get('ok'))}; same answer after finalize: {replay.mask(owned) == first}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--comborando", required=True)
    parser.add_argument("--log", type=pathlib.Path, required=True)
    parser.add_argument("--player", default="Solo")
    args = parser.parse_args()
    log = read_log(args.log)
    request = open_request_of(log, args.player)
    client = oracle.OracleProcess.start(args.comborando, str(pathlib.Path(args.comborando).parent))
    try:
        opened = oracle.parse_opened(client.call(request))
        report(Replay(client, opened, placements_of(log)), client)
    finally:
        client.close()


if __name__ == "__main__":
    main()
