#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSocketNotifier>
#include <QString>

#include <NickelHook.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "term.h"

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

// Builds the row of keys Nickel's keyboard lacks. Ctrl is sticky: it applies
// to the next typed character.
static QPushButton *kt_keys(QWidget *row, KtTerm *term) {
    QHBoxLayout *l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    auto key = [l](const char *label) {
        QPushButton *b = new QPushButton(QString::fromUtf8(label));
        b->setFocusPolicy(Qt::NoFocus); // keep the keyboard attached to the field
        l->addWidget(b);
        return b;
    };
    QPushButton *ctrl = key("Ctrl");
    ctrl->setCheckable(true);
    QObject::connect(key("^C"), &QPushButton::clicked, [term]() { term->send("\x03"); });
    QObject::connect(key("Esc"), &QPushButton::clicked, [term]() { term->send("\x1b"); });
    QObject::connect(key("Tab"), &QPushButton::clicked, [term]() { term->send("\t"); });
    QObject::connect(key("Left"), &QPushButton::clicked, [term]() { term->sendKey(XKB_KEY_Left); });
    QObject::connect(key("Down"), &QPushButton::clicked, [term]() { term->sendKey(XKB_KEY_Down); });
    QObject::connect(key("Up"), &QPushButton::clicked, [term]() { term->sendKey(XKB_KEY_Up); });
    QObject::connect(key("Right"), &QPushButton::clicked, [term]() { term->sendKey(XKB_KEY_Right); });
    return ctrl;
}

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
    QObject::connect(d, &QDialog::rejected, d, &QObject::deleteLater); // ~KtTerm kills the shell's session

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
    // Keyboard "Go" behaves like the Enter button.
    if (!QObject::connect(tef, SIGNAL(commitRequested()), d, SIGNAL(accepted())))
        nh_log("could not connect commitRequested");

    // ponytail: sizes are screen fractions, tune on device if text is too small/big.
    QSize scr = QGuiApplication::primaryScreen()->size();
    QFont mono(QStringLiteral("Courier"));
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPixelSize(scr.width() / 45);
    QFontMetrics fm(mono);
    KtTerm *term = new KtTerm(mono, scr.width() * 9 / 10 / fm.width(QLatin1Char('M')), scr.height() * 2 / 5 / fm.height());
    term->exitTarget = d;
    ConfirmationDialog__addWidget(d, term);

    QWidget *row = new QWidget();
    QPushButton *ctrl = kt_keys(row, term);
    ConfirmationDialog__addWidget(d, row);

    ConfirmationDialog__setTitle(d, QStringLiteral("KoboTerminal"));
    ConfirmationDialog__setAcceptButtonText(d, QStringLiteral("Enter"));
    ConfirmationDialog__setRejectButtonText(d, QStringLiteral("Close"));
    ConfirmationDialog__setRejectOnOutsideTap(d, false);
    ConfirmationDialog__showCloseButton(d, true);

    // Keys go to the shell as they are typed. The field always holds one
    // sentinel space, so backspace on an "empty" line still changes the text.
    static const QString S = QStringLiteral(" ");
    tle->setText(S);
    QObject::connect(tle, &QLineEdit::textChanged, term, [tle, term, ctrl](QString const& t) {
        if (t == S)
            return;
        if (t.isEmpty()) {
            term->send("\x7f");
        } else {
            QString in = t.endsWith(S) ? t.left(t.size() - 1) : t.startsWith(S) ? t.mid(1) : t;
            QByteArray b = in.toUtf8();
            if (ctrl->isChecked() && !b.isEmpty()) {
                ctrl->setChecked(false);
                uchar c = QChar(QLatin1Char(b[0])).toUpper().toLatin1();
                if (c >= '@' && c <= '_')
                    b[0] = c & 0x1f;
            }
            term->send(b);
        }
        tle->blockSignals(true);
        tle->setText(S);
        tle->blockSignals(false);
    });
    QObject::connect(d, &QDialog::accepted, term, [d, term]() {
        if (!d->isVisible()) // in case this firmware's Enter button calls accept()
            d->open();
        term->send("\r");
    });

    if (!term->start()) {
        ConfirmationDialogFactory_showOKDialog(QStringLiteral("KoboTerminal"), QStringLiteral("Could not start the shell."));
        d->deleteLater();
        return;
    }
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
