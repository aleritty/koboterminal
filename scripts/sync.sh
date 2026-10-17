#!/usr/bin/env sh
# Pushes KoboRoot.tgz to the Kobo over SSH and reboots so the firmware installs it.
# Usage: scripts/sync.sh <kobo-ip>
set -eu
cd "$(dirname "$0")/.."
[ -f KoboRoot.tgz ] || { echo "Run scripts/build.sh first" >&2; exit 1; }
ssh "root@${1:?usage: $0 <kobo-ip>}" 'cat > /mnt/onboard/.kobo/KoboRoot.tgz && sync && reboot' < KoboRoot.tgz
