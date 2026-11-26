"""Fail when the ROM extractors' asset definitions change without a COMBO_EPOCH bump.

A player's ROM archives (oot.o2r, oot-mq.o2r, mm.o2r) are re-extracted only when the archive's MAJOR version
differs from the build's, and MAJOR is COMBO_EPOCH. An upstream merge that renames assets in these definitions
leaves an old archive looking current, and the game then crashes when it asks for a resource by its new name.

Usage:
  check-rom-archive-epoch.py           check, as the CI gate does
  check-rom-archive-epoch.py --update  record the current definitions, after bumping COMBO_EPOCH

The digests come from the git index, so stage changed definitions before --update.
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RECORD = ROOT / "scripts" / "rom-archive-inputs.json"
CMAKE_LISTS = ROOT / "CMakeLists.txt"
EPOCH_PATTERN = re.compile(r"^set\(COMBO_EPOCH\s+(\d+)\)", re.MULTILINE)
EXTRACTOR_INPUTS = ("soh/assets/yml", "mm/assets/xml", "mm/assets/extractor")


def cmake_epoch():
    match = EPOCH_PATTERN.search(CMAKE_LISTS.read_text(encoding="utf-8"))
    if match is None:
        sys.exit(f"error: no 'set(COMBO_EPOCH <n>)' line in {CMAKE_LISTS}")
    return int(match.group(1))


def input_digests():
    digests = {}
    for folder in EXTRACTOR_INPUTS:
        listing = subprocess.run(["git", "ls-files", "-s", "-z", "--", folder], cwd=ROOT, capture_output=True,
                                 check=True).stdout
        if not listing:
            sys.exit(f"error: git lists no files under {folder}")
        digests[folder] = hashlib.sha256(listing).hexdigest()
    return digests


def changed_folders(recorded, current):
    return [folder for folder in EXTRACTOR_INPUTS if recorded.get(folder) != current[folder]]


def update(epoch, current):
    if RECORD.exists():
        recorded = json.loads(RECORD.read_text(encoding="utf-8"))
        if changed_folders(recorded["inputs"], current) and epoch <= recorded["comboEpoch"]:
            sys.exit(f"error: the extractor definitions changed, but COMBO_EPOCH is still {epoch}. "
                     f"Bump COMBO_EPOCH in CMakeLists.txt first.")
    RECORD.write_text(json.dumps({"comboEpoch": epoch, "inputs": current}, indent=2) + "\n", encoding="utf-8")
    print(f"recorded COMBO_EPOCH {epoch} and the extractor definitions in {RECORD.relative_to(ROOT)}")


def check(epoch, current):
    if not RECORD.exists():
        sys.exit(f"error: {RECORD.relative_to(ROOT)} is missing. Run this script with --update.")
    recorded = json.loads(RECORD.read_text(encoding="utf-8"))
    changed = changed_folders(recorded["inputs"], current)
    if not changed:
        print(f"ROM extractor definitions match the record for COMBO_EPOCH {recorded['comboEpoch']}.")
        return
    folders = ", ".join(changed)
    if epoch <= recorded["comboEpoch"]:
        sys.exit(f"error: the ROM extractor definitions changed ({folders}) but COMBO_EPOCH is still {epoch}. "
                 f"Old oot.o2r / mm.o2r archives would load as current and miss renamed assets. Bump COMBO_EPOCH "
                 f"in CMakeLists.txt, then run scripts/check-rom-archive-epoch.py --update.")
    sys.exit(f"error: the ROM extractor definitions changed ({folders}) and COMBO_EPOCH is bumped to {epoch}. "
             f"Run scripts/check-rom-archive-epoch.py --update to record them.")


def main():
    epoch = cmake_epoch()
    current = input_digests()
    if sys.argv[1:] == ["--update"]:
        update(epoch, current)
    elif not sys.argv[1:]:
        check(epoch, current)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
