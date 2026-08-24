#ifndef KTERM_PTY_H
#define KTERM_PTY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/types.h>

/* Qt-integrated PTY API (for NickelHook integration) */
int kterm_pty_qt_init(void);
void kterm_pty_qt_deinit(void);
ssize_t kterm_pty_qt_read(char *buf, size_t cap);
int kterm_pty_qt_write(const char *text, size_t len);
int kterm_pty_qt_is_running(void);
int kterm_pty_qt_get_fd(void);

#ifdef __cplusplus
}
#endif

#endif
