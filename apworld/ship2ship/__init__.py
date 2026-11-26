import json
import logging
import os
import threading
import weakref
from typing import Any, ClassVar, Dict, List, Mapping, Optional, Set, Tuple

from BaseClasses import CollectionState, Item, ItemClassification, Location, Region, Tutorial
from Options import OptionError
from worlds.AutoWorld import LogicMixin, WebWorld, World

from .host_settings import Ship2ShipSettings
from .options import OPTION_GROUPS, Ship2ShipOptions
from .oracle import (MASTER_SEED_BITS, PROTOCOL, OpenedWorld, OpenSettings, OracleError, OracleProcess, ReachAnswer,
                     is_won, parse_opened)
from .reach import ReachCache, count_pieces
from .seedout import (SEED_FILE_EXTENSION, finalize_request, other_placement, own_placement, slot_data_for)
from .tables import (CLASS_FILLER, CLASS_PROGRESSION, CLASS_TRAP, CLASS_USEFUL, MM, OOT,
                     STATIC_FILLER_NAMES, ap_name, split_ap_name, static_tables, strongest_class)

GAME_NAME = "Ship2Ship"
VICTORY = "Victory"
REGION_NAMES = {OOT: "Hyrule", MM: "Termina"}

CLASSIFICATIONS = {
    CLASS_PROGRESSION: ItemClassification.progression,
    CLASS_USEFUL: ItemClassification.useful,
    CLASS_FILLER: ItemClassification.filler,
    CLASS_TRAP: ItemClassification.trap,
}
# Pool items dropped first when the pool is larger than the locations. Never a progression item.
DROPPABLE_CLASSES = (CLASS_FILLER, CLASS_USEFUL)

logger = logging.getLogger(GAME_NAME)
TABLES = static_tables()


class Ship2ShipItem(Item):
    game = GAME_NAME


class Ship2ShipLocation(Location):
    game = GAME_NAME


class Ship2ShipWeb(WebWorld):
    theme = "ocean"
    option_groups = OPTION_GROUPS
    bug_report_page = "https://github.com/waze3174/Ship2Ship/issues"
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "How to generate a Ship2Ship slot, with the logic of Ship2Ship's own generator.",
        "English",
        "setup_en.md",
        "setup/en",
        ["Ship2Ship"],
    )]


class Ship2ShipLogic(LogicMixin):
    """Each state remembers the oracle's answer for the items it owns, until one of this world's items changes."""

    def init_mixin(self, multiworld) -> None:
        self.ship2ship_answers: Dict[int, ReachAnswer] = {}

    def copy_mixin(self, new_state):
        new_state.ship2ship_answers = dict(self.ship2ship_answers)
        return new_state


class Ship2ShipWorld(World):
    """Ocarina of Time and Majora's Mask in one randomized world, in Ship2Ship, the PC port that runs both games
    side by side. Items found in one game can open the other, and every access rule is Ship2Ship's own generator's
    logic, asked through its `comborando --oracle`, so the logic is exactly the game's."""

    game = GAME_NAME
    web = Ship2ShipWeb()
    options_dataclass = Ship2ShipOptions
    options: Ship2ShipOptions
    settings: ClassVar[Ship2ShipSettings]
    topology_present = False
    origin_region_name = "Menu"

    item_name_to_id = TABLES.item_ids
    location_name_to_id = TABLES.location_ids
    item_name_groups = TABLES.item_groups()
    location_name_groups = TABLES.location_groups()

    def __init__(self, multiworld, player):
        super().__init__(multiworld, player)
        self.oracle: Optional[OracleProcess] = None
        self.opened: Optional[OpenedWorld] = None
        self.reach_cache: Optional[ReachCache] = None
        self.master_seed = 0
        self.pieces_required = 0
        self.location_origin: Dict[str, Tuple[str, str]] = {}  # AP location name -> (game, the game's own name)
        self.class_by_name: Dict[str, str] = {}
        self.pool_items: Set[Tuple[str, str]] = set()  # (game, native name) the oracle can place for this slot
        self.filler_names: List[str] = []
        self._finalize_lock = threading.Lock()
        self._finalized_seed: Optional[Mapping[str, Any]] = None

    # ---- generation ----

    def generate_early(self) -> None:
        settings = self._open_settings()
        comborando = self._comborando_path()
        self.oracle = OracleProcess.start(comborando, os.path.dirname(comborando))
        weakref.finalize(self, self.oracle.close)
        self.master_seed = self.random.getrandbits(MASTER_SEED_BITS)
        try:
            request = settings.to_request(self.master_seed, self.multiworld.seed_name)
        except ValueError as error:
            raise OptionError(f"{self.player_name}: {error}")
        try:
            self.opened = parse_opened(self.oracle.call(request))
        except OracleError as error:
            raise OptionError(f"{self.player_name}: Ship2Ship could not set this slot up. {error}")
        logger.info("%s: open request %s", self.player_name, json.dumps(request, sort_keys=True))
        self._index_opened(self.opened)
        self.pieces_required = settings.pieces_required if settings.triforce_hunt else 0
        self.reach_cache = ReachCache(self.oracle.call, self.item_name_to_id)
        logger.info("%s: %d checks, %d never reachable, %d pool entries, effective %s", self.player_name,
                    len(self.opened.locations), len(self.opened.never_reachable), len(self.opened.pool),
                    json.dumps(self.opened.effective, sort_keys=True))

    def _open_settings(self) -> OpenSettings:
        opts = self.options
        required = opts.triforce_pieces_required.value
        total = opts.triforce_pieces_total.value
        if total < required:
            logger.warning("%s: Triforce Pieces in the Pool (%d) is below the pieces required (%d); using %d",
                           self.player_name, total, required, required)
            total = required
        return OpenSettings(
            triforce_hunt=opts.goal.current_key == "triforce_hunt",
            pieces_required=required,
            pieces_total=total,
            shared_items=frozenset(opts.shared_items.value),
            unified_inventory=bool(opts.unified_inventory),
            form_logic_credit=bool(opts.form_logic_credit),
            lost_woods_link=bool(opts.lost_woods_link),
            mm_entrances=frozenset(opts.mm_entrances.value),
            cross_entrances=frozenset(opts.cross_entrances.value),
            cross_entrance_percent=opts.cross_entrance_percent.value,
            oot_settings=dict(opts.oot_settings.value),
            mm_settings=dict(opts.mm_settings.value),
            oot_tricks=frozenset(opts.oot_tricks.value),
        )

    def _comborando_path(self) -> str:
        path = str(self.settings.comborando_path)
        if not os.path.isfile(path):
            raise FileNotFoundError(
                "Ship2Ship needs comborando to generate. Set ship2ship_options: comborando_path in host.yaml to the "
                f"comborando executable of a Ship2Ship install or the generator pack (now: '{path}').")
        return os.path.abspath(path)

    def _index_opened(self, opened: OpenedWorld) -> None:
        for game, native in opened.locations:
            self._require_known(self.location_name_to_id, ap_name(game, native), "location")
        for entry in opened.pool:
            name = ap_name(entry.game, entry.name)
            self._require_known(self.item_name_to_id, name, "item")
            known = self.class_by_name.get(name)
            self.class_by_name[name] = entry.item_class if known is None else strongest_class(known, entry.item_class)
            self.pool_items.add((entry.game, entry.name))
        self.filler_names = sorted(name for name, item_class in self.class_by_name.items()
                                   if item_class == CLASS_FILLER)

    @staticmethod
    def _require_known(table: Mapping[str, int], name: str, kind: str) -> None:
        if name not in table:
            raise OracleError(f"comborando knows the {kind} '{name}', but this apworld's id table does not: the "
                              "apworld and comborando are from different Ship2Ship versions")

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        regions = {game: Region(name, self.player, self.multiworld) for game, name in REGION_NAMES.items()}
        for region in regions.values():
            menu.connect(region)
        for bit, (game, native) in enumerate(self.opened.locations):
            if bit in self.opened.never_reachable:
                continue
            name = ap_name(game, native)
            location = Ship2ShipLocation(self.player, name, self.location_name_to_id[name], regions[game])
            location.access_rule = self._location_rule(bit)
            regions[game].locations.append(location)
            self.location_origin[name] = (game, native)
        victory = Ship2ShipLocation(self.player, VICTORY, None, menu)
        victory.access_rule = self._is_won
        victory.place_locked_item(Ship2ShipItem(VICTORY, ItemClassification.progression, None, self.player))
        menu.locations.append(victory)
        self.multiworld.regions.extend([menu, *regions.values()])
        self.multiworld.completion_condition[self.player] = lambda state: state.has(VICTORY, self.player)

    def _location_rule(self, bit: int):
        wanted = 1 << bit
        return lambda state: self._answer(state).mask & wanted != 0

    def _answer(self, state: CollectionState) -> ReachAnswer:
        answers = state.ship2ship_answers
        answer = answers.get(self.player)
        if answer is None:
            answer = self.reach_cache.answer(state.prog_items[self.player])
            answers[self.player] = answer
        return answer

    def _is_won(self, state: CollectionState) -> bool:
        pieces = count_pieces(state.prog_items[self.player])
        return is_won(self.opened.win_condition, self._answer(state), pieces, self.opened.required_pieces)

    def collect(self, state: CollectionState, item: Item) -> bool:
        changed = super().collect(state, item)
        if changed:
            state.ship2ship_answers.pop(self.player, None)
        return changed

    def remove(self, state: CollectionState, item: Item) -> bool:
        changed = super().remove(state, item)
        if changed:
            state.ship2ship_answers.pop(self.player, None)
        return changed

    def create_items(self) -> None:
        pool = [self.create_item_of_class(ap_name(entry.game, entry.name), entry.item_class)
                for entry in self.opened.pool for _ in range(entry.count)]
        slots = len(self.location_origin)
        if len(pool) > slots:
            pool = self._without_surplus(pool, len(pool) - slots)
        pool.extend(self.create_filler() for _ in range(slots - len(pool)))
        logger.info("%s: %d items for %d locations", self.player_name, len(pool), slots)
        self.multiworld.itempool.extend(pool)

    def _without_surplus(self, pool: List[Item], surplus: int) -> List[Item]:
        """The pool holds a place for checks the oracle can never reach; those places hold junk, so drop junk."""
        dropped: Set[int] = set()
        for droppable in DROPPABLE_CLASSES:
            candidates = [i for i, item in enumerate(pool) if i not in dropped
                          and CLASSIFICATIONS[droppable] == item.classification]
            self.random.shuffle(candidates)
            dropped.update(candidates[: surplus - len(dropped)])
        if len(dropped) < surplus:
            raise OptionError(f"{self.player_name}: the Ship2Ship pool has {surplus} more items than there are "
                              "reachable checks, and too few of them are junk to drop")
        return [item for i, item in enumerate(pool) if i not in dropped]

    # ---- items ----

    def create_item(self, name: str) -> Ship2ShipItem:
        return self.create_item_of_class(name, self._class_of(name))

    def create_item_of_class(self, name: str, item_class: str) -> Ship2ShipItem:
        return Ship2ShipItem(name, CLASSIFICATIONS[item_class], self.item_name_to_id[name], self.player)

    def _class_of(self, name: str) -> str:
        return self.class_by_name.get(name) or TABLES.item_facts[name].item_class

    def get_filler_item_name(self) -> str:
        return self.random.choice(self.filler_names or STATIC_FILLER_NAMES)

    # ---- output ----

    def fill_slot_data(self) -> Mapping[str, Any]:
        return slot_data_for(self._finalized(), self.opened.effective, self.oracle.library, self.master_seed, PROTOCOL)

    def generate_output(self, output_directory: str) -> None:
        name = self.multiworld.get_out_file_name_base(self.player) + SEED_FILE_EXTENSION
        with open(os.path.join(output_directory, name), "w", encoding="utf-8") as file:
            json.dump(self._finalized(), file)

    def _finalized(self) -> Mapping[str, Any]:
        """The consolidated seed, finalized once: fill_slot_data and generate_output run in different threads."""
        with self._finalize_lock:
            if self._finalized_seed is None:
                placements = [self._placement_at(location) for location in self.get_locations()
                              if location.address is not None]
                self._finalized_seed = self.oracle.call(finalize_request(placements))["seed"]
            return self._finalized_seed

    def _placement_at(self, location: Location) -> Mapping[str, Any]:
        game, check = self.location_origin[location.name]
        item = location.item
        if item.player == self.player and item.code is not None:
            item_game, native = split_ap_name(item.name)
            if (item_game, native) in self.pool_items:
                return own_placement(game, check, item_game, native)
        return other_placement(game, check, self.multiworld.get_player_name(item.player), item.name, item.advancement)
