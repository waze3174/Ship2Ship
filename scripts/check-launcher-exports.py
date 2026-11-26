"""Fail when the launcher resolves a name that its game library does not export.

The launcher and comboui look up each other's functions by string at run time: `GetSym(sohModule, "SOH_X")`,
`Combo_ResolveSym("2ship", "MM_X")`, `Sym<T>(soh, "SOH_X")`. A name that no longer exists leaves the function pointer
null and the feature silently off. On Linux and macOS the module is ignored (one process-wide dlsym), so a lookup aimed
at the wrong library passes there and fails only on Windows; this gate checks the module as well.

An export is a `COMBO_EXPORT` definition in the module's sources: soh/ for soh, mm/ for 2ship, combo/ for comboui. A
combo/ header that defines exports (combo/menu/ComboItemDrawOOT.h) belongs to every module whose sources include it.

Usage:
  check-launcher-exports.py   check, as the CI gate does
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Where each module's COMBO_EXPORT definitions live.
MODULE_FOLDERS = {"soh": "soh", "2ship": "mm", "comboui": "combo"}
# The name each lookup uses for a module: ComboShip.cpp names a variable, comborando names a short name.
LAUNCHER_MODULE_VARIABLES = {"sohModule": "soh", "mmModule": "2ship", "comboUIModule": "comboui"}
HEADLESS_MODULE_VARIABLES = {"soh": "soh", "mm": "2ship"}

LAUNCHER_LOOKUP = re.compile(r'GetSym\(\s*(\w+)\s*,\s*"(\w+)"\s*\)')
HEADLESS_LOOKUP = re.compile(r'\bSym<[^;]*?>\(\s*(\w+)\s*,\s*"(\w+)"\s*\)')
RESOLVE_LOOKUP = re.compile(r'Combo_ResolveSym\(\s*"([\w-]+)"\s*,\s*"(\w+)"\s*\)')
EXPORT_DEFINITION = re.compile(r"\bCOMBO_EXPORT\b[^;{}]*?\b(\w+)\s*\(")
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp", ".inc")
HEADER_SUFFIXES = (".h", ".hpp")
GIT_PATH_SEPARATOR = "\0"


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def files_containing(text, *folders):
    """Tracked source files under the folders that contain the text."""
    result = subprocess.run(["git", "grep", "-l", "-I", "-F", "-z", text, "--", *folders], cwd=ROOT,
                            capture_output=True, check=False)
    names = result.stdout.decode("utf-8").split(GIT_PATH_SEPARATOR)
    return [ROOT / name for name in names if name.endswith(SOURCE_SUFFIXES)]


def top_folder(path):
    return path.relative_to(ROOT).parts[0]


def modules_compiling(header):
    """The game modules whose sources include a combo header, which is then compiled into each of them."""
    modules = set()
    for path in files_containing(header.name, "soh", "mm"):
        if f'{header.name}"' in read(path):
            modules.update(module for module, folder in MODULE_FOLDERS.items() if folder == top_folder(path))
    return modules


def exports_by_module():
    exports = {module: set() for module in MODULE_FOLDERS}
    for path in files_containing("COMBO_EXPORT", *MODULE_FOLDERS.values()):
        names = set(EXPORT_DEFINITION.findall(read(path)))
        owners = {module for module, folder in MODULE_FOLDERS.items() if folder == top_folder(path)}
        if top_folder(path) == "combo" and path.suffix in HEADER_SUFFIXES:
            owners |= modules_compiling(path)
        for module in owners:
            exports[module] |= names
    return exports


def lookups():
    """(module, name, file) for every name the launcher, the games, comboui and comborando resolve."""
    found = []
    for file_name, pattern, variables in (("ComboShip.cpp", LAUNCHER_LOOKUP, LAUNCHER_MODULE_VARIABLES),
                                          ("ComboRandoHeadless.cpp", HEADLESS_LOOKUP, HEADLESS_MODULE_VARIABLES)):
        path = ROOT / "combo" / file_name
        for variable, name in pattern.findall(read(path)):
            found.append((variables.get(variable, variable), name, path.relative_to(ROOT).as_posix()))
    for path in files_containing("Combo_ResolveSym", "combo", "soh", "mm"):
        for module, name in RESOLVE_LOOKUP.findall(read(path)):
            found.append((module, name, path.relative_to(ROOT).as_posix()))
    return found


def main():
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    exports = exports_by_module()
    found = lookups()
    problems = set()
    for module, name, source in found:
        if module not in exports:
            problems.add(f"{source}: looks up {name} in an unknown module '{module}'")
        elif name not in exports[module]:
            elsewhere = [other for other, names in exports.items() if name in names]
            hint = f" It is exported by {', '.join(elsewhere)}." if elsewhere else " No module exports it."
            problems.add(f"{source}: looks up {name} in {module}, which does not export it.{hint}")
    if problems:
        print("\n".join(sorted(problems)))
        sys.exit(f"error: {len(problems)} lookup(s) have no export behind them. Fix the name or the module, or add "
                 f"the COMBO_EXPORT definition.")
    names = {(module, name) for module, name, _ in found}
    print(f"All {len(names)} names the launcher and its modules resolve are exported by the module they name.")


if __name__ == "__main__":
    main()
