from dataclasses import dataclass

from Options import Choice, OptionDict, OptionGroup, OptionSet, PerGameCommonOptions, Range, StartInventoryPool, Toggle

from .tables import CROSS_ENTRANCE_KEYS, MM_ENTRANCE_KEYS, static_tables

SHARED_ITEM_KEYS = tuple(static_tables().shared_families)  # the keys of combo/rando/SharedItems.h
MAX_TRIFORCE_PIECES = 100  # kMaxComboTriforcePieces


class Games(Choice):
    """Which games are in the slot. Both games share one world: items found in one can open the other.

    Only both is supported for now."""
    display_name = "Games"
    option_both = 0
    default = 0


class Goal(Choice):
    """How the slot is won.

    bosses: defeat Ganon in Ocarina of Time and Majora in Majora's Mask.
    triforce_hunt: collect the required Triforce Pieces, found in either game."""
    display_name = "Goal"
    option_bosses = 0
    option_triforce_hunt = 1
    default = 0


class TriforcePiecesRequired(Range):
    """Triforce Hunt: how many pieces win the game."""
    display_name = "Triforce Pieces Required"
    range_start = 1
    range_end = MAX_TRIFORCE_PIECES
    default = 10


class TriforcePiecesTotal(Range):
    """Triforce Hunt: how many pieces are in the item pool. Never fewer than the pieces required."""
    display_name = "Triforce Pieces in the Pool"
    range_start = 1
    range_end = MAX_TRIFORCE_PIECES
    default = 14


class SharedItems(OptionSet):
    """Item families that count in both games: one copy found in either game is owned in both.

    Keys: bows, bombBags, bombchuBags, magic, wallets, hookshot, fireArrows, iceArrows, lightArrows, lens, eponasSong,
    songOfStorms, goronMask, zoraMask, keatonMask, bunnyHood, maskOfTruth, songOfSoaring, minuetOfForest,
    boleroOfFire, serenadeOfWater, requiemOfSpirit, nocturneOfShadow, preludeOfLight, bottles, ocarina, songOfTime."""
    display_name = "Shared Items"
    valid_keys = frozenset(SHARED_ITEM_KEYS)


class UnifiedInventory(Toggle):
    """Every shared item family is shared, and whatever Majora's Mask starts with, Ocarina of Time starts with too."""
    display_name = "Unified Inventory"


class FormLogicCredit(Toggle):
    """The logic counts the forms Link takes in the other game (masks in Hyrule, adult Link in Termina)."""
    display_name = "Form Logic Credit"


class LostWoodsLink(Toggle):
    """The Lost Woods connects Hyrule and Termina, so the logic can cross between the games there."""
    display_name = "Lost Woods Link"
    default = 1


class MmEntrances(OptionSet):
    """Majora's Mask entrance shuffle. Keys: overworld, interior, dungeon, decoupled, mixed."""
    display_name = "Majora's Mask Entrance Shuffle"
    valid_keys = frozenset(MM_ENTRANCE_KEYS)


class CrossEntrances(OptionSet):
    """Entrances that swap between the games. Keys: overworld, interior, dungeon."""
    display_name = "Cross-Game Entrance Shuffle"
    valid_keys = frozenset(CROSS_ENTRANCE_KEYS)


class CrossEntrancePercent(Range):
    """How much of each chosen cross-game entrance pool is swapped."""
    display_name = "Cross-Game Entrance Percent"
    range_start = 0
    range_end = 100
    default = 50


class OotSettings(OptionDict):
    """Ocarina of Time's own randomizer settings, as the keys Ship2Ship's seed files store them
    (for example gRandoSettings.ShuffleCows: 1). Anything left out stays at the default.

    Export them from the Ship2Ship menu. OoT No Logic is not supported."""
    display_name = "Ocarina of Time Settings"


class MmSettings(OptionDict):
    """Majora's Mask's own randomizer settings, as the keys Ship2Ship's seed files store them. Anything left out stays
    at the default."""
    display_name = "Majora's Mask Settings"


class OotTricks(OptionSet):
    """Ocarina of Time logic tricks to allow, by their stable names."""
    display_name = "Ocarina of Time Tricks"


@dataclass
class Ship2ShipOptions(PerGameCommonOptions):
    games: Games
    goal: Goal
    triforce_pieces_required: TriforcePiecesRequired
    triforce_pieces_total: TriforcePiecesTotal
    shared_items: SharedItems
    unified_inventory: UnifiedInventory
    form_logic_credit: FormLogicCredit
    lost_woods_link: LostWoodsLink
    mm_entrances: MmEntrances
    cross_entrances: CrossEntrances
    cross_entrance_percent: CrossEntrancePercent
    oot_settings: OotSettings
    mm_settings: MmSettings
    oot_tricks: OotTricks
    start_inventory_from_pool: StartInventoryPool


OPTION_GROUPS = [
    OptionGroup("Goal", [Goal, TriforcePiecesRequired, TriforcePiecesTotal]),
    OptionGroup("Both Games", [Games, SharedItems, UnifiedInventory, FormLogicCredit, LostWoodsLink]),
    OptionGroup("Entrances", [MmEntrances, CrossEntrances, CrossEntrancePercent]),
    OptionGroup("Game Settings", [OotSettings, MmSettings, OotTricks]),
]
