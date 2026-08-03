/*
 * KoboTerminal Application Entry Point
 *
 * This file provides the main entry point for the NickelHook integration.
 * When built with -Dwith_nickelhook=true, it exports kterm_nhook_init()
 * which is called by NickelHook's loader.
 *
 * The application lifecycle:
 *   1. Nickel auto-loads the .so as a Qt plugin from /usr/local/Kobo/imageformats
 *   2. nh_init() resolves all Nickel symbols
 *   3. kterm_nhook_init() initializes PTY and terminal state
 *   4. User triggers dialog via NickelMenu or other hook
 *   5. Dialog shows, PTY runs, input/output handled via Qt signals
 *   6. User closes dialog → teardown → return to Nickel
 */

#define _GNU_SOURCE

#include "kterm/app.h"
#include "kterm/nickelhook.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Internal state: dialog visibility tracking
 * ============================================================================ */

/* Whether the terminal dialog is currently visible (defined in nickelhook.c) */
extern int kterm_dialog_visible;

/* ============================================================================
 * Public API: NickelMenu trigger callback
 * ============================================================================ */

/*
 * This function is called when the user selects "Terminal" from the
 * NickelMenu. It shows the terminal dialog non-blocking and returns
 * immediately, allowing Nickel to continue its event loop.
 *
 * The dialog lifecycle is managed by Qt signal/slot connections.
 * When the user closes the dialog, kterm_nhook_cleanup() is called.
 */
void kterm_nickelmenu_trigger(void) {
    /* Show dialog if not already visible */
    if (!kterm_dialog_visible) {
        if (kterm_nhook_dialog_show() == 0) {
            kterm_dialog_visible = 1;
            fprintf(stderr, "[koboterminal] Terminal dialog shown via NickelMenu\n");
        } else {
            fprintf(stderr, "[koboterminal] Failed to show terminal dialog\n");
        }
    }
}


