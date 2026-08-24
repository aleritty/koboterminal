#!/usr/bin/env sh
# Run on the Kobo device (as root) after copying KoboTerminal's files onto it,
# either via KoboRoot.tgz or scripts/sync.sh. Safe to re-run.
set -eu

NM_CONFIG=/mnt/onboard/.adds/nm/config
MENU_LINE='menu_item:main:Terminal:cmd_output:500:quiet:koboterminal:open'
KTERM_DIR=/mnt/onboard/.adds/koboterminal
KTERM_CONF="$KTERM_DIR/koboterminal.conf"

echo "=== KoboTerminal Install ==="

if [ ! -d /mnt/onboard/.adds/nm ]; then
	echo "NickelMenu is not installed (/mnt/onboard/.adds/nm missing)." >&2
	echo "Install NickelMenu first, then re-run this script." >&2
	exit 1
fi

mkdir -p "$KTERM_DIR"
touch "$NM_CONFIG"

if ! grep -qF "$MENU_LINE" "$NM_CONFIG" 2>/dev/null; then
	echo "$MENU_LINE" >>"$NM_CONFIG"
	echo "  added Terminal entry to $NM_CONFIG"
else
	echo "  Terminal entry already present in $NM_CONFIG"
fi

if [ ! -f "$KTERM_CONF" ]; then
	printf 'verbose=0\n' >"$KTERM_CONF"
	echo "  wrote default $KTERM_CONF"
fi

echo ""
echo "Install complete. Restart Nickel (Settings -> Quit) or reboot to activate."
