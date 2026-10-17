# KoboTerminal — Plan (native-qt branch)

Goal: NickelMenu entry opens a native terminal-like dialog inside Nickel.

## Why previous attempts failed

- `nicklhook-way`: a hand-rolled NickelHook clone (`nh_runtime.c`) built with
  meson instead of the real NickelHook + NickelTC Makefile. Mangled symbols
  were hand-typed and wrong (`_ZN13QLineEdit6setTextERK7QString`,
  `...17getConfirmationDialog...`), and it tried to PLT-hook
  `nm_action_cmd_output`, which is not an exported libnm symbol. The library
  never loaded, and even if it had, init would have failed.
- `webview-way`: daemon + xterm.js in `nickel_browser`. Untested; kept on its
  branch as a fallback.
- `main`/`dbus-way`: standalone FBInk app, fights Nickel for the screen.

## Architecture

```
NickelMenu "Terminal"
  cmd_output: echo > /tmp/koboterminal.fifo      (fails fast if plugin missing)
        |
libkoboterminal.so  (real NickelHook, /usr/local/Kobo/imageformats/)
  init: mkfifo + QSocketNotifier (event driven, zero polling)
  on byte: open dialog on Nickel's GUI thread
        |
ConfirmationDialog (Nickel's own, via dlsym — symbols copied from NickelDBus)
  + N3ConfirmationTextEditField  -> Nickel on-screen keyboard
  + KtTerm (addWidget)           -> forkpty("/bin/sh") + libtsm, grid repainted per dirty cell
  + key row (addWidget)          -> Ctrl, ^C, Esc, Tab, arrows
```

Typed characters go straight to the PTY (the field keeps a sentinel space so
backspace is detectable). Closing kills the shell's whole session.

Install = one `KoboRoot.tgz`: the `.so` plus `/mnt/onboard/.adds/nm/koboterminal`
(NickelMenu reads every file in `.adds/nm/`). Uninstall = delete that menu
file and reboot (`uninstall_xflag`), or create `/mnt/onboard/koboterminal_uninstall`.

## Phases

1. Plugin loads; menu entry shows a Nickel OK dialog with custom text.
2. Terminal dialog: keyboard field runs commands in persistent `sh`, output shown.
3. UI: monospace, sizing, e-ink-friendly look.
4. Real PTY + libtsm terminal emulation (vi/top/less, ^C, job control).

## Verify on device

- `logread | grep KoboTerminal` (nh_log goes to syslog).
- Failed init dumps log to `/mnt/onboard/.kobo/KoboTerminal_*.log`.
