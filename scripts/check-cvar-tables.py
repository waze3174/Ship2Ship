"""Fail when a table that names CVars or menu labels by string names one that its game no longer has.

Three tables are keyed on strings that upstream owns, so an upstream rename drops a row without a warning:
  - combo/gui/ComboLinkRows.h `kLinkRows`: one OoT CVar and up to two MM CVars per row. Each must exist in its own
    game's sources (soh/ for OoT, mm/ for MM, libultraship/ for both).
  - combo/gui/ComboBothGames.cpp `kSharedItems`: one CVar both games read. It must exist in both.
  - the menu filters in combo/gui/ComboMenu*.cpp: `strcmp(section, "Settings")`, `strcmp(sidebar, "Mod Menu")` and
    alike, which pick pages by their English label. Each label must appear in one of the games' menu sources.

A CVar exists when its full name is a string literal in the sources, when a CVAR_*("Rest.Of.Name") prefix macro builds
it, or when CMake/lus-cvars.cmake builds it as "${CVAR_PREFIX_*}.Rest.Of.Name" (the prefix is not compared). One shared
item is MM's alone by design and is listed in MM_ONLY_SHARED_ITEMS.

Usage:
  check-cvar-tables.py   check, as the CI gate does
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

LINK_ROWS_FILE = "combo/gui/ComboLinkRows.h"
LINK_ROWS_TABLE = "kLinkRows"
SHARED_ITEMS_FILE = "combo/gui/ComboBothGames.cpp"
SHARED_ITEMS_TABLE = "kSharedItems"
MENU_FILTER_FILES = ("combo/gui/ComboMenu.cpp", "combo/gui/ComboBothGames.cpp", "combo/gui/ComboMenuModel.cpp")
MENU_SOURCES = ("soh/soh/SohGui", "mm/2s2h/BenGui")
GAME_FOLDERS = {"oot": ("soh", "libultraship", "CMake"), "mm": ("mm", "libultraship", "CMake")}
# Shared items one game does not have, and why: OOT applies the speed modifier whenever its button is held, so only MM
# has the gate (see kSharedItems in combo/gui/ComboBothGames.cpp).
MM_ONLY_SHARED_ITEMS = {"gCheats.SpeedModifier.Mode"}

CVAR_LITERAL = r'"g[A-Za-z]+(\.[A-Za-z0-9_]+)+"'
CVAR_MACRO = r'CVAR_[A-Z_]+\(\s*"[A-Za-z0-9_.]+"'
CVAR_PREFIX_EXPANSION = r'\$\{CVAR_PREFIX_[A-Z_]+\}\.[A-Za-z0-9_.]+'
MENU_FILTER = re.compile(r'strcmp\(\s*\w*(?:[Ss]ection|[Ss]idebar)\w*\s*,\s*"([^"]+)"\s*\)')
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def git_grep_matches(pattern, folders):
    result = subprocess.run(["git", "grep", "-h", "-o", "-I", "-E", pattern, "--", *folders], cwd=ROOT,
                            capture_output=True, check=False)
    return set(result.stdout.decode("utf-8", errors="replace").split())


def known_cvars(folders):
    """(full names written as literals, name tails written inside CVAR_*() macros) in the folders' sources."""
    literals = {name.strip('"') for name in git_grep_matches(CVAR_LITERAL, folders)}
    tails = {use.split('"')[1] for use in git_grep_matches(CVAR_MACRO, folders)}
    tails |= {use.split("}.", 1)[1] for use in git_grep_matches(CVAR_PREFIX_EXPANSION, folders)}
    return literals, tails


def cvar_exists(name, known):
    literals, tails = known
    tail = name.split(".", 1)[1] if "." in name else name
    return name in literals or tail in tails


def table_rows(path, table):
    """The string literals of each top-level row of a constexpr table, as (depth-1 strings, depth-2 strings)."""
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index(table)
    start = text.index("{", text.index("=", start)) + 1
    rows, depth, row_start = [], 1, None
    for index in range(start, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
            if depth == 2:
                row_start = index
        elif char == "}":
            if depth == 2:
                rows.append(text[row_start:index + 1])
            depth -= 1
            if depth == 0:
                break
    return rows


def link_row_names(row):
    """(OoT name, MM names) of one kLinkRows row: the OoT name opens the row, MM's are in the braces after."""
    oot = STRING_LITERAL.search(row).group(1)
    inner = row[row.index("{", 1):] if "{" in row[1:] else ""
    return oot, STRING_LITERAL.findall(inner)


def shared_item_name(row):
    return STRING_LITERAL.search(row).group(1)


def check_link_rows(known):
    problems = []
    for row in table_rows(LINK_ROWS_FILE, LINK_ROWS_TABLE):
        oot, mm_names = link_row_names(row)
        if not cvar_exists(oot, known["oot"]):
            problems.append(f"{LINK_ROWS_FILE}: OoT CVar {oot} is not in soh/ or libultraship/")
        problems += [f"{LINK_ROWS_FILE}: MM CVar {name} is not in mm/ or libultraship/"
                     for name in mm_names if not cvar_exists(name, known["mm"])]
    return problems


def check_shared_items(known):
    problems = []
    for row in table_rows(SHARED_ITEMS_FILE, SHARED_ITEMS_TABLE):
        name = shared_item_name(row)
        for game in ("oot", "mm"):
            if game == "oot" and name in MM_ONLY_SHARED_ITEMS:
                continue
            if not cvar_exists(name, known[game]):
                problems.append(f"{SHARED_ITEMS_FILE}: shared CVar {name} is not in {game}'s sources")
    return problems


def menu_filter_labels():
    labels = {}
    for path in MENU_FILTER_FILES:
        text = (ROOT / path).read_text(encoding="utf-8")
        for label in MENU_FILTER.findall(text):
            labels.setdefault(label, path)
    return labels


def check_menu_filters():
    menu_text = ""
    listing = subprocess.run(["git", "ls-files", "-z", "--", *MENU_SOURCES], cwd=ROOT, capture_output=True,
                             check=True).stdout.decode("utf-8").split("\0")
    for name in listing:
        if name.endswith((".cpp", ".h")):
            menu_text += (ROOT / name).read_text(encoding="utf-8", errors="replace")
    return [f'{path}: the menu filter names "{label}", which neither game\'s menu sources contain'
            for label, path in menu_filter_labels().items() if f'"{label}"' not in menu_text]


def main():
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    known = {game: known_cvars(folders) for game, folders in GAME_FOLDERS.items()}
    link_rows = len(table_rows(LINK_ROWS_FILE, LINK_ROWS_TABLE))
    shared_items = len(table_rows(SHARED_ITEMS_FILE, SHARED_ITEMS_TABLE))
    labels = len(menu_filter_labels())
    if min(link_rows, shared_items, labels) == 0:
        sys.exit("error: found no rows in one of the tables. The table's layout changed; update this script.")
    problems = check_link_rows(known) + check_shared_items(known) + check_menu_filters()
    if problems:
        print("\n".join(problems))
        sys.exit(f"error: {len(problems)} table entr(ies) name something their game does not have. An upstream "
                 f"rename drops a row silently; follow the rename or remove the row.")
    print(f"{link_rows} link rows, {shared_items} shared items and {labels} menu filter labels all exist in their "
          f"game's sources.")


if __name__ == "__main__":
    main()
