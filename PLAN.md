# KoboTerminal — Plan

See [PLAN_OLD.md](PLAN_OLD.md) for the original NickelHook/Qt-plugin plan.
That approach is abandoned — see [Why the previous approach was dropped](#why-the-previous-approach-was-dropped).

## Objective

A terminal emulator for Kobo e-readers, launched from NickelMenu:
1. One NickelMenu tap starts a background daemon and opens a browser view onto it.
2. The view renders a real terminal (proper VT100/ANSI handling) with a PTY-backed shell.
3. Typing in the view runs commands; output renders above/inline as usual.
4. Closing the view ends the daemon too — nothing lingers after use.
5. Nothing is injected into Nickel's process — no crash-the-boot risk from this component at all.

## Current Architecture

```
NickelMenu "Terminal" (one tap)
  ├── cmd_spawn ─────────────────► kterm-server (daemon, binds 127.0.0.1:8090)
  ├── chain_success:
  │   nickel_browser ────────────► http://127.0.0.1:8090/
  │                                   │
  │                                   └── index.html + xterm.js, over a
  │                                       WebSocket back to the daemon's
  │                                       forkpty()'d /bin/sh
  └── chain_failure:
      dbg_toast ────────────────► "Could not start koboterminal server"
```

`kterm-server` (`src/kterm_server.c`) is a plain POSIX C daemon: no Qt, no
`libnickel` symbol resolution, no `dlopen` into Nickel's process at all. It
implements just enough HTTP to serve three static files and upgrade one path
to a hand-rolled WebSocket (RFC 6455 handshake via vendored SHA1 in
`src/sha1.c`, plus frame parsing/framing — no external libraries). All
VT100/ANSI escape-sequence interpretation happens client-side in vendored
[xterm.js](packaging/vendor/xterm.js); the daemon just relays raw bytes.

The daemon is single-threaded and sequential, deliberately with no
concurrency: it serves the handful of static-file requests, blocks in the
relay loop once the WebSocket upgrades, and exits the moment that WebSocket
closes. Its process lifetime IS the terminal session, which is what makes
"ends when the browser is closed" true without any extra bookkeeping.

NickelMenu can't run two action types from literally the same directive, but
it does support chaining (`chain_success`/`chain_failure`/`chain_always`) —
one menu tap runs `cmd_spawn` then `chain_success:nickel_browser` (with
`chain_failure:dbg_toast` for the rare case `cmd_spawn` itself couldn't even
launch `/bin/sh`). Per NickelMenu's own source, `cmd_spawn`'s success/failure
reflects only whether `QProcess::startDetached()` managed to fork/exec — not
`kterm-server`'s own exit code — so `chain_success` fires identically whether
the daemon just started or was already running (binding the port is the "is
a session already open" check). A repeat tap while one is active never spawns
a second daemon; the second `nickel_browser` view gets an explicit, immediate
"Terminal already in use" (HTTP 503) from the daemon's listen socket, watched
for specifically during the relay loop so a repeat tap fails fast instead of
hanging until the first session ends.

Chaining an action onto `nickel_browser` itself to kill the daemon when the
view closes doesn't work, and isn't needed: `nickel_browser` is
non-blocking — it returns immediately after requesting the view open (per
NickelMenu's own source), so a chained action would fire an instant after the
tap, not when the view is later closed. The daemon already handles "end when
the browser closes" correctly on its own side, by watching the actual
WebSocket connection's lifecycle — the only signal that genuinely reflects
when the view was torn down.

## Why the previous approach was dropped

The original plan (see [PLAN_OLD.md](PLAN_OLD.md)) built `libkoboterminal.so`
as a Qt "image format plugin" auto-loaded by Nickel via `dlopen()`, PLT-hooked
a couple of `ConfirmationDialog`/`libnickel` symbols, and rendered a terminal
via `libtsm` inside a native Nickel dialog.

That approach got the library building and packaging correctly, but it never
successfully loaded on the target device: `libkoboterminal.so` sat in
`/usr/local/Kobo/imageformats/` with correct permissions, but never appeared
in the running Nickel process's `/proc/<pid>/maps` — meaning Qt's plugin
loader never even called `dlopen()` on it. Ruled out over several rounds of
on-device diagnosis: stale-process timing, Qt version/ABI mismatch (confirmed
identical 5.2.1 on both sides), the debug/release build-key bit (fixed via
`-DQT_NO_DEBUG`, still didn't help), and the plugin-directory path itself
(confirmed correct — other real hooks like NickelMenu's own `libnm.so` load
fine from the same directory). The root cause was never conclusively
identified before the decision was made to abandon the technique entirely
rather than keep debugging an opaque, firmware-specific loading mechanism.

The new architecture sidesteps the whole problem: nothing needs to be
`dlopen`'d by Nickel or Qt at all. NickelMenu's own native, well-tested
`cmd_spawn`/`nickel_browser` actions are the only integration surface.

## Imperatives

1. Be extremely concise, both in the code and in communicating about it.
2. Nothing outside `.adds/koboterminal/` — no system files, no PLT hooks.
3. Prefer mechanisms already proven to work on this exact device (NickelMenu's
   own actions, a plain daemon+socket) over anything requiring firmware-
   specific reverse engineering.

## Already Done

| Step | Status |
|------|--------|
| Daemon: HTTP static file serving | ✅ Built, tested locally (`curl`) |
| Daemon: WebSocket handshake (SHA1 + base64) | ✅ Verified byte-for-byte against Python `hashlib` |
| Daemon: WS framing (masked client→server, unmasked server→client) | ✅ Verified via a manual Python WS client |
| Daemon: `forkpty()` + shell relay | ✅ End-to-end tested: sent a command over WS, got correct PTY output back |
| Daemon: idempotent start (bind-as-lock) | ✅ Verified — second instance exits immediately, only one process survives |
| Daemon: exits when the session ends | ✅ Verified — process exits right after the WebSocket closes |
| Daemon: repeat tap while a session is active | ✅ Verified — no second daemon spawned, second view gets an immediate 503 (no hang) |
| One-tap menu entry (`cmd_spawn` chained into `nickel_browser`) | ✅ Config written (`chain_success`/`chain_failure`, verified against NickelMenu's own source); not yet exercised against real NickelMenu |
| Cross-build for ARM (`nickeltc:1` toolchain) | ✅ Builds clean, no warnings |
| Packaging (`dist/KoboRoot.tgz`) | ✅ Contains only `.adds/koboterminal/*`, no `/usr/local/Kobo` |
| `install.sh`/`uninstall.sh` (marker-delimited NickelMenu block, daemon process cleanup) | ✅ Written, not yet exercised on-device |
| Vendored `xterm.js`/`xterm.css` (MIT) | ✅ Fetched, wired into `packaging/index.html` |
| Repo cleanup (Qt/libtsm/PLT-hook code, unused Dockerfile/toolchain, and build machinery removed) | ✅ |

## Missing / Not Yet Verified

- **On-device install + first real end-to-end test.** Everything above was
  validated on the host machine or via cross-compilation only — the daemon
  has never actually run on a real Kobo yet.
- **NickelMenu's `nickel_browser` rendering xterm.js correctly.** The
  WebView's JS engine on this firmware is untested with xterm.js specifically
  — unknown whether it needs a fallback (older ECMAScript features, a
  different xterm.js build) or works as-is.
- **On-screen/touch keyboard interaction with xterm.js's hidden textarea.**
  xterm.js expects a real keyboard by default; whether Kobo's on-screen
  keyboard triggers correctly when tapping into the terminal view is unknown.
- **Sizing/viewport.** `index.html` hardcodes 80×24; whether that fits/reads
  legibly on the actual Kobo screen resolution inside `nickel_browser` is
  unverified — may need a smaller grid or CSS font-size tuning.
- **`chain_success`/`chain_failure` real-world behavior.** Confirmed from
  NickelMenu's own source (`cmd_spawn`'s success reflects only whether
  `/bin/sh` launched), but never exercised against the real binary on this
  device.
- **`kterm-server.log` review.** The `cmd_spawn` line redirects stdout/stderr
  to `.adds/koboterminal/kterm-server.log` — never yet inspected on a real
  run.
