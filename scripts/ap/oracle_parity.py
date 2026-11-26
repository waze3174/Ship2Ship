"""Check that `comborando --oracle` answers what the game's own fill and playthrough use.

For each case it generates a seed with comborando, replays it with `--playthrough`, opens the same seed in an oracle
session and compares:

  parity       a forward sweep through the oracle (collect what is reachable, repeat) reaches every check the
               playthrough reached by the same sphere or earlier, the whole seed reaches the same number of checks
               in each game, and the win flag agrees. The oracle credits the confined placements (own-dungeon keys,
               songs, shop buys) inside one call, so it may take fewer spheres than the playthrough, never more.
  monotonicity random chains of item additions never shrink the reachable set. The Archipelago fill needs this.
  determinism  the same open gives the same locations, pool and answers.
  finalize     the placements fed back to finalize give the seed comborando wrote (hints aside).
  timing       per-call times.

Generate mode writes saves/combo/ into the working directory, so run this from a build folder or a scratch copy of the
game libraries, never from an install folder. The oracle itself writes nothing.

Usage (standard library only):
  python -I oracle_parity.py --comborando ./comborando                  all cases
  python -I oracle_parity.py --comborando ./comborando --cases default,cross
  python -I oracle_parity.py --comborando ./comborando.exe --smoke      hello, open, one reach, close
"""
import argparse
import json
import pathlib
import random
import statistics
import subprocess
import sys
import tempfile
import time

# name -> comborando generate arguments
CASES = {
    "default": [],
    "solo-oot": ["--games", "oot"],
    "solo-mm": ["--games", "mm"],
    "unified": ["--unified"],
    "cross": ["--cross", "all,pct=25"],
    "shared": ["--shared-items", "all"],
    "mm-start": ["--starting-game", "mm"],
    "lwl-off": ["--no-lost-woods-link"],
    "hunt": ["--triforce-required", "10", "--triforce-total", "14"],
    "mm-entrances": ["--mm-entrances", "all"],
}
PROTOCOL = 1
SPOILER_PATH = pathlib.Path("saves/combo/comborando.spoiler.json")
GAME_SUFFIXES = (" (OOT)", " (MM)")
MM_JUNK_PLACEHOLDER = "Junk"
MONOTONE_CHAINS = 3
MONOTONE_STEP = 8
SPHERE_LIMIT = 200
MM_GAME = "mm"
LOG_TAIL_CHARS = 6000


class Failure(Exception):
    pass


class Oracle:
    """One `comborando --oracle` process; every call is timed."""

    def __init__(self, comborando, cwd, log):
        self.process = subprocess.Popen([comborando, "--oracle"], cwd=cwd, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1)
        self.calls = []

    def call(self, request):
        started = time.perf_counter()
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        elapsed = (time.perf_counter() - started) * 1000
        if not line:
            raise Failure(f"the oracle exited during '{request.get('cmd')}'")
        response = json.loads(line)
        self.calls.append((request["cmd"], elapsed))
        if not response.get("ok"):
            raise Failure(f"{request['cmd']} failed: {response.get('error')}")
        return response

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()


def strip_suffix(name):
    for suffix in GAME_SUFFIXES:
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return name


def run(command, cwd, log):
    return subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, text=True).returncode


def generate(comborando, case, cwd, log):
    """Generate one seed for the case; returns the consolidated seed and the playthrough replay."""
    code = run([comborando, "--seed", f"ap-{case}", "--count", "1", *CASES[case]], cwd, log)
    if code != 0:
        raise Failure(f"generating '{case}' exited with {code}")
    spoiler_file = pathlib.Path(cwd) / SPOILER_PATH
    spoiler = json.loads(spoiler_file.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp:
        replay_file = pathlib.Path(tmp) / "replay.json"
        run([comborando, "--playthrough", str(SPOILER_PATH), "--playthrough-json", str(replay_file)], cwd, log)
        if not replay_file.exists():
            raise Failure(f"--playthrough of '{case}' wrote no replay")
        return spoiler, json.loads(replay_file.read_text(encoding="utf-8"))


def open_request(spoiler):
    request = {
        "cmd": "open",
        "masterSeed": spoiler["masterSeed"],
        "seed": spoiler["seed"],
        "settings": {
            "oot": {"settings": spoiler["oot"]["settings"], "enabledTricks": spoiler["oot"].get("enabledTricks", [])},
            "mm": {"settings": spoiler["mm"]["settings"]},
        },
        "games": spoiler["games"],
        "goal": spoiler["goal"],
        "startingGame": spoiler["startingGame"],
        "sharedItems": spoiler.get("sharedItems", []),
        "unifiedInventory": spoiler.get("unifiedInventory", False),
        "formLogicCredit": spoiler.get("formLogicCredit", False),
        "lostWoodsLink": spoiler.get("lostWoodsLink", False),
        "entranceSettings": spoiler.get("entranceSettings", {}),
    }
    if "masterQuestDungeons" in spoiler:
        request["masterQuestDungeons"] = spoiler["masterQuestDungeons"]
    return request


def placements_of(spoiler, known_names):
    """{(game, check): (item game, item name)} from a consolidated seed, with the names the oracle's pool uses."""
    foreign = {(f["checkGame"], f["checkName"]): (f["itemGame"], f["itemName"]) for f in spoiler.get("foreign", [])}
    placed = {}
    for game in ("oot", MM_GAME):
        for check, item in spoiler[game]["placements"].items():
            item_game, name = foreign.get((game, check), (game, strip_suffix(item)))
            if name not in known_names[item_game] and item_game == MM_GAME:
                name = MM_JUNK_PLACEHOLDER  # a cross-placed MM junk, baked into a rotation name at generation
            placed[(game, check)] = (item_game, name)
    return placed


def counts(items):
    owned = {"oot": {}, MM_GAME: {}}
    for game, name in items:
        owned[game][name] = owned[game].get(name, 0) + 1
    return owned


def bits(mask_hex):
    return int(mask_hex, 16)


def bit_set(value, index):
    return (value >> index) & 1


class World:
    """An opened seed and what the comparisons need to read off it."""

    def __init__(self, oracle, spoiler):
        self.oracle = oracle
        self.spoiler = spoiler
        self.opened = oracle.call(open_request(spoiler))
        self.locations = [tuple(loc) for loc in self.opened["locations"]]
        self.index = {loc: i for i, loc in enumerate(self.locations)}
        self.credited = [(f["game"], f["check"]) for f in self.opened["fixed"] + self.opened["forced"]]
        known = {"oot": set(), MM_GAME: set()}
        for item in self.opened["items"]:
            known[item["game"]].add(item["name"])
        for placement in self.opened["fixed"] + self.opened["forced"]:
            known[placement["itemGame"]].add(placement["item"])
        self.placed = placements_of(spoiler, known)

    def reach(self, owned):
        return self.oracle.call({"cmd": "reach", "items": owned})

    def location_items(self):
        return [self.placed[loc] for loc in self.locations]


def check_open(world):
    """The oracle's checks (fillable, confined, forced) are exactly the seed's checks."""
    from_oracle = set(world.locations) | set(world.credited)
    from_seed = set(world.placed)
    if from_oracle != from_seed:
        extra, missing = sorted(from_oracle - from_seed)[:5], sorted(from_seed - from_oracle)[:5]
        raise Failure(f"open and the seed disagree on the checks: oracle-only {extra}, seed-only {missing}")


def sweep(world):
    """Forward sweep through the oracle. Returns the reached locations, newly reached per sphere."""
    owned_items, reached, spheres = [], set(), []
    for _ in range(SPHERE_LIMIT):
        response = world.reach(counts(owned_items))
        mask = bits(response["mask"])
        newly = [loc for i, loc in enumerate(world.locations) if bit_set(mask, i) and loc not in reached]
        if not newly:
            return reached, spheres, response
        spheres.append(newly)
        reached.update(newly)
        owned_items.extend(world.placed[loc] for loc in newly)
    raise Failure(f"the oracle sweep did not settle in {SPHERE_LIMIT} spheres")


def reached_per_game(world, response):
    """Checks reached per game over the locations and the confined (fixed and forced) checks."""
    mask, credited = bits(response["mask"]), bits(response["fixedMask"])
    per_game = {"oot": 0, MM_GAME: 0}
    for i, (game, _) in enumerate(world.locations):
        per_game[game] += bit_set(mask, i)
    for i, (game, _) in enumerate(world.credited):
        per_game[game] += bit_set(credited, i)
    return per_game


def check_parity(world, replay):
    reached, spheres, last = sweep(world)
    playthrough_by_sphere = []
    for sphere in replay["spheres"]:
        playthrough_by_sphere.append({(s["game"], s["check"]) for s in sphere["steps"] if (s["game"], s["check"]) in world.index})
    oracle_so_far, playthrough_so_far = set(), set()
    exact = 0
    for k, expected in enumerate(playthrough_by_sphere):
        playthrough_so_far |= expected
        if k < len(spheres):
            oracle_so_far |= set(spheres[k])
        missing = playthrough_so_far - oracle_so_far
        if missing:
            raise Failure(f"playthrough sphere {k} reached {sorted(missing)[:5]} that the oracle sweep had not by then")
        exact += 1 if k < len(spheres) and set(spheres[k]) == expected else 0
    # The playthrough's last pass owns every placed item, the confined ones too, whether or not their check is
    # reachable; the oracle credits a confined item when its check is reached, as the fill does. Own the confined items
    # as well for the comparison, and report how many checks the credited-on-reach rule costs.
    confined = [(f["itemGame"], f["item"]) for f in world.opened["fixed"] + world.opened["forced"]]
    everything = world.reach(counts(world.location_items() + confined))
    strict = world.reach(counts(world.location_items()))
    expected = {"oot": replay["reachableOot"], MM_GAME: replay["reachableMm"]}
    reached_by = lambda response: reached_per_game(world, response)
    if reached_by(everything) != expected:
        raise Failure(f"the full inventory reaches {reached_by(everything)} checks, the playthrough {expected}")
    cost = {g: expected[g] - reached_by(strict)[g] for g in expected}
    if world.opened["win"]["condition"] == "ganon+majora":
        wins = replay["ganonReachable"] and replay["majoraReachable"]
        if bool(everything["win"]["won"]) != bool(wins):
            raise Failure(f"win flag: oracle {everything['win']['won']}, playthrough {wins}")
    return (f"{len(spheres)} oracle spheres vs {len(playthrough_by_sphere)} playthrough, {exact} identical, "
            f"confined items on unreachable checks cost {cost['oot']} OoT and {cost['mm']} MM")


def check_monotone(world, rng):
    items = world.location_items()
    for chain in range(MONOTONE_CHAINS):
        order = items[:]
        rng.shuffle(order)
        owned, previous_mask, previous_credited, previous_won = [], 0, 0, False
        for start in range(0, len(order), MONOTONE_STEP):
            added = order[start:start + MONOTONE_STEP]
            owned.extend(added)
            response = world.reach(counts(owned))
            mask, credited = bits(response["mask"]), bits(response["fixedMask"])
            lost = previous_mask & ~mask
            if lost or previous_credited & ~credited or (previous_won and not response["win"]["won"]):
                names = [world.locations[i] for i in range(len(world.locations)) if bit_set(lost, i)][:5]
                raise Failure(f"NOT MONOTONE in chain {chain}: adding {added} lost {names} "
                              f"(credited lost {previous_credited & ~credited:#x}, won was {previous_won})\n"
                              f"owned before: {counts(owned[:-len(added)])}")
            previous_mask, previous_credited, previous_won = mask, credited, response["win"]["won"]
    return f"{MONOTONE_CHAINS} chains of {len(items)} items, step {MONOTONE_STEP}"


def check_determinism(world, rng):
    sample = counts(rng.sample(world.location_items(), len(world.location_items()) // 2))
    first = world.reach(sample)
    again = World(world.oracle, world.spoiler)
    for key in ("locations", "neverReachable", "items", "fixed", "forced", "effective", "startingOwned", "win"):
        if again.opened[key] != world.opened[key]:
            raise Failure(f"opening the same seed twice gave a different '{key}'")
    second = again.reach(sample)
    for key in ("mask", "fixedMask", "win"):
        if first[key] != second[key]:
            raise Failure(f"the same items gave a different '{key}' after re-opening")
    return "open and reach repeat"


def normalized_foreign(markers):
    """By check, without the trap disguises: those are drawn in placement order, which the oracle does not share."""
    kept = [{k: v for k, v in m.items() if not k.startswith("fake")} for m in markers]
    return sorted(kept, key=lambda m: (m["checkGame"], m["checkName"]))


def check_finalize(world):
    placements = [{"game": game, "check": check, "item": {"game": item_game, "name": name}}
                  for (game, check), (item_game, name) in zip(world.locations, world.location_items())]
    seed = world.oracle.call({"cmd": "finalize", "placements": placements})["seed"]
    wanted = {k: v for k, v in world.spoiler.items() if k != "hints"}
    wanted["foreign"], seed["foreign"] = normalized_foreign(wanted["foreign"]), normalized_foreign(seed["foreign"])
    differing = sorted(k for k in set(wanted) | set(seed) if wanted.get(k) != seed.get(k))
    if differing:
        details = []
        for key in differing[:4]:
            a, b = wanted.get(key), seed.get(key)
            if isinstance(a, dict) and isinstance(b, dict):
                inner = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
                details.append(f"{key}: {inner[:4]}")
            else:
                details.append(key)
        raise Failure(f"finalize differs from the generated seed in {details}")
    return "finalize reproduces the generated seed"


def run_case(comborando, case, cwd, log, rng, timings):
    spoiler, replay = generate(comborando, case, cwd, log)
    oracle = Oracle(comborando, cwd, log)
    try:
        world = World(oracle, spoiler)
        notes = []
        for check in (lambda: check_open(world), lambda: check_parity(world, replay), lambda: check_monotone(world, rng),
                      lambda: check_determinism(world, rng), lambda: check_finalize(world)):
            notes.append(check() or "ok")
        notes.insert(0, f"{len(world.locations)} locations, {len(world.opened['neverReachable'])} never reachable")
        return notes
    finally:
        timings.extend(oracle.calls)
        oracle.close()


def print_timings(calls):
    print("per-call times (ms):")
    for cmd in ("open", "reach", "finalize"):
        times = sorted(ms for name, ms in calls if name == cmd)
        if times:
            p95 = times[min(len(times) - 1, int(len(times) * 0.95))]
            print(f"  {cmd:9} n={len(times):5} median={statistics.median(times):8.1f} p95={p95:8.1f} max={times[-1]:8.1f}")


def smoke(comborando, cwd, log):
    oracle = Oracle(comborando, cwd, log)
    try:
        hello = oracle.call({"cmd": "hello"})
        if hello["protocol"] != PROTOCOL:
            raise Failure(f"protocol {hello['protocol']}, expected {PROTOCOL}")
        opened = oracle.call({"cmd": "open", "masterSeed": 1})
        reach = oracle.call({"cmd": "reach", "items": {}})
        oracle.call({"cmd": "close"})
        print(f"smoke ok: {len(opened['locations'])} locations, {len(opened['items'])} pool entries, "
              f"mask {len(reach['mask'])} hex digits; library {hello['library']}")
    finally:
        oracle.close()
        print_timings(oracle.calls)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--comborando", required=True)
    parser.add_argument("--cwd", default=".", help="folder next to the game libraries (default: here)")
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--random-seed", type=int, default=1)
    args = parser.parse_args()
    with tempfile.TemporaryFile("w+") as log:
        try:
            if args.smoke:
                smoke(args.comborando, args.cwd, log)
                return 0
            failures, timings, rng = [], [], random.Random(args.random_seed)
            for case in args.cases.split(","):
                try:
                    print(f"[{case}] " + "; ".join(run_case(args.comborando, case, args.cwd, log, rng, timings)))
                except Failure as failure:
                    print(f"[{case}] FAIL: {failure}")
                    failures.append(case)
            print_timings(timings)
            if failures:
                print(f"FAILED: {', '.join(failures)}")
                print_log_tail(log)
                return 1
            print("all cases passed")
            return 0
        except Failure as failure:
            print(f"FAIL: {failure}")
            print_log_tail(log)
            return 1


def print_log_tail(log):
    """What comborando and the oracle printed last: the first thing to read after a failure."""
    log.seek(0)
    print("--- tail of the comborando output ---")
    print(log.read()[-LOG_TAIL_CHARS:])


if __name__ == "__main__":
    sys.exit(main())
