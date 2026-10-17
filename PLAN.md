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
  + QPlainTextEdit (addWidget)   -> output
  + QProcess("/bin/sh")          -> one persistent shell, stdin/stdout pipes
```

No PTY / VT parser: e-ink + on-screen keyboard make full-screen TUIs
pointless; a piped `sh` keeps `cd`/env state between commands.

Install = one `KoboRoot.tgz`: the `.so` plus `/mnt/onboard/.adds/nm/koboterminal`
(NickelMenu reads every file in `.adds/nm/`). Uninstall = delete that menu
file and reboot (`uninstall_xflag`), or create `/mnt/onboard/koboterminal_uninstall`.

## Phases

1. Plugin loads; menu entry shows a Nickel OK dialog with custom text.
2. Terminal dialog: keyboard field runs commands in persistent `sh`, output shown.
3. UI: monospace, sizing, scrollback limits, e-ink-friendly look.

## Verify on device

- `logread | grep KoboTerminal` (nh_log goes to syslog).
- Failed init dumps log to `/mnt/onboard/.kobo/KoboTerminal_*.log`.
