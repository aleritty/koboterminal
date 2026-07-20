#!/usr/bin/env sh
set -eu

BUILD_DIR=${1:-build-kobo}
CROSS_FILE=${2:-toolchains/kobo-cross.ini}

meson setup "$BUILD_DIR" --cross-file "$CROSS_FILE" -Dkobo_target=true
meson compile -C "$BUILD_DIR"

echo "Built Kobo target in $BUILD_DIR"
