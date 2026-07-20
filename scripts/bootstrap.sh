#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST_BUILD_DIR=${HOST_BUILD_DIR:-build-host}
KOBO_BUILD_DIR=${KOBO_BUILD_DIR:-build-kobo}
IMAGE=${IMAGE:-koboterminal-kobo-toolchain:latest}

cd "$ROOT_DIR"

if command -v git >/dev/null 2>&1; then
	if [ -f third_party/FBInk/.gitmodules ]; then
		git submodule update --init --recursive third_party/FBInk
	fi
fi

if command -v docker >/dev/null 2>&1; then
	docker build -t "$IMAGE" -f docker/kobo-toolchain/Dockerfile .
else
	echo "docker not found; skipped Kobo toolchain image build" >&2
fi

if command -v meson >/dev/null 2>&1; then
	if [ ! -d "$HOST_BUILD_DIR" ]; then
		meson setup "$HOST_BUILD_DIR" -Dkobo_target=false
	fi
	if [ ! -d "$KOBO_BUILD_DIR" ]; then
		meson setup "$KOBO_BUILD_DIR" --cross-file toolchains/kobo-cross.ini -Dkobo_target=true -Dwith_real_fbink=true
	fi
else
	echo "meson not found; skipped build directory setup" >&2
fi

echo "Bootstrap complete"