# Third-party dependencies

This folder is reserved for pinned third-party sources or integration notes.

Planned dependencies:
- FBInk: framebuffer rendering on Kobo e-ink devices
- libtsm: VT100/VT520 terminal state machine

Suggested approach:
1. Pin known-good versions (git tags or commits).
2. Add update notes and ABI compatibility notes.
3. Keep local adapter boundaries in include/kterm and src to avoid hard-coupling app logic to upstream API changes.

Licensing reminders:
- FBInk is GPLv3+
- libtsm is LGPL-2.1

Before distributing binaries, include license notices and source availability details.
