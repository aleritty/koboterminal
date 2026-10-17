#!/usr/bin/env sh
# Prints the release notes for a tag: its section of CHANGELOG.md, then how to install.
# Usage: scripts/release-notes.sh v1.1.0
set -eu
tag=${1:?usage: $0 <tag>}
cd "$(dirname "$0")/.."
awk -v t="## $tag" '$0 == t { p = 1; next } /^## / { p = 0 } p' CHANGELOG.md
cat <<'NOTES'

### How to install

You need [NickelMenu](https://github.com/pgaskin/NickelMenu) on your Kobo first.

1. Download **KoboRoot.tgz** from the files below.
2. Connect your Kobo to your computer with a USB cable and copy `KoboRoot.tgz` into the hidden
   `.kobo` folder on the `KOBOeReader` drive. (If you can't see `.kobo`, turn on "show hidden
   files" on your computer.)
3. Eject the Kobo and disconnect the cable. It installs the update and restarts by itself.

**Terminal** now appears at the bottom of NickelMenu. Updating works the same way.

To uninstall, copy **KoboRoot-uninstall.tgz** into `.kobo` the same way, renamed to `KoboRoot.tgz`.
NOTES
