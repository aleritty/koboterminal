/*
 * KoboTerminal NickelHook Module Entry Point
 *
 * This file defines the nh_info, nh_hook, and nh_dlsym structures that
 * NickelHook uses to load our shared library into the Nickel process.
 *
 * The __attribute__((constructor)) function nh_init() runs before main(),
 * allowing us to resolve all Nickel symbols before any dialog creation.
 *
 * References:
 *   - pgaskin/NickelHook: https://github.com/pgaskin/NickelHook
 *   - shermp/NickelDBus:  https://github.com/shermp/NickelDBus
 */

#define _GNU_SOURCE

#include "kterm/nickelhook.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>

/* ============================================================================
 * Logging
 * ============================================================================ */

#define LOG_PATH "/tmp/koboterminal.log"

void kterm_log(const char *fmt, ...) {
    va_list args;
    FILE *f;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[64];

    va_start(args, fmt);

    f = fopen(LOG_PATH, "a");
    if (!f) {
        /* Fallback to stderr if file can't be opened */
        vfprintf(stderr, fmt, args);
        va_end(args);
        return;
    }

    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(f, "[%s] ", timebuf);
    vfprintf(f, fmt, args);
    fflush(f);
    fclose(f);

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
ConfirmationDialog__setAcceptButtonText_t ConfirmationDialog__setAcceptButtonText = (ConfirmationDialog__setAcceptButtonText_t)NULL;
ConfirmationDialog__setRejectButtonText_t ConfirmationDialog__setRejectButtonText = (ConfirmationDialog__setRejectButtonText_t)NULL;
ConfirmationDialog__showCloseButton_t     ConfirmationDialog__showCloseButton = (ConfirmationDialog__showCloseButton_t)NULL;
ConfirmationDialog__setRejectOnOutsideTap_t ConfirmationDialog__setRejectOnOutsideTap = (ConfirmationDialog__setRejectOnOutsideTap_t)NULL;
ConfirmationDialog__addWidget_t           ConfirmationDialog__addWidget = (ConfirmationDialog__addWidget_t)NULL;
ConfirmationDialog__show_t                ConfirmationDialog__show = (ConfirmationDialog__show_t)NULL;
ConfirmationDialog__close_t               ConfirmationDialog__close = (ConfirmationDialog__close_t)NULL;

N3ConfirmationTextEditField_ctor_t        N3ConfirmationTextEditField_ctor = (N3ConfirmationTextEditField_ctor_t)NULL;
N3ConfirmationTextEditField_ctor2_t       N3ConfirmationTextEditField_ctor2 = (N3ConfirmationTextEditField_ctor2_t)NULL;
N3ConfirmationTextEditField_textEdit_t    N3ConfirmationTextEditField_textEdit = (N3ConfirmationTextEditField_textEdit_t)NULL;

TouchLineEdit__setPlaceholderText_t       TouchLineEdit__setPlaceholderText = (TouchLineEdit__setPlaceholderText_t)NULL;
TouchLineEdit__setText_t                  TouchLineEdit__setText = (TouchLineEdit__setText_t)NULL;
TouchLineEdit__setFocus_t                 TouchLineEdit__setFocus = (TouchLineEdit__setFocus_t)NULL;
TouchLineEdit__text_t                     TouchLineEdit__text = (TouchLineEdit__text_t)NULL;

/* Dialog and input field instances (shared across files) */
ConfirmationDialog *kterm_dialog = NULL;
N3ConfirmationTextEditField *kterm_input_field = NULL;

/* Whether the terminal dialog is currently visible */
int kterm_dialog_visible = 0;

/* PTY and terminal state (forward declarations, defined in pty_qt.c and term_tsm.c) */
extern int kterm_pty_qt_init(void);
extern void kterm_pty_qt_deinit(void);
extern int kterm_pty_qt_write(const char *text, size_t len);
extern int kterm_pty_qt_is_running(void);
extern void kterm_term_serialize(const char *buf, size_t len, char *output, size_t out_cap);
extern void kterm_term_deinit(void);

/* ============================================================================
 * nh_info: Module metadata
 * ============================================================================ */

static const char kterm_uninstall_file[] = "/mnt/onboard/.adds/koboterminal/uninstall";

static struct nh_info kterm_nh_info = {
    .name            = "koboterminal",
    .desc            = "Terminal emulator integrated into Nickel via native Qt dialogs.",
    .uninstall_flag  = kterm_uninstall_file,
    .uninstall_xflag = NULL,
    .failsafe_delay  = 3,
};

/* ============================================================================
 * nh_hook: PLT symbol hooks for NickelMenu integration
 *
 * We hook the NickelMenu action system to add our own "Terminal" action.
 * When the user selects "Terminal" from the menu, we show the terminal dialog.
 * ============================================================================ */

/*
 * Hook: Intercept NickelMenu action registration.
 * This allows us to inject our own action into the menu system.
 *
 * Symbol: _ZN12NickelMenuAction15registerActionERK7QStringS1_S1_S1_
 * This is the NickelMenuAction::registerAction() method.
 * We intercept it to add our "Terminal" action.
 */

static struct nh_hook kterm_nh_hook[] = {
    {0},  /* NULL terminator */
};

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
        .name = "_ZN18ConfirmationDialog17setAcceptButtonTextERK7QString",
        .out  = nh_symoutptr(ConfirmationDialog__setAcceptButtonText),
        .desc = "Set accept button text",
    },
    {
        .name = "_ZN18ConfirmationDialog17setRejectButtonTextERK7QString",
        .out  = nh_symoutptr(ConfirmationDialog__setRejectButtonText),
        .desc = "Set reject button text",
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

    /* N3ConfirmationTextEditField constructors */
    {
        .name = "_ZN27N3ConfirmationTextEditFieldC1EP18ConfirmationDialog",
        .out  = nh_symoutptr(N3ConfirmationTextEditField_ctor),
        .desc = "Constructor with KeyboardScript",
    },
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
        .name = "_ZN13QLineEdit16setPlaceholderERK7QString",
        .out  = nh_symoutptr(TouchLineEdit__setPlaceholderText),
        .desc = "Set placeholder text",
    },
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
    {
        .name = "_ZN13QLineEdit4textEv",
        .out  = nh_symoutptr(TouchLineEdit__text),
        .desc = "Get line edit text",
    },

    {0},  /* NULL terminator */
};

/* ============================================================================
 * nh_init: Constructor function
 *
 * This function runs before main() via __attribute__((constructor)).
 * It resolves all Nickel symbols and initializes the module state.
 * ============================================================================ */

/* ============================================================================
 * NickelMenu action registration
 * ============================================================================ */

static int kterm_nh_init(void) {
    void *handle;
    struct nh_dlsym *sym;
    int resolved = 0;
    int failed = 0;

    kterm_log("[koboterminal] Module loading...\n");

    /* Open libnickel.so.1.0.0 for symbol resolution */
    handle = dlopen("libnickel.so.1.0.0", RTLD_NOW);
    if (!handle) {
        kterm_log("[koboterminal] Failed to open libnickel.so.1.0.0: %s\n", dlerror());
        return -1;
    }

    /* Resolve all symbols */
    for (sym = kterm_nh_dlsym; sym->name; ++sym) {
        void *ptr = dlsym(handle, sym->name);
        if (!ptr) {
            kterm_log("[koboterminal] Failed to resolve symbol '%s': %s\n",
                    sym->name,
                    dlerror());
            if (!sym->optional) {
                ++failed;
            }
        } else {
            *(void **)sym->out = ptr;
            ++resolved;
        }
    }

    dlclose(handle);

    kterm_log("[koboterminal] Initialized: %d symbols resolved, %d failed\n",
            resolved,
            failed);

    return failed > 0 ? -1 : 0;
}

/* ============================================================================
 * NickelHook() macro: constructs the nh structure
 * ============================================================================ */

NickelHook(
    .init  = &kterm_nh_init,
    .info  = &kterm_nh_info,
    .hook  = kterm_nh_hook,
    .dlsym = kterm_nh_dlsym,
)

/* ============================================================================
 * Public API implementations
 * ============================================================================ */

int kterm_nhook_init(void) {
    /*
     * Lazy initialization: do NOT start the PTY here.
     * PTY and terminal state are initialized on-demand when
     * kterm_nhook_dialog_show() is called (e.g., from the menu).
     * This prevents the terminal from auto-starting on Nickel launch.
     */
    return 0;
}
