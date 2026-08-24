# Packaging

Files that get installed onto the Kobo device, plus the scripts to
activate/deactivate them. This file ships on-device too, at
`.adds/koboterminal/README.md` — if you're reading it there, KoboTerminal is
already installed; skip to [Uninstall](#uninstall) or
[Boot watchdog](#boot-watchdog) below.

| File | Installed to | Purpose |
| ---- | ------------- | ------- |
| `nickelmenu.conf` | `.adds/koboterminal/nickelmenu.conf` (staged copy; merged into NickelMenu's config by `install.sh`) | The "Terminal" menu entry |
| `koboterminal.conf` | `.adds/koboterminal/koboterminal.conf` | Logging config (`verbose=0`/`1`) |
| `install.sh` | `.adds/koboterminal/install.sh` | Activates the menu entry on-device |
| `uninstall.sh` | `.adds/koboterminal/uninstall.sh` | Removes everything |

`scripts/build.sh` stages these into `dist/KoboRoot/` (for the `KoboRoot.tgz`
auto-install path) and `scripts/sync.sh` pushes them straight to a running
device over SSH — both end up running `install.sh` on the device.

## Install

Requires [NickelMenu](https://pgaskin.net/NickelMenu/) already installed.

1. `scripts/build.sh` — builds the library and packages `dist/KoboRoot.tgz`
2. Either:
   - `scripts/sync.sh` — pushes files to a running device over SSH and runs
     `install.sh` automatically, or
   - copy `dist/KoboRoot.tgz` to `KOBOeReader/.kobo/`, eject, let it install
     and reboot, then SSH in and run:
     ```sh
     sh /mnt/onboard/.adds/koboterminal/install.sh
     ```
3. Restart Nickel (Settings → Quit) or reboot.

`install.sh` is idempotent — safe to re-run after every sync.

## Uninstall

```sh
ssh root@<kobo-ip> 'sh /mnt/onboard/.adds/koboterminal/uninstall.sh'
```

Then restart Nickel (Settings → Quit) or reboot.

If the library isn't loadable at all (e.g. Nickel won't start), you'll need
SSH/telnet access to the device to remove
`/usr/local/Kobo/imageformats/libkoboterminal.so` by hand — it lives on the
root filesystem, not the USB-visible `onboard` partition, so it can't be
deleted by plugging the device into a PC.

## Boot watchdog

The library disables itself automatically if it doesn't survive at least
`failsafe_delay` seconds (20s by default) after loading — this catches a boot
loop (Nickel repeatedly crashing right after our hooks are installed) instead
of leaving the device stuck.

If that triggers, you'll find
`/usr/local/Kobo/imageformats/libkoboterminal.so.disabled` instead of the
usual `.so` — Nickel now starts normally, just without the Terminal feature.
To re-enable after fixing/updating: SSH in and either rename it back
(`mv libkoboterminal.so.disabled libkoboterminal.so`) or reinstall.

This only guards against crashes within that first-boot window — not a crash
triggered later by actually opening the terminal.
