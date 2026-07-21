#!/usr/bin/env sh
set -eu

BASE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LOG_FILE="$BASE_DIR/koboterminal.log"
FONT_PATH="$BASE_DIR/PxPlus_IBM_VGA8.ttf"
APP_PID=

return_home() {
	killall -CONT nickel >/dev/null 2>&1 || true
}

terminate_child() {
	if [ -n "$APP_PID" ]; then
		kill -TERM "$APP_PID" >/dev/null 2>&1 || true
	fi
}

on_signal() {
	terminate_child
}

trap on_signal INT TERM HUP QUIT

export FBINK_NO_SW_ROTA=0
export KTERM_FONT_PATH="$FONT_PATH"

: >"$LOG_FILE"

"$BASE_DIR/koboterminal" >>"$LOG_FILE" 2>&1 &
APP_PID=$!

wait "$APP_PID"
APP_STATUS=$?

return_home

exit "$APP_STATUS"
