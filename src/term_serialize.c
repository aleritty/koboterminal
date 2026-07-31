/*
 * KoboTerminal TSM Screen Serialization
 *
 * This file provides functions to serialize the TSM terminal screen state
 * to plain text for display in the Nickel dialog. It handles:
 *   - Extracting character grid from tsm_screen
 *   - Stripping colors and attributes
 *   - Truncating to last N lines (scrollback)
 *   - Sanitizing output for Qt QString compatibility
 *
 * The serialization is called on every PTY data arrival to update
 * the dialog body with the current terminal state.
 */

#define _GNU_SOURCE

#include "kterm/term.h"

#include <libtsm.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

/* Global terminal state (shared with term_tsm.c) */
static struct kterm_term kterm_global_term;

/* ============================================================================
 * Screen serialization
 * ============================================================================ */

/*
 * Serialize the TSM screen to plain text.
 *
 * This function extracts the character grid from the tsm_screen,
 * strips all ANSI color/attribute codes, and produces a plain text
 * buffer suitable for display in the Nickel ConfirmationDialog.
 *
 * Parameters:
 *   - screen: tsm_screen pointer (from kterm_term_get_screen)
 *   - output: buffer to write serialized text to
 *   - out_cap: capacity of output buffer
 *
 * Returns:
 *   - Number of bytes written to output (excluding null terminator)
 *   - -1 on error
 */
static int kterm_serialize_screen(struct tsm_screen *screen, char *output, size_t out_cap) {
    unsigned int width, height;

    if (!screen || !output || out_cap == 0) {
        return -1;
    }

    /* Get screen dimensions */
    width = tsm_screen_get_width(screen);
    height = tsm_screen_get_height(screen);

    if (width <= 0 || height <= 0) {
        return 0;
    }

    /* TODO: Implement proper screen serialization using tsm_screen_draw callback */
    /* For now, just return empty - the scrollback buffer is passed separately */
    (void)width;
    (void)height;
    (void)screen;
    (void)out_cap;

    output[0] = '\0';
    return 0;
}

/* ============================================================================
 * Public API: Serialize terminal output
 * ============================================================================ */

/*
 * Serialize the terminal screen to plain text.
 *
 * This is the public entry point called from ui_signals.c on PTY data arrival.
 * It serializes the current TSM screen state and returns the text.
 *
 * Parameters:
 *   - scrollback: raw PTY output buffer (for reference)
 *   - scrollback_len: length of scrollback buffer
 *   - output: buffer to write serialized text to
 *   - out_cap: capacity of output buffer
 *
 * Returns:
 *   - Number of bytes written to output
 *   - -1 on error
 */
int kterm_term_serialize(const char *scrollback, size_t scrollback_len, char *output, size_t out_cap) {
    struct tsm_screen *screen;
    int written;

    if (!output || out_cap == 0) {
        return -1;
    }

    output[0] = '\0';

    /* Get the TSM screen from global terminal state */
    screen = kterm_global_term.screen;
    if (!screen) {
        /* Fallback: use scrollback buffer directly */
        if (scrollback && scrollback_len < out_cap) {
            memcpy(output, scrollback, scrollback_len);
            output[scrollback_len] = '\0';
        }
        return (int)scrollback_len;
    }

    /* Serialize screen to plain text */
    written = kterm_serialize_screen(screen, output, out_cap);
    (void)written;
    return written;
}


