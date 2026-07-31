#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${1:-build-nickelhook}
IMAGE=${IMAGE:-koboterminal-kobo-toolchain:latest}
OUTPUT_INSTALL_TGZ=${2:-koboterminal-install.tgz}
OUTPUT_UNINSTALL_TGZ="koboterminal-uninstall.tgz"

cd "$ROOT_DIR"

# Clean dist completely before building
rm -rf "$ROOT_DIR/dist"

# Clean build directory
rm -rf "$BUILD_DIR"

echo "=== Building NickelHook shared library ==="
echo "Build directory: $BUILD_DIR"

# Build inside the Kobo toolchain container
docker run --rm \
	--user "$(id -u):$(id -g)" \
	-e HOME=/tmp \
	-v "$PWD:/work" \
	-w /work \
	"$IMAGE" \
	bash -lc 'export PATH="/opt/x-tools/arm-kobo-linux-gnueabihf/bin:$PATH" && meson setup "$1" --cross-file "toolchains/kobo-cross.ini" -Dwith_nickelhook=true && meson compile -C "$1"' \
	_build "$BUILD_DIR"

# Create dist structure
mkdir -p "$ROOT_DIR/dist/.adds/koboterminal"

# Copy the shared library to uncompressed install location
cp "$BUILD_DIR/libkoboterminal.so" "$ROOT_DIR/dist/.adds/koboterminal/libkoboterminal.so"
chmod 755 "$ROOT_DIR/dist/.adds/koboterminal/libkoboterminal.so"

# Clean up build directory
rm -rf "$BUILD_DIR"

echo ""
echo "=== Packaging install tarball ==="
STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT INT TERM

mkdir -p "$STAGING/.adds/koboterminal"
cp "$ROOT_DIR/dist/.adds/koboterminal/libkoboterminal.so" "$STAGING/.adds/koboterminal/libkoboterminal.so"
chmod 755 "$STAGING/.adds/koboterminal/libkoboterminal.so"
cp packaging/README-install.txt "$STAGING/README.txt"

cd "$STAGING"
tar czf "$ROOT_DIR/$OUTPUT_INSTALL_TGZ" .

echo "Created: $ROOT_DIR/$OUTPUT_INSTALL_TGZ"
echo "Size: $(du -sh "$ROOT_DIR/$OUTPUT_INSTALL_TGZ" | cut -f1)"

cd "$ROOT_DIR"

# Package uninstall tarball
echo ""
echo "=== Packaging uninstall tarball ==="
STAGING2=$(mktemp -d)

mkdir -p "$STAGING2/.adds/koboterminal"
cp "$ROOT_DIR/dist/.adds/koboterminal/libkoboterminal.so" "$STAGING2/.adds/koboterminal/libkoboterminal.so"
chmod 755 "$STAGING2/.adds/koboterminal/libkoboterminal.so"
cp packaging/uninstall.sh "$STAGING2/uninstall.sh"
chmod +x "$STAGING2/uninstall.sh"
cp packaging/README-uninstall.txt "$STAGING2/README.txt"

cd "$STAGING2"
tar czf "$ROOT_DIR/$OUTPUT_UNINSTALL_TGZ" .

echo "Created: $ROOT_DIR/$OUTPUT_UNINSTALL_TGZ"
echo "Size: $(du -sh "$ROOT_DIR/$OUTPUT_UNINSTALL_TGZ" | cut -f1)"

rm -rf "$STAGING2"

echo ""
echo "=== Done ==="
echo "Install package:    $ROOT_DIR/$OUTPUT_INSTALL_TGZ"
echo "Uninstall package:  $ROOT_DIR/$OUTPUT_UNINSTALL_TGZ"
echo ""
echo "=== Build output ==="
echo "Uncompressed install:"
echo "  $ROOT_DIR/dist/.adds/koboterminal/libkoboterminal.so"
echo ""
echo "To install on Kobo:"
echo "  1. scp $OUTPUT_INSTALL_TGZ root@<KOBO_IP>:/tmp/"
echo "  2. ssh root@<KOBO_IP> 'cd /tmp && tar xzf $OUTPUT_INSTALL_TGZ'"
echo "  3. Restart Nickel"
echo ""
echo "To uninstall on Kobo:"
echo "  1. scp $OUTPUT_UNINSTALL_TGZ root@<KOBO_IP>:/tmp/"
echo "  2. ssh root@<KOBO_IP> 'cd /tmp && tar xzf $OUTPUT_UNINSTALL_TGZ'"
echo "  3. Run /tmp/uninstall.sh"
