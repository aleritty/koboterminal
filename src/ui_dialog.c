/*
 * KoboTerminal UI Dialog Management
 *
 * This file manages the ConfirmationDialog widget lifecycle:
 *   - Creation via Nickel's ConfirmationDialogFactory
 *   - Title and body text updates
 *   - Show/close operations
 *
 * The dialog is created on-demand when kterm_nhook_dialog_show() is called.
 * All Nickel C++ method calls go through function pointers resolved at init.
 */

#define _GNU_SOURCE

#include "kterm/nickelhook.h"
#include "kterm/pty.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Internal state
 * ============================================================================ */

/* Dialog and input field instances (forward-declared in nickelhook.c) */
extern ConfirmationDialog *kterm_dialog;
extern N3ConfirmationTextEditField *kterm_input_field;

/* Whether the terminal dialog is currently visible */
extern int kterm_dialog_visible;

/* ============================================================================
 * Dialog creation and management
 * ============================================================================ */

int kterm_nhook_dialog_show(void) {
    kterm_log("[koboterminal] Dialog show requested\n");

    /* Lazy PTY initialization: start the terminal only when dialog is shown */
    if (!kterm_pty_qt_is_running()) {
        if (kterm_pty_qt_init() != 0) {
            kterm_log("[koboterminal] PTY init failed\n");
            return -1;
        }
        kterm_log("[koboterminal] PTY started\n");
    }

    /* Create dialog if not already created */
    if (!kterm_dialog && ConfirmationDialogFactory_getConfirmationDialog) {
        kterm_dialog = ConfirmationDialogFactory_getConfirmationDialog(NULL);
        if (!kterm_dialog) {
            kterm_log("[koboterminal] Failed to create ConfirmationDialog\n");
            return -1;
        }
        kterm_log("[koboterminal] Dialog created\n");

        /* Configure dialog */
        if (ConfirmationDialog__setTitle) {
            ConfirmationDialog__setTitle(kterm_dialog, "koboterminal");
        }
        if (ConfirmationDialog__setAcceptButtonText) {
            ConfirmationDialog__setAcceptButtonText(kterm_dialog, "Ok");
        }
        if (ConfirmationDialog__setRejectButtonText) {
            ConfirmationDialog__setRejectButtonText(kterm_dialog, "Cancel");
        }
        if (ConfirmationDialog__showCloseButton) {
            ConfirmationDialog__showCloseButton(kterm_dialog, false);
        }
        if (ConfirmationDialog__setRejectOnOutsideTap) {
            ConfirmationDialog__setRejectOnOutsideTap(kterm_dialog, true);
        }
    }

    /* Create input field if not already created */
    if (!kterm_input_field && kterm_dialog && N3ConfirmationTextEditField_ctor2) {
        kterm_input_field = N3ConfirmationTextEditField_ctor2(kterm_input_field, kterm_dialog);
        if (kterm_input_field && N3ConfirmationTextEditField_textEdit) {
            TouchLineEdit *tle = N3ConfirmationTextEditField_textEdit(kterm_input_field);
            if (tle && TouchLineEdit__setPlaceholderText) {
                TouchLineEdit__setPlaceholderText(tle, "Enter command...");
            }
        }
    }

    /* Show dialog */
    if (kterm_dialog && ConfirmationDialog__show) {
        ConfirmationDialog__show(kterm_dialog);
        kterm_dialog_visible = 1;
        kterm_log("[koboterminal] Dialog shown\n");
    }

    return 0;
}




