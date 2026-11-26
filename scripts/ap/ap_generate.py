"""Generate Ship2Ship slots with Archipelago's own Generate.py and check what comes out. CI runs this; it needs a
comborando with its game libraries beside it, an Archipelago checkout with the packed apworld in custom_worlds/ and a
host.yaml that points ship2ship_options.comborando_path at comborando.

  ap_generate.py --ap-root <Archipelago> --comborando <comborando> --work <folder> [--cases solo,plando]
        the short suite: solo, with APQuest, two Ship2Ship slots, an item link, start inventory from pool, item plando
  ap_generate.py ... --fill-rate --count 50 [--presets default,shared,...] [--jobs 3] [--max-failure-percent 2]
        the failure rate of the fill, per option preset

Every generated Ship2Ship slot is checked three ways: its output file equals the seed in the multidata's slot_data, the
slot_data seed decodes, and, when the slot's items are all its own, comborando --playthrough judges the seed beatable.
That last check is ours, independent of Archipelago's own fill and accessibility check.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
import zlib

SCRIPTS = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
from world_modules import load_module  # noqa: E402

ROOT = SCRIPTS.parent.parent
SEEDOUT = ROOT / "apworld" / "ship2ship" / "seedout.py"
SHARED_FAMILIES = ROOT / "apworld" / "ship2ship" / "data" / "shared_families.json"
GAME = "Ship2Ship"
SEED_FILE_SUFFIX = ".ship2ship.json"
GENERATE_TIMEOUT_SECONDS = 1500
PLAYTHROUGH_TIMEOUT_SECONDS = 300
BASE_SEED = 1000
SPOILER_LEVEL = "2"
FILL_RATE_SPOILER_LEVEL = "0"  # the failure is in the fill, not the playthrough spoiler
LOG_TAIL_LINES = 40
MIDO_CHESTS = ["KF Mido Top Left Chest (OoT)", "KF Mido Top Right Chest (OoT)", "KF Mido Bottom Left Chest (OoT)"]
FAILURE_MARKERS = ("FillError", "OptionError", "OracleError", "Exception", "Error:")
ITEM_LINK = {"name": "Shared", "item_pool": ["Masks"], "replacement_item": "Blue Rupee (OoT)", "link_replacement": False}
AFTERMATH = ("EOF when reading a line", "Press enter to close")  # Archipelago waits for Enter after an error
PLAYTHROUGH_LOCK = threading.Lock()  # --playthrough writes saves/combo/ beside comborando

PRESETS = {
    "default": {},
    "shared": {"shared_items": "ALL_SHARED"},
    "unified": {"unified_inventory": True},
    "cross": {"cross_entrances": ["overworld", "interior", "dungeon"], "cross_entrance_percent": 25},
    "mm_entrances": {"mm_entrances": ["overworld", "interior", "dungeon", "decoupled", "mixed"]},
    "hunt": {"goal": "triforce_hunt", "triforce_pieces_required": 10, "triforce_pieces_total": 14},
    "lwl_off": {"lost_woods_link": False},
}


seedout = load_module("ship2ship_seedout", SEEDOUT)


def option_value(value):
    if value == "ALL_SHARED":
        value = list(json.loads(SHARED_FAMILIES.read_text(encoding="utf-8"))["families"])
    return json.dumps(value)


def ship2ship_yaml(name, options=None):
    lines = [f"name: {name}", f"game: {GAME}", f"{GAME}:"]
    lines += [f"  {key}: {option_value(value)}" for key, value in (options or {}).items()] or ["  {}"]
    return "\n".join(lines) + "\n"


def apquest_yaml(name):
    return f"name: {name}\ngame: APQuest\nAPQuest: {{}}\n"


def short_suite():
    """case -> ({file name: yaml}, what to expect)"""
    plando = {"item": "Progressive Hookshot (OoT)", "locations": MIDO_CHESTS, "force": True}
    return {
        "solo": ({"Solo.yaml": ship2ship_yaml("Solo")}, {"playthrough": True}),
        "with-apquest": ({"Hero.yaml": ship2ship_yaml("Hero"), "Quest.yaml": apquest_yaml("Quest")},
                         {"other_items": True}),
        "two-ship2ship": ({"One.yaml": ship2ship_yaml("One"), "Two.yaml": ship2ship_yaml("Two")},
                          {"other_items": True}),
        "item-link": ({f"{n}.yaml": ship2ship_yaml(n, {"item_links": [ITEM_LINK]}) for n in ("Linker", "Partner")},
                      {"other_items": True}),
        "start-inventory": ({"Starter.yaml": ship2ship_yaml("Starter", {
            "start_inventory": {"Lens of Truth (OoT)": 1},
            "start_inventory_from_pool": {"Progressive Hookshot (OoT)": 1}})}, {}),
        "plando": ({"Plando.yaml": ship2ship_yaml("Plando", {"plando_items": [plando]})},
                   {"playthrough": True, "plando": (plando["item"], MIDO_CHESTS)}),
    }


def tail(path, lines=LOG_TAIL_LINES):
    return "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:])


def failure_summary(log_path):
    """The exception line a failed generation ended with."""
    text = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    for line in reversed(text):
        if any(aftermath in line for aftermath in AFTERMATH):
            continue
        if any(marker in line for marker in FAILURE_MARKERS) and not line.startswith(" "):
            return line.strip()[:300]
    return "failed without a recognizable error"


class Generation:
    def __init__(self, ap_root, work, name, files, seed, spoiler=SPOILER_LEVEL):
        self.ap_root = ap_root
        self.folder = work / name
        self.players = self.folder / "players"
        self.out = self.folder / "out"
        self.log = self.folder / "generate.log"
        self.seed = seed
        self.spoiler = spoiler
        self.seconds = 0.0
        self.failure = None
        for stale in (self.players, self.out):
            stale.mkdir(parents=True, exist_ok=True)
        for file_name, text in files.items():
            (self.players / file_name).write_text(text, encoding="utf-8")

    def run(self):
        command = [sys.executable, "Generate.py", "--player_files_path", str(self.players), "--outputpath",
                   str(self.out), "--seed", str(self.seed), "--spoiler", self.spoiler, "--plando",
                   "bosses,items,connections,texts"]
        started = time.perf_counter()
        with self.log.open("w", encoding="utf-8") as log:
            try:
                code = subprocess.run(command, cwd=self.ap_root, stdin=subprocess.DEVNULL, stdout=log,
                                      stderr=subprocess.STDOUT,
                                      timeout=GENERATE_TIMEOUT_SECONDS,
                                      env={**os.environ, "SKIP_REQUIREMENTS_UPDATE": "1"}).returncode
            except subprocess.TimeoutExpired:
                code = None
        self.seconds = time.perf_counter() - started
        if code != 0:
            self.failure = "timed out" if code is None else failure_summary(self.log)
        return self.failure is None

    def archive(self):
        archives = sorted(self.out.glob("AP_*.zip"))
        if len(archives) != 1:
            raise AssertionError(f"expected one AP_*.zip in {self.out}, found {[a.name for a in archives]}")
        return archives[0]


def read_multidata(ap_root, archive):
    """The decoded multidata of an archive, read with Archipelago's own restricted unpickler."""
    sys.path.insert(0, str(ap_root))
    from Utils import restricted_loads  # noqa: E402  (Archipelago's, importable only once the root is on the path)
    bundle = zipfile.ZipFile(archive)
    name = next(n for n in bundle.namelist() if n.endswith(".archipelago"))
    return restricted_loads(zlib.decompress(bundle.read(name)[1:])), bundle


class ChecksFailed(Exception):
    pass


def check_ship2ship_slot(slot, info, multidata, bundle, comborando, expect, report):
    slot_data = multidata["slot_data"][slot]
    from_slot_data = seedout.decode_seed(slot_data["seed"])
    suffix = f"_P{slot}_{info.name.replace(' ', '_')}{SEED_FILE_SUFFIX}"
    files = [n for n in bundle.namelist() if n.endswith(suffix)]
    if len(files) != 1:
        raise ChecksFailed(f"slot {slot}: expected one *{suffix} in the output, found {files}")
    raw = bundle.read(files[0])
    if json.loads(raw) != from_slot_data:
        raise ChecksFailed(f"slot {slot}: the output file and the slot_data seed differ")
    others = seedout.other_players_items(from_slot_data)
    report.append(f"slot {slot} '{info.name}': slot_data seed {len(slot_data['seed'])} chars "
                  f"({slot_data['seed_json_bytes']} bytes of JSON), {len(others)} other-player items, "
                  f"effective {json.dumps(slot_data['effective'], sort_keys=True)[:160]}")
    if expect.get("other_items") and not others:
        raise ChecksFailed(f"slot {slot}: expected other players' items in this slot, found none")
    if expect.get("playthrough"):
        report.append(f"slot {slot}: " + playthrough(comborando, raw, files[0]))
    return files[0], raw


def playthrough(comborando, raw, label):
    """comborando's own forward traversal of the slot's seed; the verdict is its exit code."""
    with tempfile.TemporaryDirectory() as folder:
        seed_file = pathlib.Path(folder) / "slot-seed.json"
        seed_file.write_bytes(raw)
        with PLAYTHROUGH_LOCK:
            result = subprocess.run([comborando, "--playthrough", str(seed_file)], cwd=pathlib.Path(comborando).parent,
                                    capture_output=True, text=True, timeout=PLAYTHROUGH_TIMEOUT_SECONDS)
    last = (result.stdout.strip().splitlines() or ["no output"])[-1]
    if result.returncode != 0:
        raise ChecksFailed(f"comborando --playthrough says {label} is not beatable (exit {result.returncode}): "
                           f"{last}\n{result.stdout[-1500:]}{result.stderr[-500:]}")
    return f"--playthrough ok ({last})"


def check_plando(spoiler_text, expectation):
    item, locations = expectation
    found = [line for line in spoiler_text.splitlines()
             if any(line.strip().startswith(location) for location in locations) and line.rstrip().endswith(item)]
    if not found:
        raise ChecksFailed(f"plando: {item} is not at any of {locations} in the spoiler")
    return f"plando placed: {found[0].strip()}"


def check_generation(gen, comborando, expect):
    report = []
    multidata, bundle = read_multidata(gen.ap_root, gen.archive())
    with bundle:
        for slot, info in multidata["slot_info"].items():
            if info.game == GAME and not info.group_members:
                check_ship2ship_slot(slot, info, multidata, bundle, comborando, expect, report)
        if "plando" in expect:
            spoiler = next(n for n in bundle.namelist() if n.endswith("_Spoiler.txt"))
            report.append(check_plando(bundle.read(spoiler).decode("utf-8"), expect["plando"]))
        spoiler_names = [n for n in bundle.namelist() if n.endswith("_Spoiler.txt")]
        if spoiler_names:
            links = sum(1 for line in bundle.read(spoiler_names[0]).decode("utf-8").splitlines()
                        if "Item Link:" in line)
            report.append(f"{links} item link locations in the spoiler")
    return report


def diagnose(comborando, log_path):
    """Replays the failed slots against a fresh oracle (ap_diagnose.py) and prints what it finds."""
    log = log_path.read_text(encoding="utf-8", errors="replace")
    players = re.findall(r"^(.+?): open request \{", log, re.M)
    if len(players) != 1:  # the placements of several players read differently in the log
        return
    for player in players:
        result = subprocess.run([sys.executable, "-I", str(SCRIPTS / "ap_diagnose.py"), "--comborando", comborando,
                                 "--log", str(log_path), "--player", player], capture_output=True, text=True,
                                timeout=PLAYTHROUGH_TIMEOUT_SECONDS)
        print(f"--- diagnosis of {player} ---\n{result.stdout}{result.stderr[-2000:]}")


def run_short_suite(args):
    results = []
    cases = short_suite()
    unknown = [c for c in args.cases if c not in cases]
    if unknown:
        sys.exit(f"unknown case(s) {unknown}; known: {list(cases)}")
    for name, (files, expect) in ((n, cases[n]) for n in (args.cases or cases)):
        gen = Generation(args.ap_root, args.work, name, files, BASE_SEED)
        print(f"::group::{name}")
        ok = gen.run()
        print(f"[{name}] generation {'ok' if ok else 'FAILED'} in {gen.seconds:.1f} s")
        report = []
        try:
            if ok:
                report = check_generation(gen, args.comborando, expect)
        except (ChecksFailed, AssertionError) as error:
            ok = False
            gen.failure = str(error)
        for line in report:
            print(f"[{name}] {line}")
        if gen.failure:
            print(f"[{name}] FAILED: {gen.failure}\n--- tail of {gen.log} ---\n{tail(gen.log)}")
            diagnose(args.comborando, gen.log)
        print("::endgroup::")
        results.append((name, ok, gen.seconds))
    print("\nshort suite:")
    for name, ok, seconds in results:
        print(f"  {name:16} {'ok' if ok else 'FAILED':7} {seconds:7.1f} s")
    return 0 if all(ok for _, ok, _ in results) else 1


def fill_rate_run(args, preset, index):
    name = f"{preset}-{index:03d}"
    gen = Generation(args.ap_root, args.work / "fill-rate", name,
                     {"Solo.yaml": ship2ship_yaml("Solo", PRESETS[preset])}, BASE_SEED + index, FILL_RATE_SPOILER_LEVEL)
    ok = gen.run()
    detail = gen.failure
    if ok:
        try:
            check_generation(gen, args.comborando, {"playthrough": True})
        except (ChecksFailed, AssertionError) as error:
            ok, detail = False, "beatability: " + str(error).splitlines()[0]
    return preset, index, ok, gen.seconds, detail


def run_fill_rate(args):
    presets = list(PRESETS) if args.presets == "all" else args.presets.split(",")
    unknown = [p for p in presets if p not in PRESETS]
    if unknown:
        sys.exit(f"unknown preset(s) {unknown}; known: {list(PRESETS)}")
    work = [(preset, index) for preset in presets for index in range(args.count)]
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for outcome in pool.map(lambda item: fill_rate_run(args, *item), work):
            results.append(outcome)
            if not outcome[2]:
                print(f"[{outcome[0]} #{outcome[1]}] FAILED in {outcome[3]:.0f} s: {outcome[4]}", flush=True)
    worst = 0.0
    print(f"\nfill failure rate, {args.count} seeds per preset (seeds {BASE_SEED}..{BASE_SEED + args.count - 1}):")
    for preset in presets:
        mine = [r for r in results if r[0] == preset]
        failed = [r for r in mine if not r[2]]
        times = [r[3] for r in mine]
        percent = 100.0 * len(failed) / len(mine)
        worst = max(worst, percent)
        print(f"  {preset:14} {len(failed):3}/{len(mine):3} failed ({percent:4.1f}%)  generation s: median "
              f"{statistics.median(times):.1f}, max {max(times):.1f}")
        for kind in sorted({r[4] for r in failed}):
            print(f"      {sum(1 for r in failed if r[4] == kind)}x {kind}")
    (args.work / "fill-rate.json").write_text(json.dumps(
        [{"preset": p, "seed": BASE_SEED + i, "ok": ok, "seconds": s, "failure": d} for p, i, ok, s, d in results],
        indent=1), encoding="utf-8")
    if worst > args.max_failure_percent:
        print(f"the worst preset fails {worst:.1f}% of seeds, above the {args.max_failure_percent}% target")
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ap-root", type=pathlib.Path, required=True)
    parser.add_argument("--comborando", required=True)
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument("--cases", type=lambda text: text.split(","), default=[],
                        help="the short suite's cases to run (default: all)")
    parser.add_argument("--fill-rate", action="store_true")
    parser.add_argument("--count", type=int, default=50, help="seeds per preset (fill rate)")
    parser.add_argument("--presets", default="all")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--max-failure-percent", type=float, default=2.0)
    args = parser.parse_args()
    args.ap_root, args.work = args.ap_root.resolve(), args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    return run_fill_rate(args) if args.fill_rate else run_short_suite(args)


if __name__ == "__main__":
    sys.exit(main())
