#include "kterm/input.h"

#include <string.h>

int kterm_input_init(struct kterm_input *input) {
    input->initialized = 1;
    return 0;
}

int kterm_input_poll(struct kterm_input *input, struct kterm_input_event *event) {
    if (!input || !input->initialized || !event) {
        return -1;
    }

    memset(event, 0, sizeof(*event));
    event->kind = KTERM_INPUT_NONE;
    return 0;
}

void kterm_input_deinit(struct kterm_input *input) {
    if (!input) {
        return;
    }

    input->initialized = 0;
}
