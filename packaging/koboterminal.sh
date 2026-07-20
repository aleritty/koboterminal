#!/usr/bin/env sh
set -eu

BASE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LOG_FILE="$BASE_DIR/koboterminal.log"
FONT_PATH="$BASE_DIR/PxPlus_IBM_VGA8.ttf"

export FBINK_NO_SW_ROTA=0
export KTERM_FONT_PATH="$FONT_PATH"

exec "$BASE_DIR/koboterminal" >>"$LOG_FILE" 2>&1
