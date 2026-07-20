#!/usr/bin/env sh
set -eu

BASE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LOG_FILE="$BASE_DIR/koboterminal.log"

export FBINK_NO_SW_ROTA=0

exec "$BASE_DIR/koboterminal" >>"$LOG_FILE" 2>&1
