#include "kterm/osk.h"

void kterm_osk_init(struct kterm_osk *osk, int auto_open) {
    osk->visible = auto_open ? 1 : 0;
}

void kterm_osk_toggle(struct kterm_osk *osk) {
    osk->visible = !osk->visible;
}

int kterm_osk_visible(const struct kterm_osk *osk) {
    return osk->visible;
}
