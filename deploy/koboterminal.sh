#!/usr/bin/env sh
set -eu

BASE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LOG_FILE="$BASE_DIR/koboterminal.log"
FONT_PATH="$BASE_DIR/PxPlus_IBM_VGA8.ttf"
APP_PID=
NICKEL_PID=

return_home() {
	# Restore Kobo interface (nickel)
	if [ -n "$NICKEL_PID" ]; then
		kill -CONT "$NICKEL_PID" 2>/dev/null || true
	fi
}

terminate_child() {
	if [ -n "$APP_PID" ]; then
		kill -TERM "$APP_PID" 2>/dev/null || true
		wait "$APP_PID" 2>/dev/null || true
	fi
}

on_signal() {
	terminate_child
	return_home
	exit 1
}

trap on_signal INT TERM HUP QUIT

export FBINK_NO_SW_ROTA=0
export KTERM_FONT_PATH="$FONT_PATH"

# Stop Kobo interface (nickel) before starting app
if command -v pgrep >/dev/null; then
	NICKEL_PID=$(pgrep -x nickel 2>/dev/null || true)
	if [ -n "$NICKEL_PID" ]; then
		kill -STOP "$NICKEL_PID" 2>/dev/null || true
		sleep 1
		# Ensure it's fully stopped
		kill -STOP "$NICKEL_PID" 2>/dev/null || true
	fi
fi

# Blank the screen immediately using FBInk
fbink -c " " >/dev/null 2>&1 || true

: >"$LOG_FILE"

"$BASE_DIR/koboterminal" >>"$LOG_FILE" 2>&1 &
APP_PID=$!

wait "$APP_PID"
APP_STATUS=$?

return_home

exit "$APP_STATUS"
