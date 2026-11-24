#!/usr/bin/env bash
# full-coop-sync task 0.2: launch two ComboShip instances in one Anchor room for co-op testing.
#
#   scripts/coop/run-two-instances.sh [--relay] [--host 127.0.0.1] [--port 43383] [--room coop-test]
#                                     [--bin build-cmake/combo] [--data /path/with/o2r/files]
#
# Each instance gets its own SHIP_HOME (separate saves + comboship.json) under .coop-instances/, seeded with
# the Anchor CVars (host/port/room/team/name/color, Enabled=1) so both connect on boot. The ROM archives
# (oot.o2r / oot-mq.o2r / mm.o2r) and port archives (soh.o2r, 2ship.o2r) are symlinked from --data (default:
# the bin dir). --relay also starts a local Anchor relay (`go run .` in $ANCHOR_SRC, or docker).
# Ctrl+C stops everything this script started.
set -euo pipefail

HOST=127.0.0.1
PORT=43383
ROOM=coop-test
BIN="build-cmake/combo"
DATA=""
RELAY=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host) HOST="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --room) ROOM="$2"; shift 2 ;;
        --bin) BIN="$2"; shift 2 ;;
        --data) DATA="$2"; shift 2 ;;
        --relay) RELAY=1; shift ;;
        -h|--help) sed -n 2,13p "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
BIN="$(cd "$BIN" && pwd)"
DATA="${DATA:-$BIN}"
ROOT="$(pwd)/.coop-instances"
PIDS=()
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM

if [[ $RELAY -eq 1 ]]; then
    if [[ -n "${ANCHOR_SRC:-}" ]] && command -v go >/dev/null; then
        (cd "$ANCHOR_SRC" && go run .) > "$ROOT-relay.log" 2>&1 &
    elif command -v docker >/dev/null; then
        docker run --rm -p "$PORT:43383" ghcr.io/garrettjoecox/anchor:latest > "$ROOT-relay.log" 2>&1 &
    else
        echo "--relay needs ANCHOR_SRC + go, or docker" >&2; exit 1
    fi
    PIDS+=($!)
    sleep 2
fi

launch() { # name r g b
    local name="$1" home="$ROOT/$1"
    mkdir -p "$home"
    for f in oot.o2r oot-mq.o2r mm.o2r soh.o2r 2ship.o2r; do
        [[ -e "$DATA/$f" && ! -e "$home/$f" ]] && ln -s "$DATA/$f" "$home/$f"
    done
    if [[ ! -f "$home/comboship.json" ]]; then
        cat > "$home/comboship.json" <<JSON
{ "CVars": { "gRemote": { "Anchor": {
    "Enabled": 1, "Host": "$HOST", "Port": $PORT, "RoomId": "$ROOM", "TeamId": "default",
    "Name": "$name", "Color": { "Value": { "R": $2, "G": $3, "B": $4, "Type": "RGB" } },
    "Coop": { "Debug": 1 } } } } }
JSON
    fi
    (cd "$BIN" && SHIP_HOME="$home" ./ComboShip) > "$ROOT/$name.log" 2>&1 &
    PIDS+=($!)
    echo "started $name (SHIP_HOME=$home, log $ROOT/$name.log)"
}

launch Host 100 255 100
sleep 3 # the first client to join creates the room and becomes its owner (the seed host)
launch Joiner 100 150 255
wait
