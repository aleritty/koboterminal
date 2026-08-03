#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KOBOROOT_DIR="$ROOT_DIR/dist/KoboRoot"
SO_FILE="$KOBOROOT_DIR/usr/local/Kobo/imageformats/libkoboterminal.so"
CFG_DIR="$KOBOROOT_DIR/mnt/onboard/.adds/koboterminal"
REMOTE_USER=${REMOTE_USER:-root}
REMOTE_ROOT=${REMOTE_ROOT:-/mnt/onboard}
INSTALL_DIR=${1:-.adds/koboterminal}
IMAGEFORMATS_DIR=${IMAGEFORMATS_DIR:-/usr/local/Kobo/imageformats}
ASKPASS_HELPER=

cleanup() {
	if [ -n "$ASKPASS_HELPER" ] && [ -f "$ASKPASS_HELPER" ]; then
		rm -f "$ASKPASS_HELPER"
	fi
}

trap cleanup EXIT INT TERM

ask_ip() {
	printf 'Kobo IP: ' >&2
	IFS= read -r KOBO_IP
	printf '%s\n' "$KOBO_IP"
}

require_command() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required command: $1" >&2
		exit 1
	fi
}

make_askpass_helper() {
	ASKPASS_HELPER=$(mktemp "${TMPDIR:-/tmp}/sync-nickelhook-askpass.XXXXXX")
	cat >"$ASKPASS_HELPER" <<'EOF'
#!/usr/bin/env sh
printf '\n'
EOF
	chmod +x "$ASKPASS_HELPER"
}

run_ssh() {
	SSH_ASKPASS="$ASKPASS_HELPER" \
	DISPLAY=dummy \
	SSH_ASKPASS_REQUIRE=force \
	setsid -w ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=accept-new "$@"
}

cd "$ROOT_DIR"

require_command ssh
require_command tar
require_command mktemp

make_askpass_helper

if [ ! -f "$SO_FILE" ]; then
	echo "Missing dist/KoboRoot/usr/local/Kobo/imageformats/libkoboterminal.so. Run scripts/build.sh first." >&2
	exit 1
fi

KOBO_IP=$(ask_ip)
if [ -z "$KOBO_IP" ]; then
	echo "No Kobo IP provided" >&2
	exit 1
fi

# Ping test before connecting
echo "Pinging $KOBO_IP ..."
if ! ping -c 3 -W 5 "$KOBO_IP" >/dev/null 2>&1; then
	echo "Error: Ping to $KOBO_IP failed. Is the Kobo device online?" >&2
	exit 1
fi

REMOTE_DIR="$REMOTE_ROOT/$INSTALL_DIR"

echo "Syncing to $REMOTE_USER@$KOBO_IP"

run_ssh "$REMOTE_USER@$KOBO_IP" "mkdir -p '$REMOTE_DIR' '$IMAGEFORMATS_DIR'"

# Install shared library to imageformats for Qt plugin autoload.
tar -cf - -C "$(dirname "$SO_FILE")" "$(basename "$SO_FILE")" | \
	run_ssh "$REMOTE_USER@$KOBO_IP" "tar -C '$IMAGEFORMATS_DIR' -xf -"

# Sync runtime configs to .adds if present.
if [ -d "$CFG_DIR" ]; then
	tar -cf - -C "$CFG_DIR" . | \
		run_ssh "$REMOTE_USER@$KOBO_IP" "tar -C '$REMOTE_DIR' -xf -"
fi

echo ""
echo "=== Sync complete ==="
echo "Library synced to: $REMOTE_USER@$KOBO_IP:$IMAGEFORMATS_DIR/libkoboterminal.so"
echo "Config files synced to: $REMOTE_USER@$KOBO_IP:$REMOTE_DIR"
echo ""
echo "To activate on Kobo:"
echo "  Restart Nickel (Settings → Quit) or reboot the device"
echo "  Nickel will auto-load the library from imageformats"
