"""The client side of `comborando --oracle` (protocol 1, see combo/rando/ComboOracleServer.h).

One request per line out, one response per line in, both JSON objects. The process answers one closed question,
"with exactly these items owned, which checks are reachable", through the same code the game's own fill and playthrough
use, so the logic is the game's and there is no Python copy of it.
"""
import json
import subprocess
import tempfile
import threading
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, FrozenSet, Mapping, Sequence, Tuple

from .tables import CROSS_ENTRANCE_KEYS, MM, MM_ENTRANCE_KEYS, OOT

PROTOCOL = 1
ORACLE_ARGUMENT = "--oracle"
SHUTDOWN_SECONDS = 30
LOG_TAIL_CHARS = 4000
MASTER_SEED_BITS = 32

WIN_BOSSES = "ganon+majora"
WIN_GANON = "ganon"
WIN_MAJORA = "majora"
WIN_PIECES = "pieces"

GAMES_KEY = "ootmm"
STARTING_GAME = "OOT"
GOAL_BOSSES = "bosses"
GOAL_TRIFORCE_HUNT = "triforceHunt"


class OracleError(RuntimeError):
    pass


class OracleClient:
    """Speaks the protocol over two text streams. One call at a time: the lock covers write and read, so threads
    (Archipelago's accessibility check, spheres and slot data run in parallel) never mix up their answers."""

    def __init__(self, to_oracle, from_oracle, describe_failure: Callable[[], str] = lambda: ""):
        self._to_oracle = to_oracle
        self._from_oracle = from_oracle
        self._describe_failure = describe_failure
        self._lock = threading.Lock()
        self.library: Mapping[str, Any] = {}

    def call(self, request: Mapping[str, Any]) -> Dict[str, Any]:
        command = request.get("cmd")
        with self._lock:
            try:
                self._to_oracle.write(json.dumps(request) + "\n")
                self._to_oracle.flush()
                line = self._from_oracle.readline()
            except (OSError, ValueError) as error:
                raise OracleError(f"the oracle stopped answering '{command}': {error}. {self._describe_failure()}")
        if not line:
            raise OracleError(f"the oracle exited during '{command}'. {self._describe_failure()}")
        try:
            response = json.loads(line)
        except ValueError:
            raise OracleError(f"the oracle answered '{command}' with something that is not JSON: {line[:200]!r}")
        if not response.get("ok"):
            raise OracleError(f"the oracle refused '{command}': {response.get('error', 'no reason given')}")
        return response

    def check_protocol(self) -> None:
        hello = self.call({"cmd": "hello"})
        if hello.get("protocol") != PROTOCOL:
            raise OracleError(f"this apworld speaks oracle protocol {PROTOCOL}, but comborando speaks "
                              f"{hello.get('protocol')}. Use the comborando that matches the apworld")
        self.library = hello.get("library", {})

    def close(self) -> None:
        with self._lock:
            try:
                self._to_oracle.close()
            except OSError:
                pass


class OracleProcess(OracleClient):
    """A comborando child process. It exits when its stdin closes; stderr goes to a file for the failure message."""

    def __init__(self, command: Sequence[str], working_directory: str):
        self._log = tempfile.TemporaryFile("w+", encoding="utf-8", errors="replace")
        try:
            self.process = subprocess.Popen(list(command), cwd=working_directory, stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE, stderr=self._log, text=True, encoding="utf-8",
                                            bufsize=1)
        except OSError as error:
            self._log.close()
            raise OracleError(f"could not start {command[0]}: {error}")
        super().__init__(self.process.stdin, self.process.stdout, self._log_tail)

    @classmethod
    def start(cls, comborando: str, working_directory: str) -> "OracleProcess":
        process = cls([comborando, ORACLE_ARGUMENT], working_directory)
        try:
            process.check_protocol()
        except OracleError:
            process.close()
            raise
        return process

    def _log_tail(self) -> str:
        try:
            self._log.seek(0)
            tail = self._log.read()[-LOG_TAIL_CHARS:]
        except (OSError, ValueError):
            return ""
        return f"comborando said: {tail}" if tail.strip() else ""

    def close(self) -> None:
        super().close()
        try:
            self.process.wait(timeout=SHUTDOWN_SECONDS)
        except subprocess.TimeoutExpired:
            self.process.kill()
        if self.process.stdout:
            self.process.stdout.close()
        self._log.close()


def clean_game_settings(label: str, values: Mapping[str, Any]) -> Dict[str, Any]:
    """A game's settings block as the oracle restores it: CVar-style names to whole numbers or text."""
    cleaned: Dict[str, Any] = {}
    for key, value in values.items():
        if not isinstance(key, str) or not key:
            raise ValueError(f"{label}: '{key}' is not a setting name")
        if isinstance(value, bool):
            value = int(value)
        if not isinstance(value, (int, str)):
            raise ValueError(f"{label}: '{key}' must be a whole number or text, not {value!r}")
        cleaned[key] = value
    return cleaned


@dataclass(frozen=True)
class OpenSettings:
    """Everything the oracle decides a seed from, except the seed itself. Maps one-to-one onto the `open` request."""
    triforce_hunt: bool
    pieces_required: int
    pieces_total: int
    shared_items: FrozenSet[str]
    unified_inventory: bool
    form_logic_credit: bool
    lost_woods_link: bool
    mm_entrances: FrozenSet[str]
    cross_entrances: FrozenSet[str]
    cross_entrance_percent: int
    oot_settings: Mapping[str, Any] = field(default_factory=dict)
    mm_settings: Mapping[str, Any] = field(default_factory=dict)
    oot_tricks: FrozenSet[str] = frozenset()

    def to_request(self, master_seed: int, seed_label: str) -> Dict[str, Any]:
        return {
            "cmd": "open",
            "masterSeed": master_seed,
            "seed": seed_label,
            "games": GAMES_KEY,
            "startingGame": STARTING_GAME,
            "goal": {"type": GOAL_TRIFORCE_HUNT if self.triforce_hunt else GOAL_BOSSES,
                     "requiredPieces": self.pieces_required, "totalPieces": self.pieces_total},
            "sharedItems": sorted(self.shared_items),
            "unifiedInventory": self.unified_inventory,
            "formLogicCredit": self.form_logic_credit,
            "lostWoodsLink": self.lost_woods_link,
            "entranceSettings": {
                "mm": {key: key in self.mm_entrances for key in MM_ENTRANCE_KEYS},
                "cross": {**{key: key in self.cross_entrances for key in CROSS_ENTRANCE_KEYS},
                          "percent": self.cross_entrance_percent},
            },
            "settings": {
                "oot": {"settings": clean_game_settings("OoT settings", self.oot_settings),
                        "enabledTricks": sorted(self.oot_tricks)},
                "mm": {"settings": clean_game_settings("MM settings", self.mm_settings)},
            },
        }


@dataclass(frozen=True)
class PoolEntry:
    game: str
    name: str
    count: int
    item_class: str
    category: str
    advancement: bool


@dataclass(frozen=True)
class OpenedWorld:
    """What `open` decided before the fill: the checks, the free item pool and how the seed is won."""
    locations: Tuple[Tuple[str, str], ...]  # (game, name); bit i of a reach mask is locations[i]
    never_reachable: FrozenSet[int]
    pool: Tuple[PoolEntry, ...]
    win_condition: str
    required_pieces: int
    effective: Mapping[str, Any]


def parse_opened(response: Mapping[str, Any]) -> OpenedWorld:
    return OpenedWorld(
        locations=tuple((game, name) for game, name in response["locations"]),
        never_reachable=frozenset(response["neverReachable"]),
        pool=tuple(PoolEntry(e["game"], e["name"], e["count"], e["class"], e["category"], e["advancement"])
                   for e in response["items"]),
        win_condition=response["win"]["condition"],
        required_pieces=response["win"]["requiredPieces"],
        effective=response["effective"],
    )


@dataclass(frozen=True)
class ReachAnswer:
    mask: int  # bit i set when locations[i] is reachable
    ganon: bool
    majora: bool


def parse_reach(response: Mapping[str, Any]) -> ReachAnswer:
    win = response["win"]
    return ReachAnswer(int(response["mask"], 16), bool(win["ganon"]), bool(win["majora"]))


def is_won(condition: str, answer: ReachAnswer, pieces: int, required_pieces: int) -> bool:
    """Whether the seed is won. The Triforce Piece count is the caller's: it is not part of what the oracle is asked."""
    if condition == WIN_PIECES:
        return pieces >= required_pieces
    if condition == WIN_GANON:
        return answer.ganon
    if condition == WIN_MAJORA:
        return answer.majora
    return answer.ganon and answer.majora


def reach_request(items_by_game: Mapping[str, Mapping[str, int]]) -> Dict[str, Any]:
    return {"cmd": "reach", "items": {game: dict(items_by_game.get(game, {})) for game in (OOT, MM)}}
