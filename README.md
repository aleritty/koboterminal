# KoboTerminal

A terminal dialog for Kobo e-readers, opened from [NickelMenu](https://github.com/pgaskin/NickelMenu).
It is a [NickelHook](https://github.com/pgaskin/NickelHook) mod: it runs inside Nickel and uses
Nickel's own dialog and on-screen keyboard. The shell runs on a real PTY and output is parsed by
[libtsm](https://github.com/Aetf/libtsm), so `vi`, `top`, `less`, job control and `^C` work.
Keys are sent as you type; a row of buttons adds Ctrl (sticky), ^C, Esc, Tab and arrows.

Closing the dialog kills every process the shell started. Use `setsid cmd &` to keep a
daemon running after you close it. Colours are ignored (e-ink); inverse video is kept.

## Install

Requires NickelMenu.

1. `scripts/build.sh` (needs Docker; uses the `ghcr.io/pgaskin/nickeltc:1` image).
2. Copy `KoboRoot.tgz` to `KOBOeReader/.kobo/`, eject, and let the device reboot.
   Or run `scripts/sync.sh <kobo-ip>` over SSH.
3. Open NickelMenu and tap **Terminal**.

## Uninstall

Delete `KOBOeReader/.adds/nm/koboterminal` and reboot.

## Debugging

- `logread | grep KoboTerminal` over SSH/telnet.
- If init fails, the log is dumped to `KOBOeReader/.kobo/KoboTerminal_*.log`.
- The **KoboTerminal plugin not loaded** toast means the menu entry could not find
  `/tmp/koboterminal.fifo`, so the library was not loaded into Nickel.

See [PLAN.md](PLAN.md) for the design and for why the earlier attempts failed.

## License

MIT. NickelHook is MIT (Patrick Gaskin). libtsm is MIT, except its hash table (`vendor/libtsm/src/shared/shl-htable.*`), which is LGPL-2.1 (from CCAN).
