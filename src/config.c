#include "kterm/config.h"

void kterm_config_defaults(struct kterm_config *cfg) {
    cfg->cols = 80;
    cfg->rows = 24;
    cfg->font_size = 24;
    cfg->keyboard_auto_open = 1;
    cfg->partial_refresh_every = 1;
    cfg->full_refresh_every = 40;
}
