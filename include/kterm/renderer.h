#ifndef KTERM_RENDERER_H
#define KTERM_RENDERER_H

#include "kterm/config.h"
#include "kterm/term.h"

struct kterm_renderer {
  int frame_index;
};

int kterm_renderer_init(struct kterm_renderer *renderer,
                        const struct kterm_config *cfg);
void kterm_renderer_draw(struct kterm_renderer *renderer,
                         const struct kterm_term *term);
void kterm_renderer_deinit(struct kterm_renderer *renderer);

#endif
