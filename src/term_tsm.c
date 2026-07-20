#include "kterm/term.h"

#include <ctype.h>
#include <string.h>

int kterm_term_init(struct kterm_term *term, int cols, int rows) {
    if (!term) {
        return -1;
    }

    term->cols = cols;
    term->rows = rows;
    term->bytes_fed = 0;
    term->preview_len = 0;
    term->preview[0] = '\0';
    return 0;
}

void kterm_term_feed(struct kterm_term *term, const char *buf, size_t len) {
    size_t i;

    if (!term || !buf || len == 0) {
        return;
    }

    term->bytes_fed += len;

    for (i = 0; i < len; ++i) {
        char ch = buf[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (term->preview_len < (KTERM_TERM_PREVIEW_CAP - 1)) {
                term->preview[term->preview_len++] = '\n';
            } else {
                memmove(term->preview, term->preview + 1, KTERM_TERM_PREVIEW_CAP - 2);
                term->preview[KTERM_TERM_PREVIEW_CAP - 2] = '\n';
            }
            continue;
        }

        if (isprint((unsigned char)ch)) {
            if (term->preview_len < (KTERM_TERM_PREVIEW_CAP - 1)) {
                term->preview[term->preview_len++] = ch;
            } else {
                memmove(term->preview, term->preview + 1, KTERM_TERM_PREVIEW_CAP - 2);
                term->preview[KTERM_TERM_PREVIEW_CAP - 2] = ch;
            }
        }
    }

    term->preview[term->preview_len] = '\0';
}

void kterm_term_resize(struct kterm_term *term, int cols, int rows) {
    if (!term) {
        return;
    }

    term->cols = cols;
    term->rows = rows;
}

const char *kterm_term_preview(const struct kterm_term *term) {
    if (!term) {
        return "";
    }

    return term->preview;
}

void kterm_term_deinit(struct kterm_term *term) {
    if (!term) {
        return;
    }

    memset(term, 0, sizeof(*term));
}
