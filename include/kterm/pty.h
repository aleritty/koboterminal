#ifndef KTERM_PTY_H
#define KTERM_PTY_H

#include <stddef.h>
#include <sys/types.h>

/* Qt-integrated PTY API (for NickelHook integration) */
int kterm_pty_qt_init(void);
void kterm_pty_qt_deinit(void);
ssize_t kterm_pty_qt_read(char *buf, size_t cap);
int kterm_pty_qt_write(const char *text, size_t len);
int kterm_pty_qt_is_running(void);

#endif
