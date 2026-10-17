#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>

#include <NickelHook.h>

#define KT_FIFO "/tmp/koboterminal.fifo"

// Nickel types we only use through their Qt base classes.
typedef QDialog ConfirmationDialog;
typedef QWidget N3ConfirmationTextEditField;

// Signatures and the text-edit-field recipe come from NickelDBus (NDBCfmDlg.cc).
static void (*ConfirmationDialogFactory_showOKDialog)(QString const& title, QString const& body);
static ConfirmationDialog *(*ConfirmationDialogFactory_showTextEditDialog)(QString const& title);
static void (*ConfirmationDialog__setTitle)(ConfirmationDialog *_this, QString const&);
static void (*ConfirmationDialog__setAcceptButtonText)(ConfirmationDialog *_this, QString const&);
static void (*ConfirmationDialog__setRejectButtonText)(ConfirmationDialog *_this, QString const&);
static void (*ConfirmationDialog__showCloseButton)(ConfirmationDialog *_this, bool);
static void (*ConfirmationDialog__setRejectOnOutsideTap)(ConfirmationDialog *_this, bool);
static void (*ConfirmationDialog__addWidget)(ConfirmationDialog *_this, QWidget *);
static N3ConfirmationTextEditField *(*N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS)(N3ConfirmationTextEditField *_this, ConfirmationDialog *, int keyboardScript);
static N3ConfirmationTextEditField *(*N3ConfirmationTextEditField__N3ConfirmationTextEditField)(N3ConfirmationTextEditField *_this, ConfirmationDialog *); // fw 4.6
static QLineEdit *(*N3ConfirmationTextEditField__textEdit)(N3ConfirmationTextEditField *_this);

static QPointer<ConfirmationDialog> kt_dlg;

// The shell gets its own session, so closing the terminal kills everything it
// started (a forgotten `ping` or busy loop would otherwise drain the battery).
// Output is coalesced and capped: e-ink refreshes at most 4x/s, and a flood
// like `cat /dev/zero` can't eat Nickel's memory or GUI thread.
class KtShell : public QProcess {
public:
    QByteArray buf;
    bool truncated = false;
    QTimer flush;
    pid_t pgid = 0; // kept after sh exits, so its background jobs still get killed
    explicit KtShell(QObject *parent) : QProcess(parent) {
        connect(this, &QProcess::started, [this]() { pgid = pid(); });
        flush.setSingleShot(true);
        flush.setInterval(250);
        connect(this, &QProcess::readyRead, [this]() {
            buf += readAll();
            if (buf.size() > 16384) {
                buf = buf.right(16384);
                truncated = true;
            }
            if (!flush.isActive())
                flush.start();
        });
    }
    ~KtShell() {
        disconnect(); // finished()/readyRead must not reach the half-destroyed dialog
        if (pgid > 0)
            ::kill(-pgid, SIGKILL);
    }
protected:
    void setupChildProcess() { setsid(); }
};

static void kt_open() {
    if (kt_dlg)
        return;
    if (!ConfirmationDialogFactory_showTextEditDialog || !ConfirmationDialog__setTitle || !ConfirmationDialog__setAcceptButtonText ||
        !ConfirmationDialog__setRejectButtonText || !ConfirmationDialog__showCloseButton || !ConfirmationDialog__setRejectOnOutsideTap ||
        !ConfirmationDialog__addWidget || !N3ConfirmationTextEditField__textEdit ||
        !(N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS || N3ConfirmationTextEditField__N3ConfirmationTextEditField)) {
        ConfirmationDialogFactory_showOKDialog(QStringLiteral("KoboTerminal"), QStringLiteral("Unsupported firmware: missing Nickel symbols."));
        return;
    }

    ConfirmationDialog *d = ConfirmationDialogFactory_showTextEditDialog(QString());
    if (!d)
        return;
    d->hide();
    kt_dlg = d;

    // Nickel's field brings up its on-screen keyboard. The real object size is
    // unknown, so over-allocate like NickelDBus does; it is parented to the dialog.
    N3ConfirmationTextEditField *tef = reinterpret_cast<N3ConfirmationTextEditField*>(calloc(1, 256));
    if (N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS)
        N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS(tef, d, 1);
    else
        N3ConfirmationTextEditField__N3ConfirmationTextEditField(tef, d);
    QLineEdit *tle = N3ConfirmationTextEditField__textEdit(tef);
    if (!tle) {
        nh_log("textEdit() returned null");
        d->deleteLater();
        return;
    }
    // Keyboard "Go" behaves like the Run button.
    if (!QObject::connect(tef, SIGNAL(commitRequested()), d, SIGNAL(accepted())))
        nh_log("could not connect commitRequested");

    QPlainTextEdit *out = new QPlainTextEdit();
    out->setReadOnly(true);
    out->setFocusPolicy(Qt::NoFocus);
    out->setMaximumBlockCount(1000);
    // ponytail: sizes are screen fractions, tune on device if text is too small/big.
    QSize scr = QGuiApplication::primaryScreen()->size();
    QFont mono(QStringLiteral("Courier"));
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPixelSize(scr.width() / 40); // ~40 columns wide
    out->setFont(mono);
    out->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    out->setMinimumSize(scr.width() * 9 / 10, scr.height() * 2 / 5);
    out->setStyleSheet(QStringLiteral("QPlainTextEdit { background: white; color: black; border: 2px solid black; }"));
    ConfirmationDialog__addWidget(d, out);

    ConfirmationDialog__setTitle(d, QStringLiteral("KoboTerminal"));
    ConfirmationDialog__setAcceptButtonText(d, QStringLiteral("Run"));
    ConfirmationDialog__setRejectButtonText(d, QStringLiteral("Close"));
    ConfirmationDialog__setRejectOnOutsideTap(d, false);
    ConfirmationDialog__showCloseButton(d, true);

    // One persistent shell, so cd/exports carry over between commands.
    KtShell *sh = new KtShell(d);
    sh->setProcessChannelMode(QProcess::MergedChannels);
    sh->setWorkingDirectory(QStringLiteral("/")); // never hold /mnt/onboard busy (USB sharing)
    auto append = [out](QString const& s) {
        if (out->document()->characterCount() > 100000) // maxBlockCount can't cap one endless line
            out->clear();
        out->moveCursor(QTextCursor::End);
        out->insertPlainText(s);
        out->ensureCursorVisible();
    };
    // Context `out`: auto-disconnects if Nickel destroys the dialog first.
    QObject::connect(&sh->flush, &QTimer::timeout, out, [sh, append]() {
        if (sh->truncated)
            append(QStringLiteral("[...]\n"));
        append(QString::fromUtf8(sh->buf));
        sh->buf.clear();
        sh->truncated = false;
    });
    QObject::connect(sh, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), d, &QDialog::reject);

    QObject::connect(d, &QDialog::accepted, [d, tle, sh, append]() {
        if (!d->isVisible()) // in case this firmware's Run button calls accept()
            d->open();
        QString cmd = tle->text();
        tle->clear();
        append(QStringLiteral("$ ") + cmd + QStringLiteral("\n"));
        sh->write((cmd + QStringLiteral("\n")).toUtf8());
    });
    QObject::connect(d, &QDialog::rejected, d, &QObject::deleteLater); // ~KtShell kills the shell

    sh->start(QStringLiteral("/bin/sh"));
    d->setModal(true);
    d->open();
}

// NickelMenu writes a byte to the FIFO; the notifier fires on Nickel's event
// loop, so no polling and no threads. O_RDWR keeps a writer open, so the FIFO
// never hits EOF (which would make the notifier spin).
static int kt_init() {
    unlink(KT_FIFO);
    if (mkfifo(KT_FIFO, 0600)) {
        nh_log("mkfifo %s: %m", KT_FIFO);
        return -1;
    }
    int fd = open(KT_FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        nh_log("open %s: %m", KT_FIFO);
        return -1;
    }
    // init runs inside dlopen, on whatever thread Qt loaded the plugin from.
    // Widgets may only be touched on the GUI thread, so put the notifier there.
    if (!QCoreApplication::instance()) {
        nh_log("no QCoreApplication");
        close(fd);
        return -1;
    }
    QSocketNotifier *n = new QSocketNotifier(fd, QSocketNotifier::Read);
    n->moveToThread(QCoreApplication::instance()->thread());
    QObject::connect(n, &QSocketNotifier::activated, [](int fd) {
        char buf[64];
        while (read(fd, buf, sizeof(buf)) > 0);
        kt_open();
    });
    nh_log("listening on %s", KT_FIFO);
    return 0;
}

static struct nh_info KoboTerminal = {
    .name            = "KoboTerminal",
    .desc            = "Terminal dialog opened from NickelMenu",
    .uninstall_flag  = "/mnt/onboard/koboterminal_uninstall",
    .uninstall_xflag = "/mnt/onboard/.adds/nm/koboterminal",
};

static struct nh_hook KoboTerminalHook[] = {
    {0},
};

// Only showOKDialog is required (it reports everything else missing).
static struct nh_dlsym KoboTerminalDlsym[] = {
    {.name = "_ZN25ConfirmationDialogFactory12showOKDialogERK7QStringS2_",                .out = nh_symoutptr(ConfirmationDialogFactory_showOKDialog)},
    {.name = "_ZN25ConfirmationDialogFactory18showTextEditDialogERK7QString",              .out = nh_symoutptr(ConfirmationDialogFactory_showTextEditDialog),                        .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog8setTitleERK7QString",                                .out = nh_symoutptr(ConfirmationDialog__setTitle),                                         .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog19setAcceptButtonTextERK7QString",                    .out = nh_symoutptr(ConfirmationDialog__setAcceptButtonText),                              .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog19setRejectButtonTextERK7QString",                    .out = nh_symoutptr(ConfirmationDialog__setRejectButtonText),                              .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog15showCloseButtonEb",                                 .out = nh_symoutptr(ConfirmationDialog__showCloseButton),                                  .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog21setRejectOnOutsideTapEb",                           .out = nh_symoutptr(ConfirmationDialog__setRejectOnOutsideTap),                            .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog9addWidgetEP7QWidget",                                .out = nh_symoutptr(ConfirmationDialog__addWidget),                                        .desc = 0, .optional = true},
    {.name = "_ZN27N3ConfirmationTextEditFieldC1EP18ConfirmationDialog14KeyboardScript", .out = nh_symoutptr(N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS),          .desc = 0, .optional = true},
    {.name = "_ZN27N3ConfirmationTextEditFieldC1EP18ConfirmationDialog",                  .out = nh_symoutptr(N3ConfirmationTextEditField__N3ConfirmationTextEditField),            .desc = 0, .optional = true},
    {.name = "_ZNK27N3ConfirmationTextEditField8textEditEv",                              .out = nh_symoutptr(N3ConfirmationTextEditField__textEdit),                                .desc = 0, .optional = true},
    {0},
};

NickelHook(
    .init  = kt_init,
    .info  = &KoboTerminal,
    .hook  = KoboTerminalHook,
    .dlsym = KoboTerminalDlsym,
)
