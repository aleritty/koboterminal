# Packaging

Files that get installed onto the Kobo device, plus the scripts to
activate/deactivate them. This file ships on-device too, at
`.adds/koboterminal/README.md` — if you're reading it there, KoboTerminal is
already installed; skip to [Uninstall](#uninstall) below.

| File | Installed to | Purpose |
| ---- | ------------- | ------- |
| `kterm-server` | `.adds/koboterminal/kterm-server` | The daemon: HTTP/WebSocket server + PTY shell |
| `index.html`, `vendor/xterm.js`, `vendor/xterm.css` | `.adds/koboterminal/*` | The terminal page (served by `kterm-server` itself) |
| `nickelmenu.conf` | `.adds/koboterminal/nickelmenu.conf` (staged copy; merged into NickelMenu's config by `install.sh`) | The chained "Terminal" menu entry (`cmd_spawn` + `chain_success:nickel_browser` + `chain_failure:dbg_toast`) |
| `install.sh` | `.adds/koboterminal/install.sh` | Activates the menu entries on-device |
| `uninstall.sh` | `.adds/koboterminal/uninstall.sh` | Stops the daemon and removes everything |

`scripts/build.sh` stages these into `dist/KoboRoot/` (for the `KoboRoot.tgz`
auto-install path) and `scripts/sync.sh` pushes them straight to a running
device over SSH — both end up running `install.sh` on the device.

Nothing is installed outside `.adds/koboterminal/` — no `/usr/local/Kobo`
touch, no system files, no PLT hooks into Nickel. The daemon is a plain
standalone executable; NickelMenu just starts it and opens a browser view
onto it.

## Install

Requires [NickelMenu](https://pgaskin.net/NickelMenu/) already installed.

1. `scripts/build.sh` — cross-builds `kterm-server` and packages `dist/KoboRoot.tgz`
2. Either:
   - `scripts/sync.sh` — pushes files to a running device over SSH and runs
     `install.sh` automatically, or
   - copy `dist/KoboRoot.tgz` to `KOBOeReader/.kobo/`, eject, let it install
     and reboot, then SSH in and run:
     ```sh
     sh /mnt/onboard/.adds/koboterminal/install.sh
     ```
3. Restart Nickel (Settings → Quit) or reboot.

`install.sh` is idempotent — safe to re-run after every sync; it replaces its
own menu-entry block in NickelMenu's config rather than duplicating it.

## Uninstall

```sh
ssh root@<kobo-ip> 'sh /mnt/onboard/.adds/koboterminal/uninstall.sh'
```

This kills any running `kterm-server` process(es), removes the menu entries,
and deletes everything under `.adds/koboterminal/`. Then restart Nickel
(Settings → Quit) or reboot.

Since nothing lives outside `.adds/koboterminal/` (a normal onboard folder),
you can also just delete that folder directly — over SSH, or by plugging the
device into a PC (unlike the old Qt-plugin approach, there's no root
filesystem file to worry about).
