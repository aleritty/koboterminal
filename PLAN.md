# KoboTerminal — Design notes

Goal: NickelMenu entry opens a native terminal-like dialog inside Nickel.

## Why previous attempts failed

Earlier approaches (their branches were removed at 1.0.0; the commits before
"Restart on real NickelHook" are their history):

- Hand-rolled NickelHook clone (`nh_runtime.c`) built with meson instead of
  the real NickelHook + NickelTC Makefile. Mangled symbols were hand-typed and
  wrong (`_ZN13QLineEdit6setTextERK7QString`, `...17getConfirmationDialog...`),
  and it tried to PLT-hook `nm_action_cmd_output`, which is not an exported
  libnm symbol. The library never loaded, and even if it had, init would have
  failed.
- Daemon + xterm.js in `nickel_browser` (the "WIP: webview approach" commit). Never tested.
- Standalone FBInk app: fights Nickel for the screen and touch input.

## Architecture

```text
NickelMenu "Terminal"
  cmd_output: echo > /tmp/koboterminal.fifo      (fails fast if plugin missing)
        |
libkoboterminal.so  (real NickelHook, /usr/local/Kobo/imageformats/)
  init: mkfifo + QSocketNotifier (event driven, zero polling)
  on byte: open dialog on Nickel's GUI thread
        |
ConfirmationDialog (Nickel's own, via dlsym — symbols copied from NickelDBus)
  + N3ConfirmationTextEditField  -> Nickel on-screen keyboard
  + KtTerm (addWidget)           -> forkpty("/bin/sh") + libtsm, grid repainted per dirty cell,
                                    1000-line scrollback (swipe), tap opens the keyboard
  + key rows (addWidget)         -> Esc / - ~ ⌨ | Tab Ctrl Alt Home End (KtKey: touch-aware,
                                    Kobo's Qt sends QTouchEvents, QPushButton needs a left click)
  + KtWifiKeepAlive              -> vetoes WirelessWatchdog::aboutToKillWifi while open
  + KtFit                        -> terminal height (pixel-exact) so the dialog ends at the
                                    keyboard's top edge (screen bottom when it's hidden);
                                    width follows the layout, cols/rows = size / cell size

Nickel's ConfirmationDialog (fw 4.x, from the layout dump): a full-screen
transparent window; the visible box is QFrame 'frame', centred above the
KeyboardFrame 'keyboardFrame'; our widgets live in 'contentContainer'. We make
'frame' screen-wide with thin margins; KtFit grows the terminal until the
frame's height equals the keyboard's top (screen height when it's hidden).
Nickel insets 'frame' (~58 px sides, ~82 px top): zeroed with setContentsMargins
on 'frame' and 'contentContainer' (re-applied after each fit) and
`#frame { padding: 0 }`. Touch arrives as QTouchEvent (TouchBegin 194, Update
195, End 196); the keyboard is shown/hidden via KeyboardFrame::show()/hide().
  PlugWorkflowManager::aboutToConnect (USB storage) -> dialog reject() -> session killed
```

The text field is collapsed to zero height when its `tapped()` signal exists
(taps on the terminal emit it), and the terminal is moved above it so Nickel
keeps it above the keyboard. No title, no close button (the user asked for the space): `exit`, Ctrl+d or a
USB connection close it. The shell starts in `/mnt/onboard`.

Process tree: forkpty child = session leader, a watcher that ignores ^C/HUP,
forks the shell, waits for it and exits. Its exit closes a pipe whose read
end Nickel watches, so `exit` is detected with no polling or timers at all.
Then the whole session is killed (`setsid` escapes) and the watcher reaped.

Typed characters go straight to the PTY (the field keeps two sentinel spaces
with the cursor between them: backspace is detectable, and cursor moves from
Nickel's ← → keys are forwarded as arrow keys). Closing kills the shell's whole session.

Install = one `KoboRoot.tgz`: the `.so` plus `/mnt/onboard/.adds/nm/zz-koboterminal`
(NickelMenu reads every file in `.adds/nm/`, alphabetically; `zz-` puts the entry last).

Uninstall is triggered by `/mnt/onboard/koboterminal_uninstall` existing
(`uninstall_flag`) or the menu file missing (`uninstall_xflag`). NickelHook then
deletes the library and the flag and calls `kt_uninstall()`, which removes
everything else in the footprint below. `KoboRoot-uninstall.tgz` ships the
library plus the flag, so the cleanup runs even if the installed library is
missing or broken.

## Footprint (keep up to date)

Every path the mod can create on the device. **Adding a new one means updating
all three places:** `kt_uninstall()` in `src/koboterminal.cc`, the manual
`rm` commands in README "Uninstall", and this table.

| Path | Created by | Removed by |
| --- | --- | --- |
| `/usr/local/Kobo/imageformats/libkoboterminal.so` (+ `.failsafe` during init) | KoboRoot.tgz | NickelHook |
| `/mnt/onboard/.adds/nm/zz-koboterminal` | KoboRoot.tgz | `kt_uninstall` |
| `/mnt/onboard/.adds/nm/koboterminal` | KoboRoot.tgz before the rename | `kt_init` (on upgrade), `kt_uninstall` |
| `/mnt/onboard/koboterminal_uninstall` | KoboRoot-uninstall.tgz / user | NickelHook |
| `/tmp/koboterminal.fifo` | `kt_init` (tmpfs, also gone on reboot) | `kt_uninstall` |
| `/tmp/koboterminal_history` | shell history (`HISTFILE`, tmpfs) | `kt_uninstall` |
| `/mnt/onboard/.kobo/KoboTerminal_*.log` | NickelHook `nh_dump_log` on failed init | `kt_uninstall` |
| `/mnt/onboard/.adds/koboterminal/` | earlier dev builds (legacy) | `kt_uninstall` |
| `# BEGIN/END KOBOTERMINAL` block in `/mnt/onboard/.adds/nm/menu` | earlier dev builds (legacy) | `kt_uninstall` |

The shell itself may create files (history, user files); those belong to the
user and are not tracked.

## Phases

1. Plugin loads; menu entry shows a Nickel OK dialog with custom text.
2. Terminal dialog: keyboard field runs commands in persistent `sh`, output shown.
3. UI: monospace, sizing, e-ink-friendly look.
4. Real PTY + libtsm terminal emulation (vi/top/less, ^C, job control).
5. Full uninstall (`kt_uninstall`, `KoboRoot-uninstall.tgz`), CI on GitHub and GitLab.
6. Device feedback: no title/buttons, menu entry last, bundled font, Termux keys,
   scrollback, tap-to-keyboard, Wi-Fi keepalive, start in /mnt/onboard.

## Verify on device

- `logread | grep KoboTerminal` (nh_log goes to syslog).
- Failed init dumps log to `/mnt/onboard/.kobo/KoboTerminal_*.log`.
