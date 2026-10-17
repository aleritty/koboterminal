#!/usr/bin/env sh
# Run on the Kobo device (as root) after copying KoboTerminal's files onto it,
# either via KoboRoot.tgz or scripts/sync.sh. Safe to re-run.
set -eu

NM_CONFIG=/mnt/onboard/.adds/nm/menu
KTERM_DIR=/mnt/onboard/.adds/koboterminal
MARKER_BEGIN='# BEGIN KOBOTERMINAL'
MARKER_END='# END KOBOTERMINAL'

echo "=== KoboTerminal Install ==="

if [ ! -d /mnt/onboard/.adds/nm ]; then
	echo "NickelMenu is not installed (/mnt/onboard/.adds/nm missing)." >&2
	echo "Install NickelMenu first, then re-run this script." >&2
	exit 1
fi

mkdir -p "$KTERM_DIR"
touch "$NM_CONFIG"

# Drop any prior KoboTerminal block (marker-delimited, so this survives menu
# entries changing across versions) before re-adding the current one, so
# upgrades don't duplicate or strand old entries.
if grep -q "$MARKER_BEGIN" "$NM_CONFIG" 2>/dev/null; then
	sed -i "/$MARKER_BEGIN/,/$MARKER_END/d" "$NM_CONFIG"
fi

{
	echo "$MARKER_BEGIN"
	cat "$KTERM_DIR/nickelmenu.conf"
	echo "$MARKER_END"
} >>"$NM_CONFIG"
echo "  Menu entries set in $NM_CONFIG"

echo ""
echo "Install complete. Restart Nickel (Settings -> Quit) or reboot to activate."
echo "Then: NickelMenu -> Terminal."
