#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <path-to-koboterminal-binary>"
  exit 1
fi

BIN_PATH=$1
DIST_ROOT=dist/koboterminal
APP_DIR="$DIST_ROOT/.adds/koboterminal"
NM_DIR="$DIST_ROOT/.adds/nm"

rm -rf "$DIST_ROOT"
mkdir -p "$APP_DIR" "$NM_DIR"

cp "$BIN_PATH" "$APP_DIR/koboterminal"
cp packaging/nickelmenu/koboterminal.ini "$NM_DIR/koboterminal"
cp packaging/koboterminal.sh "$APP_DIR/koboterminal.sh"
chmod +x "$APP_DIR/koboterminal" "$APP_DIR/koboterminal.sh"

echo "Package tree created at $DIST_ROOT"
echo "Copy contents of $DIST_ROOT to Kobo storage root"
