/*
 * KoboTerminal UI Dialog Management
 */

#include "kterm/nickelhook.h"
#include "kterm/pty.h"
#include "kterm/term.h"

#include <QByteArray>
#include <QObject>
#include <QSocketNotifier>
#include <QString>

#include <stdlib.h>

extern ConfirmationDialog *kterm_dialog;
extern N3ConfirmationTextEditField *kterm_input_field;

namespace {

/*
 * Bridges the PTY master fd and the TouchLineEdit's Qt signals into the
 * dialog: PTY output feeds the TSM parser and refreshes the dialog text;
 * Enter on the input field writes the pending line to the PTY. Both paths
 * are signal/slot driven so Nickel's event loop is never blocked.
 */
class KTermBridge : public QObject {
  Q_OBJECT
public:
  void attach(int pty_fd, TouchLineEdit *line_edit) {
    detach();

    notifier_ = new QSocketNotifier(pty_fd, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &KTermBridge::onPtyReadable);

    if (line_edit) {
      QObject *edit = reinterpret_cast<QObject *>(line_edit);
      connect(edit, SIGNAL(textChanged(QString)), this, SLOT(onTextChanged(QString)));
      connect(edit, SIGNAL(returnPressed()), this, SLOT(onReturnPressed()));
    }
    line_edit_ = line_edit;
  }

  void detach() {
    delete notifier_;
    notifier_ = nullptr;
    line_edit_ = nullptr;
    pending_.clear();
  }

private slots:
  void onPtyReadable() {
    char buf[4096];
    ssize_t n;
    bool any = false;

    while ((n = kterm_pty_qt_read(buf, sizeof(buf))) > 0) {
      kterm_term_feed(kterm_term_global(), buf, (size_t)n);
      any = true;
    }

    if (!kterm_pty_qt_is_running()) {
      detach();
    }

    if (any) {
      refresh_text();
    }
  }

  void onTextChanged(const QString &text) { pending_ = text; }

  void onReturnPressed() {
    if (pending_.isEmpty()) {
      return;
    }

    QByteArray utf8 = pending_.toUtf8();
    kterm_pty_qt_write(utf8.constData(), (size_t)utf8.size());
    kterm_pty_qt_write("\n", 1);
    pending_.clear();

    if (line_edit_ && TouchLineEdit__setText) {
      QString empty;
      TouchLineEdit__setText(line_edit_, static_cast<const void *>(&empty));
    }
  }

private:
  void refresh_text() {
    char out[8192];
    int n = kterm_term_serialize(out, sizeof(out));
    if (n < 0 || !ConfirmationDialog__setText) {
      return;
    }

    QString text = QString::fromUtf8(out, n);
    ConfirmationDialog__setText(kterm_dialog, static_cast<const void *>(&text));
  }

  QSocketNotifier *notifier_ = nullptr;
  TouchLineEdit *line_edit_ = nullptr;
  QString pending_;
};

KTermBridge g_bridge;

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

    g_bridge.attach(kterm_pty_qt_get_fd(), line_edit_);

    if (ConfirmationDialog__show) {
      ConfirmationDialog__show(kterm_dialog);
      kterm_log_info("[koboterminal] Dialog shown\n");
    }

    return 0;
  }

  void reset() {
    g_bridge.detach();
    kterm_dialog = NULL;
    kterm_input_field = NULL;
    line_edit_ = NULL;
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

    if (ConfirmationDialog__setTitle) {
      QString title("Terminal");
      ConfirmationDialog__setTitle(kterm_dialog, static_cast<const void *>(&title));
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
    if (kterm_input_field) {
      return 0;
    }
    if (!kterm_dialog) {
      return -1;
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

    line_edit_ = N3ConfirmationTextEditField_textEdit(kterm_input_field);
    if (line_edit_ && TouchLineEdit__setFocus) {
      TouchLineEdit__setFocus(line_edit_);
    }

    return 0;
  }

  TouchLineEdit *line_edit_ = nullptr;
};

KTermDialog g_dialog;

} // namespace

extern "C" int kterm_nhook_dialog_show(void) { return g_dialog.show(); }

extern "C" void kterm_nhook_dialog_reset(void) { g_dialog.reset(); }

#include "ui_dialog.moc"