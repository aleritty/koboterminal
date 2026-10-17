# KoboTerminal

A terminal dialog for Kobo e-readers, opened from [NickelMenu](https://github.com/pgaskin/NickelMenu).
It is a [NickelHook](https://github.com/pgaskin/NickelHook) mod: it runs inside Nickel and uses
Nickel's own dialog and on-screen keyboard. Commands run in one persistent `/bin/sh`, so `cd`
and exported variables carry over between commands.

There is no PTY and no escape-sequence handling. Interactive full-screen programs (`vi`, `top`)
will not work.

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

MIT. NickelHook is MIT, by Patrick Gaskin.
