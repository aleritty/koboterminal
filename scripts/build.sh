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

# Create dist structure
mkdir -p "$DIST_DIR/.adds/koboterminal"
mkdir -p "$DIST_DIR/usr/local/Kobo/imageformats"

# Copy the shared library to Qt plugin auto-load location
cp "$BUILD_DIR/libkoboterminal.so" "$DIST_DIR/usr/local/Kobo/imageformats/libkoboterminal.so"
chmod 755 "$DIST_DIR/usr/local/Kobo/imageformats/libkoboterminal.so"

# Copy NickelMenu config into install location
if [ -f "$ROOT_DIR/deploy/nickelmenu.conf" ]; then
	cp "$ROOT_DIR/deploy/nickelmenu.conf" "$DIST_DIR/.adds/koboterminal/nickelmenu.conf"
	chmod 644 "$DIST_DIR/.adds/koboterminal/nickelmenu.conf"
fi

# Copy logging config into install location
if [ -f "$ROOT_DIR/deploy/koboterminal.conf" ]; then
	cp "$ROOT_DIR/deploy/koboterminal.conf" "$DIST_DIR/.adds/koboterminal/koboterminal.conf"
	chmod 644 "$DIST_DIR/.adds/koboterminal/koboterminal.conf"
fi

# Clean up build directory
rm -rf "$BUILD_DIR"

cd "$ROOT_DIR"

# Package KoboRoot artifacts for direct Kobo update install (.kobo/KoboRoot.tgz)
echo ""
echo "=== Packaging KoboRoot artifacts ==="
rm -rf "$OUTPUT_KOBOROOT_DIR"
mkdir -p "$OUTPUT_KOBOROOT_DIR/usr/local/Kobo/imageformats"
mkdir -p "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal"

cp "$DIST_DIR/usr/local/Kobo/imageformats/libkoboterminal.so" "$OUTPUT_KOBOROOT_DIR/usr/local/Kobo/imageformats/libkoboterminal.so"
chmod 755 "$OUTPUT_KOBOROOT_DIR/usr/local/Kobo/imageformats/libkoboterminal.so"

if [ -f "$DIST_DIR/.adds/koboterminal/nickelmenu.conf" ]; then
	cp "$DIST_DIR/.adds/koboterminal/nickelmenu.conf" "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/nickelmenu.conf"
	chmod 644 "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/nickelmenu.conf"
fi
if [ -f "$DIST_DIR/.adds/koboterminal/koboterminal.conf" ]; then
	cp "$DIST_DIR/.adds/koboterminal/koboterminal.conf" "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/koboterminal.conf"
	chmod 644 "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/koboterminal.conf"
fi
if [ -f "$DIST_DIR/.adds/koboterminal/koboterminal-trigger.sh" ]; then
	cp "$DIST_DIR/.adds/koboterminal/koboterminal-trigger.sh" "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/koboterminal-trigger.sh"
	chmod 755 "$OUTPUT_KOBOROOT_DIR/mnt/onboard/.adds/koboterminal/koboterminal-trigger.sh"
fi

rm -f "$OUTPUT_KOBOROOT_TGZ"
tar czf "$OUTPUT_KOBOROOT_TGZ" -C "$OUTPUT_KOBOROOT_DIR" .

echo "Created: $OUTPUT_KOBOROOT_TGZ"
echo "Size: $(du -sh "$OUTPUT_KOBOROOT_TGZ" | cut -f1)"

# Keep dist clean: only retain KoboRoot/ and KoboRoot.tgz.
rm -rf "$DIST_DIR/.adds" "$DIST_DIR/usr"

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
echo "  2. Safely eject the Kobo"
echo "  3. Wait for reboot/install to complete"
