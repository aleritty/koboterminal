#include "kterm/renderer.h"

#ifdef KTERM_USE_REAL_FBINK
#include <fbink.h>
#endif

#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef KTERM_USE_REAL_FBINK
static int fbink_fbfd = -1;
static FBInkConfig fbink_cfg;
static FBInkOTConfig fbink_ot_cfg;
static bool fbink_ready = false;
static bool fbink_font_ready = false;
static bool fbink_trace_frames = false;
static char fbink_last_window[384];
static bool fbink_has_drawn = false;

static void kterm_renderer_show_startup_banner(void) {
    FBInkConfig splash_cfg;
    FBInkOTConfig splash_ot;

    if (!fbink_ready) {
        return;
    }

    splash_cfg = fbink_cfg;
    splash_ot = fbink_ot_cfg;

    splash_cfg.is_cleared = true;
    splash_cfg.row = 0;
    splash_cfg.col = 0;
    splash_cfg.is_centered = false;
    splash_cfg.no_refresh = false;

    splash_ot.margins.top = 0;
    splash_ot.margins.left = 0;
    splash_ot.margins.right = 0;
    splash_ot.margins.bottom = 0;
    splash_ot.is_centered = true;
    splash_ot.size_px = 24;

    (void)fbink_cls(fbink_fbfd, &splash_cfg, NULL, false);
    (void)fbink_print_ot(fbink_fbfd, "koboterminal", &splash_ot, &splash_cfg, NULL);
}

#define KTERM_WINDOW_INNER_WIDTH 38
#define KTERM_WINDOW_OUTER_WIDTH (KTERM_WINDOW_INNER_WIDTH + 2)

static void kterm_append(char *buffer, size_t cap, size_t *used, const char *text) {
    size_t text_len;

    if (!buffer || !used || !text || *used >= cap) {
        return;
    }

    text_len = strlen(text);
    if (text_len >= cap - *used) {
        text_len = cap - *used - 1;
    }

    memcpy(buffer + *used, text, text_len);
    *used += text_len;
    buffer[*used] = '\0';
}

static void kterm_append_border(char *buffer, size_t cap, size_t *used, char left, char fill, char right) {
    size_t i;
    char line[KTERM_WINDOW_OUTER_WIDTH + 4];

    if (!buffer || !used || *used >= cap) {
        return;
    }

    line[0] = left;
    for (i = 0; i < KTERM_WINDOW_INNER_WIDTH; ++i) {
        line[i + 1] = fill;
    }
    line[KTERM_WINDOW_INNER_WIDTH + 1] = right;
    line[KTERM_WINDOW_INNER_WIDTH + 2] = '\n';
    line[KTERM_WINDOW_INNER_WIDTH + 3] = '\0';
    kterm_append(buffer, cap, used, line);
}

static void kterm_sanitize_preview(const char *input, char *output, size_t cap) {
    size_t used = 0;

    if (!output || cap == 0) {
        return;
    }

    output[0] = '\0';
    if (!input) {
        return;
    }

    while (*input && used + 1 < cap) {
        unsigned char ch = (unsigned char) *input++;

        if (ch == '\n') {
            output[used++] = '\n';
            continue;
        }

        if (ch == '\r' || ch == '\t' || ch == ' ') {
            if (used > 0 && output[used - 1] != ' ' && output[used - 1] != '\n') {
                output[used++] = ' ';
            }
            continue;
        }

        if (!isprint(ch)) {
            continue;
        }

        if (used + 1 >= cap) {
            break;
        }

        output[used++] = (char) ch;
    }

    while (used > 0 && output[used - 1] == ' ') {
        --used;
    }

    output[used] = '\0';
}

static void kterm_append_window_line(char *buffer, size_t cap, size_t *used, const char *text) {
    size_t line_len = 0;
    char line[KTERM_WINDOW_OUTER_WIDTH + 4];

    if (!buffer || !used || !text || *used >= cap) {
        return;
    }

    line[0] = '|';
    while (line_len < KTERM_WINDOW_INNER_WIDTH) {
        char ch = text[line_len];

        if (ch == '\0' || ch == '\n') {
            break;
        }

        line[line_len + 1] = ch;
        ++line_len;
    }

    while (line_len < KTERM_WINDOW_INNER_WIDTH) {
        line[line_len + 1] = ' ';
        ++line_len;
    }

    line[KTERM_WINDOW_INNER_WIDTH + 1] = '|';
    line[KTERM_WINDOW_INNER_WIDTH + 2] = '\n';
    line[KTERM_WINDOW_INNER_WIDTH + 3] = '\0';
    kterm_append(buffer, cap, used, line);
}

static void kterm_build_window(int frame_index, const struct kterm_term *term, const char *preview, char *output, size_t cap) {
    char clean_preview[96];
    char title_line[64];
    char frame_line[64];
    size_t used = 0;
    const char *line_start;
    const char *line_end;

    if (!output || cap == 0) {
        return;
    }

    output[0] = '\0';
    kterm_sanitize_preview(preview, clean_preview, sizeof(clean_preview));

    kterm_append_border(output, cap, &used, '+', '-', '+');

    snprintf(title_line,
             sizeof(title_line),
             "| %-*.*s|\n",
             KTERM_WINDOW_INNER_WIDTH - 1,
             KTERM_WINDOW_INNER_WIDTH - 1,
             "KoboTerminal");
    kterm_append(output, cap, &used, title_line);

    kterm_append_border(output, cap, &used, '|', '-', '|');

    snprintf(frame_line,
             sizeof(frame_line),
             "| frame %-2d  %d x %d%-*s|\n",
             frame_index,
             term->cols,
             term->rows,
             (int) (KTERM_WINDOW_INNER_WIDTH - 16),
             "");
    kterm_append(output, cap, &used, frame_line);

    if (clean_preview[0] == '\0') {
        kterm_append_window_line(output, cap, &used, "waiting for shell output");
    } else {
        line_start = clean_preview;
        while (*line_start != '\0') {
            char line[KTERM_WINDOW_INNER_WIDTH + 1];
            size_t line_len = 0;

            line_end = line_start;
            while (*line_end != '\0' && *line_end != '\n' && line_len < KTERM_WINDOW_INNER_WIDTH) {
                line[line_len++] = *line_end++;
            }
            line[line_len] = '\0';
            kterm_append_window_line(output, cap, &used, line);

            while (*line_end != '\0' && *line_end != '\n') {
                ++line_end;
            }
            if (*line_end == '\n') {
                ++line_end;
            }
            line_start = line_end;
        }
    }

    kterm_append_border(output, cap, &used, '+', '-', '+');
}
#endif

int kterm_renderer_init(struct kterm_renderer *renderer, const struct kterm_config *cfg) {
    renderer->frame_index = 0;

#ifdef KTERM_USE_REAL_FBINK
    const char *font_path;

    (void)cfg;
    memset(&fbink_cfg, 0, sizeof(fbink_cfg));
    memset(&fbink_ot_cfg, 0, sizeof(fbink_ot_cfg));
    fbink_cfg.is_quiet = true;
    fbink_cfg.no_viewport = false;
    fbink_cfg.is_inverted = false;
    fbink_cfg.is_cleared = true;
    fbink_cfg.row = 0;
    fbink_cfg.col = 0;
    fbink_cfg.is_centered = false;
    fbink_cfg.is_padded = false;
    fbink_cfg.is_rpadded = false;
    fbink_cfg.is_overlay = false;
    fbink_cfg.is_bgless = false;
    fbink_cfg.is_fgless = false;
    fbink_cfg.no_refresh = false;
    fbink_cfg.fg_color = FG_GRAY2;
    fbink_cfg.bg_color = BG_BLACK;

    fbink_ot_cfg.margins.top = 0;
    fbink_ot_cfg.margins.left = 12;
    fbink_ot_cfg.margins.right = 12;
    fbink_ot_cfg.margins.bottom = 0;
    fbink_ot_cfg.style = FNT_REGULAR;
    fbink_ot_cfg.size_px = 24;
    fbink_ot_cfg.is_centered = false;
    fbink_ot_cfg.padding = NO_PADDING;
    fbink_ot_cfg.is_formatted = false;
    fbink_ot_cfg.compute_only = false;
    fbink_ot_cfg.no_truncation = false;

    fbink_fbfd = fbink_open();
    if (fbink_fbfd < 0) {
        fprintf(stderr, "FBInk open failed\n");
        return -1;
    }

    if (fbink_init(fbink_fbfd, &fbink_cfg) != EXIT_SUCCESS) {
        fprintf(stderr, "FBInk init failed\n");
        (void)fbink_close(fbink_fbfd);
        fbink_fbfd = -1;
        return -1;
    }

    font_path = getenv("KTERM_FONT_PATH");
    if (!font_path || font_path[0] == '\0') {
        font_path = "PxPlus_IBM_VGA8.ttf";
    }

    fbink_trace_frames = getenv("KTERM_TRACE_FRAMES") != NULL;

    if (fbink_add_ot_font(font_path, FNT_REGULAR) != EXIT_SUCCESS) {
        fprintf(stderr, "FBInk font load failed: %s\n", font_path);
        (void)fbink_close(fbink_fbfd);
        fbink_fbfd = -1;
        return -1;
    }

    (void)fbink_set_fg_pen_rgba(0x9f, 0xff, 0x9f, 0xff, false, true);
    (void)fbink_set_bg_pen_rgba(0x00, 0x00, 0x00, 0xff, false, true);

    fbink_font_ready = true;

    fbink_ready = true;
    fbink_has_drawn = false;
    kterm_renderer_show_startup_banner();
    fprintf(stderr, "KoboTerminal framebuffer renderer active\n");
#else
    (void)cfg;
#endif

    return 0;
}

void kterm_renderer_draw(struct kterm_renderer *renderer, const struct kterm_term *term) {
    const char *preview = kterm_term_preview(term);
    char window[384];

    renderer->frame_index += 1;

#ifdef KTERM_USE_REAL_FBINK
    if (!fbink_ready) {
        return;
    }

    if (fbink_trace_frames) {
        fprintf(stderr,
                "[frame=%d cols=%d rows=%d bytes=%zu preview=\"%s\"]\n",
                renderer->frame_index,
                term->cols,
                term->rows,
                term->bytes_fed,
                preview);
    }

    kterm_build_window(renderer->frame_index, term, preview, window, sizeof(window));

    if (fbink_has_drawn && strcmp(window, fbink_last_window) == 0) {
        return;
    }

    fbink_cfg.is_cleared = true;
    fbink_ot_cfg.size_px = 24;
    if (fbink_print_ot(fbink_fbfd, window, &fbink_ot_cfg, &fbink_cfg, NULL) >= 0) {
        snprintf(fbink_last_window, sizeof(fbink_last_window), "%s", window);
        fbink_has_drawn = true;
    }
#else
    printf("[frame=%d cols=%d rows=%d bytes=%zu preview=\"%s\"]\n",
           renderer->frame_index,
           term->cols,
           term->rows,
           term->bytes_fed,
           preview);
#endif
}

void kterm_renderer_deinit(struct kterm_renderer *renderer) {
    (void)renderer;

#ifdef KTERM_USE_REAL_FBINK
    if (fbink_font_ready) {
        (void)fbink_free_ot_fonts();
        fbink_font_ready = false;
    }

    if (fbink_fbfd >= 0) {
        (void)fbink_close(fbink_fbfd);
        fbink_fbfd = -1;
    }

    fbink_ready = false;
    fbink_has_drawn = false;
#endif
}
