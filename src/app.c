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
#include <time.h>

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

    kterm_config_defaults(&cfg);

    if (kterm_pty_start(&pty, "/bin/sh") != 0) {
        fprintf(stderr, "PTY init failed\n");
        return 1;
    }

    if (kterm_term_init(&term, cfg.cols, cfg.rows) != 0) {
        fprintf(stderr, "Terminal state init failed\n");
        kterm_pty_stop(&pty);
        return 1;
    }

    if (kterm_renderer_init(&renderer, &cfg) != 0) {
        fprintf(stderr, "Renderer init failed\n");
        kterm_term_deinit(&term);
        kterm_pty_stop(&pty);
        return 1;
    }

    if (kterm_input_init(&input) != 0) {
        fprintf(stderr, "Input init failed\n");
        kterm_renderer_deinit(&renderer);
        kterm_term_deinit(&term);
        kterm_pty_stop(&pty);
        return 1;
    }

    kterm_osk_init(&osk, cfg.keyboard_auto_open);
    (void)kterm_pty_write(&pty, bootstrap_cmd, strlen(bootstrap_cmd));
    (void)kterm_pty_write(&pty, "\n", 1);

    {
        int tick;
        for (tick = 0; tick < 64; ++tick) {
            struct kterm_input_event ev;
            ssize_t nread;

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

            if (tick == 2 && kterm_osk_visible(&osk)) {
                kterm_osk_toggle(&osk);
            }

            kterm_renderer_draw(&renderer, &term);
            kterm_sleep_ms(20);
        }
    }

    kterm_input_deinit(&input);
    kterm_renderer_deinit(&renderer);
    kterm_term_deinit(&term);
    kterm_pty_stop(&pty);

    return 0;
}
