#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=${IMAGE:-ghcr.io/pgaskin/nickeltc:1}

cd "$ROOT_DIR"

echo "=== Bootstrapping KoboTerminal ==="

# scripts/build.sh cross-compiles inside this prebuilt toolchain image.
if command -v docker >/dev/null 2>&1; then
	if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
		echo "Pulling Kobo toolchain image: $IMAGE"
		docker pull "$IMAGE"
	else
		echo "Toolchain image '$IMAGE' already present; skipping pull"
	fi
else
	echo "docker not found; scripts/build.sh will need it" >&2
fi

echo ""
echo "=== Bootstrap complete ==="
echo ""
echo "Next steps:"
echo "  1. Build and package: scripts/build.sh"
echo "  2. Deploy to Kobo: scripts/sync.sh"
echo ""
echo "Note: docker/Dockerfile builds an alternative koxtoolchain-based image;"
echo "scripts/build.sh does not use it by default (see IMAGE above)."