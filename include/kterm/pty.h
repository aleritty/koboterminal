#ifndef KTERM_PTY_H
#define KTERM_PTY_H

#include <stddef.h>
#include <sys/types.h>

struct kterm_pty {
  int master_fd;
  pid_t child_pid;
  int running;
  int child_exit_status;
  char shell[64];
};

int kterm_pty_start(struct kterm_pty *pty, const char *shell_path);
ssize_t kterm_pty_read(struct kterm_pty *pty, char *buf, size_t cap);
ssize_t kterm_pty_write(struct kterm_pty *pty, const char *buf, size_t len);
int kterm_pty_is_running(struct kterm_pty *pty);
void kterm_pty_stop(struct kterm_pty *pty);

#endif
