#define _GNU_SOURCE
#include "kterm/term.h"

#include <libtsm.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Logging callback - silent */
static void kterm_tsm_log(void *data, const char *file, int line, const char *func,
                          const char *subs, unsigned int sev, const char *format, va_list args) {
    /* Silent logging - uncomment for debugging */
    (void)data;
    (void)file;
    (void)line;
    (void)func;
    (void)subs;
    (void)sev;
    (void)format;
    (void)args;
}

/* VTE write callback - sends output from the terminal app to the PTY master */
static void kterm_vte_write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data) {
    (void)vte;
    (void)u8;
    (void)len;
    (void)data;
}

int kterm_term_init(struct kterm_term *term, int cols, int rows) {
    int rc;

    if (!term) {
        return -1;
    }

    memset(term, 0, sizeof(*term));

    /* Create the TSM screen */
    rc = tsm_screen_new(&term->screen, kterm_tsm_log, NULL);
    if (rc != 0) {
        fprintf(stderr, "Failed to create TSM screen: %s\n", strerror(-rc));
        return -1;
    }

    /* Resize the screen to the desired dimensions */
    rc = tsm_screen_resize(term->screen, (unsigned int)cols, (unsigned int)rows);
    if (rc != 0) {
        fprintf(stderr, "Failed to resize TSM screen: %s\n", strerror(-rc));
        tsm_screen_unref(term->screen);
        term->screen = NULL;
        return -1;
    }

    /* Create the TSM VTE (Virtual Terminal Emulator) */
    rc = tsm_vte_new(&term->vte, term->screen, kterm_vte_write_cb, NULL,
                     kterm_tsm_log, NULL);
    if (rc != 0) {
        fprintf(stderr, "Failed to create TSM VTE: %s\n", strerror(-rc));
        tsm_screen_unref(term->screen);
        term->screen = NULL;
        return -1;
    }

    fprintf(stderr, "TSM terminal initialized: %dx%d\n", cols, rows);
    return 0;
}

int kterm_term_feed(struct kterm_term *term, const char *buf, size_t len) {
    if (!term || !buf || len == 0) {
        return -1;
    }

    /* Feed the data to the VTE for parsing */
    tsm_vte_input(term->vte, buf, len);

    return 0;
}

void kterm_term_deinit(struct kterm_term *term) {
    if (!term) {
        return;
    }

    if (term->vte) {
        tsm_vte_unref(term->vte);
        term->vte = NULL;
    }

    if (term->screen) {
        tsm_screen_unref(term->screen);
        term->screen = NULL;
    }

    memset(term, 0, sizeof(*term));
}

struct tsm_screen *kterm_term_get_screen(struct kterm_term *term) {
    if (!term) {
        return NULL;
    }
    return term->screen;
}

/* ============================================================================
 * Global terminal singleton (shared by the PTY reader and the serializer)
 * ============================================================================ */

static struct kterm_term kterm_global_term;
static int kterm_global_term_active = 0;

int kterm_term_global_init(int cols, int rows) {
    if (kterm_global_term_active) {
        kterm_term_deinit(&kterm_global_term);
        kterm_global_term_active = 0;
    }

    if (kterm_term_init(&kterm_global_term, cols, rows) != 0) {
        return -1;
    }

    kterm_global_term_active = 1;
    return 0;
}

void kterm_term_global_deinit(void) {
    if (!kterm_global_term_active) {
        return;
    }

    kterm_term_deinit(&kterm_global_term);
    kterm_global_term_active = 0;
}

struct kterm_term *kterm_term_global(void) {
    return kterm_global_term_active ? &kterm_global_term : NULL;
}
