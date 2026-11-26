"""The static names, ids and facts about every item and location of both games.

Everything here is readable without a seed: Archipelago needs the id tables when it loads the world, and an item link
builds items from a world that never ran generate_early.

A name is the game's own name plus a game tag, "Progressive Hookshot (OoT)". The ids are append-only
(data/ids.json); data/groups.json holds each name's category and area. Both come from `comborando --ap-tables`
through scripts/ap/update_tables.py.
"""
import functools
import json
import pkgutil
from dataclasses import dataclass
from typing import Callable, Dict, FrozenSet, Mapping, Tuple

OOT = "oot"
MM = "mm"
GAME_TAGS = {OOT: " (OoT)", MM: " (MM)"}
GAME_LABELS = {OOT: "OoT", MM: "MM"}

CLASS_PROGRESSION = "progression"
CLASS_USEFUL = "useful"
CLASS_FILLER = "filler"
CLASS_TRAP = "trap"
# From most to least important to the logic; an item of two classes keeps the first.
CLASS_ORDER = (CLASS_PROGRESSION, CLASS_TRAP, CLASS_USEFUL, CLASS_FILLER)

JUNK_CATEGORY = "junk"

TRIFORCE_PIECE_NAMES = frozenset({"Triforce Piece (OoT)", "Piece of the Triforce (MM)"})
# Repeatable junk any seed's pool can absorb; used for filler when no seed is open (an item link's world).
STATIC_FILLER_NAMES = ("Blue Rupee (OoT)", "Red Rupee (OoT)", "Green Rupee (OoT)", "Purple Rupee (OoT)")

CATEGORY_GROUPS = {
    "major": "Major Items",
    "lesser": "Lesser Items",
    "junk": "Junk",
    "smallKey": "Small Keys",
    "bossKey": "Boss Keys",
    "token": "Skulltula Tokens",
    "mask": "Masks",
    "strayFairy": "Stray Fairies",
    "health": "Heart Items",
}

_AREA_PREFIXES = ("in ", "at ", "on ")

MM_ENTRANCE_KEYS = ("overworld", "interior", "dungeon", "decoupled", "mixed")
CROSS_ENTRANCE_KEYS = ("overworld", "interior", "dungeon")


def ap_name(game: str, native_name: str) -> str:
    return native_name + GAME_TAGS[game]


def split_ap_name(name: str) -> Tuple[str, str]:
    """("oot", "Progressive Hookshot") for "Progressive Hookshot (OoT)". ValueError when the name has no game tag."""
    for game, tag in GAME_TAGS.items():
        if name.endswith(tag):
            return game, name[: -len(tag)]
    raise ValueError(f"'{name}' has no game tag")


def item_class_of(category: str, advancement: bool, trap: bool) -> str:
    """The class the oracle gives a pool item (OracleItemClass in ComboOracleServer.h)."""
    if trap:
        return CLASS_TRAP
    if advancement:
        return CLASS_PROGRESSION
    return CLASS_FILLER if category == JUNK_CATEGORY else CLASS_USEFUL


def strongest_class(first: str, second: str) -> str:
    return min(first, second, key=CLASS_ORDER.index)


def area_group_name(game: str, area: str) -> str:
    for prefix in _AREA_PREFIXES:
        if area.startswith(prefix):
            area = area[len(prefix):]
            break
    return f"{GAME_LABELS[game]}: {area}"


@dataclass(frozen=True)
class ItemFacts:
    category: str
    advancement: bool
    trap: bool

    @property
    def item_class(self) -> str:
        return item_class_of(self.category, self.advancement, self.trap)


@dataclass(frozen=True)
class SharedFamily:
    key: str
    oot_name: str
    mm_name: str | None

    def ap_names(self) -> Tuple[str, ...]:
        names = [ap_name(OOT, self.oot_name)]
        if self.mm_name:
            names.append(ap_name(MM, self.mm_name))
        return tuple(names)


@dataclass(frozen=True)
class Tables:
    item_ids: Mapping[str, int]
    location_ids: Mapping[str, int]
    item_facts: Mapping[str, ItemFacts]
    location_areas: Mapping[str, str]
    shared_families: Mapping[str, SharedFamily]

    def item_groups(self) -> Dict[str, FrozenSet[str]]:
        groups: Dict[str, set] = {GAME_LABELS[game]: set() for game in GAME_TAGS}
        for name, facts in self.item_facts.items():
            groups[GAME_LABELS[split_ap_name(name)[0]]].add(name)
            groups.setdefault(CATEGORY_GROUPS.get(facts.category, facts.category), set()).add(name)
        for family in self.shared_families.values():
            groups[f"Shared: {family.key}"] = set(family.ap_names())
        return {group: frozenset(names) for group, names in groups.items()}

    def location_groups(self) -> Dict[str, FrozenSet[str]]:
        groups: Dict[str, set] = {GAME_LABELS[game]: set() for game in GAME_TAGS}
        for name, area in self.location_areas.items():
            game = split_ap_name(name)[0]
            groups[GAME_LABELS[game]].add(name)
            groups.setdefault(area_group_name(game, area), set()).add(name)
        return {group: frozenset(names) for group, names in groups.items()}


Reader = Callable[[str], bytes]


def package_reader(package: str) -> Reader:
    """Reads a data file of the world, from a folder or from inside the .apworld zip."""

    def read(name: str) -> bytes:
        data = pkgutil.get_data(package, "data/" + name)
        if data is None:
            raise FileNotFoundError(f"{package}: data/{name}")
        return data

    return read


def load_tables(read: Reader) -> Tables:
    ids = json.loads(read("ids.json"))
    groups = json.loads(read("groups.json"))
    families = json.loads(read("shared_families.json"))["families"]
    item_ids, location_ids = ids["items"], ids["locations"]
    missing = (set(groups["items"]) ^ set(item_ids)) | (set(groups["locations"]) ^ set(location_ids))
    if missing:
        raise ValueError(f"ids.json and groups.json name different things, e.g. {sorted(missing)[:3]}")
    return Tables(
        item_ids=item_ids,
        location_ids=location_ids,
        item_facts={name: ItemFacts(**facts) for name, facts in groups["items"].items()},
        location_areas=groups["locations"],
        shared_families={key: SharedFamily(key, f["oot"], f["mm"]) for key, f in families.items()},
    )


@functools.cache
def static_tables() -> Tables:
    """The tables shipped with the world, read once from its own package."""
    return load_tables(package_reader(__package__))
