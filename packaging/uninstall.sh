#!/usr/bin/env sh
# Uninstall KoboTerminal - Complete Cleanup
# Usage: ./uninstall.sh

echo "=== KoboTerminal Uninstall ==="
echo ""
echo "This script will remove:"
echo "  1. /mnt/onboard/.adds/koboterminal/libkoboterminal.so (shared library)"
echo "  2. /mnt/onboard/.adds/koboterminal/ directory (if empty)"
echo ""

# Remove shared library
if [ -f /mnt/onboard/.adds/koboterminal/libkoboterminal.so ]; then
	echo "Removing /mnt/onboard/.adds/koboterminal/libkoboterminal.so..."
	rm -f /mnt/onboard/.adds/koboterminal/libkoboterminal.so
	echo "  ✓ Removed shared library"
else
	echo "  ℹ No shared library found at /mnt/onboard/.adds/koboterminal/libkoboterminal.so"
fi

# Remove nickelhook directory if empty
if [ -d /mnt/onboard/.adds/koboterminal ]; then
	rmdir /mnt/onboard/.adds/koboterminal 2>/dev/null && echo "  ✓ Removed /mnt/onboard/.adds/koboterminal/" || true
fi

echo ""
echo "=== Uninstall complete ==="
echo ""
echo "IMPORTANT: Restart Nickel (Settings → Quit) or reboot the device"
