"""Which checks the owned items reach, asked of the oracle and remembered.

Archipelago asks "can this location be reached" thousands of times with only a few thousand distinct item sets. The
answer is a pure, monotone function of the owned items, so it is cached by the sorted (item, count) tuple. Triforce
Pieces never change what is reachable, so they are left out of the key and counted in Python.
"""
import threading
from typing import Callable, Dict, Mapping, Tuple

from .oracle import ReachAnswer, reach_request, parse_reach
from .tables import GAME_TAGS, TRIFORCE_PIECE_NAMES, split_ap_name

ReachKey = Tuple[Tuple[str, int], ...]
Ask = Callable[[Dict], Dict]


def reach_key(owned: Mapping[str, int], known_names: Mapping[str, object]) -> ReachKey:
    """The owned item names the oracle knows, minus Triforce Pieces, sorted, with their counts."""
    return tuple(sorted((name, count) for name, count in owned.items()
                        if count > 0 and name in known_names and name not in TRIFORCE_PIECE_NAMES))


def count_pieces(owned: Mapping[str, int]) -> int:
    return sum(owned.get(name, 0) for name in TRIFORCE_PIECE_NAMES)


def request_for(key: ReachKey) -> Dict:
    by_game: Dict[str, Dict[str, int]] = {game: {} for game in GAME_TAGS}
    for name, count in key:
        game, native = split_ap_name(name)
        by_game[game][native] = count
    return reach_request(by_game)


class ReachCache:
    """One per world. Safe for the threads Archipelago generates in: hits take no lock, a miss asks the oracle once."""

    def __init__(self, ask: Ask, known_names: Mapping[str, object]):
        self._ask = ask
        self._known_names = known_names
        self._answers: Dict[ReachKey, ReachAnswer] = {}
        self._lock = threading.Lock()
        self.misses = 0

    def answer(self, owned: Mapping[str, int]) -> ReachAnswer:
        key = reach_key(owned, self._known_names)
        found = self._answers.get(key)
        if found is not None:
            return found
        with self._lock:
            found = self._answers.get(key)
            if found is None:
                found = parse_reach(self._ask(request_for(key)))
                self._answers[key] = found
                self.misses += 1
        return found

    def __len__(self) -> int:
        return len(self._answers)
