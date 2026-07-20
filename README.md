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

Dependencies are intentionally not vendored yet. See third_party/README.md.

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

1. Install a Kobo-compatible cross toolchain (for example KOReader koxtoolchain).
2. Copy toolchains/kobo-cross.ini and adjust paths for your environment.
3. Configure and build:

meson setup build-kobo --cross-file toolchains/kobo-cross.ini -Dkobo_target=true
meson compile -C build-kobo

## Packaging for NickelMenu

After building the Kobo binary:

scripts/package-kobo.sh build-kobo/koboterminal

This creates a Kobo USB package tree under dist/koboterminal that you can copy to the device storage root.

## Keyboard note

Standalone framebuffer apps generally cannot directly invoke Nickel's system keyboard API.
This project therefore implements an in-app virtual keyboard path as the practical default.
