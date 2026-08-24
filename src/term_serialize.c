/*
 * KoboTerminal TSM Screen Serialization
 *
 * Serializes the TSM terminal screen state to plain text for display in the
 * Nickel dialog: walks the character grid via tsm_screen_draw(), stripping
 * colors/attributes down to plain UTF-8 text.
 *
 * Called from ui_dialog.cpp on every PTY data arrival to refresh the dialog
 * body with the current terminal state.
 */

#define _GNU_SOURCE

#include "kterm/term.h"

#include <libtsm.h>

/* ============================================================================
 * Screen serialization
 * ============================================================================ */

/* Accumulator passed through tsm_screen_draw's per-cell callback */
struct kterm_draw_ctx {
    char *out;
    size_t cap;
    size_t pos;
    unsigned int row;
};

static void kterm_draw_put(struct kterm_draw_ctx *ctx, char c) {
    if (ctx->pos + 1 < ctx->cap) {
        ctx->out[ctx->pos++] = c;
    }
}

static void kterm_draw_put_utf8(struct kterm_draw_ctx *ctx, uint32_t cp) {
    if (cp < 0x80) {
        kterm_draw_put(ctx, (char)cp);
    } else if (cp < 0x800) {
        kterm_draw_put(ctx, (char)(0xC0 | (cp >> 6)));
        kterm_draw_put(ctx, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        kterm_draw_put(ctx, (char)(0xE0 | (cp >> 12)));
        kterm_draw_put(ctx, (char)(0x80 | ((cp >> 6) & 0x3F)));
        kterm_draw_put(ctx, (char)(0x80 | (cp & 0x3F)));
    } else {
        kterm_draw_put(ctx, (char)(0xF0 | (cp >> 18)));
        kterm_draw_put(ctx, (char)(0x80 | ((cp >> 12) & 0x3F)));
        kterm_draw_put(ctx, (char)(0x80 | ((cp >> 6) & 0x3F)));
        kterm_draw_put(ctx, (char)(0x80 | (cp & 0x3F)));
    }
}

/* tsm_screen_draw callback: called once per visible cell, in row-major order */
static int kterm_draw_cb(struct tsm_screen *con, uint64_t id, const uint32_t *ch,
                          size_t len, unsigned int width, unsigned int posx,
                          unsigned int posy, const struct tsm_screen_attr *attr,
                          tsm_age_t age, void *data) {
    struct kterm_draw_ctx *ctx = data;

    (void)con;
    (void)id;
    (void)width;
    (void)posx;
    (void)attr;
    (void)age;

    if (posy != ctx->row) {
        kterm_draw_put(ctx, '\n');
        ctx->row = posy;
    }

    if (len == 0) {
        /* Empty/blank cell */
        kterm_draw_put(ctx, ' ');
        return 0;
    }

    /* Only the base codepoint is kept; combining marks (ch[1..]) are dropped
     * since the output is a plain-text approximation, not a faithful replay. */
    kterm_draw_put_utf8(ctx, ch[0]);
    return 0;
}

/*
 * Serialize the TSM screen to plain text.
 *
 * Walks the current screen grid via tsm_screen_draw() and produces a plain
 * UTF-8 text buffer (colors/attributes stripped) suitable for display in the
 * Nickel ConfirmationDialog.
 *
 * Returns the number of bytes written to output (excluding the null
 * terminator), or -1 on error.
 */
static int kterm_serialize_screen(struct tsm_screen *screen, char *output, size_t out_cap) {
    struct kterm_draw_ctx ctx;

    if (!screen || !output || out_cap == 0) {
        return -1;
    }

    ctx.out = output;
    ctx.cap = out_cap;
    ctx.pos = 0;
    ctx.row = 0;

    tsm_screen_draw(screen, kterm_draw_cb, &ctx);

    output[ctx.pos] = '\0';
    return (int)ctx.pos;
}

/* ============================================================================
 * Public API: Serialize terminal output
 * ============================================================================ */

int kterm_term_serialize(char *output, size_t out_cap) {
    struct kterm_term *term;
    struct tsm_screen *screen;

    if (!output || out_cap == 0) {
        return -1;
    }

    output[0] = '\0';

    term = kterm_term_global();
    screen = term ? kterm_term_get_screen(term) : NULL;
    if (!screen) {
        return 0;
    }

    return kterm_serialize_screen(screen, output, out_cap);
}


