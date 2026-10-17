<h1 align="center">KoboTerminal</h1>

<p align="center">
  A real terminal on your Kobo, one tap away in NickelMenu.
</p>

<p align="center">
  <a href="https://github.com/aleritty/koboterminal/actions/workflows/build.yml"><img alt="Build" src="https://github.com/aleritty/koboterminal/actions/workflows/build.yml/badge.svg"></a>
  <a href="https://github.com/aleritty/koboterminal/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/aleritty/koboterminal?include_prereleases"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <img alt="Platform: Kobo" src="https://img.shields.io/badge/platform-Kobo%20(Nickel)-black">
  <a href="https://github.com/pgaskin/NickelHook"><img alt="Built with NickelHook" src="https://img.shields.io/badge/built%20with-NickelHook-orange"></a>
</p>

---

Sometimes you just want a shell on your e-reader: check a log, restart a service, poke at a
config file, without reaching for a laptop and SSH. KoboTerminal gives you exactly that. Tap
**Terminal** in NickelMenu and a full-screen terminal opens right inside Kobo's own interface, with
Kobo's own on-screen keyboard. Type `exit` and it's gone, along with everything you started in it.

It's a real terminal, not a command box: the shell runs on a proper PTY, so `vi`, `top`, `less`,
tab completion and `^C` all behave the way you'd expect.

> **Heads-up:** so far this has only been tested on a **Kobo Libra Colour**. It's designed to adapt
> to other screens and firmware versions, but if you try it elsewhere, please open an issue and
> tell me how it went.

## What it's like to use

- **Tap anywhere on the terminal** to bring up the keyboard. The **⌨** key hides it again, and the
  terminal takes the whole screen.
- **Swipe down** to read back through older output (about 1000 lines), **swipe up** to come back.
  Typing anything jumps straight back to the live screen.
- **A row of extra keys** sits above the keyboard with what Kobo's keyboard is missing: Esc, `/`,
  `-`, `~`, ↑ ↓, Tab, Ctrl, Alt, Home and End. Ctrl and Alt work like sticky keys: tap Ctrl, then `c`,
  and you've sent `^C`. ↑ ↓ bring back previous commands; the keyboard's own ← → move the cursor in
  the shell, in `vi` and so on.
- **Colour, where your Kobo has it.** On colour models (like the Libra Colour) the usual terminal
  colours show up, in a palette tuned for colour e-ink: text stays dark and backgrounds stay light, so
  nothing fades away. Black-and-white models stay strictly black on white, which is always the most
  readable. Prefer black and white on a colour screen too? Open `KOBOeReader/.adds/nm/zz-koboterminal`
  and enable the **Terminal (B/W)** entry (the file explains how).
- **It's gentle on e-ink.** Only the characters that changed are redrawn, and output is batched so a
  command and its result usually cost a single refresh. It comes with its own monospace font
  (JetBrains Mono), since Kobo doesn't ship one.
- **Wi-Fi stays on** while the terminal is open, so an SSH session or a download doesn't die when
  Nickel decides you've been idle long enough.
- **It cleans up after itself.** Closing the terminal kills every process started from it. While
  it's closed, nothing runs and nothing polls. Plugging in USB closes it automatically, so the drive
  can be shared.

The shell starts in `/mnt/onboard`, the folder you see as `KOBOeReader` over USB.

> The shell runs as **root**, like Kobo's own software. That's what makes it useful, and also what
> makes it easy to break things. Know what you type.

If you want something to keep running after you close the terminal, give it its own session:

```sh
setsid telnetd -l /bin/sh &
```

## Installing

You need [NickelMenu](https://github.com/pgaskin/NickelMenu) installed first.

1. Download `KoboRoot.tgz` from the [latest release](https://github.com/aleritty/koboterminal/releases/latest).
2. Connect your Kobo over USB and copy it into the hidden `KOBOeReader/.kobo/` folder.
3. Eject. The Kobo installs it and reboots on its own.

That's it: **Terminal** is now the last entry in NickelMenu. Updating works the same way; just copy
the new `KoboRoot.tgz` over.

Only two files are added to the device:

| File | What it is |
| --- | --- |
| `/usr/local/Kobo/imageformats/libkoboterminal.so` | the terminal itself, loaded by Kobo's software at startup |
| `/mnt/onboard/.adds/nm/zz-koboterminal` | the NickelMenu entry |

## Uninstalling

Uninstalling removes everything KoboTerminal ever created, not just the menu entry. Pick whichever
way is easiest:

- **The uninstall package.** Download `KoboRoot-uninstall.tgz` from the release, copy it into
  `KOBOeReader/.kobo/` **renamed to `KoboRoot.tgz`**, and eject. After the reboot it's gone. (The menu
  entry may linger until one more reboot.)
- **Over USB.** Delete `KOBOeReader/.adds/nm/zz-koboterminal`, or create an empty file called
  `koboterminal_uninstall` in the root of `KOBOeReader`. Eject and reboot; KoboTerminal removes
  itself on startup.
- **By hand, over SSH**, which works even if something went badly wrong:

  ```sh
  rm -f /usr/local/Kobo/imageformats/libkoboterminal.so* \
        /mnt/onboard/.adds/nm/zz-koboterminal /mnt/onboard/koboterminal_uninstall \
        /mnt/onboard/.kobo/KoboTerminal_*.log /tmp/koboterminal.fifo /tmp/koboterminal_history
  reboot
  ```

## When something goes wrong

- **A "KoboTerminal plugin not loaded" message** when you tap the menu entry means Kobo's software
  didn't load the library. Check that `libkoboterminal.so` is in `/usr/local/Kobo/imageformats/`.
- **If Kobo's software crashes while loading KoboTerminal**, it switches the plugin off for the next
  boot, so your device always comes back. A log is saved as `KOBOeReader/.kobo/KoboTerminal_*.log`.
- **Layout or touch problems on a different model?** Over SSH, run

  ```sh
  logread | grep KoboTerminal
  ```

  and attach the output to an issue. It includes the screen layout, every resize, the first touch
  events and how the colour support was detected, which is usually enough to fix things.

## Building it yourself

All you need is Git and Docker; the Kobo toolchain
([NickelTC](https://github.com/pgaskin/NickelTC)) runs in a container.

```sh
git clone https://github.com/aleritty/koboterminal.git
cd koboterminal
scripts/build.sh          # builds KoboRoot.tgz and KoboRoot-uninstall.tgz
scripts/sync.sh <kobo-ip> # optional: copy it to the Kobo over SSH and reboot
```

<details>
<summary>More: manual build, tests, releases</summary>

**Manual build.** Inside the toolchain image (or anywhere NickelTC is on your `PATH`):

```sh
docker run --rm -it -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 bash
make clean && make && make strip    # src/libkoboterminal.so
make koboroot koboroot-uninstall    # the two packages
```

Run `make clean` after editing a header. `VERSION=1.2.3 scripts/build.sh` overrides the version
string, which otherwise comes from `git describe`.

**Tests.** `tests/run.sh` builds the terminal widget for your computer and checks the important
parts with a real shell: typing, `^C`, `exit`, and that nothing is left running afterwards. It needs a
C/C++ compiler, `pkg-config` and the Qt 5 development headers (Debian/Ubuntu:
`apt install g++ make pkg-config qtbase5-dev`).

**Releases.** [GitHub Actions](.github/workflows/build.yml) and [GitLab CI](.gitlab-ci.yml) test and
build every push. Pushing a `v*` tag (`git tag v1.2.3 && git push origin v1.2.3`) publishes a release
with both packages and the third-party notices attached; a tag with a `-`, like `v1.2.3-rc1`, is a
prerelease on GitHub.

**How it works.** [PLAN.md](PLAN.md) has the design notes: how the plugin hooks into NickelMenu, how
the dialog is laid out, and why earlier approaches didn't work out.

</details>

## Credits

KoboTerminal stands on the shoulders of some great open-source work:

- [NickelHook](https://github.com/pgaskin/NickelHook) and [NickelMenu](https://github.com/pgaskin/NickelMenu)
  by Patrick Gaskin, which make mods like this possible at all;
- [NickelDBus](https://github.com/shermp/NickelDBus) by Sherman Perry, whose code showed how to use
  Kobo's dialogs, keyboard and Wi-Fi from a plugin;
- [libtsm](https://github.com/kmscon/libtsm), which does the actual terminal emulation;
- [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono), the font.

## License

KoboTerminal is [MIT](LICENSE) licensed. The bundled libraries and font keep their own licenses
(MIT, LGPL-2.1 for one small part of libtsm, and the SIL Open Font License); see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Kobo is a trademark of Rakuten Kobo Inc.; this
project isn't affiliated with or endorsed by Rakuten Kobo.

<sub>A note on how this was made: KoboTerminal is a vibe-coded project. Most of the code was written
by an AI assistant (Claude) through conversation, then tested and refined by hand on a real device.
It works well for me, but do read the code before trusting it with a root shell.</sub>
