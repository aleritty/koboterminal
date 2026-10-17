#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${1:-build}
IMAGE=${IMAGE:-ghcr.io/pgaskin/nickeltc:1}
PULL_IMAGE=${PULL_IMAGE:-0}
NICKELTC_HOME="$ROOT_DIR/.nickeltc-home"
DIST_DIR="$ROOT_DIR/dist"
OUTPUT_KOBOROOT_TGZ="$DIST_DIR/KoboRoot.tgz"
OUTPUT_KOBOROOT_DIR="$DIST_DIR/KoboRoot"

cd "$ROOT_DIR"

# Clean dist completely before building
rm -rf "$DIST_DIR"

# Clean build directory
rm -rf "$BUILD_DIR"

echo "=== Building kterm-server ==="
echo "Build directory: $BUILD_DIR"
echo "Docker image: $IMAGE"

ensure_image_ready() {
	if [ "$PULL_IMAGE" = "1" ]; then
		echo "=== Pulling NickelTC image: $IMAGE ==="
		docker pull "$IMAGE"
		return
	fi

	if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
		echo "=== Pulling NickelTC image: $IMAGE ==="
		docker pull "$IMAGE"
	fi
}

ensure_image_ready
mkdir -p "$NICKELTC_HOME"

# Build inside the Kobo toolchain container
echo "=== Running Meson setup/compile in Docker ==="
docker run --rm \
	--user "$(id -u):$(id -g)" \
	-e HOME="$NICKELTC_HOME" \
	-v "$PWD:$PWD" \
	-v "$NICKELTC_HOME:$NICKELTC_HOME" \
	-w "$PWD" \
	"$IMAGE" \
	bash -lc '
		set -eu
		export PATH="$HOME/.local/bin:/tc/arm-nickel-linux-gnueabihf/bin:$PATH"

		# Pinned to the last meson release supporting the Python 3.7 in this
		# toolchain image (meson 1.12+ refuses to run on it at all).
		MESON_VERSION=1.11.2
		if ! command -v meson >/dev/null 2>&1 || [ "$(meson --version)" != "$MESON_VERSION" ]; then
			echo "Installing meson $MESON_VERSION in container user environment..."
			python3 -m pip install --user "meson==$MESON_VERSION"
		fi

		meson setup "$1" --cross-file "toolchains/kobo-cross.ini" --buildtype=release
		meson compile -C "$1"
	' \
	_build "$BUILD_DIR"

# Package KoboRoot artifacts for direct Kobo update install (.kobo/KoboRoot.tgz)
echo ""
echo "=== Packaging KoboRoot artifacts ==="
rm -rf "$OUTPUT_KOBOROOT_DIR"
mkdir -p "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal"
KTERM_OUT="$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal"

cp "$BUILD_DIR/kterm-server" "$KTERM_OUT/kterm-server"
chmod 755 "$KTERM_OUT/kterm-server"

# Vendored xterm.js frontend + our page, config, docs, install/uninstall.
for f in index.html nickelmenu.conf README.md; do
	cp "$ROOT_DIR/packaging/$f" "$KTERM_OUT/$f"
	chmod 644 "$KTERM_OUT/$f"
done
for f in xterm.js xterm.css; do
	cp "$ROOT_DIR/packaging/vendor/$f" "$KTERM_OUT/$f"
	chmod 644 "$KTERM_OUT/$f"
done
for f in install.sh uninstall.sh; do
	cp "$ROOT_DIR/packaging/$f" "$KTERM_OUT/$f"
	chmod 755 "$KTERM_OUT/$f"
done

# Clean up build directory
rm -rf "$BUILD_DIR"

cd "$ROOT_DIR"

rm -f "$OUTPUT_KOBOROOT_TGZ"
tar czf "$OUTPUT_KOBOROOT_TGZ" -C "$OUTPUT_KOBOROOT_DIR" .

echo "Created: $OUTPUT_KOBOROOT_TGZ"
echo "Size: $(du -sh "$OUTPUT_KOBOROOT_TGZ" | cut -f1)"

echo ""
echo "=== Done ==="
echo "KoboRoot package:   $OUTPUT_KOBOROOT_TGZ"
echo "KoboRoot folder:    $OUTPUT_KOBOROOT_DIR"
echo ""
echo "=== Build output ==="
echo "KoboRoot tree for sync.sh:"
echo "  $OUTPUT_KOBOROOT_DIR"
echo "KoboRoot package:"
echo "  $OUTPUT_KOBOROOT_TGZ"
echo ""
echo "To install via KoboRoot.tgz:"
echo "  1. Copy $(basename "$OUTPUT_KOBOROOT_TGZ") to KOBOeReader/.kobo/"
echo "  2. Safely eject the Kobo, wait for reboot/install to complete"
echo "  3. ssh in and run: sh /mnt/onboard/.adds/koboterminal/install.sh"
echo ""
echo "Or use scripts/sync.sh to push + activate over SSH in one step."
