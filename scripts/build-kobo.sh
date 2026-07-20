#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${1:-build}
CROSS_FILE=${2:-toolchains/kobo-cross.ini}
IMAGE=${IMAGE:-koboterminal-kobo-toolchain:latest}
TOOLCHAIN_ROOT=${TOOLCHAIN_ROOT:-/opt/x-tools/arm-kobo-linux-gnueabihf}
DIST_ROOT=$ROOT_DIR/dist/koboterminal
APP_DIR="$DIST_ROOT/.adds/koboterminal"
HOST_UID=$(id -u)
HOST_GID=$(id -g)

cd "$ROOT_DIR"

docker run --rm \
	--user "$HOST_UID:$HOST_GID" \
	-e HOME=/tmp \
	-v "$PWD:/work" \
	-w /work \
	"$IMAGE" \
	bash -lc 'export PATH="$1/bin:$PATH" && meson setup "$2" --cross-file "$3" -Dkobo_target=true -Dwith_real_fbink=true && meson compile -C "$2"' \
	bash "$TOOLCHAIN_ROOT" "$BUILD_DIR" "$CROSS_FILE"

rm -rf "$DIST_ROOT"
mkdir -p "$APP_DIR"

cp "$BUILD_DIR/koboterminal" "$APP_DIR/koboterminal"
cp packaging/koboterminal.sh "$APP_DIR/koboterminal.sh"
cp third_party/FBInk/fonts/PxPlus_IBM_VGA8.ttf "$APP_DIR/PxPlus_IBM_VGA8.ttf"
chmod +x "$APP_DIR/koboterminal" "$APP_DIR/koboterminal.sh"

rm -rf "$BUILD_DIR"

echo "Built and packaged Kobo target using $IMAGE"
