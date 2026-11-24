#!/bin/bash
# run.sh <name> <seconds> [ENV=VAL ...] — headless run in docker; log -> <name>.log, screenshots -> shots/<name>-N.png
NAME=$1; SECS=$2; shift 2
ENVS=""; for e in "$@"; do ENVS="$ENVS -e $e"; done
mkdir -p ~/wombo-test/shots
cd ~/wombo-test && docker run --rm --user 1000:1000 -e HOME=/tmp -e SHIP_HOME=/t/data -e APPIMAGE_EXTRACT_AND_RUN=1 \
  -e LIBGL_ALWAYS_SOFTWARE=1 -e DISPLAY=:99 $ENVS -v "$PWD":/t -w /t/data wombo-run:noble bash -c "
Xvfb :99 -screen 0 1280x720x24 >/dev/null 2>&1 & sleep 2
/t/ComboShip.AppImage > /t/$NAME.log 2>&1 & PID=\$!
for i in \$(seq 1 $SECS); do sleep 1; if (( i % 8 == 0 )); then xwd -root -display :99 2>/dev/null | convert xwd:- /t/shots/$NAME-\$i.png 2>/dev/null; fi; kill -0 \$PID 2>/dev/null || break; done
kill \$PID 2>/dev/null; sleep 2; pkill -f ComboShip; true"
echo "exit; log lines: $(wc -l < ~/wombo-test/$NAME.log)"
