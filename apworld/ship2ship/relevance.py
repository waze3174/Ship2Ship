"""Items the game's logic reads although the generator does not flag them as advancement.

Ship2Ship's own generator only needs its advancement items placed reachably, so a location behind a junk-class item
(arrows, rupees) can sit unreachable in its seeds. Archipelago needs every location reachable by progression alone, so
those items have to count as progression in a slot. The oracle says which: an item matters when a slot that owns every
other item still reaches less without it.
"""
from typing import Callable, Dict, Iterable, List, Mapping

AnswerOf = Callable[[Mapping[str, int]], object]


def logic_relevant_names(pool: Mapping[str, int], candidates: Iterable[str], answer_of: AnswerOf) -> List[str]:
    """The candidate names (all their copies) whose absence changes what the full pool reaches, in name order.

    Names are dropped one at a time while the answer stays the full pool's; the ones that cannot be dropped are the
    ones the logic needs, so owning all of them and the progression items reaches everything.
    """
    target = answer_of(pool)
    remaining: Dict[str, int] = dict(pool)
    needed: List[str] = []
    for name in sorted(candidates):
        without = {key: count for key, count in remaining.items() if key != name}
        if answer_of(without) == target:
            remaining = without
        else:
            needed.append(name)
    return needed
