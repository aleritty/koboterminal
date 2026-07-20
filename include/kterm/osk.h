#ifndef KTERM_OSK_H
#define KTERM_OSK_H

struct kterm_osk {
  int visible;
};

void kterm_osk_init(struct kterm_osk *osk, int auto_open);
void kterm_osk_toggle(struct kterm_osk *osk);
int kterm_osk_visible(const struct kterm_osk *osk);

#endif
