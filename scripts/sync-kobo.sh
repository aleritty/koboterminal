#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REMOTE_USER=${REMOTE_USER:-root}
REMOTE_ROOT=${REMOTE_ROOT:-/mnt/onboard}
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
	ASKPASS_HELPER=$(mktemp "${TMPDIR:-/tmp}/koboterminal-askpass.XXXXXX")
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
require_command setsid
require_command mktemp

make_askpass_helper

if [ ! -d dist/koboterminal/.adds/koboterminal ]; then
	echo "Missing dist/koboterminal/.adds/koboterminal. Run scripts/build-kobo.sh first." >&2
	exit 1
fi

KOBO_IP=$(ask_ip)
if [ -z "$KOBO_IP" ]; then
	echo "No Kobo IP provided" >&2
	exit 1
fi

REMOTE_DIR="$REMOTE_ROOT/.adds/koboterminal"

run_ssh "$REMOTE_USER@$KOBO_IP" "mkdir -p '$REMOTE_DIR' && rm -rf '$REMOTE_DIR'/*"

tar -C dist/koboterminal/.adds -cf - koboterminal | \
	run_ssh "$REMOTE_USER@$KOBO_IP" "tar -C '$REMOTE_ROOT/.adds' -xf -"

echo "Synced KoboTerminal to $REMOTE_USER@$KOBO_IP:$REMOTE_DIR"