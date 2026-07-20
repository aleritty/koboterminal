#ifndef KTERM_CONFIG_H
#define KTERM_CONFIG_H

#include <stdbool.h>

struct kterm_config {
  int cols;
  int rows;
  int font_size;
  bool keyboard_auto_open;
  int partial_refresh_every;
  int full_refresh_every;
};

void kterm_config_defaults(struct kterm_config *cfg);

#endif
