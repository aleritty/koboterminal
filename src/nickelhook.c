/*
 * KoboTerminal NickelHook Module Entry Point
 *
 * This file defines the nh_info, nh_hook, and nh_dlsym structures that
 * nh_runtime.c's bootstrap constructor uses to resolve symbols and apply
 * hooks before any dialog creation.
 *
 * References:
 *   - pgaskin/NickelHook: https://github.com/pgaskin/NickelHook
 *   - shermp/NickelDBus:  https://github.com/shermp/NickelDBus
 */

#define _GNU_SOURCE

#include "kterm/nickelhook.h"

#include <dlfcn.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

/* ============================================================================
 * Logging
 * ============================================================================ */

#define KTERM_LOG_PATH "/mnt/onboard/.adds/koboterminal/lkoboterminal.log"
#define KTERM_LOG_CONF_PATH "/mnt/onboard/.adds/koboterminal/koboterminal.conf"

typedef enum {
    KTERM_LOG_LEVEL_INFO = 0,
    KTERM_LOG_LEVEL_ERROR = 1,
} kterm_log_level_t;

static int kterm_parse_bool_value(const char *s) {
    if (!s) {
        return 0;
    }

    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }

    if (!*s) {
        return 0;
    }

    if (*s == '1') {
        return 1;
    }

    if (*s == '0') {
        return 0;
    }

    if (strncasecmp(s, "true", 4) == 0 || strncasecmp(s, "yes", 3) == 0 || strncasecmp(s, "on", 2) == 0) {
        return 1;
    }

    return 0;
}

static int kterm_log_is_verbose_enabled(void) {
    FILE *f = fopen(KTERM_LOG_CONF_PATH, "r");
    char line[256];

    if (!f) {
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        char *key;
        char *value;

        while (*s && isspace((unsigned char)*s)) {
            ++s;
        }
        if (!*s || *s == '#') {
            continue;
        }

        key = s;
        value = strchr(s, '=');
        if (!value) {
            if (kterm_parse_bool_value(key)) {
                fclose(f);
                return 1;
            }
            continue;
        }

        *value++ = '\0';

        /* Trim key trailing whitespace */
        {
            char *end = key + strlen(key);
            while (end > key && isspace((unsigned char)end[-1])) {
                *--end = '\0';
            }
        }

        if (strcmp(key, "verbose") == 0 || strcmp(key, "debug") == 0) {
            int enabled = kterm_parse_bool_value(value);
            fclose(f);
            return enabled;
        }
    }

    fclose(f);
    return 0;
}

static void kterm_log_v(kterm_log_level_t level, const char *fmt, va_list args) {
    FILE *f;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[64];
    const char *level_str = level == KTERM_LOG_LEVEL_ERROR ? "ERROR" : "INFO";

    if (level == KTERM_LOG_LEVEL_INFO && !kterm_log_is_verbose_enabled()) {
        return;
    }

    f = fopen(KTERM_LOG_PATH, "a");
    if (!f) {
        if (level == KTERM_LOG_LEVEL_ERROR) {
            fprintf(stderr, "[koboterminal] log open failed (%s): %s\n", KTERM_LOG_PATH, strerror(errno));
            vfprintf(stderr, fmt, args);
        }
        return;
    }

    if (tm_info) {
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(timebuf, sizeof(timebuf), "time-unavailable");
    }

    fprintf(f, "[%s] [%s] ", timebuf, level_str);
    vfprintf(f, fmt, args);
    fflush(f);
    fclose(f);
}

void kterm_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kterm_log_v(KTERM_LOG_LEVEL_INFO, fmt, args);
    va_end(args);
}

void kterm_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kterm_log_v(KTERM_LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}

/* ============================================================================
 * Internal state
 * ============================================================================ */

/* Pointer to the original NickelDialogFactory method (if we hook it) */
ConfirmationDialogFactory_getConfirmationDialog_t
    ConfirmationDialogFactory_getConfirmationDialog = (ConfirmationDialogFactory_getConfirmationDialog_t)NULL;

/* Resolved Nickel C++ method pointers */
ConfirmationDialog__setTitle_t            ConfirmationDialog__setTitle = (ConfirmationDialog__setTitle_t)NULL;
ConfirmationDialog__setText_t             ConfirmationDialog__setText = (ConfirmationDialog__setText_t)NULL;
ConfirmationDialog__showCloseButton_t     ConfirmationDialog__showCloseButton = (ConfirmationDialog__showCloseButton_t)NULL;
ConfirmationDialog__setRejectOnOutsideTap_t ConfirmationDialog__setRejectOnOutsideTap = (ConfirmationDialog__setRejectOnOutsideTap_t)NULL;
ConfirmationDialog__addWidget_t           ConfirmationDialog__addWidget = (ConfirmationDialog__addWidget_t)NULL;
ConfirmationDialog__show_t                ConfirmationDialog__show = (ConfirmationDialog__show_t)NULL;
ConfirmationDialog__close_t               ConfirmationDialog__close = (ConfirmationDialog__close_t)NULL;

N3ConfirmationTextEditField_ctor2_t       N3ConfirmationTextEditField_ctor2 = (N3ConfirmationTextEditField_ctor2_t)NULL;
N3ConfirmationTextEditField_textEdit_t    N3ConfirmationTextEditField_textEdit = (N3ConfirmationTextEditField_textEdit_t)NULL;

TouchLineEdit__setText_t                  TouchLineEdit__setText = (TouchLineEdit__setText_t)NULL;
TouchLineEdit__setFocus_t                 TouchLineEdit__setFocus = (TouchLineEdit__setFocus_t)NULL;

/* Dialog and input field instances (shared across files) */
ConfirmationDialog *kterm_dialog = NULL;
N3ConfirmationTextEditField *kterm_input_field = NULL;

static ConfirmationDialog__close_t kterm_original_ConfirmationDialog_close = NULL;

typedef enum {
    KTERM_NM_ACTION_RESULT_TYPE_SILENT = 0,
} kterm_nm_action_result_type_t;

typedef struct kterm_nm_action_result {
    kterm_nm_action_result_type_t type;
    char *msg;
    int skip;
} kterm_nm_action_result_t;

typedef kterm_nm_action_result_t *(*kterm_nm_action_cmd_output_t)(const char *arg);
typedef kterm_nm_action_result_t *(*kterm_nm_action_result_silent_t)(void);

static kterm_nm_action_cmd_output_t kterm_original_nm_action_cmd_output = NULL;
static kterm_nm_action_result_silent_t kterm_nm_action_result_silent = NULL;

/* PTY and terminal state (forward declarations, defined in pty_qt.c and term_tsm.c) */
extern int kterm_pty_qt_init(void);
extern void kterm_pty_qt_deinit(void);
extern int kterm_pty_qt_write(const char *text, size_t len);
extern int kterm_pty_qt_is_running(void);

/* ============================================================================
 * nh_info: Module metadata
 * ============================================================================ */

static const char kterm_uninstall_file[] = "/mnt/onboard/.adds/koboterminal/uninstall";

static struct nh_info kterm_nh_info = {
    .name            = "koboterminal",
    .desc            = "Terminal emulator integrated into Nickel via native Qt dialogs.",
    .uninstall_flag  = kterm_uninstall_file,
    .uninstall_xflag = NULL,
    /* Boot-loop watchdog window (see nh_runtime.c): seconds Nickel must stay
     * up after our hooks are installed before this boot counts as healthy.
     * Set <= 0 to disable the watchdog entirely. */
    .failsafe_delay  = 20,
};

/* ============================================================================
 * nh_hook: PLT symbol hooks for NickelMenu integration
 *
 * Hooks NickelMenu's cmd_output action dispatcher (nm_action_cmd_output) to
 * intercept our own "koboterminal:*" actions before falling through to the
 * original, and hooks ConfirmationDialog::close to tear down the PTY when
 * the terminal dialog closes.
 * ============================================================================ */

static struct nh_hook kterm_nh_hook[] = {
    {
        .sym = "nm_action_cmd_output",
        .sym_new = "kterm_nm_action_cmd_output",
        .lib = "libnm.so",
        .out = nh_symoutptr(kterm_original_nm_action_cmd_output),
        .desc = "Intercept NickelMenu cmd_output for KoboTerminal",
        .optional = true,
    },
    {
        .sym = "nm_action_cmd_output",
        .sym_new = "kterm_nm_action_cmd_output",
        .lib = "/usr/local/Kobo/libnm.so",
        .out = nh_symoutptr(kterm_original_nm_action_cmd_output),
        .desc = "Intercept NickelMenu cmd_output (/usr/local/Kobo/libnm.so)",
        .optional = true,
    },
    {
        .sym = "nm_action_cmd_output",
        .sym_new = "kterm_nm_action_cmd_output",
        .lib = "libnm.so.1",
        .out = nh_symoutptr(kterm_original_nm_action_cmd_output),
        .desc = "Intercept NickelMenu cmd_output (libnm.so.1)",
        .optional = true,
    },
    {
        .sym = "_ZN18ConfirmationDialog5closeEv",
        .sym_new = "kterm_ConfirmationDialog_close",
        .lib = "libnickel.so.1.0.0",
        .out = nh_symoutptr(kterm_original_ConfirmationDialog_close),
        .desc = "Hook ConfirmationDialog::close to cleanup PTY",
        .optional = true,
    },
    {0},  /* NULL terminator */
};

void kterm_ConfirmationDialog_close(ConfirmationDialog *dlg) {
    kterm_log_info("[koboterminal] Intercepted ConfirmationDialog::close\n");
    if (dlg == kterm_dialog) {
        kterm_log_info("[koboterminal] Terminal dialog closing, deinitializing PTY\n");
        kterm_pty_qt_deinit();
        kterm_nhook_dialog_reset();
    }

    if (kterm_original_ConfirmationDialog_close) {
        kterm_original_ConfirmationDialog_close(dlg);
    }
}

static char *kterm_trim(char *s) {
    char *end;

    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    return s;
}

static const char *kterm_cmd_output_command(char *arg) {
    char *cmd = arg;
    char *timeout_s = kterm_trim(strsep(&cmd, ":"));
    char *end = NULL;
    long timeout;
    char *raw_cmd;
    char *option;

    if (!cmd) {
        return NULL;
    }

    timeout = strtol(timeout_s, &end, 10);
    if (!timeout_s[0] || (end && *end) || timeout <= 0 || timeout >= 10000) {
        return NULL;
    }

    raw_cmd = kterm_trim(cmd);
    if (!raw_cmd[0]) {
        return NULL;
    }

    cmd = raw_cmd;
    option = kterm_trim(strsep(&cmd, ":"));
    if (cmd && !strcmp(option, "quiet")) {
        return kterm_trim(cmd);
    }

    return raw_cmd;
}

static kterm_nm_action_result_t *kterm_nm_silent_result(void) {
    if (!kterm_nm_action_result_silent) {
        *(void **)(&kterm_nm_action_result_silent) = dlsym(RTLD_DEFAULT, "nm_action_result_silent");
    }

    if (kterm_nm_action_result_silent) {
        return kterm_nm_action_result_silent();
    }

    kterm_nm_action_result_t *res = calloc(1, sizeof(*res));
    if (!res) {
        return NULL;
    }

    res->type = KTERM_NM_ACTION_RESULT_TYPE_SILENT;
    return res;
}

static bool kterm_handle_nm_command(const char *command) {
    const char *action;

    if (!command || strncmp(command, "koboterminal:", 13) != 0) {
        return false;
    }

    action = command + 13;
    if (!strcmp(action, "open")) {
        kterm_log_info("[koboterminal] Handling NickelMenu open action\n");
        return kterm_nhook_dialog_show() == 0;
    }

    if (strncmp(action, "cmd:", 4) == 0) {
        const char *pty_command = action + 4;

        while (*pty_command && isspace((unsigned char)*pty_command)) {
            ++pty_command;
        }

        if (!*pty_command) {
            return true;
        }

        if (!kterm_pty_qt_is_running() && kterm_nhook_dialog_show() != 0) {
            return false;
        }

        if (kterm_pty_qt_write(pty_command, strlen(pty_command)) < 0) {
            kterm_log_error("[koboterminal] Failed to write PTY command\n");
            return false;
        }

        (void)kterm_pty_qt_write("\n", 1);
        kterm_log_info("[koboterminal] Sent PTY command: %s\n", pty_command);
        return true;
    }

    kterm_log_error("[koboterminal] Unknown NickelMenu action: %s\n", action);
    return false;
}

__attribute__((visibility("default")))
kterm_nm_action_result_t *kterm_nm_action_cmd_output(const char *arg) {
    char *tmp;
    const char *command;
    bool handled;

    if (!arg || !kterm_original_nm_action_cmd_output) {
        return NULL;
    }

    tmp = strdup(arg);
    if (!tmp) {
        return kterm_original_nm_action_cmd_output(arg);
    }

    command = kterm_cmd_output_command(tmp);
    handled = kterm_handle_nm_command(command);
    free(tmp);

    if (handled) {
        return kterm_nm_silent_result();
    }

    return kterm_original_nm_action_cmd_output(arg);
}

/* ============================================================================
 * nh_dlsym: Symbol resolution for Nickel C++ methods
 *
 * These are the exact mangled C++ symbol names from Nickel's libnickel.so.1.0.0.
 * They correspond to the ConfirmationDialogFactory, ConfirmationDialog,
 * N3ConfirmationTextEditField, and TouchLineEdit classes.
 * ============================================================================ */

static struct nh_dlsym kterm_nh_dlsym[] = {
    /* ConfirmationDialogFactory */
    {
        .name = "_ZN25ConfirmationDialogFactory17getConfirmationDialogEP7QWidget",
        .out  = nh_symoutptr(ConfirmationDialogFactory_getConfirmationDialog),
        .desc = "Create a ConfirmationDialog instance",
    },

    /* ConfirmationDialog methods */
    {
        .name = "_ZN18ConfirmationDialog8setTitleERK7QString",
        .out  = nh_symoutptr(ConfirmationDialog__setTitle),
        .desc = "Set dialog title",
    },
    {
        .name = "_ZN18ConfirmationDialog7setTextERK7QString",
        .out  = nh_symoutptr(ConfirmationDialog__setText),
        .desc = "Set dialog body text",
    },
    {
        .name = "_ZN18ConfirmationDialog13showCloseButtonEb",
        .out  = nh_symoutptr(ConfirmationDialog__showCloseButton),
        .desc = "Show/hide close button",
    },
    {
        .name = "_ZN18ConfirmationDialog20setRejectOnOutsideTapEb",
        .out  = nh_symoutptr(ConfirmationDialog__setRejectOnOutsideTap),
        .desc = "Reject on outside tap",
    },
    {
        .name = "_ZN18ConfirmationDialog9addWidgetEP7QWidget",
        .out  = nh_symoutptr(ConfirmationDialog__addWidget),
        .desc = "Add child widget to dialog",
    },
    {
        .name = "_ZN18ConfirmationDialog4showEv",
        .out  = nh_symoutptr(ConfirmationDialog__show),
        .desc = "Show the dialog",
    },
    {
        .name = "_ZN18ConfirmationDialog5closeEv",
        .out  = nh_symoutptr(ConfirmationDialog__close),
        .desc = "Close the dialog",
    },

    /* N3ConfirmationTextEditField constructor */
    {
        .name = "_ZN27N3ConfirmationTextEditFieldC1EP18ConfirmationDialogS0_",
        .out  = nh_symoutptr(N3ConfirmationTextEditField_ctor2),
        .desc = "Constructor without KeyboardScript",
    },

    /* N3ConfirmationTextEditField::textEdit() */
    {
        .name = "_ZN27N3ConfirmationTextEditField8textEditEv",
        .out  = nh_symoutptr(N3ConfirmationTextEditField_textEdit),
        .desc = "Get TouchLineEdit from text field",
    },

    /* TouchLineEdit methods */
    {
        .name = "_ZN13QLineEdit6setTextERK7QString",
        .out  = nh_symoutptr(TouchLineEdit__setText),
        .desc = "Set line edit text",
    },
    {
        .name = "_ZN13QLineEdit5focusEv",
        .out  = nh_symoutptr(TouchLineEdit__setFocus),
        .desc = "Set focus on line edit",
    },

    {0},  /* NULL terminator */
};

/* ============================================================================
 * NickelHook() macro: constructs the nh structure
 *
 * Symbol resolution happens once, in nh_runtime.c's bootstrap constructor,
 * before hooks are applied — no separate .init pass is needed here.
 * ============================================================================ */

NickelHook(
    .info  = &kterm_nh_info,
    .hook  = kterm_nh_hook,
    .dlsym = kterm_nh_dlsym,
)
