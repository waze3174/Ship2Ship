import json
import logging
import os
import threading
import weakref
from dataclasses import dataclass
from typing import Any, ClassVar, Dict, List, Mapping, Optional, Set, Tuple

from BaseClasses import CollectionState, Item, ItemClassification, Location, Region, Tutorial
from Options import OptionError
from worlds.AutoWorld import LogicMixin, WebWorld, World

from .host_settings import Ship2ShipSettings
from .options import OPTION_GROUPS, Ship2ShipOptions
from .oracle import (MASTER_SEED_BITS, PROTOCOL, OpenedWorld, OpenSettings, OracleError, OracleProcess, PoolEntry,
                     ReachAnswer, is_won, parse_opened)
from .layout import MAX_OPEN_ATTEMPTS, LayoutReport, assess_layout, pool_counts
from .reach import ReachCache, count_pieces
from .relevance import logic_relevant_names
from .seedout import (SEED_FILE_EXTENSION, finalize_request, other_placement, own_placement, slot_data_for)
from .tables import (CLASS_FILLER, CLASS_PROGRESSION, CLASS_TRAP, CLASS_USEFUL, MM, OOT,
                     STATIC_FILLER_NAMES, ap_name, foreign_item_class, split_ap_name, static_tables,
                     strongest_class)

GAME_NAME = "Ship2Ship"
VICTORY = "Victory"
REGION_NAMES = {OOT: "Hyrule", MM: "Termina"}

CLASSIFICATIONS = {
    CLASS_PROGRESSION: ItemClassification.progression,
    CLASS_USEFUL: ItemClassification.useful,
    CLASS_FILLER: ItemClassification.filler,
    CLASS_TRAP: ItemClassification.trap,
}
# The classes of items that are neither progression nor a trap: dropped first when the pool is larger than the
# locations, and the ones the logic might still read.
JUNK_CLASSES = (CLASS_FILLER, CLASS_USEFUL)

logger = logging.getLogger(GAME_NAME)
TABLES = static_tables()


@dataclass(frozen=True)
class OpenAttempt:
    master_seed: int
    opened: OpenedWorld
    cache: ReachCache
    report: LayoutReport


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
        self.logic_relevant: Set[str] = set()  # junk-class names the logic reads, kept as progression
        self._finalize_lock = threading.Lock()
        self._finalized_seed: Optional[Mapping[str, Any]] = None

    # ---- generation ----

    def generate_early(self) -> None:
        settings = self._open_settings()
        comborando = self._comborando_path()
        self.oracle = OracleProcess.start(comborando, os.path.dirname(comborando))
        weakref.finalize(self, self.oracle.close)
        attempt = self._open_playable_layout(settings)
        self.master_seed, self.opened, self.reach_cache = attempt.master_seed, attempt.opened, attempt.cache
        logger.info("%s: open request %s", self.player_name,
                    json.dumps(settings.to_request(self.master_seed, self.multiworld.seed_name), sort_keys=True))
        self._index_opened(self.opened)
        self.pieces_required = settings.pieces_required if settings.triforce_hunt else 0
        self._promote_junk_the_logic_reads()
        logger.info("%s: layout: %s", self.player_name, attempt.report.describe())
        logger.info("%s: %d checks, %d never reachable, %d pool entries, effective %s", self.player_name,
                    len(self.opened.locations), len(self.opened.never_reachable), len(self.opened.pool),
                    json.dumps(self.opened.effective, sort_keys=True))

    def _open_playable_layout(self, settings: OpenSettings) -> "OpenAttempt":
        """Opens the world with fresh master seeds until the layout is playable (layout.py), like the generator's own
        attempts. When none is, the best one seen is opened again, or the slot fails if none can be won."""
        best: Optional[OpenAttempt] = None
        last_error = ""
        for number in range(1, MAX_OPEN_ATTEMPTS + 1):
            master_seed = self.random.getrandbits(MASTER_SEED_BITS)
            try:
                attempt = self._open_attempt(settings, master_seed)
            except OracleError as error:
                last_error = str(error)
                logger.info("%s: attempt %d (master seed %d) could not open: %s", self.player_name, number,
                            master_seed, last_error)
                continue
            logger.info("%s: attempt %d (master seed %d): %s", self.player_name, number, master_seed,
                        attempt.report.describe())
            if attempt.report.acceptable:
                return attempt
            if best is None or attempt.report.rank < best.report.rank:
                best = attempt
        if best is None:
            raise OptionError(f"{self.player_name}: Ship2Ship could not set this slot up in {MAX_OPEN_ATTEMPTS} "
                              f"attempts. The last answer was: {last_error}")
        if not best.report.winnable:
            raise OptionError(f"{self.player_name}: none of {MAX_OPEN_ATTEMPTS} Ship2Ship layouts could be won with "
                              "every item; these settings may not be playable")
        logger.warning("%s: no layout in %d attempts was fully playable; using the best (%s)", self.player_name,
                       MAX_OPEN_ATTEMPTS, best.report.describe())
        return self._open_attempt(settings, best.master_seed)

    def _open_attempt(self, settings: OpenSettings, master_seed: int) -> "OpenAttempt":
        try:
            request = settings.to_request(master_seed, self.multiworld.seed_name)
        except ValueError as error:
            raise OptionError(f"{self.player_name}: {error}")
        opened = parse_opened(self.oracle.call(request))
        cache = ReachCache(self.oracle.call, self.item_name_to_id)
        self._require_known_names(opened)
        return OpenAttempt(master_seed, opened, cache, assess_layout(opened, pool_counts(opened), cache.answer))

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

    def _require_known_names(self, opened: OpenedWorld) -> None:
        for game, native in opened.locations:
            self._require_known(self.location_name_to_id, ap_name(game, native), "location")
        for entry in opened.pool:
            self._require_known(self.item_name_to_id, ap_name(entry.game, entry.name), "item")

    def _index_opened(self, opened: OpenedWorld) -> None:
        for entry in opened.pool:
            name = ap_name(entry.game, entry.name)
            known = self.class_by_name.get(name)
            self.class_by_name[name] = entry.item_class if known is None else strongest_class(known, entry.item_class)
            self.pool_items.add((entry.game, entry.name))
        self._refresh_filler_names()

    def _refresh_filler_names(self) -> None:
        self.filler_names = sorted(name for name, item_class in self.class_by_name.items()
                                   if item_class == CLASS_FILLER)

    def _promote_junk_the_logic_reads(self) -> None:
        """Items the generator calls junk but its logic reads count as progression here, or Archipelago's accessibility
        check finds locations behind them unreachable."""
        pool = pool_counts(self.opened)
        candidates = [name for name, item_class in self.class_by_name.items() if item_class in JUNK_CLASSES]
        self.logic_relevant = set(logic_relevant_names(pool, candidates, self.reach_cache.answer))
        for name in self.logic_relevant:
            self.class_by_name[name] = CLASS_PROGRESSION
        self._refresh_filler_names()
        logger.info("%s: junk the logic reads, now progression: %s", self.player_name, sorted(self.logic_relevant))

    @staticmethod
    def _require_known(table: Mapping[str, int], name: str, kind: str) -> None:
        if name not in table:
            raise RuntimeError(f"comborando knows the {kind} '{name}', but this apworld's id table does not: the "
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
        pool = [self.create_item_of_class(ap_name(entry.game, entry.name), self._pool_class_of(entry))
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
        for droppable in JUNK_CLASSES:
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

    def _pool_class_of(self, entry: PoolEntry) -> str:
        return CLASS_PROGRESSION if ap_name(entry.game, entry.name) in self.logic_relevant else entry.item_class

    def _class_of(self, name: str) -> str:
        return self.class_by_name.get(name) or TABLES.item_facts[name].item_class

    def get_filler_item_name(self) -> str:
        return self.random.choice(self.filler_names or STATIC_FILLER_NAMES)

    # ---- output ----

    def fill_slot_data(self) -> Mapping[str, Any]:
        logger.info("%s: %d oracle questions (distinct item sets), %.1f s asking", self.player_name,
                    self.reach_cache.misses, self.reach_cache.seconds)
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
                request = finalize_request(placements, self.player_name, self.multiworld.seed_name)
                self._finalized_seed = self.oracle.call(request)["seed"]
            return self._finalized_seed

    def _placement_at(self, location: Location) -> Mapping[str, Any]:
        game, check = self.location_origin[location.name]
        item = location.item
        if item.player == self.player and item.code is not None:
            item_game, native = split_ap_name(item.name)
            if (item_game, native) in self.pool_items:
                return own_placement(game, check, item_game, native)
        return other_placement(game, check, self.multiworld.get_player_name(item.player), item.name, item.advancement,
                               foreign_item_class(item.advancement, item.useful, item.trap))
