#include "kterm/renderer.h"

#include <stdio.h>

int kterm_renderer_init(struct kterm_renderer *renderer, const struct kterm_config *cfg) {
    (void)cfg;
    renderer->frame_index = 0;
    return 0;
}

void kterm_renderer_draw(struct kterm_renderer *renderer, const struct kterm_term *term) {
    const char *preview = kterm_term_preview(term);

    renderer->frame_index += 1;
    printf("[frame=%d cols=%d rows=%d bytes=%zu preview=\"%s\"]\n",
           renderer->frame_index,
           term->cols,
           term->rows,
           term->bytes_fed,
           preview);
}

void kterm_renderer_deinit(struct kterm_renderer *renderer) {
    (void)renderer;
}
