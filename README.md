# KoboTerminal

KoboTerminal is a framebuffer terminal emulator for Kobo devices.

Project goals:
- Full-screen terminal experience on Kobo Libra Colour
- VT parsing and terminal state with libtsm
- Rendering through FBInk
- Touch input and in-app on-screen keyboard
- Launch from NickelMenu

## Current state

This repository currently contains a buildable implementation scaffold:
- Module boundaries for PTY, terminal parser, renderer, input, and OSK
- Host-build stubs to validate architecture early
- Cross-build templates for Kobo toolchains
- Packaging scripts for NickelMenu deployment

Recent implementation progress:
- PTY backend now uses a real `forkpty` shell process on host builds.
- The app loop writes bootstrap commands to the shell and reads back real output.
- Terminal state keeps a rolling printable preview for renderer diagnostics.

## Planned external dependencies

- FBInk (GPLv3+)
- libtsm (LGPL-2.1)

The third-party sources are vendored under third_party/. See third_party/README.md.

## Build on host

Requirements:
- Meson
- Ninja
- C compiler with C11 support

Build:

meson setup build-host -Dkobo_target=false
meson compile -C build-host
./build-host/koboterminal

## Configure Kobo cross-build

For a fresh checkout, bootstrap the vendored third-party sources, the Kobo toolchain image, and the build directories with:

scripts/bootstrap.sh

1. Build the Kobo toolchain container:

docker build -t koboterminal-kobo-toolchain -f docker/kobo-toolchain/Dockerfile .

2. Build the Kobo binary inside that container:

scripts/build-kobo.sh

You can override the image with `IMAGE` if you want to use a different local container tag.

`scripts/build-kobo.sh` also packages the Kobo payload into `dist/koboterminal` and removes the build directory when it finishes.

## Packaging for NickelMenu

Run the full Kobo build and packaging flow with:

scripts/build-kobo.sh

That leaves the packaged tree in `dist/koboterminal` and removes the build directory.

To sync directly to a Kobo over SSH, use:

scripts/sync-kobo.sh

That script only asks for the Kobo IP address and uses OpenSSH's askpass path with an empty password, then streams the already packaged `dist/koboterminal/.adds/koboterminal` tree over `tar`.

If you want to package manually, `scripts/package-kobo.sh <path-to-binary>` still works as a helper.

This creates a Kobo USB package tree under dist/koboterminal that you can copy to the device storage root.

Add it to your nickelmenu like this:
menu_item :main    :KoboTerminal      :cmd_spawn    :quiet:exec /mnt/onboard/.adds/koboterminal/koboterminal.sh

## Keyboard note

Standalone framebuffer apps generally cannot directly invoke Nickel's system keyboard API.
This project therefore implements an in-app virtual keyboard path as the practical default.
