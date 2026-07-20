#ifndef KTERM_INPUT_H
#define KTERM_INPUT_H

#include <stddef.h>

enum kterm_input_kind {
  KTERM_INPUT_NONE = 0,
  KTERM_INPUT_TEXT,
  KTERM_INPUT_KEY,
};

struct kterm_input_event {
  enum kterm_input_kind kind;
  char text[16];
};

struct kterm_input {
  int initialized;
};

int kterm_input_init(struct kterm_input *input);
int kterm_input_poll(struct kterm_input *input,
                     struct kterm_input_event *event);
void kterm_input_deinit(struct kterm_input *input);

#endif
