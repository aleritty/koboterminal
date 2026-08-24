#ifndef KTERM_TERM_H
#define KTERM_TERM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * kterm_term wraps TSM (Terminal State Machine) + VTE (Virtual Terminal
 * Emulator). It manages a tsm_screen + tsm_vte pair, feeds PTY output to the
 * VTE, and serializes the screen to plain text for the dialog body.
 */

struct kterm_term {
  struct tsm_screen *screen;
  struct tsm_vte *vte;
};

int kterm_term_init(struct kterm_term *term, int cols, int rows);
int kterm_term_feed(struct kterm_term *term, const char *buf, size_t len);
void kterm_term_deinit(struct kterm_term *term);

/* Get the tsm_screen pointer for rendering callbacks */
struct tsm_screen *kterm_term_get_screen(struct kterm_term *term);

/*
 * Serialize the terminal screen to plain text.
 * Used by the NickelHook UI to update the dialog body.
 *
 * Returns the number of bytes written to output, or -1 on error.
 */
int kterm_term_serialize(char *output, size_t out_cap);

/*
 * Global terminal singleton, shared between the PTY reader (feeds it) and
 * the dialog serializer (reads it). Lifecycle mirrors the PTY: init when the
 * shell is spawned, deinit when the dialog closes.
 */
int kterm_term_global_init(int cols, int rows);
void kterm_term_global_deinit(void);
struct kterm_term *kterm_term_global(void);

#ifdef __cplusplus
}
#endif

#endif
