#!/usr/bin/env sh
# Builds KtTerm + libtsm for the host against Qt5 and runs the test offscreen.
set -eu
cd "$(dirname "$0")/.."
T=vendor/libtsm
OUT=${TMPDIR:-/tmp}/kt_term_test
mkdir -p "$OUT"
for f in $T/src/tsm/*.c $T/src/shared/shl-htable.c $T/external/wcwidth/wcwidth.c; do
	cc -c -O1 -fPIC -fvisibility=hidden -DSHL_EXPORT= -I$T/src/tsm -I$T/src/shared -I$T/external -I$T/external/wcwidth "$f" -o "$OUT/$(basename "$f").o"
done
c++ -std=gnu++11 -fPIC -O1 -Wall -Isrc -INickelHook -I$T/src/tsm -I$T/external $(pkg-config --cflags Qt5Widgets) \
	tests/term_test.cc src/term.cc "$OUT"/*.c.o -o "$OUT/term_test" $(pkg-config --libs Qt5Widgets) -lutil
QT_QPA_PLATFORM=offscreen "$OUT/term_test"
