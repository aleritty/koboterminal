#!/usr/bin/env sh
# Cross-builds with NickelTC and writes KoboRoot.tgz (copy to KOBOeReader/.kobo/).
set -eu
cd "$(dirname "$0")/.."
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
	"${IMAGE:-ghcr.io/pgaskin/nickeltc:1}" \
	sh -c 'make clean && make && arm-nickel-linux-gnueabihf-strip --strip-unneeded src/libkoboterminal.so && make koboroot'
