"""Whether an opened world is one worth playing.

Ship2Ship's own generator runs a fill per "attempt" and starts the next attempt with a new entrance layout when the
fill cannot place everything reachably. Archipelago does the fill, so the apworld asks the oracle for a layout and keeps
asking, with a new master seed, until the layout is playable: the seed can be won with every item, every check can be
reached with every item, and something can be reached with nothing owned.
"""
from dataclasses import dataclass
from typing import Callable, Dict, Mapping

from .oracle import OpenedWorld, ReachAnswer, is_won
from .reach import count_pieces
from .tables import ap_name

MAX_OPEN_ATTEMPTS = 30

AnswerOf = Callable[[Mapping[str, int]], ReachAnswer]


@dataclass(frozen=True)
class LayoutReport:
    winnable: bool  # the goal is reachable with every item owned
    first_sphere: int  # checks reachable with nothing owned
    never_reachable: int  # checks nothing reaches even with every item owned

    @property
    def acceptable(self) -> bool:
        return self.winnable and self.first_sphere > 0 and self.never_reachable == 0

    @property
    def rank(self):
        """Smaller is better, for choosing among layouts when none is acceptable."""
        return (not self.winnable, self.first_sphere == 0, self.never_reachable)

    def describe(self) -> str:
        return (f"{'winnable' if self.winnable else 'not winnable'}, {self.first_sphere} checks reachable with nothing "
                f"owned, {self.never_reachable} never reachable")


def pool_counts(opened: OpenedWorld) -> Dict[str, int]:
    """The opened world's item pool as AP names and counts."""
    counts: Dict[str, int] = {}
    for entry in opened.pool:
        name = ap_name(entry.game, entry.name)
        counts[name] = counts.get(name, 0) + entry.count
    return counts


def assess_layout(opened: OpenedWorld, pool: Mapping[str, int], answer_of: AnswerOf) -> LayoutReport:
    """`pool` is the AP-named item counts of the opened world's pool."""
    everything = answer_of(pool)
    return LayoutReport(
        winnable=is_won(opened.win_condition, everything, count_pieces(pool), opened.required_pieces),
        first_sphere=bin(answer_of({}).mask).count("1"),
        never_reachable=len(opened.never_reachable),
    )
