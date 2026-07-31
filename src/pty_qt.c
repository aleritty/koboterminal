/*
 * KoboTerminal PTY Backend (Qt Integration)
 *
 * This file provides PTY management integrated with Nickel's Qt event loop.
 * Instead of polling in a standalone loop, we use QSocketNotifier to
 * receive notifications when the PTY master FD has data available.
 *
 * The PTY lifecycle:
 *   1. forkpty() creates a PTY pair
 *   2. Child process execs /bin/sh on the slave side
 *   3. Parent (this module) registers master FD with QSocketNotifier
 *   4. On data available: read from master, feed to TSM, update dialog
 *   5. On user input: write to master FD
 *
 * Note: QSocketNotifier is declared here but the actual Qt integration
 * (signal connections, slot implementations) lives in ui_signals.c.
 * This file focuses on PTY management.
 */

#define _GNU_SOURCE

#include "kterm/pty.h"
#include "kterm/nickelhook.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ============================================================================
 * Internal state
 * ============================================================================ */

/* PTY file descriptor and child PID */
static int kterm_pty_master_fd = -1;
static pid_t kterm_pty_child_pid = 0;
static int kterm_pty_running = 0;

/* Shell path */
static const char *kterm_pty_shell = "/bin/sh";

/* ============================================================================
 * PTY lifecycle
 * ============================================================================ */

int kterm_pty_qt_init(void) {
    pid_t pid;
    int flags;

    /* Reset state */
    kterm_pty_master_fd = -1;
    kterm_pty_child_pid = 0;
    kterm_pty_running = 0;

    /* Create PTY pair and fork child */
    pid = forkpty(&kterm_pty_master_fd, NULL, NULL, NULL);
    if (pid < 0) {
        kterm_log("[koboterminal] forkpty() failed: %s\n", strerror(errno));
        return -1;
    }
    kterm_log("[koboterminal] forkpty() succeeded, child_pid=%d\n", pid);

    if (pid == 0) {
        /* Child process: exec shell on slave side */
        /* Set environment for terminal emulation */
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "xterm-256color", 1);

        /* Execute shell */
        execl(kterm_pty_shell, kterm_pty_shell, (char *)NULL);
        _exit(127);
    }

    /* Parent process */
    kterm_pty_child_pid = pid;
    kterm_pty_running = 1;

    /* Set master FD to non-blocking (for QSocketNotifier compatibility) */
    flags = fcntl(kterm_pty_master_fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(kterm_pty_master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    kterm_log("[koboterminal] PTY initialized: master_fd=%d, child_pid=%d\n",
            kterm_pty_master_fd,
            kterm_pty_child_pid);

    return 0;
}

void kterm_pty_qt_deinit(void) {
    if (kterm_pty_child_pid > 0 && kterm_pty_running) {
        /* Send SIGHUP to process group, fallback to SIGTERM */
        kill(kterm_pty_child_pid, SIGHUP);
        usleep(100000);  /* 100ms grace period */
        kill(kterm_pty_child_pid, SIGTERM);
    }

    /* Wait for child to exit */
    if (kterm_pty_child_pid > 0) {
        int status;
        waitpid(kterm_pty_child_pid, &status, 0);
        kterm_pty_child_pid = 0;
    }

    kterm_pty_running = 0;

    /* Close master FD */
    if (kterm_pty_master_fd >= 0) {
        close(kterm_pty_master_fd);
        kterm_pty_master_fd = -1;
    }

    kterm_log("[koboterminal] PTY deinitialized\n");
}

ssize_t kterm_pty_qt_read(char *buf, size_t cap) {
    ssize_t nread;

    if (!buf || cap == 0 || !kterm_pty_running) {
        return 0;
    }

    nread = read(kterm_pty_master_fd, buf, cap);
    if (nread > 0) {
        return nread;
    }

    /* Handle EAGAIN/EWOULDBLOCK/EINTR (non-blocking FD) */
    if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }

    /* Check child state on error */
    if (nread < 0) {
        int status;
        pid_t ret = waitpid(kterm_pty_child_pid, &status, WNOHANG);
        if (ret == kterm_pty_child_pid) {
            kterm_pty_running = 0;
        }
    }

    return 0;
}

int kterm_pty_qt_write(const char *text, size_t len) {
    ssize_t nwritten;

    if (!text || len == 0 || !kterm_pty_running) {
        return -1;
    }

    nwritten = write(kterm_pty_master_fd, text, len);
    if (nwritten < 0) {
        fprintf(stderr, "[koboterminal] PTY write failed: %s\n", strerror(errno));
        return -1;
    }

    return (int)nwritten;
}

int kterm_pty_qt_is_running(void) {
    if (!kterm_pty_running) {
        return 0;
    }

    /* Check child state via waitpid */
    int status;
    pid_t ret = waitpid(kterm_pty_child_pid, &status, WNOHANG);
    if (ret == kterm_pty_child_pid) {
        kterm_pty_running = 0;
        return 0;
    }

    return 1;
}
