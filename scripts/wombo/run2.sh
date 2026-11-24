#!/bin/bash
# run2.sh <name> <seconds> "<client A env...>" "<client B env...>"
# Two headless ComboShip clients (wombo-run:noble, Xvfb) in ONE Anchor room on a LOCAL relay: the container
# wombo-anchor-coop (ghcr.io/garrettjoecox/anchor) on the private docker network wombo-coop. Nothing connects
# to a public server. Test dir (default ~/wombo-test-coop, override COOP_TEST_DIR) holds:
#   ComboShip.AppImage, dataA/ and dataB/ = copies of a run.sh data dir whose comboship.json has
#   gRemote.Anchor.{Enabled=1, Host="wombo-anchor-coop", Port=43383, RoomId=<same>, Name=<distinct>}.
# Logs -> <name>-A.log / <name>-B.log, screenshots every 6 s -> shots/<name>-{A,B}-N.png.
# B starts B_DELAY seconds after A (default 4), so A creates the room and owns it.
NAME=$1
SECS=$2
ENVA=$3
ENVB=$4
DIR="${COOP_TEST_DIR:-$HOME/wombo-test-coop}"
cd "$DIR" || exit 1
mkdir -p shots
docker network inspect wombo-coop >/dev/null 2>&1 || docker network create wombo-coop >/dev/null
if [ -z "$(docker ps -q -f name=^wombo-anchor-coop$)" ]; then
    docker rm -f wombo-anchor-coop >/dev/null 2>&1
    docker run -d --rm --name wombo-anchor-coop --network wombo-coop ghcr.io/garrettjoecox/anchor:latest >/dev/null
    sleep 2
fi
rm -f dataA/.instance.lock dataB/.instance.lock
client() { # tag envs
    local TAG=$1 ENVS=""
    for e in $2; do ENVS="$ENVS -e $e"; done
    docker run --rm --name "wombo-coop-$NAME-$TAG" --network wombo-coop --user 1000:1000 -e HOME=/tmp \
        -e SHIP_HOME=/t/data$TAG -e APPIMAGE_EXTRACT_AND_RUN=1 -e LIBGL_ALWAYS_SOFTWARE=1 -e DISPLAY=:99 \
        -e SDL_AUDIODRIVER=dummy $ENVS -v "$DIR":/t -w /t/data$TAG wombo-run:noble bash -c "
Xvfb :99 -screen 0 1280x720x24 >/dev/null 2>&1 & sleep 2
/t/ComboShip.AppImage > /t/$NAME-$TAG.log 2>&1 & PID=\$!
for i in \$(seq 1 $SECS); do sleep 1; if (( i % 6 == 0 )); then xwd -root -display :99 2>/dev/null | convert xwd:- /t/shots/$NAME-$TAG-\$i.png 2>/dev/null; fi; kill -0 \$PID 2>/dev/null || break; done
kill \$PID 2>/dev/null; sleep 2; pkill -f ComboShip; true" &
}
client A "$ENVA"
sleep "${B_DELAY:-4}"
client B "$ENVB"
wait
echo "done: $(wc -l <"$NAME-A.log") / $(wc -l <"$NAME-B.log") log lines"
