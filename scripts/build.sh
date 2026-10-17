#!/usr/bin/env sh
# Cross-builds with NickelTC in Docker and writes KoboRoot.tgz and KoboRoot-uninstall.tgz.
# VERSION=... overrides the version string (default: git describe).
set -eu
cd "$(dirname "$0")/.."
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src ${VERSION:+-e VERSION} \
	"${IMAGE:-ghcr.io/pgaskin/nickeltc:1.0}" \
	sh -c 'git config --global --add safe.directory /src 2>/dev/null; make clean && make all strip koboroot koboroot-uninstall'
