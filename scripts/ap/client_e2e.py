"""End-to-end test of Ship2Ship's Archipelago client against a real Archipelago server on this machine.

  client_e2e.py --ap-core <Archipelago folder> --harness <comboApClientHarness.exe> --ids <ids.json> --work <folder>
                [--two <AP_x.zip of two Ship2Ship slots>] [--start-inventory <AP_x.zip of a solo slot with a start
                inventory>] [--mixed <AP_x.zip of Ship2Ship + APQuest>] [--port 38299]

Run it with the Python of the Archipelago venv (it needs the `websockets` package). The server is Archipelago's own
MultiServer.py, bound to 127.0.0.1 only and stopped when the script ends. Each Ship2Ship slot is the real client
(combo/net/ComboArchipelago.cpp) over a fake game (combo/tests/ComboApClientHarness.cpp); an extra websocket client
connects as the same slot and records what the server itself delivered, so the harness is checked against the server's
own list and not only against itself.

The zips are what Archipelago's Generate.py writes: the multidata and one <room>_P<n>_<slot>.ship2ship.json per slot.
"""
import argparse
import asyncio
import json
import os
import pathlib
import queue
import re
import subprocess
import sys
import threading
import time
import uuid
import zipfile

import websockets

PHASE_CONNECTED = 2
PHASE_REFUSED = 3
PHASE_WRONG_ROOM = 4
ITEMS_HANDLING = 0b101
TAGS = {"oot": " (OoT)", "mm": " (MM)"}
TIMEOUT = 30
SERVER_START_TIMEOUT = 90


class Check:
    def __init__(self):
        self.results = []

    def __call__(self, name, ok, detail=""):
        self.results.append((name, bool(ok), detail))
        print(("PASS " if ok else "FAIL ") + name + (f"  [{detail}]" if detail and not ok else ""), flush=True)

    def failed(self):
        return [r for r in self.results if not r[1]]


def wait_until(predicate, timeout=TIMEOUT, interval=0.1):
    end = time.time() + timeout
    while time.time() < end:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return None


class Server:
    """Archipelago's MultiServer on 127.0.0.1, one multidata, with a savefile so it can be restarted."""

    def __init__(self, python, core, multidata, port, log, extra=()):
        self.cmd = [python, "MultiServer.py", "--host", "127.0.0.1", "--port", str(port), *extra, str(multidata)]
        self.core = core
        self.log = pathlib.Path(log)
        self.port = port
        self.process = None

    def start(self):
        self.log.parent.mkdir(parents=True, exist_ok=True)
        out = open(self.log, "ab")
        self.process = subprocess.Popen(self.cmd, cwd=self.core, stdout=out, stderr=subprocess.STDOUT)
        listening = f"server listening on 127.0.0.1:{self.port}"
        if not wait_until(lambda: listening in self.log.read_text(errors="replace"), SERVER_START_TIMEOUT, 0.5):
            raise RuntimeError("the server did not start; see " + str(self.log))

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(10)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.process = None

    def text(self):
        return self.log.read_text(errors="replace")


class Harness:
    """The real client over a fake game, driven through stdin."""

    def __init__(self, exe, slot, seed_name, server, state, cache, password="", extra=()):
        self.slot = slot
        self.cmd = [str(exe), "--slot", slot, "--seed-name", seed_name, "--server", server, "--state", str(state),
                    "--cache", str(cache), *extra]
        if password:
            self.cmd += ["--password", password]
        self.lines = queue.Queue()
        self.log_lines = []
        self.process = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            line = line.rstrip("\n")
            if line.startswith("STATE "):
                self.lines.put(json.loads(line[6:]))
            else:
                self.log_lines.append(line)

    def send(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def dump(self):
        while not self.lines.empty():
            self.lines.get_nowait()
        self.send("dump")
        try:
            return self.lines.get(timeout=10)
        except queue.Empty:
            raise RuntimeError(f"{self.slot}: no state; log tail: {self.log_lines[-5:]}")

    def granted(self):
        return [g["name"] + TAGS[g["game"]] for g in self.dump()["game"]["granted"]]

    def phase(self):
        return self.dump()["status"]["phase"]

    def quit(self):
        try:
            self.send("quit")
            self.process.wait(10)
        except Exception:
            self.process.kill()


class Observer:
    """A second websocket client of a slot: what the server delivers to it is what the real client must act on."""

    def __init__(self, port, slot, game, ids):
        self.port, self.slot, self.game = port, slot, game
        self.item_names = {v: k for k, v in ids["items"].items()}
        self.received = {}  # the server's list: index -> item name
        self.stop = threading.Event()
        self.thread = threading.Thread(target=lambda: asyncio.run(self._run()), daemon=True)
        self.thread.start()

    async def _run(self):
        # The server restarts in one test: reconnect, as a client does, and read the list again.
        while not self.stop.is_set():
            try:
                await self._session()
            except (OSError, websockets.exceptions.WebSocketException):
                await asyncio.sleep(0.5)

    async def _session(self):
        async with websockets.connect(f"ws://127.0.0.1:{self.port}", max_size=None) as ws:
            while not self.stop.is_set():
                try:
                    raw = await asyncio.wait_for(ws.recv(), 0.2)
                except asyncio.TimeoutError:
                    continue
                for packet in json.loads(raw):
                    if packet["cmd"] == "RoomInfo":
                        await ws.send(json.dumps([{
                            "cmd": "Connect", "game": self.game, "name": self.slot, "uuid": str(uuid.uuid4()),
                            "password": None, "version": {"major": 0, "minor": 6, "build": 4, "class": "Version"},
                            "items_handling": ITEMS_HANDLING, "tags": ["Tracker"], "slot_data": False}]))
                    elif packet["cmd"] == "ReceivedItems":
                        for offset, item in enumerate(packet["items"]):
                            self.received[packet["index"] + offset] = self.item_names.get(item["item"], "?")

    def names(self):
        return [self.received[index] for index in sorted(self.received)]

    def close(self):
        self.stop.set()
        self.thread.join(5)


class Room:
    """One extracted generation: the multidata and each slot's seed."""

    def __init__(self, zip_path, work, ids):
        self.dir = pathlib.Path(work)
        self.dir.mkdir(parents=True, exist_ok=True)
        zipfile.ZipFile(zip_path).extractall(self.dir)
        self.multidata = next(self.dir.glob("*.archipelago"))
        self.seeds = {}
        for path in self.dir.glob("*.ship2ship.json"):
            slot = re.match(r".*_P\d+_(.+)\.ship2ship\.json", path.name).group(1)
            self.seeds[slot] = json.loads(path.read_text())
        self.ids = ids
        any_seed = next(iter(self.seeds.values()))
        self.seed_name = any_seed["archipelago"]["seedName"]

    def location_name(self, game, check):
        return check + TAGS[game]

    def other_checks(self, slot, to_player=None):
        """The checks of `slot` that hold another player's item: (AP location name, the item's AP name)."""
        out = []
        for other in self.seeds[slot]["archipelago"]["otherPlayersItems"]:
            if to_player is None or other["player"] == to_player:
                out.append((self.location_name(other["game"], other["check"]), other["item"]))
        return [o for o in out if o[0] in self.ids["locations"]]

    def own_checks(self, slot):
        seed = self.seeds[slot]
        others = {self.location_name(o["game"], o["check"]) for o in seed["archipelago"]["otherPlayersItems"]}
        names = [self.location_name(g, c) for g in ("oot", "mm") for c in seed[g]["placements"]]
        return [n for n in names if n in self.ids["locations"] and n not in others]


def new_harness(exe, room, slot, port, work, tag, **kw):
    state = pathlib.Path(work) / f"{slot}-{tag}.state.json"
    return Harness(exe, slot, room.seed_name, f"ws://127.0.0.1:{port}", state, pathlib.Path(work) / "cache", **kw), state


def stripped(names):
    return sorted(re.sub(r" \((OoT|MM)\)$", "", n) for n in names)


def test_two_slots(args, ids, check, work):
    room = Room(args.two, work / "two", ids)
    server = Server(args.python, args.ap_core, room.multidata, args.port, work / "two-server.log")
    started = []
    try:
        server.start()
        one, _ = new_harness(args.harness, room, "One", args.port, work, "a")
        two, two_state = new_harness(args.harness, room, "Two", args.port, work, "a")
        started += [one, two]
        two_view = Observer(args.port, "Two", "Ship2Ship", ids)
        one_view = Observer(args.port, "One", "Ship2Ship", ids)

        check("both clients connect", wait_until(lambda: one.phase() == PHASE_CONNECTED and two.phase() == PHASE_CONNECTED))
        check("a fresh file with nothing found grants nothing", one.granted() == [] and two.granted() == [])

        to_two = room.other_checks("One", "Two")
        one_own = room.own_checks("One")
        opened = to_two[:5]
        one.send("open " + "|".join([o[0] for o in opened] + one_own[:3]))
        want = stripped(o[1] for o in opened)
        got = wait_until(lambda: len(two.granted()) >= len(want) and two.granted())
        check("slot Two receives the 5 items One's checks held, once each", got and stripped(got) == want,
              f"want {want} got {got}")
        check("the server delivered the same list to Two's observer", stripped(two_view.names()) == want,
              f"{two_view.names()}")
        check("the received count is saved with the grants", two.dump()["game"]["receivedIndex"] == 5)
        check("One's own checks sent: nothing comes back to One", one.granted() == [])
        sent_known = wait_until(lambda: one.dump()["status"]["checksKnown"] >= 8)
        check("One knows 8 checks are on the server (5 sent to Two, 3 own)", sent_known)
        check("Two got toasts naming the items and One", any("from One" in t for t in two.dump()["game"]["toasts"]),
              str(two.dump()["game"]["toasts"][:2]))

        # a reconnect of Two grants nothing twice; new checks of One still arrive
        two.send("disconnect")
        check("Two disconnects", wait_until(lambda: two.phase() == 0))
        one.send("open " + "|".join(o[0] for o in to_two[5:7]))
        time.sleep(1)
        check("while Two is away nothing is granted", len(two.granted()) == 5)
        two.send("connect")
        check("Two reconnects", wait_until(lambda: two.phase() == PHASE_CONNECTED))
        want7 = stripped(o[1] for o in to_two[:7])
        got = wait_until(lambda: len(two.granted()) >= 7 and two.granted())
        time.sleep(1.5)
        got = two.granted()
        check("after the reconnect Two holds exactly the 7 items, none twice", stripped(got) == want7, f"{got}")
        check("the count is 7", two.dump()["game"]["receivedIndex"] == 7)

        # not ready (the games cannot take the work): items wait, then arrive
        two.send("ready 0")
        one.send("open " + "|".join(o[0] for o in to_two[7:9]))
        waiting = wait_until(lambda: two.dump()["status"]["itemsWaiting"] == 2)
        check("with the games not ready two items wait and none is given", waiting and len(two.granted()) == 7)
        two.send("ready 1")
        wait_until(lambda: len(two.granted()) == 9)
        check("when the games are ready they are given", len(two.granted()) == 9 and
              stripped(two.granted()) == stripped(o[1] for o in to_two[:9]))

        # a grant the game cannot make is not counted and is tried again
        two.send("failgrants 1")
        one.send("open " + to_two[9][0])
        time.sleep(1.5)
        check("a failed grant leaves the count where it was", two.dump()["game"]["receivedIndex"] == 9)
        two.send("failgrants 0")
        wait_until(lambda: len(two.granted()) == 10)
        check("and is given once the game can", len(two.granted()) == 10 and two.dump()["game"]["receivedIndex"] == 10)

        # a restart of the game: same save, new process
        two.quit()
        started.remove(two)
        two, _ = new_harness(args.harness, room, "Two", args.port, work, "a")  # same state file name: same save
        started.append(two)
        check("Two reconnects after a restart", wait_until(lambda: two.phase() == PHASE_CONNECTED))
        time.sleep(1.5)
        check("a restart grants nothing again", len(two.granted()) == 10 and two.dump()["game"]["receivedIndex"] == 10)
        one.send("open " + to_two[10][0])
        wait_until(lambda: len(two.granted()) == 11)
        check("and the next new item arrives once", len(two.granted()) == 11)

        # the server restarts (its savefile keeps the room)
        server.stop()
        time.sleep(1)
        server.start()
        check("both clients reconnect after the server restarts",
              wait_until(lambda: one.phase() == PHASE_CONNECTED and two.phase() == PHASE_CONNECTED, 60))
        one.send("open " + to_two[11][0])
        wait_until(lambda: len(two.granted()) == 12)
        time.sleep(1.5)
        check("after a server restart nothing is granted twice", len(two.granted()) == 12 and
              stripped(two.granted()) == stripped(o[1] for o in to_two[:12]), str(stripped(two.granted())))
        check("the observer's list is the one Two holds", stripped(two_view.names()) == stripped(two.granted()))

        # the other direction
        to_one = room.other_checks("Two", "One")
        two.send("open " + "|".join(o[0] for o in to_one[:3]))
        wait_until(lambda: len(one.granted()) == 3)
        check("slot One receives what Two's checks held", stripped(one.granted()) == stripped(o[1] for o in to_one[:3]))
        check("One's observer agrees", stripped(one_view.names()) == stripped(one.granted()))

        # the goal
        check("no goal yet", "completed their goal" not in server.text())
        one.send("goal 1")
        check("the goal reaches the server", wait_until(lambda: "completed their goal" in server.text(), 15))

        one_view.close()
        two_view.close()
    finally:
        for h in started:
            h.quit()
        server.stop()

    # a harness for the wrong room, the wrong slot and the wrong password never syncs
    server = Server(args.python, args.ap_core, room.multidata, args.port, work / "two-server-b.log")
    try:
        server.start()
        wrong, _ = new_harness(args.harness, room, "One", args.port, work, "wrongroom")
        wrong.quit()
        bad = Harness(args.harness, "One", "not-this-room", f"ws://127.0.0.1:{args.port}",
                      work / "wrongroom.state.json", work / "cache")
        check("a file made for another room is refused", wait_until(lambda: bad.phase() == PHASE_WRONG_ROOM),
              str(bad.dump()["status"]))
        bad.send("open " + room.own_checks("One")[0])
        time.sleep(1)
        check("and sends and grants nothing", "sent " not in "\n".join(bad.log_lines))
        bad.quit()
        nobody = Harness(args.harness, "Nobody", room.seed_name, f"ws://127.0.0.1:{args.port}",
                         work / "nobody.state.json", work / "cache")
        check("a slot that is not in the room is refused", wait_until(lambda: nobody.phase() == PHASE_REFUSED),
              str(nobody.dump()["status"]))
        nobody.quit()
    finally:
        server.stop()


def test_start_inventory(args, ids, check, work):
    room = Room(args.start_inventory, work / "start", ids)
    server = Server(args.python, args.ap_core, room.multidata, args.port, work / "start-server.log")
    started = []
    try:
        server.start()
        slot = next(iter(room.seeds))
        client, state = new_harness(args.harness, room, slot, args.port, work, "a")
        started.append(client)
        view = Observer(args.port, slot, "Ship2Ship", ids)
        check("the start inventory is granted on the first connect",
              wait_until(lambda: len(client.granted()) >= 2 and client.granted()), "")
        got = client.granted()
        check("it is exactly the two items the file asked for", stripped(got) == stripped(view.names()) and
              stripped(got) == ["Lens of Truth", "Progressive Hookshot"], str(got))
        check("no toast for start inventory items", client.dump()["game"]["toasts"] == [])
        check("the count is saved", client.dump()["game"]["receivedIndex"] == 2)
        client.quit()
        started.clear()
        client, _ = new_harness(args.harness, room, slot, args.port, work, "a")
        started.append(client)
        check("restarted, it reconnects", wait_until(lambda: client.phase() == PHASE_CONNECTED))
        time.sleep(1.5)
        check("the start inventory is granted once only", len(client.granted()) == 2)
        client.send("file 1")  # a new file of the same room: its own count starts at 0 and the list is asked for again
        check("a new file of the room gets the start inventory once", wait_until(lambda: len(client.granted()) == 2))
        view.close()
    finally:
        for h in started:
            h.quit()
        server.stop()


def test_mixed(args, ids, check, work):
    room = Room(args.mixed, work / "mixed", ids)
    server = Server(args.python, args.ap_core, room.multidata, args.port, work / "mixed-server.log")
    started = []
    try:
        server.start()
        slot = next(iter(room.seeds))
        client, _ = new_harness(args.harness, room, slot, args.port, work, "a")
        started.append(client)
        check("a Ship2Ship slot connects in a room with another game", wait_until(lambda: client.phase() == PHASE_CONNECTED))
        others = room.other_checks(slot)
        if others:
            client.send("open " + "|".join(o[0] for o in others[:4]))
            check("its checks that hold the other game's items reach the server",
                  wait_until(lambda: client.dump()["status"]["checksKnown"] >= 4))
            check("the server logged them", wait_until(lambda: f"{slot}" in server.text() and " sent " in server.text()))
        check("the room's data package for both games is cached",
              len(list((work / "cache").glob("*.json"))) >= 2)
    finally:
        for h in started:
            h.quit()
        server.stop()


def test_wss(args, ids, check, work):
    """wss:// against MultiServer's own TLS support, with a throwaway certificate.

    wswrap does not verify the certificate of a loopback address (localhost, 127.0.0.1, ::1), so this proves the TLS
    handshake and the protocol over it, not the certificate check."""
    room = Room(args.start_inventory, work / "wss", ids)
    cert, key = pathlib.Path(args.tls_cert), pathlib.Path(args.tls_key)
    server = Server(args.python, args.ap_core, room.multidata, args.port, work / "wss-server.log",
                    ("--cert", str(cert), "--cert_key", str(key)))
    slot = next(iter(room.seeds))
    started = []
    try:
        server.start()
        trusting = Harness(args.harness, slot, room.seed_name, f"wss://127.0.0.1:{args.port}", work / "wss-a.json",
                           work / "cache", extra=("--cert-store", str(cert)))
        started.append(trusting)
        check("wss:// connects when the certificate is trusted", wait_until(lambda: trusting.phase() == PHASE_CONNECTED),
              str(trusting.dump()["status"]))
        check("and the start inventory arrives over it", wait_until(lambda: len(trusting.granted()) == 2))
        plain = Harness(args.harness, slot, room.seed_name, f"ws://127.0.0.1:{args.port}", work / "wss-c.json",
                        work / "cache")
        started.append(plain)
        time.sleep(4)
        check("plain ws:// to a TLS server does not connect", plain.phase() != PHASE_CONNECTED)
    finally:
        for h in started:
            h.quit()
        server.stop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ap-core", required=True)
    parser.add_argument("--harness", required=True)
    parser.add_argument("--ids", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--two", help="the zip of two Ship2Ship slots, slots One and Two")
    parser.add_argument("--start-inventory", help="the zip of one slot whose start inventory is Lens of Truth and the "
                                                  "Progressive Hookshot")
    parser.add_argument("--mixed")
    parser.add_argument("--tls-cert", help="a PEM certificate (and --tls-key its key) for the wss:// suite")
    parser.add_argument("--tls-key")
    parser.add_argument("--port", type=int, default=38299)
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()
    ids = json.loads(pathlib.Path(args.ids).read_text())
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    check = Check()
    suites = []
    if args.two:
        suites.append(("two Ship2Ship slots", test_two_slots))
    if args.start_inventory:
        suites.append(("start inventory", test_start_inventory))
    if args.mixed:
        suites.append(("Ship2Ship with another game", test_mixed))
    if args.tls_cert and args.tls_key and args.start_inventory:
        suites.append(("wss://", test_wss))
    for name, suite in suites:
        print(f"== {name}", flush=True)
        try:
            suite(args, ids, check, work)
        except Exception as error:  # a crashed suite is a failure, never a pass
            check(f"{name}: ran to the end", False, repr(error))
    failed = check.failed()
    print(f"{len(check.results) - len(failed)} of {len(check.results)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
