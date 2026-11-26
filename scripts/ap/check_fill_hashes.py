"""Fail when a generated seed's fingerprint differs from the pinned one.

comborando prints `seed '<tag>' (<master seed>) PASS (...) fingerprint=<hex>` for every seed it generates. The
fingerprint covers the fill's placements and everything decided before it (entrances, Master Quest rolls, prices), so
a change to the fill or to the setup in front of it changes the value. CI pipes the seed runs through this script
with the Linux table, which holds one `<tag> <fingerprint>` line per seed.

A change that is meant to move seeds regenerates the table with --update and commits it with the reason.

Usage:
  check_fill_hashes.py --log fill.log --expected scripts/ap/expected-fill-hashes.linux.txt
  check_fill_hashes.py --log fill.log --expected scripts/ap/expected-fill-hashes.linux.txt --update
"""
import argparse
import pathlib
import re
import sys

SEED_LINE = re.compile(r"\[comborando\]\s+seed (\S+) \(\d+\) PASS .*fingerprint=([0-9a-f]{16})")
HEADER = "# <seed tag> <fingerprint>; written by check_fill_hashes.py --update\n"


def read_fingerprints(log_path):
    """Return {tag: fingerprint} for every generated seed in the log; a tag seen twice must agree."""
    found = {}
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SEED_LINE.search(line)
        if not match:
            continue
        tag, fingerprint = match.groups()
        if found.setdefault(tag, fingerprint) != fingerprint:
            sys.exit(f"seed {tag} generated two different fingerprints in one log: the fill is not deterministic")
    return found


def read_expected(path):
    expected = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            tag, fingerprint = line.split()
            expected[tag] = fingerprint
    return expected


def write_expected(path, fingerprints):
    path.write_text(HEADER + "".join(f"{tag} {fingerprints[tag]}\n" for tag in sorted(fingerprints)), encoding="utf-8")


def compare(found, expected):
    """Return one message per seed whose fingerprint moved, went missing or was never pinned."""
    problems = [f"{tag}: generated but not pinned (run with --update)" for tag in sorted(set(found) - set(expected))]
    for tag, pinned in sorted(expected.items()):
        if tag not in found:
            problems.append(f"{tag}: pinned but not generated in this run")
        elif found[tag] != pinned:
            problems.append(f"{tag}: fingerprint {found[tag]}, pinned {pinned}")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--log", required=True, type=pathlib.Path)
    parser.add_argument("--expected", required=True, type=pathlib.Path)
    parser.add_argument("--update", action="store_true", help="rewrite the table from the log")
    args = parser.parse_args()

    found = read_fingerprints(args.log)
    if not found:
        sys.exit(f"no generated seed with a fingerprint in {args.log}")
    if args.update:
        write_expected(args.expected, found)
        print(f"wrote {len(found)} fingerprints to {args.expected}")
        return
    if not args.expected.exists():
        print(f"no pinned table at {args.expected}; fingerprints from this run:")
        print("".join(f"{tag} {found[tag]}\n" for tag in sorted(found)), end="")
        return
    problems = compare(found, read_expected(args.expected))
    if problems:
        sys.exit("fill fingerprints moved:\n  " + "\n  ".join(problems))
    print(f"{len(found)} seeds generated, all pinned fingerprints unchanged")


if __name__ == "__main__":
    main()
