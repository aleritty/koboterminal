#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QFile>
#include <QGuiApplication>
#include <QBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSocketNotifier>
#include <QString>

#include <NickelHook.h>
#include <libtsm.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "term.h"
#include "wifi.h"

#define KT_FIFO "/tmp/koboterminal.fifo"
// NickelMenu reads its config files alphabetically; "zz-" puts the entry last.
#define KT_MENU "/mnt/onboard/.adds/nm/zz-koboterminal"
#define KT_MENU_OLD "/mnt/onboard/.adds/nm/koboterminal" // name used before v0.2

// Nickel types we only use through their Qt base classes.
typedef QDialog ConfirmationDialog;
typedef QWidget N3ConfirmationTextEditField;

// Signatures and the text-edit-field recipe come from NickelDBus (NDBCfmDlg.cc).
static void (*ConfirmationDialogFactory_showOKDialog)(QString const& title, QString const& body);
static ConfirmationDialog *(*ConfirmationDialogFactory_showTextEditDialog)(QString const& title);
static void (*ConfirmationDialog__showCloseButton)(ConfirmationDialog *_this, bool);
static void (*ConfirmationDialog__setRejectOnOutsideTap)(ConfirmationDialog *_this, bool);
static void (*ConfirmationDialog__addWidget)(ConfirmationDialog *_this, QWidget *);
static N3ConfirmationTextEditField *(*N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS)(N3ConfirmationTextEditField *_this, ConfirmationDialog *, int keyboardScript);
static N3ConfirmationTextEditField *(*N3ConfirmationTextEditField__N3ConfirmationTextEditField)(N3ConfirmationTextEditField *_this, ConfirmationDialog *); // fw 4.6
static QLineEdit *(*N3ConfirmationTextEditField__textEdit)(N3ConfirmationTextEditField *_this);

// Optional: missing ones only disable the feature (Wi-Fi keepalive, close on USB).
static QObject *(*WirelessWatchdog__sharedInstance)();
static QObject *(*PlugWorkflowManager__sharedInstance)();

static QPointer<ConfirmationDialog> kt_dlg;

// The bundled monospace font (JetBrains Mono NL Medium, OFL), registered once.
// Kobo firmware ships no monospace font.
static QFont kt_font(int pixelSize) {
    static QString family;
    if (family.isNull()) {
        int id = QFontDatabase::addApplicationFont(QStringLiteral(":/koboterminal/mono.ttf"));
        QStringList f = QFontDatabase::applicationFontFamilies(id);
        family = f.isEmpty() ? QStringLiteral("Courier") : f.first();
        if (f.isEmpty())
            nh_log("could not load the bundled font, falling back to %s", qPrintable(family));
    }
    QFont font(family);
    font.setStyleHint(QFont::TypeWriter);
    font.setPixelSize(pixelSize);
    return font;
}

// Sticky modifiers from the key row: they apply to the next key, then release.
struct KtMods {
    QPushButton *ctrl, *alt;
    unsigned take() const {
        unsigned m = (ctrl->isChecked() ? TSM_CONTROL_MASK : 0) | (alt->isChecked() ? TSM_ALT_MASK : 0);
        ctrl->setChecked(false);
        alt->setChecked(false);
        return m;
    }
};

// Sends typed text; pending modifiers apply to its first character.
static void kt_type(KtTerm *term, KtMods const& mods, QString const& s) {
    unsigned m = mods.take();
    if (!m || s.isEmpty()) {
        term->send(s.toUtf8());
        return;
    }
    uint c = s.at(0).unicode();
    term->sendKey(c < 0x80 ? c : 0, m, c);
    term->send(s.mid(1).toUtf8());
}

// The keys Nickel's keyboard lacks, in Termux's default extra-keys layout.
static KtMods kt_keys(QWidget *box, KtTerm *term, QFont const& font) {
    static const struct { const char *label, *text; uint32_t keysym; } keys[2][7] = {
        {{"Esc", "\x1b", 0}, {"/", "/", 0}, {"-", "-", 0}, {"Home", 0, XKB_KEY_Home},
         {"\u2191", 0, XKB_KEY_Up}, {"End", 0, XKB_KEY_End}, {"PgUp", 0, XKB_KEY_Page_Up}},
        {{"Tab", "\t", 0}, {"Ctrl", 0, 0}, {"Alt", 0, 0}, {"\u2190", 0, XKB_KEY_Left},
         {"\u2193", 0, XKB_KEY_Down}, {"\u2192", 0, XKB_KEY_Right}, {"PgDn", 0, XKB_KEY_Page_Down}},
    };
    QGridLayout *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    KtMods mods = {nullptr, nullptr};
    QPushButton *b[2][7];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 7; c++) {
            b[r][c] = new QPushButton(QString::fromUtf8(keys[r][c].label));
            b[r][c]->setFont(font); // Nickel's UI fonts may lack the arrows
            b[r][c]->setFocusPolicy(Qt::NoFocus); // keep the keyboard attached to the field
            grid->addWidget(b[r][c], r, c);
        }
    }
    mods.ctrl = b[1][1];
    mods.alt = b[1][2];
    mods.ctrl->setCheckable(true);
    mods.alt->setCheckable(true);
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 7; c++) {
            const char *text = keys[r][c].text;
            uint32_t keysym = keys[r][c].keysym;
            if (text)
                QObject::connect(b[r][c], &QPushButton::clicked, [term, mods, text]() { kt_type(term, mods, QString::fromUtf8(text)); });
            else if (keysym)
                QObject::connect(b[r][c], &QPushButton::clicked, [term, mods, keysym]() { term->sendKey(keysym, mods.take()); });
        }
    }
    return mods;
}

static void kt_open() {
    if (kt_dlg) { // e.g. Nickel hid it (sleep, USB): bring the same session back
        if (!kt_dlg->isVisible())
            kt_dlg->open();
        return;
    }
    if (!ConfirmationDialogFactory_showTextEditDialog || !ConfirmationDialog__showCloseButton ||
        !ConfirmationDialog__setRejectOnOutsideTap || !ConfirmationDialog__addWidget || !N3ConfirmationTextEditField__textEdit ||
        !(N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS || N3ConfirmationTextEditField__N3ConfirmationTextEditField)) {
        ConfirmationDialogFactory_showOKDialog(QStringLiteral("KoboTerminal"), QStringLiteral("Unsupported firmware: missing Nickel symbols."));
        return;
    }

    // No title and no accept/reject buttons: the keyboard's Go key is Enter, the X closes.
    ConfirmationDialog *d = ConfirmationDialogFactory_showTextEditDialog(QString());
    if (!d)
        return;
    d->hide();
    kt_dlg = d;
    QObject::connect(d, &QDialog::rejected, d, &QObject::deleteLater); // ~KtTerm kills the shell's session
    // The shell starts in /mnt/onboard, so close (and kill it) before USB
    // storage mode, or the busy partition could not be shared.
    if (QObject *pwm = PlugWorkflowManager__sharedInstance ? PlugWorkflowManager__sharedInstance() : nullptr)
        QObject::connect(pwm, SIGNAL(aboutToConnect()), d, SLOT(reject()));
    if (QObject *wd = WirelessWatchdog__sharedInstance ? WirelessWatchdog__sharedInstance() : nullptr)
        new KtWifiKeepAlive(wd, d);
    ConfirmationDialog__setRejectOnOutsideTap(d, false);
    ConfirmationDialog__showCloseButton(d, true);

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
    // Keyboard "Go" sends Enter (see the accepted handler below).
    if (!QObject::connect(tef, SIGNAL(commitRequested()), d, SIGNAL(accepted())))
        nh_log("could not connect commitRequested");

    // ponytail: sizes are screen fractions, tune on device if text is too small/big.
    // 30% of the height leaves room for the key rows above Nickel's keyboard.
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scr = screen ? screen->size() : QSize(1072, 1448); // Clara HD, if Qt has no screen yet
    QFont mono = kt_font(scr.width() / 45);
    QFontMetrics fm(mono);
    KtTerm *term = new KtTerm(mono, scr.width() * 9 / 10 / fm.width(QLatin1Char('M')), scr.height() * 3 / 10 / fm.height());
    term->exitTarget = d;
    ConfirmationDialog__addWidget(d, term);
    QWidget *row = new QWidget();
    KtMods mods = kt_keys(row, term, mono);
    ConfirmationDialog__addWidget(d, row);

    // The text field is only a keyboard receiver. If its tapped() signal (which
    // opens the keyboard) exists, collapse the field, let a tap on the terminal
    // open the keyboard instead, and put the terminal above the field so Nickel
    // keeps it above the keyboard. Otherwise leave the field as it is.
    if (tle->metaObject()->indexOfSignal("tapped()") >= 0) {
        tef->setFixedHeight(0);
        term->tapTarget = tle;
        if (QBoxLayout *l = qobject_cast<QBoxLayout*>(tef->parentWidget() ? tef->parentWidget()->layout() : nullptr)) {
            int i = l->indexOf(tef);
            if (i >= 0 && l->indexOf(term) >= 0 && l->indexOf(row) >= 0) {
                l->removeWidget(term);
                l->removeWidget(row);
                l->insertWidget(i, row);
                l->insertWidget(i, term);
            }
        }
    } else {
        nh_log("TouchLineEdit has no tapped() signal; keeping the text field visible");
    }

    // Keys go to the shell as they are typed. The field always holds one
    // sentinel space, so backspace on an "empty" line still changes the text.
    // A reentrancy guard (not blockSignals) lets Nickel's own listeners on the
    // field still see the reset.
    static const QString S = QStringLiteral(" ");
    static bool resetting = false;
    tle->setText(S);
    QObject::connect(tle, &QLineEdit::textChanged, term, [tle, term, mods](QString const& t) {
        if (resetting || t == S)
            return;
        if (t.isEmpty())
            term->send("\x7f");
        else
            kt_type(term, mods, t.endsWith(S) ? t.left(t.size() - 1) : t.startsWith(S) ? t.mid(1) : t);
        resetting = true;
        tle->setText(S);
        resetting = false;
    });
    QObject::connect(d, &QDialog::accepted, term, [d, term]() {
        if (!d->isVisible()) // in case this firmware's Go key calls accept()
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
// On failure the FIFO is removed again: a FIFO without a reader would make
// the menu's `echo` hang until NickelMenu's timeout instead of failing fast.
static int kt_init() {
    // init runs inside dlopen, on whatever thread Qt loaded the plugin from.
    // Widgets may only be touched on the GUI thread, so the notifier goes there.
    if (!QCoreApplication::instance()) {
        nh_log("no QCoreApplication");
        return -1;
    }
    unlink(KT_FIFO);
    if (mkfifo(KT_FIFO, 0600)) {
        nh_log("mkfifo %s: %m", KT_FIFO);
        return -1;
    }
    int fd = open(KT_FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        nh_log("open %s: %m", KT_FIFO);
        unlink(KT_FIFO);
        return -1;
    }
    QSocketNotifier *n = new QSocketNotifier(fd, QSocketNotifier::Read);
    n->moveToThread(QCoreApplication::instance()->thread());
    QObject::connect(n, &QSocketNotifier::activated, [](int rfd) {
        char buf[64];
        while (read(rfd, buf, sizeof(buf)) > 0); // drain; any number of taps opens one dialog
        kt_open();
    });
    nh_log("listening on %s", KT_FIFO);
    unlink(KT_MENU_OLD); // upgrade from the old menu file name, or the entry would show twice
    return 0;
}

// Every file this mod (or an earlier experimental build) can leave behind.
// Keep in sync with the "Footprint" list in PLAN.md and the manual steps in
// README.md. NickelHook itself deletes the library and the uninstall flag.
static bool kt_rm(QString const& p, bool dir = false) {
    QByteArray b = QFile::encodeName(p);
    if (access(b.constData(), F_OK))
        return true; // nh_delete_* fails on missing paths
    return dir ? nh_delete_dir(b.constData()) : nh_delete_file(b.constData());
}

// `&` (not `&&`) on purpose: every removal is attempted even if one fails.
static bool kt_uninstall() {
    bool ok = kt_rm(QStringLiteral(KT_MENU)) & kt_rm(QStringLiteral(KT_MENU_OLD)) & kt_rm(QStringLiteral(KT_FIFO)) & kt_rm(QStringLiteral(KT_HISTFILE));
    QDir kobo(QStringLiteral("/mnt/onboard/.kobo")); // nh_dump_log() output from failed inits
    for (QString const& f : kobo.entryList(QStringList(QStringLiteral("KoboTerminal_*.log")), QDir::Files))
        ok &= kt_rm(kobo.filePath(f));

    // Earlier builds kept files in .adds/koboterminal and a marker block in the NickelMenu config.
    QDir legacy(QStringLiteral("/mnt/onboard/.adds/koboterminal"));
    if (legacy.exists()) {
        for (QString const& f : legacy.entryList(QDir::Files | QDir::Hidden))
            ok &= kt_rm(legacy.filePath(f));
        ok &= kt_rm(legacy.path(), true);
    }
    QFile menu(QStringLiteral("/mnt/onboard/.adds/nm/menu"));
    if (menu.open(QIODevice::ReadOnly)) {
        QString cfg = QString::fromUtf8(menu.readAll());
        menu.close();
        QRegularExpression block(QStringLiteral("# BEGIN KOBOTERMINAL.*?# END KOBOTERMINAL\\n?"), QRegularExpression::DotMatchesEverythingOption);
        if (cfg.contains(block)) {
            QSaveFile out(menu.fileName());
            ok &= out.open(QIODevice::WriteOnly) && out.write(cfg.remove(block).toUtf8()) >= 0 && out.commit();
        }
    }
    nh_log("uninstall cleanup %s", ok ? "done" : "incomplete");
    return ok;
}

static struct nh_info KoboTerminal = {
    .name            = "KoboTerminal",
    .desc            = "Terminal dialog opened from NickelMenu",
    .uninstall_flag  = "/mnt/onboard/koboterminal_uninstall",
    .uninstall_xflag = KT_MENU,
};

static struct nh_hook KoboTerminalHook[] = {
    {0},
};

// Only showOKDialog is required (it reports everything else missing).
static struct nh_dlsym KoboTerminalDlsym[] = {
    {.name = "_ZN25ConfirmationDialogFactory12showOKDialogERK7QStringS2_",                .out = nh_symoutptr(ConfirmationDialogFactory_showOKDialog)},
    {.name = "_ZN25ConfirmationDialogFactory18showTextEditDialogERK7QString",              .out = nh_symoutptr(ConfirmationDialogFactory_showTextEditDialog),                        .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog15showCloseButtonEb",                                 .out = nh_symoutptr(ConfirmationDialog__showCloseButton),                                  .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog21setRejectOnOutsideTapEb",                           .out = nh_symoutptr(ConfirmationDialog__setRejectOnOutsideTap),                            .desc = 0, .optional = true},
    {.name = "_ZN18ConfirmationDialog9addWidgetEP7QWidget",                                .out = nh_symoutptr(ConfirmationDialog__addWidget),                                        .desc = 0, .optional = true},
    {.name = "_ZN27N3ConfirmationTextEditFieldC1EP18ConfirmationDialog14KeyboardScript", .out = nh_symoutptr(N3ConfirmationTextEditField__N3ConfirmationTextEditFieldKS),          .desc = 0, .optional = true},
    {.name = "_ZN27N3ConfirmationTextEditFieldC1EP18ConfirmationDialog",                  .out = nh_symoutptr(N3ConfirmationTextEditField__N3ConfirmationTextEditField),            .desc = 0, .optional = true},
    {.name = "_ZN16WirelessWatchdog14sharedInstanceEv",                                    .out = nh_symoutptr(WirelessWatchdog__sharedInstance),                                    .desc = 0, .optional = true},
    {.name = "_ZN19PlugWorkflowManager14sharedInstanceEv",                                 .out = nh_symoutptr(PlugWorkflowManager__sharedInstance),                                 .desc = 0, .optional = true},
    {.name = "_ZNK27N3ConfirmationTextEditField8textEditEv",                              .out = nh_symoutptr(N3ConfirmationTextEditField__textEdit),                                .desc = 0, .optional = true},
    {0},
};

NickelHook(
    .init  = kt_init,
    .info  = &KoboTerminal,
    .hook  = KoboTerminalHook,
    .dlsym = KoboTerminalDlsym,
    .uninstall = kt_uninstall,
)
