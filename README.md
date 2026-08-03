# KoboTerminal

A terminal emulator for Kobo e-reader devices, integrated with Nickel via `LD_PRELOAD` injection. Adds a native "Terminal" action to the NickelMenu, launching a full PTY shell inside a Qt dialog.

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
      - [1. Bootstrap](#1-bootstrap)
      - [2. Build](#2-build)
      - [3. Deploy to Kobo](#3-deploy-to-kobo)
      - [4. Restart Nickel](#4-restart-nickel)
    - [Launch](#launch)
    - [Uninstall](#uninstall)
  - [🛠️ Development](#️-development)
    - [Prerequisites](#prerequisites)
    - [Build](#build)
      - [Host build (for testing/debugging)](#host-build-for-testingdebugging)
      - [Kobo cross-build](#kobo-cross-build)
      - [Full project build (standalone Kobo target)](#full-project-build-standalone-kobo-target)
    - [Project Structure](#project-structure)
  - [📖 Usage](#-usage)
    - [NickelMenu Trigger](#nickelmenu-trigger)
    - [PTY Commands](#pty-commands)
  - [📦 Packaging](#-packaging)
  - [🔧 Troubleshooting](#-troubleshooting)
    - [Terminal doesn't appear in NickelMenu](#terminal-doesnt-appear-in-nickelmenu)
    - [Build fails](#build-fails)
    - [Dialog crashes on show](#dialog-crashes-on-show)
    - [NickelMenu hook returns error 127](#nickelmenu-hook-returns-error-127)
  - [📄 License](#-license)
  - [🙏 Acknowledgements](#-acknowledgements)

---

## ✨ Features

- **NickelMenu Integration** — Adds a "Terminal" action to the NickelMenu via `LD_PRELOAD` injection
- **Full PTY Shell** — Runs a real shell process with full terminal emulation
- **VT Escape Sequences** — Parses ANSI/VT100 escape sequences via vendored `libtsm`
- **Native Qt Dialog** — Uses Nickel's `ConfirmationDialog` for a seamless look and feel
- **Touch Input** — Captures user input through Nickel's `TouchLineEdit` widget
- **Non-blocking** — Dialog shows without blocking Nickel's event loop

---

## 🏗️ Architecture

```bash
┌──────────────────────────────────────────────────────────────┐
│                        Nickel (Kobo UI)                      │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  LD_PRELOAD: libkoboterminal.so                         │ │
│  │                                                         │ │
│  │  ┌───────────────────────────────────────────────────┐  │ │
│  │  │  nickelhook.c        — Library entry point        │  │ │
│  │  │                      — Symbol resolution          │  │ │
│  │  │                      — NickelMenu action reg.     │  │ │
│  │  └───────────────────────────────────────────────────┘  │ │
│  │                                                         │ │
│  │  ┌───────────────────────────────────────────────────┐  │ │
│  │  │  ui_dialog.c           — Dialog lifecycle         │  │ │
│  │  │  ui_input.c            — Input field management   │  │ │
│  │  │  ui_signals.c          — Qt signal/slot wiring    │  │ │
│  │  └───────────────────────────────────────────────────┘  │ │
│  │                                                         │ │
│  │  ┌───────────────────────────────────────────────────┐  │ │
│  │  │  pty_qt.c              — PTY management           │  │ │
│  │  │  term_tsm.c            — VT escape parser         │  │ │
│  │  │  term_serialize.c      — Screen → plain text      │  │ │
│  │  └───────────────────────────────────────────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

**Key Components:**

| File | Purpose |
| ---- | ------- |
| `src/nickelhook.c` | Library entry point, Nickel symbol resolution, NickelMenu action registration |
| `src/app.c` | Application lifecycle, signal handling, NickelMenu trigger callback |
| `src/ui_dialog.c` | `ConfirmationDialog` lifecycle management |
| `src/ui_input.c` | `TouchLineEdit` input field management |
| `src/ui_signals.c` | Qt signal/slot connections |
| `src/pty_qt.c` | PTY creation, read/write, Qt integration |
| `src/term_tsm.c` | VT escape sequence parser (via `libtsm`) |
| `src/term_serialize.c` | TSM screen buffer → plain text conversion |
| `src/main.c` | Standalone host-mode entry point |

---

## 🚀 Quick Start

### Install on Kobo

#### 1. Bootstrap

```bash
# Initialize submodules and build toolchain (first time only)
scripts/bootstrap.sh
```

#### 2. Build

```bash
# Build the shared library
scripts/build.sh
```

#### 3. Deploy to Kobo

```bash
scripts/sync.sh
```

#### 4. Restart Nickel

Go to **Settings → Quit** to restart Nickel, or reboot the device.

### Launch

Once activated:

1. Tap the screen to bring up the **NickelMenu**
2. Select **Terminal** from the menu
3. The terminal dialog opens with a PTY shell
4. Type commands in the input field at the bottom
5. Press **OK** to execute, **Cancel** or **X** to close

### Uninstall

**Automated (via SSH):**

```bash
scripts/uninstall-nickelhook.sh
```

**Manual:**

```bash
# On the Kobo device
rm /usr/local/Kobo/imageformats/libkoboterminal.so
rmdir /mnt/onboard/.adds/koboterminal 2>/dev/null || true
```

Then restart Nickel.

---

## 🛠️ Development

### Prerequisites

| Requirement | Version | Notes |
| ----------- | ------- | ----- |
| Docker | Any recent version | For cross-compilation |
| Meson | ≥ 1.1 | Build system |
| Ninja | Any | Build backend |
| GCC | ≥ 14 (target) | ARM cross-compiler (provided by Docker image) |
| Host C compiler | Any with C11 support | For host-mode builds |

### Build

#### Host build (for testing/debugging)

```bash
meson setup build-host -Dwith_nickelhook=true
meson compile -C build-host
```

#### Kobo cross-build

```bash
# Bootstrap toolchain (first time only)
docker build -t koboterminal-kobo-toolchain -f docker/kobo-toolchain/Dockerfile .

# Build
scripts/build.sh
```

#### Full project build (standalone Kobo target)

```bash
scripts/build-kobo.sh
```

This builds the standalone `koboterminal` binary with FBInk framebuffer rendering.

### Project Structure

```bash
├── src/                    # Source files
├── include/kterm/          # Public headers
├── toolchains/             # Cross-compilation toolchain config
├── docker/                 # Docker crossbuild toolchain
├── scripts/                # Build & install scripts
│   ├── build.sh            # Build the library
│   ├── sync.sh             # Sync the library to kobo
│   ├── bootstrap.sh        # setup the repository for the first build
├── meson.build             # Meson build definition
├── meson_options.txt       # Build options
└── README.md               # This file
```

---

## 📖 Usage

### NickelMenu Trigger

The terminal is triggered from the NickelMenu:

```bash
NickelMenu → Terminal → [PTY Shell Opens]
```

The dialog is non-blocking — Nickel continues its event loop while the terminal is active.

### PTY Commands

Once the terminal is open, you can run any shell command:

```bash
ls -la
cat /proc/version
top
vi
nano
```

Standard VT100 escape sequences are supported for terminal rendering.

---

## 📦 Packaging

The `scripts/build.sh` script builds the shared library and produces deployment artifacts in `dist/KoboRoot/` and `dist/KoboRoot.tgz`.

Deploy to Kobo:

```bash
scripts/sync.sh
```

For uninstallation, use the `deploy/uninstall.sh` script on the Kobo device.

---

## 🔧 Troubleshooting

### Terminal doesn't appear in NickelMenu

1. Verify the library is installed:

   ```bash
  ls -la /usr/local/Kobo/imageformats/libkoboterminal.so
   ```

2. Restart Nickel after making changes.

### Build fails

1. Ensure Docker image is up to date:

   ```bash
   docker build -t koboterminal-kobo-toolchain -f docker/kobo-toolchain/Dockerfile .
   ```

2. Check Meson version:

   ```bash
   meson --version  # Should be ≥ 1.1
   ```

### Dialog crashes on show

1. Verify the library is installed on the Kobo:

   ```bash
  ls -la /usr/local/Kobo/imageformats/libkoboterminal.so
   ```

2. Check Nickel logs:

   ```bash
   log_view /var/log/nickel.log
   ```

### NickelMenu hook returns error 127

KoboTerminal now writes hook logs to:

```bash
/mnt/onboard/.adds/koboterminal/lkoboterminal.log
```

Logging mode is controlled by:

```bash
/mnt/onboard/.adds/koboterminal/koboterminal.conf
```

Set `verbose=0` for errors only (default), or `verbose=1` for detailed logs.

---

## 📄 License

This project is released under the [MIT License](LICENSE).

---

## 🙏 Acknowledgements

- [NickelHook](https://github.com/pgaskin/NickelHook) — LD_PRELOAD injection framework for Nickel
- [libtsm](https://github.com/cedo00/libtsm) — Terminal State Machine library