#!/usr/bin/env sh
# Uninstall KoboTerminal - Complete Cleanup
# Usage: ./uninstall.sh

echo "=== KoboTerminal Uninstall ==="
echo ""
echo "This script will remove:"
echo "  1. /usr/local/Kobo/imageformats/libkoboterminal.so (shared library)"
echo "  2. /mnt/onboard/.adds/koboterminal/koboterminal.conf (if present)"
echo "  3. /mnt/onboard/.adds/koboterminal/lkoboterminal.log (if present)"
echo "  4. /mnt/onboard/.adds/koboterminal/ directory (if empty)"
echo ""

# Remove shared library
if [ -f /usr/local/Kobo/imageformats/libkoboterminal.so ]; then
	echo "Removing /usr/local/Kobo/imageformats/libkoboterminal.so..."
	rm -f /usr/local/Kobo/imageformats/libkoboterminal.so
	echo "  ✓ Removed shared library"
else
	echo "  ℹ No shared library found at /usr/local/Kobo/imageformats/libkoboterminal.so"
fi

if [ -f /mnt/onboard/.adds/koboterminal/koboterminal.conf ]; then
	rm -f /mnt/onboard/.adds/koboterminal/koboterminal.conf
	echo "  ✓ Removed logging configuration"
fi

if [ -f /mnt/onboard/.adds/koboterminal/lkoboterminal.log ]; then
	rm -f /mnt/onboard/.adds/koboterminal/lkoboterminal.log
	echo "  ✓ Removed log file"
fi

# Remove nickelhook directory if empty
if [ -d /mnt/onboard/.adds/koboterminal ]; then
	rmdir /mnt/onboard/.adds/koboterminal 2>/dev/null && echo "  ✓ Removed /mnt/onboard/.adds/koboterminal/" || true
fi

echo ""
echo "=== Uninstall complete ==="
echo ""
echo "IMPORTANT: Restart Nickel (Settings → Quit) or reboot the device"
