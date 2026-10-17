# KoboTerminal

A terminal emulator for Kobo e-reader devices. A tiny standalone daemon owns a PTY-backed shell and serves it over a local WebSocket; [xterm.js](https://github.com/xtermjs/xterm.js) renders it in Nickel's own browser view. NickelMenu starts the daemon and opens the view — nothing runs inside Nickel's process.

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

- **No Nickel process injection** — no `dlopen`, no PLT hooking, no mangled C++ symbols, no Qt ABI/build-key concerns. The daemon is a plain standalone executable.
- **Real terminal rendering** — [xterm.js](https://github.com/xtermjs/xterm.js) handles all VT100/ANSI escape sequences client-side; the daemon just relays raw bytes.
- **Real PTY shell** — `forkpty()` + `/bin/sh` for the terminal session.
- **Self-contained** — everything lives under `.adds/koboterminal/`; nothing touches `/usr/local/Kobo` or any system file.
- **One tap, ephemeral** — the menu entry starts the daemon and opens the view in one action; the daemon's lifetime IS that session, so closing the view ends it — nothing lingers.
- **Idempotent start** — the daemon binds `127.0.0.1:8090`; if a session is already open, a repeat tap's spawn just fails to bind and exits quietly.

---

## 🏗️ Architecture

```
NickelMenu "Terminal" (one tap)
  ├── cmd_spawn ─────────► kterm-server (daemon, binds 127.0.0.1:8090)
  └── chain_success:
      nickel_browser ────► http://127.0.0.1:8090/
                              │
                              └── index.html + xterm.js, over a WebSocket
                                  back to the daemon's forkpty()'d /bin/sh
```

`kterm-server` implements just enough HTTP to serve three static files (`index.html`, `xterm.js`, `xterm.css`) and to upgrade one path to a WebSocket (hand-rolled RFC 6455 handshake/framing — no external libraries). No VT100 parsing happens on the device side at all; xterm.js does that in the browser. The process is single-threaded and sequential: it serves the handful of static-file requests, then blocks in the relay loop once the WebSocket upgrade happens — and exits as soon as that WebSocket closes, so the daemon's lifetime is exactly the terminal session's lifetime.

**Key files:**

| File | Purpose |
| --- | --- |
| `src/kterm_server.c` | The daemon: HTTP/WebSocket server, `forkpty()` + relay loop, fork-per-connection |
| `src/sha1.c`/`sha1.h` | Vendored SHA1, used only for the WebSocket handshake's `Sec-WebSocket-Accept` |
| `packaging/index.html` | Terminal page: creates an xterm.js `Terminal`, wires it to a WebSocket |
| `packaging/vendor/xterm.js`, `xterm.css` | Vendored xterm.js (MIT), served locally — no CDN/internet dependency on-device |

---

## 🚀 Quick Start

### Install on Kobo

Requires [NickelMenu](https://pgaskin.net/NickelMenu/) already installed on the device.

```bash
scripts/bootstrap.sh   # first time only: pulls the cross toolchain image
scripts/build.sh        # cross-builds kterm-server, packages dist/KoboRoot.tgz
scripts/sync.sh          # pushes to a running device over SSH and activates it
```

See [packaging/README.md](packaging/README.md) for the `KoboRoot.tgz`-on-boot alternative and full details.

### Launch

1. Tap the screen to bring up the **NickelMenu**
2. Select **Terminal** — starts the daemon and opens Nickel's browser view onto the shell, in one tap
3. Type commands; close the browser view to end the session — the daemon exits with it, nothing left running

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
| Host C compiler | Any with C11 support | The daemon is plain POSIX C — builds and runs natively on Linux too |

### Build

**Host build** (the daemon has no Kobo-specific dependency — build and run it directly for iterating/testing):

```bash
meson setup build-host
meson compile -C build-host
./build-host/kterm-server 8090   # then open http://127.0.0.1:8090/ against packaging/ assets
```

**Kobo cross-build:**

```bash
scripts/bootstrap.sh   # first time only
scripts/build.sh
```

`scripts/build.sh` cross-compiles inside `ghcr.io/pgaskin/nickeltc:1` and packages the result into `dist/KoboRoot/` and `dist/KoboRoot.tgz`.

### Project Structure

```bash
├── src/                    # kterm-server source (plain C, no Qt/Nickel dependency)
├── toolchains/             # Cross-compilation toolchain config
├── scripts/                # Build & install scripts
│   ├── bootstrap.sh        # First-time toolchain image pull
│   ├── build.sh            # Cross-build + package dist/KoboRoot(.tgz)
│   └── sync.sh             # Push to a running device over SSH + activate
├── packaging/              # Files installed onto the device + install/uninstall scripts
│   └── vendor/             # Vendored xterm.js/xterm.css (MIT)
├── dist/                   # Build output (gitignored, recreated by build.sh)
├── meson.build
└── README.md
```

---

## 📖 Usage

Tapping "Terminal" runs a chained NickelMenu action: `cmd_spawn` launches `kterm-server` in the background, then `chain_success` opens Nickel's own browser view (`nickel_browser`) at `http://127.0.0.1:8090/` (a `chain_failure:dbg_toast` reports the rare case where the spawn itself couldn't happen). `cmd_spawn`'s reported success only reflects whether `/bin/sh` launched, not `kterm-server`'s own exit code, so this fires the same way whether the daemon just started or was already running (binding the port is the "already running" check) — safe to tap again while a session is active. The daemon's process lifetime is scoped to that one WebSocket session: it serves the page's static files, then blocks relaying the `forkpty()`'d `/bin/sh` over the WebSocket, and exits as soon as that WebSocket closes — closing the browser view leaves nothing running.

---

## 🔧 Troubleshooting

### "Terminal" shows a connection error

Check `ps | grep kterm-server` over SSH (the daemon may not have started, or exited early), and `/mnt/onboard/.adds/koboterminal/kterm-server.log` (stdout/stderr redirected there by the `cmd_spawn` line) for anything unexpected.

### Build fails

1. Re-pull the toolchain image: `docker pull ghcr.io/pgaskin/nickeltc:1`
2. Check Meson version: `meson --version` (should be ≥ 1.1)

### Menu entries missing or duplicated after upgrading

`install.sh` manages a marker-delimited block (`# BEGIN KOBOTERMINAL` / `# END KOBOTERMINAL`) in NickelMenu's config, so re-running it after an upgrade replaces the whole block cleanly. If it looks wrong, re-run `sh /mnt/onboard/.adds/koboterminal/install.sh`.

---

## 📄 License

This project is released under the [MIT License](LICENSE).

---

## 🙏 Acknowledgements

- [NickelMenu](https://github.com/pgaskin/NickelMenu) — `cmd_spawn`/`nickel_browser` actions this relies on
- [xterm.js](https://github.com/xtermjs/xterm.js) — the terminal emulator rendered client-side
