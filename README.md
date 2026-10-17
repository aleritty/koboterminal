<h1 align="center">KoboTerminal</h1>

<p align="center">
  A real terminal for Kobo e-readers, opened from NickelMenu, running inside Nickel.
</p>

<p align="center">
  <a href="https://github.com/aleritty/koboterminal/actions/workflows/build.yml"><img alt="Build" src="https://github.com/aleritty/koboterminal/actions/workflows/build.yml/badge.svg"></a>
  <a href="https://github.com/aleritty/koboterminal/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/aleritty/koboterminal?include_prereleases"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <img alt="Platform: Kobo" src="https://img.shields.io/badge/platform-Kobo%20(Nickel)-black">
  <a href="https://github.com/pgaskin/NickelHook"><img alt="Built with NickelHook" src="https://img.shields.io/badge/built%20with-NickelHook-orange"></a>
</p>

---

- [Features](#features)
- [Requirements](#requirements)
- [Install](#install)
- [Usage](#usage)
- [Uninstall](#uninstall)
- [Building](#building)
- [Dependencies](#dependencies)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Features

- **Native.** A [NickelHook](https://github.com/pgaskin/NickelHook) mod that uses Nickel's own dialog
  and on-screen keyboard. Nothing to launch, and Nickel keeps running.
- **Real PTY.** `/bin/sh` runs on a pseudo-terminal and output is parsed by
  [libtsm](https://github.com/kmscon/libtsm), so `vi`, `top`, `less`, job control and `^C` work.
- **Extra keys.** A button row adds Ctrl (applies to the next key), ^C, Esc, Tab and the arrows,
  which Nickel's keyboard lacks.
- **E-ink friendly.** Only changed cells are redrawn, at most every 150 ms. Colours are ignored;
  inverse video (cursor, status bars) is kept.
- **Clean.** Closing the dialog kills everything the shell started. Nothing runs or polls while
  the terminal is closed. Uninstalling removes every file the mod created.

## Requirements

- A Kobo e-reader on firmware 4.x (uses the same Nickel symbols as
  [NickelDBus](https://github.com/shermp/NickelDBus); firmware 4.6 and later).
- [NickelMenu](https://github.com/pgaskin/NickelMenu) (latest release: v0.6.0).

## Install

1. Download `KoboRoot.tgz` from the [latest release](https://github.com/aleritty/koboterminal/releases/latest),
   or [build it](#building).
2. Connect the Kobo over USB and copy `KoboRoot.tgz` into the hidden `KOBOeReader/.kobo/` folder.
3. Eject the device. It installs the update and reboots.

With SSH access you can do step 2–3 in one go: `scripts/sync.sh <kobo-ip>`.

This installs two files:

| File | Purpose |
| --- | --- |
| `/usr/local/Kobo/imageformats/libkoboterminal.so` | The mod (Nickel loads it at startup) |
| `/mnt/onboard/.adds/nm/koboterminal` | The NickelMenu entry (`KOBOeReader/.adds/nm/koboterminal`) |

## Usage

Open NickelMenu and tap **Terminal**. Tap the text field to bring up the keyboard; keys go to the
shell as you type. **Enter** (or the keyboard's Go key) sends Enter, **Close** ends the session.

Closing the dialog kills every process started from it. To keep something running afterwards,
start it in its own session: `setsid telnetd -l /bin/sh &`.

## Uninstall

Every method below removes all of the mod's traces:

- the library;
- the menu entry;
- the `/tmp/koboterminal.fifo` pipe and `/tmp/koboterminal_history` shell history;
- `KOBOeReader/.kobo/KoboTerminal_*.log` crash logs;
- leftovers from earlier development builds: the `KOBOeReader/.adds/koboterminal/` folder and a
  `# BEGIN KOBOTERMINAL` block in `KOBOeReader/.adds/nm/menu`.

**With the uninstall package (recommended):** copy `KoboRoot-uninstall.tgz` from the release into
`KOBOeReader/.kobo/`, **renamed to `KoboRoot.tgz`**, and eject. The device installs it, reboots,
and the mod removes itself on startup. The Terminal menu item may stay visible until the next reboot.

**Over USB, without the package:** create an empty file named `koboterminal_uninstall` in the root of
`KOBOeReader`, or delete `KOBOeReader/.adds/nm/koboterminal`. Eject and reboot. The mod removes
itself on startup.

**Fully manual, over SSH or telnet** (works even if the mod no longer loads):

```sh
rm -f /usr/local/Kobo/imageformats/libkoboterminal.so \
      /usr/local/Kobo/imageformats/libkoboterminal.so.failsafe \
      /mnt/onboard/.adds/nm/koboterminal \
      /mnt/onboard/koboterminal_uninstall \
      /mnt/onboard/.kobo/KoboTerminal_*.log \
      /tmp/koboterminal.fifo /tmp/koboterminal_history
rm -rf /mnt/onboard/.adds/koboterminal
[ -f /mnt/onboard/.adds/nm/menu ] && sed -i '/# BEGIN KOBOTERMINAL/,/# END KOBOTERMINAL/d' /mnt/onboard/.adds/nm/menu
reboot
```

Deleting the library while Nickel is running is safe; it stays loaded until the reboot.

## Building

### Initial setup

You need Git and Docker; everything else runs in the NickelTC toolchain image.

```sh
git clone https://github.com/aleritty/koboterminal.git
cd koboterminal
docker pull ghcr.io/pgaskin/nickeltc:1.0
```

To run the host tests you also need a C/C++ compiler, `pkg-config` and the Qt 5 development
headers (Debian/Ubuntu: `apt install g++ make pkg-config qtbase5-dev`).

### Build

```sh
scripts/build.sh          # writes KoboRoot.tgz and KoboRoot-uninstall.tgz
tests/run.sh              # host test of the terminal widget (real PTY, ^C, cleanup)
```

`VERSION=1.2.3 scripts/build.sh` overrides the version string (default: `git describe`).

### Manual build (without the script)

Inside the toolchain image, or anywhere [NickelTC](https://github.com/pgaskin/NickelTC) is on `PATH`:

```sh
docker run --rm -it -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 bash
make clean
make                      # src/libkoboterminal.so
make strip                # optional, drops debug symbols
make koboroot             # KoboRoot.tgz
make koboroot-uninstall   # KoboRoot-uninstall.tgz
```

Run `make clean` after editing headers.

### Continuous integration

Both [GitHub Actions](.github/workflows/build.yml) and [GitLab CI](.gitlab-ci.yml) run the host
test and build both packages:

- **Branches and pull/merge requests:** test builds, downloadable as workflow/job artifacts.
- **Tags `v*`:** a release with `KoboRoot.tgz` and `KoboRoot-uninstall.tgz` attached. On GitHub, a
  tag with a `-` (for example `v1.0.0-rc1`) becomes a prerelease.

```sh
git tag v1.0.0 && git push origin v1.0.0
```

## Dependencies

Vendored libraries are in the repository; nothing is downloaded at build time.

| Dependency | Version | Where | License |
| --- | --- | --- | --- |
| [NickelHook](https://github.com/pgaskin/NickelHook) | `bcd90ba` (2026-07-26, latest; no tagged releases) | `NickelHook/` | MIT |
| [libtsm](https://github.com/kmscon/libtsm) | v4.8.0 (latest) | `vendor/libtsm/` | MIT; hash table LGPL-2.1 |
| [NickelTC](https://github.com/pgaskin/NickelTC) toolchain | `ghcr.io/pgaskin/nickeltc:1.0` (GCC 4.9.4) | Docker image | — |
| Qt | 5.2.1, the version in Kobo firmware (from NickelTC) | toolchain sysroot | LGPL |
| [NickelMenu](https://github.com/pgaskin/NickelMenu) | runtime, latest v0.6.0 | on the device | MIT |
| Qt 5 development headers | any 5.x (host tests only) | host | LGPL |

libtsm carries one local change, described in [vendor/libtsm/VERSION](vendor/libtsm/VERSION).

## Troubleshooting

- **"KoboTerminal plugin not loaded" toast:** the menu entry couldn't find `/tmp/koboterminal.fifo`,
  so Nickel didn't load the library. Check that `libkoboterminal.so` is in
  `/usr/local/Kobo/imageformats/`.
- **Logs:** `logread | grep KoboTerminal` over SSH or telnet.
- **Failed startup:** NickelHook writes the log to `KOBOeReader/.kobo/KoboTerminal_*.log`, and
  disables the mod for the next boot if Nickel crashed while loading it.

Design notes and history are in [PLAN.md](PLAN.md).

## License

[MIT](LICENSE). NickelHook is MIT (Patrick Gaskin). libtsm is MIT, except its hash table
(`vendor/libtsm/src/shared/shl-htable.*`), which is LGPL-2.1 (from CCAN).
