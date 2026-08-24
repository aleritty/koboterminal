# KoboTerminal

A terminal emulator for Kobo e-reader devices, integrated with Nickel via [NickelHook](https://github.com/pgaskin/NickelHook)-style Qt plugin auto-loading. Adds a native "Terminal" action to the NickelMenu, launching a full PTY shell inside a Qt dialog.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Build](https://github.com/aleritty/koboterminal/actions/workflows/build.yml/badge.svg)](https://github.com/aleritty/koboterminal/actions/workflows/build.yml)

---

## Table of Contents

- [KoboTerminal](#koboterminal)
  - [Table of Contents](#table-of-contents)
  - [✨ Features](#-features)
  - [🏗️ Architecture](#️-architecture)
  - [🚀 Quick Start](#-quick-start)
    - [Install on Kobo](#install-on-kobo)
    - [Launch](#launch)
    - [Uninstall](#uninstall)
  - [🛠️ Development](#️-development)
    - [Prerequisites](#prerequisites)
    - [Build](#build)
    - [Project Structure](#project-structure)
  - [📖 Usage](#-usage)
  - [🔧 Troubleshooting](#-troubleshooting)
  - [📄 License](#-license)
  - [🙏 Acknowledgements](#-acknowledgements)

---

## ✨ Features

- **NickelMenu Integration** — Adds a "Terminal" action to the NickelMenu by hooking NickelMenu's action dispatcher in-process
- **Full PTY Shell** — Runs a real shell process with full terminal emulation
- **VT Escape Sequences** — Parses ANSI/VT100 escape sequences via vendored `libtsm`
- **Native Qt Dialog** — Uses Nickel's `ConfirmationDialog` for a seamless look and feel
- **Touch Input** — Captures user input through Nickel's `TouchLineEdit` widget
- **Non-blocking** — PTY I/O and dialog updates are signal/slot driven, so Nickel's event loop is never blocked
- **Boot watchdog** — self-disables if Nickel doesn't survive loading it, so a bad hook can't brick the boot flow

---

## 🏗️ Architecture

`libkoboterminal.so` is installed as a Qt image-format plugin (`/usr/local/Kobo/imageformats/`), which makes Nickel's own Qt plugin loader `dlopen()` it at startup — no `LD_PRELOAD` involved. A constructor then PLT-hooks NickelMenu's action dispatcher and a couple of Nickel dialog symbols.

```
Nickel (main process)
  └── /usr/local/Kobo/imageformats/libkoboterminal.so  (Qt auto-loads it)
        ├── nh_runtime.c     — bootstrap constructor: symbol resolution + PLT hooking
        ├── nickelhook.c     — nh_info/nh_hook/nh_dlsym tables, NickelMenu action hook, logging
        ├── ui_dialog.cpp    — ConfirmationDialog + TouchLineEdit lifecycle, Qt signal/slot wiring
        ├── pty_qt.c         — PTY fork/read/write, QSocketNotifier integration
        ├── term_tsm.c       — libtsm screen + VTE terminal emulation
        └── term_serialize.c — screen → plain text serialization
```

**Key Components:**

| File | Purpose |
| --- | --- |
| `src/nh_runtime.c` | Constructor: resolves symbols, applies PLT hooks, rolls back on failure |
| `src/nickelhook.c` | `nh_info`/`nh_hook`/`nh_dlsym` tables, NickelMenu `cmd_output` hook, logging |
| `src/ui_dialog.cpp` | `ConfirmationDialog`/`TouchLineEdit` lifecycle, PTY↔dialog signal/slot bridge |
| `src/pty_qt.c` | PTY creation, read/write, `QSocketNotifier` integration |
| `src/term_tsm.c` | VT escape sequence parser (via `libtsm`) |
| `src/term_serialize.c` | TSM screen buffer → plain text conversion |

---

## 🚀 Quick Start

### Install on Kobo

Requires [NickelMenu](https://pgaskin.net/NickelMenu/) already installed on the device.

```bash
scripts/bootstrap.sh   # first time only: pulls the cross toolchain image
scripts/build.sh        # builds the library, packages dist/KoboRoot.tgz
scripts/sync.sh          # pushes to a running device over SSH and activates it
```

See [packaging/README.md](packaging/README.md) for the `KoboRoot.tgz`-on-boot alternative and full details.

### Launch

1. Tap the screen to bring up the **NickelMenu**
2. Select **Terminal** from the menu
3. The terminal dialog opens with a PTY shell
4. Type commands in the input field, press **Enter** to run them
5. Close the dialog to end the PTY session

### Uninstall

```bash
ssh root@<kobo-ip> 'sh /mnt/onboard/.adds/koboterminal/uninstall.sh'
```

Then restart Nickel (Settings → Quit) or reboot.

---

## 🛠️ Development

### Prerequisites

| Requirement | Version | Notes |
| --- | --- | --- |
| Docker | Any recent version | For cross-compilation |
| Meson | ≥ 1.1 | Build system |
| Ninja | Any | Build backend |
| Host C/C++ compiler | Any with C11 support | For host-mode builds |

### Build

**Host build** (for compile-checking/iterating on non-Kobo-specific logic):

```bash
meson setup build-host -Dwith_nickelhook=true
meson compile -C build-host
```

**Kobo cross-build:**

```bash
scripts/bootstrap.sh   # first time only
scripts/build.sh
```

`scripts/build.sh` cross-compiles inside `ghcr.io/pgaskin/nickeltc:1` and packages the result into `dist/KoboRoot/` and `dist/KoboRoot.tgz`. `docker/Dockerfile` builds an alternative, koxtoolchain-based toolchain image — it's not used by `scripts/build.sh` by default.

### Project Structure

```bash
├── src/                    # Library source
├── include/kterm/          # Public headers
├── vendor/nickelhook/      # Vendored NickelHook Qt-plugin auto-load trick
├── subprojects/            # Meson subprojects (libtsm.wrap + patch)
├── toolchains/             # Cross-compilation toolchain config
├── docker/                 # Cross toolchain Dockerfile (alternative to nickeltc image)
├── scripts/                # Build & install scripts
│   ├── bootstrap.sh        # First-time toolchain image pull
│   ├── build.sh            # Cross-build + package dist/KoboRoot(.tgz)
│   └── sync.sh             # Push to a running device over SSH + activate
├── packaging/              # Files installed onto the device + install/uninstall scripts
├── dist/                   # Build output (gitignored, recreated by build.sh)
├── meson.build
├── meson_options.txt
└── README.md
```

---

## 📖 Usage

The terminal is triggered from the NickelMenu (`NickelMenu → Terminal`). NickelMenu invokes `koboterminal:open`, which the hooked `nm_action_cmd_output` intercepts in-process — no external process is spawned for the trigger itself.

Once the dialog is open, run any shell command; standard VT100 escape sequences are supported for rendering.

---

## 🔧 Troubleshooting

### Terminal doesn't appear in NickelMenu

1. Verify the library is installed: `ls -la /usr/local/Kobo/imageformats/libkoboterminal.so` — if you instead find `libkoboterminal.so.disabled`, the boot watchdog tripped (see below).
2. Verify the menu entry is in NickelMenu's actual config: `grep koboterminal /mnt/onboard/.adds/nm/config` — if missing, run `sh /mnt/onboard/.adds/koboterminal/install.sh`
3. Restart Nickel after making changes.

### Build fails

1. Re-pull the toolchain image: `docker pull ghcr.io/pgaskin/nickeltc:1`
2. Check Meson version: `meson --version` (should be ≥ 1.1)

### Dialog crashes on show / boot loop

The mangled Nickel symbol names this project hooks are firmware-specific and haven't been verified against every firmware version. If a hook resolves to the wrong method, calling it can crash Nickel outright.

A built-in watchdog protects against this at boot: if Nickel doesn't stay up for `failsafe_delay` seconds (20s by default) after the library loads, the *next* load renames `libkoboterminal.so` to `libkoboterminal.so.disabled` and skips hooking, so Nickel starts normally without it — no SSH intervention needed to break the loop. It only guards that initial window, not a crash triggered later by actually using the terminal. See [packaging/README.md](packaging/README.md#boot-watchdog) for recovery.

Check `/mnt/onboard/.adds/koboterminal/lkoboterminal.log` and `lkoboterminal.bootstrap.log` either way.

### NickelMenu hook returns an error / no logs

Logging goes to `/mnt/onboard/.adds/koboterminal/lkoboterminal.log`, controlled by `/mnt/onboard/.adds/koboterminal/koboterminal.conf` (`verbose=0` for errors only, `verbose=1` for detailed logs).

---

## 📄 License

This project is released under the [MIT License](LICENSE).

---

## 🙏 Acknowledgements

- [NickelHook](https://github.com/pgaskin/NickelHook) — Qt plugin auto-load + PLT hooking technique for Nickel
- [libtsm](https://github.com/Aetf/libtsm) — Terminal State Machine library
