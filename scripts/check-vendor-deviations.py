"""Fail when a vendored upstream file is modified and docs/deviations/ does not name it.

soh/ and mm/ are vendored copies of SoH and 2Ship, and every change to them has to be logged so the next upstream
merge does not undo it (docs/UPSTREAM_MERGES.md). The pristine upstream is the vendor commit of the merge pin in
upstream-pins.json ("vendor: soh develop <sha>"), an ancestor of the branch, so the gate needs the full history
(`fetch-depth: 0` in CI). Files that only we added are not vendored files and are not listed.

A file is named when a docs/deviations/*.md file contains its path, its path without the soh/ or mm/ folder, or its file
name (as a whole word).

Many modified files were never named. Those are recorded in scripts/undocumented-deviations.json, so the gate fails only
on a file that is modified and unnamed and is not in the record, and on a recorded file that is named by now or no
longer modified. Document the file in docs/deviations/, then run --update.

Usage:
  check-vendor-deviations.py           check, as the CI gate does
  check-vendor-deviations.py --update  record the modified, unnamed files
  check-vendor-deviations.py --list    print them, with the vendor commit each is compared against
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RECORD = ROOT / "scripts" / "undocumented-deviations.json"
PINS = ROOT / "upstream-pins.json"
DEVIATIONS = ROOT / "docs" / "deviations"
VENDORED = ("soh", "mm")
MODIFIED_STATUSES = ("M", "D", "T")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, check=False, text=True, encoding="utf-8")


def merged_pins():
    upstreams = json.loads(PINS.read_text(encoding="utf-8"))["upstreams"]
    return {key: upstreams[key]["mergedSha"] for key in VENDORED}


def vendor_commit(key, pin):
    """The commit of pristine upstream that the pin names, or exit when this checkout lacks it."""
    found = git("log", "-1", "--format=%H", f"--grep=^vendor: {key} .* {pin}$", "HEAD").stdout.strip()
    if found:
        return found
    shallow = git("rev-parse", "--is-shallow-repository").stdout.strip() == "true"
    reason = "this is a shallow clone: fetch the full history (fetch-depth: 0)" if shallow else (
        f"no 'vendor: {key} ... {pin}' commit is an ancestor of HEAD. Is upstream-pins.json ahead of the merge?")
    sys.exit(f"error: cannot find the vendor commit of {key} {pin}: {reason}")


def modified_files(key, commit):
    listing = git("diff", "--name-status", "--no-renames", "-z", commit, "HEAD", "--", key)
    if listing.returncode != 0:
        sys.exit(f"error: git diff against {commit} failed: {listing.stderr.strip()}")
    fields = listing.stdout.split("\0")
    pairs = zip(fields[0::2], fields[1::2])
    return {path for status, path in pairs if status[:1] in MODIFIED_STATUSES}


def deviation_text():
    return "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in sorted(DEVIATIONS.glob("*.md")))


def is_named(path, text):
    without_folder = path.split("/", 1)[1]
    file_name = path.rsplit("/", 1)[-1]
    if path in text or without_folder in text:
        return True
    return re.search(rf"(?<![\w./-]){re.escape(file_name)}(?![\w-])", text) is not None


def undocumented(pins):
    text = deviation_text()
    result = set()
    for key, pin in pins.items():
        result |= {path for path in modified_files(key, vendor_commit(key, pin)) if not is_named(path, text)}
    return result


def write_record(pins, files):
    record = {"vendorPins": pins, "files": sorted(files)}
    RECORD.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"recorded {len(files)} modified, unnamed vendored files in {RECORD.relative_to(ROOT)}")


def check(pins, current):
    if not RECORD.exists():
        sys.exit(f"error: {RECORD.relative_to(ROOT)} is missing. Run this script with --update.")
    record = json.loads(RECORD.read_text(encoding="utf-8"))
    if record["vendorPins"] != pins:
        sys.exit(f"error: the vendor pins moved ({record['vendorPins']} -> {pins}). Run this script with --update "
                 f"after the merge.")
    recorded = set(record["files"])
    new = sorted(current - recorded)
    stale = sorted(recorded - current)
    if new:
        print("Modified vendored files that docs/deviations/ does not name:")
        print("\n".join(f"  {path}" for path in new))
    if stale:
        print("Recorded files that are named by now, or no longer modified (run --update):")
        print("\n".join(f"  {path}" for path in stale))
    if new:
        sys.exit("error: log each new deviation in docs/deviations/ (what it changes and why), then run "
                 "scripts/check-vendor-deviations.py --update.")
    if stale:
        sys.exit("error: the record is stale. Run scripts/check-vendor-deviations.py --update.")
    print(f"No new undocumented deviations ({len(current)} older ones are recorded).")


def main():
    pins = merged_pins()
    if sys.argv[1:] == ["--list"]:
        for key, pin in pins.items():
            print(f"{key}: compared against vendor commit {vendor_commit(key, pin)} ({pin})")
        print("\n".join(sorted(undocumented(pins))))
    elif sys.argv[1:] == ["--update"]:
        write_record(pins, undocumented(pins))
    elif not sys.argv[1:]:
        check(pins, undocumented(pins))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
