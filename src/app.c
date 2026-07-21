#define _POSIX_C_SOURCE 200809L

#include "kterm/app.h"

#include "kterm/config.h"
#include "kterm/input.h"
#include "kterm/osk.h"
#include "kterm/pty.h"
#include "kterm/renderer.h"
#include "kterm/term.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

static volatile sig_atomic_t kterm_stop_requested = 0;

static void kterm_handle_signal(int signo) {
    (void)signo;
    kterm_stop_requested = 1;
}

static void kterm_install_signal_handlers(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = kterm_handle_signal;
    sigemptyset(&sa.sa_mask);

    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGHUP, &sa, NULL);
    (void)sigaction(SIGQUIT, &sa, NULL);
}

static void kterm_return_to_home(void) {
#ifdef KTERM_TARGET_KOBO
    (void)system("killall -CONT nickel >/dev/null 2>&1 || true");
#endif
}

static void kterm_sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    (void)nanosleep(&ts, NULL);
}

int kterm_app_run(void) {
    struct kterm_config cfg;
    struct kterm_pty pty;
    struct kterm_term term;
    struct kterm_renderer renderer;
    struct kterm_input input;
    struct kterm_osk osk;
    char rx[256];
    static const char *bootstrap_cmd = "printf 'koboterminal app loop online\\n'; uname -s; printf '\\n$ '";
    int exit_code = 0;

    kterm_stop_requested = 0;
    kterm_install_signal_handlers();

    kterm_config_defaults(&cfg);

    if (kterm_pty_start(&pty, "/bin/sh") != 0) {
        fprintf(stderr, "PTY init failed\n");
        kterm_return_to_home();
        return 1;
    }

    if (kterm_term_init(&term, cfg.cols, cfg.rows) != 0) {
        fprintf(stderr, "Terminal state init failed\n");
        kterm_pty_stop(&pty);
        kterm_return_to_home();
        return 1;
    }

    if (kterm_renderer_init(&renderer, &cfg) != 0) {
        fprintf(stderr, "Renderer init failed\n");
        kterm_term_deinit(&term);
        kterm_pty_stop(&pty);
        kterm_return_to_home();
        return 1;
    }

    if (kterm_input_init(&input) != 0) {
        fprintf(stderr, "Input init failed\n");
        kterm_renderer_deinit(&renderer);
        kterm_term_deinit(&term);
        kterm_pty_stop(&pty);
        kterm_return_to_home();
        return 1;
    }

    kterm_osk_init(&osk, cfg.keyboard_auto_open);
    (void)kterm_pty_write(&pty, bootstrap_cmd, strlen(bootstrap_cmd));
    (void)kterm_pty_write(&pty, "\n", 1);

    {
        for (;;) {
            struct kterm_input_event ev;
            ssize_t nread;

            if (kterm_stop_requested) {
                exit_code = 1;
                break;
            }

            if (kterm_input_poll(&input, &ev) != 0) {
                break;
            }

            if (ev.kind == KTERM_INPUT_TEXT) {
                kterm_pty_write(&pty, ev.text, sizeof(ev.text));
            }

            nread = kterm_pty_read(&pty, rx, sizeof(rx));
            if (nread > 0) {
                kterm_term_feed(&term, rx, (size_t)nread);
            }

            if (!kterm_pty_is_running(&pty)) {
                break;
            }

            kterm_renderer_draw(&renderer, &term);
            kterm_sleep_ms(200);
        }
    }

    kterm_input_deinit(&input);
    kterm_renderer_deinit(&renderer);
    kterm_term_deinit(&term);
    kterm_pty_stop(&pty);

    kterm_return_to_home();

    return exit_code;
}
