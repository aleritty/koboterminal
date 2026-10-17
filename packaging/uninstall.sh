#!/usr/bin/env sh
# Run on the Kobo device (as root) to remove KoboTerminal completely.
set -eu

KTERM_DIR=/mnt/onboard/.adds/koboterminal
NM_CONFIG=/mnt/onboard/.adds/nm/menu
MARKER_BEGIN='# BEGIN KOBOTERMINAL'
MARKER_END='# END KOBOTERMINAL'

echo "=== KoboTerminal Uninstall ==="

# Stop the running daemon (and any still-alive per-connection children —
# they share the same process image, just fork()'d, not exec()'d).
PIDS=$(ps | grep '[k]term-server' | awk '{print $1}') || true
if [ -n "${PIDS:-}" ]; then
	kill $PIDS 2>/dev/null || true
	echo "  stopped kterm-server (pid(s): $PIDS)"
fi

if [ -f "$NM_CONFIG" ] && grep -q "$MARKER_BEGIN" "$NM_CONFIG"; then
	sed -i "/$MARKER_BEGIN/,/$MARKER_END/d" "$NM_CONFIG"
	echo "  removed menu entries from $NM_CONFIG"
fi

if [ -d "$KTERM_DIR" ]; then
	rm -rf "$KTERM_DIR" 2>/dev/null && echo "  removed $KTERM_DIR" || true
fi

echo ""
echo "Uninstall complete. Restart Nickel (Settings -> Quit) or reboot."
