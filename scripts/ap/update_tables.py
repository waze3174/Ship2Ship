"""Keep the Archipelago id table in step with the two game libraries.

`comborando --ap-tables <file>` lists every item and check under its AP name ("Progressive Hookshot (OoT)"). The id
table apworld/ship2ship/data/ids.json maps each name to a number, forever: OoT ids are 1,000,000 + n and MM ids
2,000,000 + n, counted separately for items and locations. New names get the next number; a name that leaves the
libraries keeps its id (a rename needs an entry under "aliases": {old name: new name}).

The same dump also gives apworld/ship2ship/data/groups.json: each item's category, whether the game's logic needs it and
whether it is a trap (what the apworld classifies an item by when no seed is open, as in an item link), and each
location's area. That file is rewritten whole on every --write; only the ids are append-only.

  update_tables.py --comborando ./comborando --check     fail on a stale table or a conflicting name (CI)
  update_tables.py --comborando ./comborando --write     add the new names
  update_tables.py --tables tables.json --check          the same from a dump made earlier

comborando loads the game libraries from the working folder, so run it from one (--cwd).
"""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

GAME_BASE = {"oot": 1_000_000, "mm": 2_000_000}
TABLES = ("items", "locations")
IDS_VERSION = 1
GROUPS_VERSION = 1
DATA_FOLDER = pathlib.Path(__file__).resolve().parent.parent.parent / "apworld" / "ship2ship" / "data"
DEFAULT_IDS = DATA_FOLDER / "ids.json"
GROUPS_FILE = "groups.json"


def dump_tables(comborando, cwd):
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "tables.json"
        subprocess.run([comborando, "--ap-tables", str(out)], cwd=cwd, check=True)
        return json.loads(out.read_text(encoding="utf-8"))


def load_ids(path):
    if not path.exists():
        return {"version": IDS_VERSION, "items": {}, "locations": {}, "aliases": {}}
    return json.loads(path.read_text(encoding="utf-8"))


def names_by_game(entries):
    """{game: sorted AP names} for one table of the dump."""
    grouped = {game: [] for game in GAME_BASE}
    for entry in entries:
        grouped[entry["game"]].append(entry["apName"])
    return {game: sorted(names) for game, names in grouped.items()}


def next_id(ids_of_table, game):
    base = GAME_BASE[game]
    used = [i for i in ids_of_table.values() if base < i < base + 1_000_000]
    return max(used, default=base) + 1


def missing_names(tables, ids):
    """{table: {game: [AP names the id table lacks]}}"""
    known = lambda table: set(ids[table]) | set(ids.get("aliases", {}))
    return {table: {game: [n for n in names if n not in known(table)]
                    for game, names in names_by_game(tables[table]).items()} for table in TABLES}


def add_missing(ids, missing):
    for table in TABLES:
        for game, names in missing[table].items():
            for name in names:  # already sorted
                ids[table][name] = next_id(ids[table], game)


def build_groups(tables):
    """The static facts the apworld needs about every name, whatever seed is open."""
    by_name = lambda entries: sorted(entries, key=lambda e: e["apName"])
    return {
        "version": GROUPS_VERSION,
        "items": {e["apName"]: {"category": e["category"], "advancement": e["advancement"], "trap": e["trap"]}
                  for e in by_name(tables["items"])},
        "locations": {e["apName"]: e["area"] for e in by_name(tables["locations"])},
    }


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=1, sort_keys=True) + "\n", encoding="utf-8")


def problems_of(tables, ids):
    problems = [f"{c['table']}: '{c['apName']}' is listed twice with different data" for c in tables.get("conflicts", [])]
    for table in TABLES:
        values = list(ids[table].values())
        if len(values) != len(set(values)):
            problems.append(f"{table}: an id is used twice in ids.json")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--comborando")
    source.add_argument("--tables", type=pathlib.Path)
    parser.add_argument("--cwd", default=".")
    parser.add_argument("--ids", type=pathlib.Path, default=DEFAULT_IDS)
    parser.add_argument("--groups", type=pathlib.Path, help="default: groups.json next to --ids")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    args = parser.parse_args()

    groups_path = args.groups or args.ids.parent / GROUPS_FILE
    tables = (json.loads(args.tables.read_text(encoding="utf-8")) if args.tables
              else dump_tables(args.comborando, args.cwd))
    groups = build_groups(tables)
    ids = load_ids(args.ids)
    problems = problems_of(tables, ids)
    missing = missing_names(tables, ids)
    total = sum(len(names) for per_game in missing.values() for names in per_game.values())
    present = {table: {e["apName"] for e in tables[table]} for table in TABLES}
    gone = sum(len(set(ids[table]) - present[table] - set(ids.get("aliases", {}))) for table in TABLES)
    if gone:
        print(f"note: {gone} name(s) in ids.json are no longer in the libraries; their ids stay reserved")

    if args.write:
        if problems:
            sys.exit("; ".join(problems))
        add_missing(ids, missing)
        ids["version"] = IDS_VERSION
        ids.setdefault("aliases", {})
        write_json(args.ids, ids)
        write_json(groups_path, groups)
        print(f"{total} new name(s); {args.ids} has {len(ids['items'])} items and {len(ids['locations'])} locations; "
              f"{groups_path} written")
        return
    if not groups_path.exists() or json.loads(groups_path.read_text(encoding="utf-8")) != groups:
        problems.append(f"{groups_path.name} is stale. Run update_tables.py --write and commit it.")
    if total:
        problems.append(f"ids.json is stale: {total} name(s) have no id. Run update_tables.py --write and commit it. "
                        + "; ".join(f"{t}/{g}: {n[:3]}" for t, per in missing.items() for g, n in per.items() if n))
    if problems:
        sys.exit("\n".join(problems))
    print(f"ids.json is current: {len(tables['items'])} items, {len(tables['locations'])} locations")


if __name__ == "__main__":
    main()
