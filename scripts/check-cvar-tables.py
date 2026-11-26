"""Fail when a table that names CVars or menu labels by string names one that its game no longer has.

Four tables are keyed on strings that upstream owns, so an upstream rename drops a row without a warning:
  - combo/gui/ComboLinkRows.h `kLinkRows`: one OoT CVar and up to two MM CVars per row. Each must exist in its own
    game's sources (soh/ for OoT, mm/ for MM, libultraship/ for both).
  - combo/gui/ComboBothGames.cpp `kSharedItems`: one CVar both games read. It must exist in both.
  - combo/gui/ComboCrossGamePanels.cpp `kSwitches`: the live cross-game switches. Each CVar must be read somewhere
    outside the table (soh/, mm/ or the rest of combo/), and each upstream `gEnhancements.*` name the table writes
    through must exist in one of the games.
  - the menu labels in combo/gui/ComboMenuNav.cpp: the section and page names (`kSettingsSection`, `kModMenuPage`,
    `kProgramPages`, the aliases' port labels and alike), which pick the ports' pages by their English label. Each
    label must appear in one of the games' menu sources.

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
LIVE_SWITCHES_FILE = "combo/gui/ComboCrossGamePanels.cpp"
LIVE_SWITCHES_TABLE = "kSwitches"
MENU_LABEL_FILE = "combo/gui/ComboMenuNav.cpp"
MENU_SOURCES = ("soh/soh/SohGui", "mm/2s2h/BenGui")
GAME_FOLDERS = {"oot": ("soh", "libultraship", "CMake"), "mm": ("mm", "libultraship", "CMake")}
# Shared items one game does not have, and why: OOT applies the speed modifier whenever its button is held, so only MM
# has the gate (see kSharedItems in combo/gui/ComboBothGames.cpp).
MM_ONLY_SHARED_ITEMS = {"gCheats.SpeedModifier.Mode"}

CVAR_LITERAL = r'"g[A-Za-z]+(\.[A-Za-z0-9_]+)+"'
CVAR_MACRO = r'CVAR_[A-Z_]+\(\s*"[A-Za-z0-9_.]+"'
CVAR_PREFIX_EXPANSION = r'\$\{CVAR_PREFIX_[A-Z_]+\}\.[A-Za-z0-9_.]+'
UPSTREAM_CVAR_LITERAL = re.compile(r'"(gEnhancements(?:\.[A-Za-z0-9_]+)+)"')
MENU_LABEL_CONSTANT = re.compile(r'constexpr const char\* k\w*(?:Section|Page)\s*=\s*"([^"]+)"')
MENU_LABEL_ARRAY = re.compile(r'constexpr const char\* kProgramPages\[\]\s*=\s*\{([^}]*)\}')
MENU_LABEL_ALIAS = re.compile(r'\{\s*"([^"]+)"\s*,\s*"[^"]+"\s*\}')
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


def menu_labels():
    text = (ROOT / MENU_LABEL_FILE).read_text(encoding="utf-8")
    labels = set(MENU_LABEL_CONSTANT.findall(text))
    for array in MENU_LABEL_ARRAY.findall(text):
        labels.update(STRING_LITERAL.findall(array))
    labels.update(MENU_LABEL_ALIAS.findall(text))
    return labels


def check_menu_labels():
    menu_text = ""
    listing = subprocess.run(["git", "ls-files", "-z", "--", *MENU_SOURCES], cwd=ROOT, capture_output=True,
                             check=True).stdout.decode("utf-8").split("\0")
    for name in listing:
        if name.endswith((".cpp", ".h")):
            menu_text += (ROOT / name).read_text(encoding="utf-8", errors="replace")
    return [f'{MENU_LABEL_FILE}: the menu names "{label}", which neither game\'s menu sources contain'
            for label in sorted(menu_labels()) if f'"{label}"' not in menu_text]


def is_read_outside(name, table_file):
    """True when the CVar's full name is a string literal in a tracked source file other than the table's own."""
    result = subprocess.run(["git", "grep", "-l", "-I", "-F", f'"{name}"', "--", "soh", "mm", "combo"], cwd=ROOT,
                            capture_output=True, check=False)
    files = result.stdout.decode("utf-8", errors="replace").split()
    return any(path != table_file for path in files)


def live_switch_names():
    return [shared_item_name(row) for row in table_rows(LIVE_SWITCHES_FILE, LIVE_SWITCHES_TABLE)]


def check_live_switches(known):
    problems = [f"{LIVE_SWITCHES_FILE}: live switch {name} is not read anywhere outside the table"
                for name in live_switch_names() if not is_read_outside(name, LIVE_SWITCHES_FILE)]
    text = (ROOT / LIVE_SWITCHES_FILE).read_text(encoding="utf-8")
    for name in sorted(set(UPSTREAM_CVAR_LITERAL.findall(text))):
        if not any(cvar_exists(name, known[game]) for game in known):
            problems.append(f"{LIVE_SWITCHES_FILE}: upstream CVar {name} is in neither game's sources")
    return problems


def main():
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    known = {game: known_cvars(folders) for game, folders in GAME_FOLDERS.items()}
    link_rows = len(table_rows(LINK_ROWS_FILE, LINK_ROWS_TABLE))
    shared_items = len(table_rows(SHARED_ITEMS_FILE, SHARED_ITEMS_TABLE))
    live_switches = len(live_switch_names())
    labels = len(menu_labels())
    if min(link_rows, shared_items, live_switches, labels) == 0:
        sys.exit("error: found no rows in one of the tables. The table's layout changed; update this script.")
    problems = check_link_rows(known) + check_shared_items(known) + check_live_switches(known) + check_menu_labels()
    if problems:
        print("\n".join(problems))
        sys.exit(f"error: {len(problems)} table entr(ies) name something their game does not have. An upstream "
                 f"rename drops a row silently; follow the rename or remove the row.")
    print(f"{link_rows} link rows, {shared_items} shared items, {live_switches} live switches and {labels} menu labels "
          f"all exist in their game's sources.")


if __name__ == "__main__":
    main()
