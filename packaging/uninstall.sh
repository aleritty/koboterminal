#!/usr/bin/env sh
# Run on the Kobo device (as root) to remove KoboTerminal completely.
set -eu

SO_FILE=/usr/local/Kobo/imageformats/libkoboterminal.so
KTERM_DIR=/mnt/onboard/.adds/koboterminal
NM_CONFIG=/mnt/onboard/.adds/nm/config
MENU_LINE='menu_item:main:Terminal:cmd_output:500:quiet:koboterminal:open'

echo "=== KoboTerminal Uninstall ==="

if [ -f "$SO_FILE" ]; then
	rm -f "$SO_FILE"
	echo "  removed $SO_FILE"
else
	echo "  no shared library found at $SO_FILE"
fi

if [ -f "$NM_CONFIG" ] && grep -qF "$MENU_LINE" "$NM_CONFIG"; then
	grep -vF "$MENU_LINE" "$NM_CONFIG" >"$NM_CONFIG.tmp"
	mv "$NM_CONFIG.tmp" "$NM_CONFIG"
	echo "  removed Terminal entry from $NM_CONFIG"
fi

for f in koboterminal.conf nickelmenu.conf lkoboterminal.log lkoboterminal.bootstrap.log install.sh uninstall.sh; do
	if [ -f "$KTERM_DIR/$f" ]; then
		rm -f "$KTERM_DIR/$f"
		echo "  removed $KTERM_DIR/$f"
	fi
done

if [ -d "$KTERM_DIR" ]; then
	rmdir "$KTERM_DIR" 2>/dev/null && echo "  removed $KTERM_DIR" || true
fi

echo ""
echo "Uninstall complete. Restart Nickel (Settings -> Quit) or reboot."
