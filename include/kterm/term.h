#ifndef KTERM_TERM_H
#define KTERM_TERM_H

#include <stddef.h>
#include <stdint.h>

/*
 * kterm_term wraps TSM (Terminal State Machine) + VTE (Virtual Terminal
 * Emulator). It manages a tsm_screen + tsm_vte pair, feeds PTY output to the
 * VTE, and provides access to the screen for rendering via FBInk.
 */

struct kterm_term {
  struct tsm_screen *screen;
  struct tsm_vte *vte;
  int cols;
  int rows;
  size_t bytes_fed;
};

int kterm_term_init(struct kterm_term *term, int cols, int rows);
int kterm_term_feed(struct kterm_term *term, const char *buf, size_t len);
int kterm_term_resize(struct kterm_term *term, int cols, int rows);
void kterm_term_deinit(struct kterm_term *term);

/* Get screen dimensions for rendering */
int kterm_term_get_cols(const struct kterm_term *term);
int kterm_term_get_rows(const struct kterm_term *term);

/* Get the tsm_screen pointer for rendering callbacks */
struct tsm_screen *kterm_term_get_screen(struct kterm_term *term);

/* Get the tsm_vte pointer for feeding input */
struct tsm_vte *kterm_term_get_vte(struct kterm_term *term);

#endif
