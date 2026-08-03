#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=${IMAGE:-koboterminal-kobo-toolchain:latest}

cd "$ROOT_DIR"

echo "=== Bootstrapping KoboTerminal ==="

# Build Kobo toolchain Docker image (first time only)
if command -v docker >/dev/null 2>&1; then
	if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
		echo "Building Kobo toolchain image..."
		docker build -t "$IMAGE" -f docker/kobo-toolchain/Dockerfile .
	else
		echo "Toolchain image '$IMAGE' already exists; skipping build"
	fi
else
	echo "docker not found; skipped Kobo toolchain image build" >&2
fi

echo ""
echo "=== Bootstrap complete ==="
echo ""
echo "Next steps:"
echo "  1. Build and package: scripts/build.sh"
echo "  2. Deploy to Kobo: scripts/sync.sh"