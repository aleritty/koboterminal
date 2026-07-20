#define _XOPEN_SOURCE 700

#include "kterm/pty.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void kterm_pty_refresh_child_state(struct kterm_pty *pty) {
    int status = 0;
    pid_t ret;

    if (!pty || !pty->running || pty->child_pid <= 0) {
        return;
    }

    ret = waitpid(pty->child_pid, &status, WNOHANG);
    if (ret == pty->child_pid) {
        pty->running = 0;
        pty->child_exit_status = status;
    }
}

int kterm_pty_start(struct kterm_pty *pty, const char *shell_path) {
    pid_t pid;
    int flags;
    const char *effective_shell;

    if (!pty) {
        return -1;
    }

    memset(pty, 0, sizeof(*pty));

    effective_shell = shell_path ? shell_path : getenv("SHELL");
    if (!effective_shell || effective_shell[0] == '\0') {
        effective_shell = "/bin/sh";
    }

    snprintf(pty->shell, sizeof(pty->shell), "%s", effective_shell);

    pid = forkpty(&pty->master_fd, NULL, NULL, NULL);
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execl(pty->shell, pty->shell, (char *)NULL);
        _exit(127);
    }

    pty->child_pid = pid;
    pty->running = 1;

    flags = fcntl(pty->master_fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(pty->master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    return 0;
}

ssize_t kterm_pty_read(struct kterm_pty *pty, char *buf, size_t cap) {
    ssize_t nread;

    if (!pty || !buf || cap == 0) {
        return -1;
    }

    if (!pty->running) {
        return 0;
    }

    nread = read(pty->master_fd, buf, cap);
    if (nread > 0) {
        return nread;
    }

    if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        kterm_pty_refresh_child_state(pty);
        return 0;
    }

    kterm_pty_refresh_child_state(pty);
    return 0;
}

ssize_t kterm_pty_write(struct kterm_pty *pty, const char *buf, size_t len) {
    ssize_t nwritten;

    if (!pty || !pty->running) {
        return -1;
    }

    if (!buf || len == 0) {
        return 0;
    }

    nwritten = write(pty->master_fd, buf, len);
    if (nwritten < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }

    if (nwritten < 0) {
        kterm_pty_refresh_child_state(pty);
        return -1;
    }

    return nwritten;
}

int kterm_pty_is_running(struct kterm_pty *pty) {
    if (!pty) {
        return 0;
    }

    kterm_pty_refresh_child_state(pty);
    return pty->running;
}

void kterm_pty_stop(struct kterm_pty *pty) {
    int status = 0;

    if (!pty) {
        return;
    }

    if (pty->child_pid > 0 && pty->running) {
        (void)kill(pty->child_pid, SIGTERM);
        (void)waitpid(pty->child_pid, &status, 0);
    }

    if (pty->master_fd >= 0) {
        (void)close(pty->master_fd);
    }

    pty->running = 0;
    pty->master_fd = -1;
}
