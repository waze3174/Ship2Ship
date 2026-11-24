#!/bin/bash
# usage: [WOMBO_SRC=<repo>] [HARNESS=1] scripts/wombo/build.sh <tag>
#   builds in docker (image wombo-build:noble), packages _packages/ComboShip-*.AppImage.
#   HARNESS=1 -> separate build dir build-harness with -DCOMBO_HARNESS=ON (virtual controller COMBO_VPAD; test only,
#   never ship it). Without it: build-cmake, harness OFF (release).
set -e
TAG="$1"
SRC="${WOMBO_SRC:-$HOME/Projects/wombocomboship}"
if [ -n "$HARNESS" ]; then BDIR=build-harness; HOPT=ON; else BDIR=build-cmake; HOPT=OFF; fi
docker run --rm --user 1000:1000 -e HOME=/tmp -e CCACHE_DIR=/ccache -e BDIR=$BDIR -e HOPT=$HOPT \
  -v "$SRC":/workspace/wombocomboship/ComboShip -v "$HOME/.cache/wombo-ccache":/ccache \
  -w /workspace/wombocomboship/ComboShip wombo-build:noble bash -c '
set -e
cmake -B $BDIR -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOMBO_HARNESS=$HOPT -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build $BDIR --target libultraship soh 2ship comboui ComboShip comborando -j 4 -- -k 0
cmake --build $BDIR --target GenerateSohOtr Generate2ShipOtr -j 4
APPIMAGE_EXTRACT_AND_RUN=1 scripts/linux/make-appimage.sh $BDIR _packages
'
ls -t _packages/*.AppImage | head -1
