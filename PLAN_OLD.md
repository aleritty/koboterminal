# KoboTerminal — NickelHook Integration Plan

## Objective

Make the KoboTerminal NickelHook shared library (`libkoboterminal.so`) functional on a Kobo e-reader:
1. The library loads into Nickel's process via NickelHook
2. A NickelMenu entry launches the terminal dialog (native modal window)
3. Only at thit point a PTY is spawned.
4. The terminal renders as similarily possible as the common terminal emulators, with an input box below and the native keyboard open. The commands sent from the input box are executed and the output rendered above the input box in the "main window"
5. CLosing the dialog closes the PTY and frees up all the resources.
6. The library does not slow down the device.

## Imperatives:
1. be extremely concise, both in the code and answers.
2. follow nickeHook documentations and directives: https://github.com/pgaskin/NickelHook
3. 

## Current Status

| Area | Status |
|------|--------|
| **Build** | ✅ Compiles successfully (39KB `libkoboterminal.so`) |
| **Library loading** | ❌ Library is incorporated but doesn't work — no menu entry, device slows down or won't wake from sleep |
| **NickelMenu integration** | ❌ No menu entry exists to launch the terminal |
| **Performance** | ❌ Library causes device slowdown / sleep prevention |
| **Symbol resolution** | ⚠️ Symbols are resolved via `nh_dlsym` but may be wrong for the target firmware |
| **Dialog rendering** | ❌ Not tested — `kterm_term_serialize()` returns empty output |
| **Input handling** | ❌ Not tested — no input path wired up |

## Architecture Overview

```
Nickel (main process)
  └── libnm.so (NickelMenu)
  └── libkoboterminal.so (our module, auto-loaded from /mnt/onboard/.adds/koboterminal/)
        ├── nickelhook.c — nh_info/nh_hook/nh_dlsym, symbol resolution
        ├── app.c — public API (kterm_nickelmenu_trigger)
        ├── pty_qt.c — PTY fork, QSocketNotifier integration
        ├── ui_dialog.c — ConfirmationDialog creation, show/close
        ├── term_tsm.c — TSM screen + VTE terminal emulation
        ├── term_serialize.c — screen → plain text serialization
```

## Issues to Diagnose and Fix

### Issue 1: No NickelMenu Entry

**Problem:** No menu item exists to trigger the terminal dialog.

**Root Cause:** The `kterm_nickelmenu_trigger()` function in `app.c` exists but is never called. NickelMenu cannot call C functions directly — it only supports `cmd_spawn`, `kfmon`, and built-in actions.

**Plan:**
- [ ] **TASK 1.1:** Create a NickelMenu config file (`nickelmenu.conf`) with a menu entry
  - Option A: Use `cmd_spawn` to touch a trigger file, then add a file-watcher thread in `nickelhook.c`
  - Option B: Use KFMon to watch a trigger file and call a function directly
  - **Recommended:** Option A — simpler, no external dependencies
- [ ] **TASK 1.2:** Implement a lightweight file-watcher thread in `nickelhook.c`
  - Poll `/tmp/koboterminal.trigger` every 500ms
  - When file appears: call `kterm_nhook_dialog_show()`, remove file, return
  - Must be non-blocking, low CPU, not prevent sleep

### Issue 2: Device Slowdown / Sleep Prevention

**Problem:** The library causes device slowdown or prevents sleep.

**Likely Causes:**
- [ ] **TASK 2.1:** Check for busy-waiting or tight polling loops
  - Review `nickelhook.c` constructor — any spin loops?
  - Review `pty_qt.c` — QSocketNotifier setup correct?
- [ ] **TASK 2.2:** Check for missing cleanup on dialog close
  - `kterm_pty_qt_deinit()` must be called when dialog closes
  - PTY child process must be reaped
- [ ] **TASK 2.3:** Check for Qt event loop interference
  - Nickel's event loop must not be blocked
  - All dialog operations must be async (signal/slot, not blocking calls)

### Issue 3: Symbol Resolution May Be Wrong

**Problem:** The mangled C++ symbol names in `kterm_nh_dlsym[]` may not match the target firmware's `libnickel.so.1.0.0`.

**Plan:**
- [ ] **TASK 3.1:** Verify symbol names against the actual firmware
  - Use `nm -C` or `c++filt` on the target's `libnickel.so.1.0.0`
  - Compare resolved addresses — if NULL, symbols are wrong
- [ ] **TASK 3.2:** Add logging to track which symbols resolve successfully
  - Log each symbol name + resolved address
  - If any critical symbol is NULL, log a clear error

### Issue 4: Dialog Rendering Empty

**Problem:** `kterm_term_serialize()` returns empty output.

**Root Cause:** The function has a TODO comment: "Implement proper screen serialization using tsm_screen_draw callback".

**Plan:**
- [ ] **TASK 4.1:** Implement `kterm_serialize_screen()` using `tsm_screen_draw`
  - Extract character grid from `tsm_screen`
  - Strip ANSI color/attribute codes
  - Produce plain text buffer for `ConfirmationDialog__setText`
- [ ] **TASK 4.2:** Test with a simple `echo hello` to verify text appears

### Issue 5: Input Path Not Wired

**Problem:** No input path from the dialog's text field to the PTY master.

**Plan:**
- [ ] **TASK 5.1:** Wire up input signal/slot
  - Connect `TouchLineEdit` text changes to `kterm_pty_qt_write()`
  - Handle Enter key to send command to shell
- [ ] **TASK 5.2:** Test with a simple command (`ls`, `echo`)

## Execution Order

```
Phase 1: Diagnostics (understand why it's broken)
  ├── TASK 2.x — Check for busy loops, missing cleanup
  └── TASK 3.x — Verify symbol resolution

Phase 2: Core Fix (make it work)
  ├── TASK 1.x — Add NickelMenu entry + file-watcher trigger
  ├── TASK 4.x — Implement screen serialization
  └── TASK 5.x — Wire up input path

Phase 3: Validation (verify it works)
  ├── Build and sync to Kobo
  ├── Test menu entry launches dialog
  ├── Test text renders correctly
  ├── Test input works
  └── Verify no slowdown / sleep prevention
```

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `packaging/nickelmenu.conf` | **Create** | NickelMenu config with "Terminal" menu entry |
| `src/nickelhook.c` | **Modify** | Add file-watcher thread, symbol resolution logging |
| `src/term_serialize.c` | **Modify** | Implement `kterm_serialize_screen()` using `tsm_screen_draw` |
| `src/ui_dialog.c` | **Modify** | Add input handling, dialog close cleanup |
| `src/pty_qt.c` | **Modify** | Verify QSocketNotifier setup, add cleanup logging |

## Success Criteria

1. ✅ Library loads without slowing down the device
2. ✅ NickelMenu entry launches terminal dialog
3. ✅ Terminal text renders correctly (not empty)
4. ✅ Input works (commands execute, output appears)
5. ✅ Dialog closes cleanly, PTY deinitializes
6. ✅ Device can sleep and wake normally

## Notes

- NickelMenu **cannot** call C functions directly — only shell commands or KFMon triggers
- The library is loaded via NickelHook's auto-loading mechanism from `.adds/`
- All Qt operations must be async (signal/slot) to not block Nickel's event loop
- Symbol names are firmware-specific — must verify against target device
- Logging goes to `/tmp/koboterminal.log` (not `.adds/` — filesystem is read-only)
