#!/usr/bin/env bash
# Stage the Linux runtime tree and package it as a self-contained AppImage.
#
#   scripts/linux/make-appimage.sh [build-dir] [packages-dir]
#
#   build-dir     CMake build tree with libultraship/soh/2ship/comboui/Ship2Ship and the
#                 GenerateSohOtr / Generate2ShipOtr targets already built (default: build-cmake)
#   packages-dir  output directory (default: _packages); produces
#                   <packages-dir>/stage                                — runnable tree (the
#                       same layout the Ship2Ship-linux CI artifact ships)
#                   <packages-dir>/Ship2Ship-<version>-linux-x86_64.AppImage
#
# The plain staged tree links against the build host's system libraries (a user on another
# distro hits e.g. "libtinyxml2.so.10: cannot open shared object file"); the AppImage bundles
# them via linuxdeploy, and combo/linux/AppRun recreates the launcher's portable cwd layout
# in a writable data dir at run time. See docs/BUILDING_LINUX.md.
set -euo pipefail

BUILD="${1:-build-cmake}"
OUT="${2:-_packages}"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$(cd "$BUILD" && pwd)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
DEPLOY="$BUILD/combo"

VERSION="$(sed -n 's/^set(SHIP2SHIP_VERSION "\([^"]*\)".*/\1/p' "$SRC/CMakeLists.txt")"
if [ -z "$VERSION" ]; then
    echo "WARNING: could not read SHIP2SHIP_VERSION from CMakeLists.txt" >&2
    VERSION="0.0.0"
fi

# --- Stage the runnable tree (binaries + port assets; game archives oot.o2r / mm.o2r are
# ROM-extracted by the player and never shipped). cp -L dereferences the module symlinks the
# POST_BUILD staging creates. Keep this list in sync with combo/linux/AppRun.
STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -L "$DEPLOY/Ship2Ship" "$DEPLOY/comborando" "$DEPLOY/comboui.so" "$DEPLOY/libsoh.so" \
      "$DEPLOY/lib2ship.so" "$DEPLOY/libultraship.so" "$STAGE/"
cp -r "$DEPLOY/assets" "$STAGE/"
for d in mods presets randomizer; do
    if [ -d "$DEPLOY/$d" ]; then cp -r "$DEPLOY/$d" "$STAGE/"; fi
done
cp "$BUILD/soh/soh.o2r" "$BUILD/mm/2ship.o2r" "$STAGE/"

# --- AppDir: the staged tree under usr/bin. comborando (the headless generator and the Archipelago oracle) rides
# along; combo/linux/AppRun runs it for `Ship2Ship.AppImage --comborando <args>`.
APPDIR="$OUT/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp -r "$STAGE"/. "$APPDIR/usr/bin/"

# --- linuxdeploy: bundles every shared-library dependency (SDL2, tinyxml2, spdlog, ...)
# except the AppImage exclude list (glibc, libGL, X11/Wayland client libs — host-provided).
# The pinned release matches upstream 2ship's CMake/Packaging.cmake.
LINUXDEPLOY="${LINUXDEPLOY:-$OUT/linuxdeploy-x86_64.AppImage}"
if [ ! -x "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy..."
    curl -fsSL -o "$LINUXDEPLOY" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20240109-1/linuxdeploy-x86_64.AppImage
    chmod +x "$LINUXDEPLOY"
fi

# --deploy-deps-only: the launcher dlopens these rather than linking them, so linuxdeploy
# must be told to walk their dependencies; the files themselves stay in usr/bin, where
# AppRun's symlink farm (and its LD_LIBRARY_PATH) expects them.
# APPIMAGE_EXTRACT_AND_RUN: works without FUSE (containers, CI runners, WSL).
(
    cd "$OUT"
    export APPIMAGE_EXTRACT_AND_RUN=1
    export OUTPUT="Ship2Ship-$VERSION-linux-x86_64.AppImage"
    export VERSION
    "$LINUXDEPLOY" --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/Ship2Ship" \
        --deploy-deps-only "$APPDIR/usr/bin/libsoh.so" \
        --deploy-deps-only "$APPDIR/usr/bin/lib2ship.so" \
        --deploy-deps-only "$APPDIR/usr/bin/libultraship.so" \
        --deploy-deps-only "$APPDIR/usr/bin/comboui.so" \
        --desktop-file "$SRC/combo/linux/Ship2Ship.desktop" \
        --icon-file "$SRC/combo/linux/Ship2Ship.png" \
        --custom-apprun "$SRC/combo/linux/AppRun" \
        --output appimage
)

echo "AppImage: $OUT/Ship2Ship-$VERSION-linux-x86_64.AppImage"
