#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${1:-build-nickelhook}
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

echo "=== Building NickelHook shared library ==="
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

		if ! command -v meson >/dev/null 2>&1; then
			echo "Installing Meson in container user environment..."
			python3 -m pip install --user meson
		fi

		if ! command -v arm-nickel-linux-gnueabihf-pkg-config >/dev/null 2>&1 && ! command -v arm-nickel-linux-gnueabihf-pkgconf >/dev/null 2>&1; then
			echo "Error: cross pkg-config tool not found in container PATH" >&2
			exit 1
		fi

		if ! command -v moc >/dev/null 2>&1; then
			echo "Error: moc not found in container image." >&2
			exit 1
		fi

		meson setup "$1" --cross-file "toolchains/kobo-cross.ini" -Dwith_nickelhook=true
		meson compile -C "$1"
	' \
	_build "$BUILD_DIR"

# Package KoboRoot artifacts for direct Kobo update install (.kobo/KoboRoot.tgz)
echo ""
echo "=== Packaging KoboRoot artifacts ==="
rm -rf "$OUTPUT_KOBOROOT_DIR"
mkdir -p "$OUTPUT_KOBOROOT_DIR/usr/local/Kobo/imageformats"
mkdir -p "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal"

# Shared library, loaded by Nickel via Qt plugin auto-loading from imageformats.
cp "$BUILD_DIR/libkoboterminal.so" "$OUTPUT_KOBOROOT_DIR/usr/local/Kobo/imageformats/libkoboterminal.so"
chmod 755 "$OUTPUT_KOBOROOT_DIR/usr/local/Kobo/imageformats/libkoboterminal.so"

# Runtime config + docs, staged under .adds/ for install.sh/uninstall.sh and
# for the user to find on-device.
for f in nickelmenu.conf koboterminal.conf README.md; do
	cp "$ROOT_DIR/packaging/$f" "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/$f"
	chmod 644 "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/$f"
done
for f in install.sh uninstall.sh; do
	cp "$ROOT_DIR/packaging/$f" "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/$f"
	chmod 755 "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/$f"
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
