/*
 * KoboTerminal UI Dialog Management
 */

#include "kterm/nickelhook.h"
#include "kterm/pty.h"

#include <stdlib.h>

extern ConfirmationDialog *kterm_dialog;
extern N3ConfirmationTextEditField *kterm_input_field;
extern int kterm_dialog_visible;

namespace {

class KTermDialog {
public:
  int show() {
    if (!kterm_pty_qt_is_running() && kterm_pty_qt_init() != 0) {
      kterm_log_error("[koboterminal] PTY init failed\n");
      return -1;
    }

    if (ensure_dialog() != 0 || ensure_input() != 0) {
      return -1;
    }

    if (ConfirmationDialog__show) {
      ConfirmationDialog__show(kterm_dialog);
      kterm_dialog_visible = 1;
      kterm_log_info("[koboterminal] Dialog shown\n");
    }

    return 0;
  }

  void reset() {
    kterm_dialog = NULL;
    kterm_input_field = NULL;
    kterm_dialog_visible = 0;
  }

private:
  int ensure_dialog() {
    if (kterm_dialog) {
      return 0;
    }

    if (!ConfirmationDialogFactory_getConfirmationDialog) {
      kterm_log_error("[koboterminal] Missing ConfirmationDialogFactory\n");
      return -1;
    }

    kterm_dialog = ConfirmationDialogFactory_getConfirmationDialog(NULL);
    if (!kterm_dialog) {
      kterm_log_error("[koboterminal] Failed to create ConfirmationDialog\n");
      return -1;
    }

    if (ConfirmationDialog__showCloseButton) {
      ConfirmationDialog__showCloseButton(kterm_dialog, false);
    }
    if (ConfirmationDialog__setRejectOnOutsideTap) {
      ConfirmationDialog__setRejectOnOutsideTap(kterm_dialog, true);
    }

    return 0;
  }

  int ensure_input() {
    if (kterm_input_field || !kterm_dialog) {
      return 0;
    }

    if (!N3ConfirmationTextEditField_ctor2 ||
        !N3ConfirmationTextEditField_textEdit ||
        !ConfirmationDialog__addWidget) {
      kterm_log_error("[koboterminal] Missing text field symbols\n");
      return -1;
    }

    void *storage = calloc(1, 512);
    if (!storage) {
      kterm_log_error("[koboterminal] Failed to allocate text field storage\n");
      return -1;
    }

    kterm_input_field = N3ConfirmationTextEditField_ctor2(
        static_cast<N3ConfirmationTextEditField *>(storage), kterm_dialog);
    if (!kterm_input_field) {
      free(storage);
      kterm_log_error("[koboterminal] Failed to construct input field\n");
      return -1;
    }

    ConfirmationDialog__addWidget(kterm_dialog, kterm_input_field);

    TouchLineEdit *line_edit =
        N3ConfirmationTextEditField_textEdit(kterm_input_field);
    if (line_edit && TouchLineEdit__setFocus) {
      TouchLineEdit__setFocus(line_edit);
    }

    return 0;
  }
};

KTermDialog g_dialog;

} // namespace

extern "C" int kterm_nhook_dialog_show(void) { return g_dialog.show(); }

extern "C" void kterm_nhook_dialog_reset(void) { g_dialog.reset(); }