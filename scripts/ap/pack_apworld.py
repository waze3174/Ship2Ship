"""Pack apworld/ship2ship into ship2ship.apworld, laid out like Archipelago's "Build APWorlds" component writes it.

The zip holds one folder, ship2ship/, named like the zip. Its archipelago.json is the world's manifest plus the two
container fields Archipelago's loader reads (version and compatible_version, APWorldContainer in worlds/Files.py).

  pack_apworld.py --out build/ship2ship.apworld
"""
import argparse
import json
import pathlib
import zipfile

WORLD_FOLDER = pathlib.Path(__file__).resolve().parent.parent.parent / "apworld" / "ship2ship"
MANIFEST = "archipelago.json"
CONTAINER_VERSION = 7
SKIPPED_FOLDERS = {"__pycache__"}
SKIPPED_SUFFIXES = {".pyc"}


def world_files(folder):
    for path in sorted(folder.rglob("*")):
        relative = path.relative_to(folder)
        if path.is_file() and not SKIPPED_FOLDERS & set(relative.parts) and path.suffix not in SKIPPED_SUFFIXES:
            yield path, relative


def manifest_for(folder):
    manifest = json.loads((folder / MANIFEST).read_text(encoding="utf-8"))
    manifest.update({"version": CONTAINER_VERSION, "compatible_version": CONTAINER_VERSION})
    return manifest


def pack(folder, out):
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, relative in world_files(folder):
            if relative.as_posix() != MANIFEST:
                archive.write(path, (pathlib.PurePosixPath(folder.name) / relative.as_posix()).as_posix())
        archive.writestr(f"{folder.name}/{MANIFEST}", json.dumps(manifest_for(folder)))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("build") / "ship2ship.apworld")
    args = parser.parse_args()
    print(f"{pack(WORLD_FOLDER, args.out)}: {args.out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
