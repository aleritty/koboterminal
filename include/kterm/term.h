#ifndef KTERM_TERM_H
#define KTERM_TERM_H

#include <stddef.h>

#define KTERM_TERM_PREVIEW_CAP 160

struct kterm_term {
    int cols;
    int rows;
    size_t bytes_fed;
    char preview[KTERM_TERM_PREVIEW_CAP];
    size_t preview_len;
};

int kterm_term_init(struct kterm_term *term, int cols, int rows);
void kterm_term_feed(struct kterm_term *term, const char *buf, size_t len);
void kterm_term_resize(struct kterm_term *term, int cols, int rows);
const char *kterm_term_preview(const struct kterm_term *term);
void kterm_term_deinit(struct kterm_term *term);

#endif
