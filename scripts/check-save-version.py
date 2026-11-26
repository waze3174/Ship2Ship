"""Fail when the enums that saves index by change without a COMBO_RELEASE_VERSION minor bump.

SoH and 2Ship write some save fields as flat arrays indexed by an enum, or as the enum's number: `randoSettings` by
`RandomizerSettingKey`, `itemLocations` by `RandomizerCheck`, the placed item as a `RandomizerGet` value, 2Ship's
`randoSaveChecks` and `randoSaveOptions` by `RandoCheckId` and `RandoOptionId`. An entry added or moved mid-enum shifts
every later number, so a save from before reads the wrong values or runs off its array (the 2026-06-20 start-up crash,
docs/UPSTREAM_MERGES.md). Ship2Ship's own saves are retired by the major.minor of COMBO_RELEASE_VERSION, which is bumped
by hand; this gate makes sure it is.

The digests cover the enumerators with comments and spacing removed, so a comment or a reformat does not trip it.

Usage:
  check-save-version.py           check, as the CI gate does
  check-save-version.py --update  record the current enums, after bumping COMBO_RELEASE_VERSION

The digests come from the git index, so stage changed headers before --update.
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RECORD = ROOT / "scripts" / "save-version-inputs.json"
CMAKE_LISTS = ROOT / "CMakeLists.txt"
VERSION_PATTERN = re.compile(r'^set\(COMBO_RELEASE_VERSION\s+"(\d+)\.(\d+)\.(\d+)"', re.MULTILINE)

RANDOMIZER_ENUMS = "soh/soh/Enhancements/randomizer/randomizerEnums/"
# SoH's headers are lists of RANDO_ENUM_ITEM lines, so the whole file is the enum.
WHOLE_FILE_INPUTS = tuple(RANDOMIZER_ENUMS + name for name in (
    "RandomizerCheck.h", "RandomizerGet.h", "RandomizerInf.h", "RandomizerSettingKey.h", "RandomizerTrick.h",
    "RandomizerMiscEnums.h", "RandomizerOptions.h"))
# 2Ship keeps its enums among structs in one header.
ENUM_BLOCK_INPUTS = ("mm/2s2h/Rando/Types.h",)
INPUTS = WHOLE_FILE_INPUTS + ENUM_BLOCK_INPUTS

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")
ENUM_BLOCK = re.compile(r"\benum\b[^{;]*\{.*?\}[^;]*;", re.DOTALL)


def release_major_minor():
    match = VERSION_PATTERN.search(CMAKE_LISTS.read_text(encoding="utf-8"))
    if match is None:
        sys.exit(f'error: no \'set(COMBO_RELEASE_VERSION "<major>.<minor>.<patch>"\' line in {CMAKE_LISTS}')
    return int(match.group(1)), int(match.group(2))


def indexed_text(path):
    """The file as staged in the git index, so the check does not depend on line endings in the work tree."""
    result = subprocess.run(["git", "show", f":{path}"], cwd=ROOT, capture_output=True, check=False)
    if result.returncode != 0:
        sys.exit(f"error: {path} is not tracked by git")
    return result.stdout.decode("utf-8", errors="replace")


def enumerators(path):
    text = LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", indexed_text(path)))
    if path in ENUM_BLOCK_INPUTS:
        text = "\n".join(ENUM_BLOCK.findall(text))
    return " ".join(text.split())


def input_digests():
    return {path: hashlib.sha256(enumerators(path).encode("utf-8")).hexdigest() for path in INPUTS}


def changed_inputs(recorded, current):
    return [path for path in INPUTS if recorded.get(path) != current[path]]


def version_text(version):
    return f"{version[0]}.{version[1]}"


def load_record():
    if not RECORD.exists():
        sys.exit(f"error: {RECORD.relative_to(ROOT)} is missing. Run this script with --update.")
    record = json.loads(RECORD.read_text(encoding="utf-8"))
    return record, (record["releaseMajor"], record["releaseMinor"])


def update(version, current):
    if RECORD.exists():
        record, recorded_version = load_record()
        if changed_inputs(record["inputs"], current) and version <= recorded_version:
            sys.exit(f"error: the save-indexing enums changed, but COMBO_RELEASE_VERSION is still "
                     f"{version_text(version)}.x. Bump its minor in CMakeLists.txt first.")
    record = {"releaseMajor": version[0], "releaseMinor": version[1], "inputs": current}
    RECORD.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"recorded COMBO_RELEASE_VERSION {version_text(version)}.x and the save-indexing enums in "
          f"{RECORD.relative_to(ROOT)}")


def check(version, current):
    record, recorded_version = load_record()
    changed = changed_inputs(record["inputs"], current)
    if not changed:
        print(f"Save-indexing enums match the record for COMBO_RELEASE_VERSION {version_text(recorded_version)}.x.")
        return
    files = ", ".join(changed)
    if version <= recorded_version:
        sys.exit(f"error: the save-indexing enums changed ({files}) but COMBO_RELEASE_VERSION is still "
                 f"{version_text(version)}.x. Saves made before would be read with shifted numbers. Bump the minor "
                 f"in CMakeLists.txt, then run scripts/check-save-version.py --update.")
    sys.exit(f"error: the save-indexing enums changed ({files}) and COMBO_RELEASE_VERSION is bumped to "
             f"{version_text(version)}.x. Run scripts/check-save-version.py --update to record them.")


def main():
    version = release_major_minor()
    current = input_digests()
    if sys.argv[1:] == ["--update"]:
        update(version, current)
    elif not sys.argv[1:]:
        check(version, current)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
