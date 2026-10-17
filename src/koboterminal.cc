#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QFile>
#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPointer>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QTouchEvent>
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
static void *(*Device__getCurrentDevice)(); // as used by NickelMenu

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

// Nickel's on-screen keyboard (a KeyboardFrame, created lazily), either inside
// the dialog or as its own window. Null until it exists.
static QWidget *kt_find_keyboard(QWidget *dialog) {
    auto is = [](QWidget *w) { return strstr(w->metaObject()->className(), "KeyboardFrame") != nullptr; };
    for (QWidget *w : dialog->findChildren<QWidget*>())
        if (is(w))
            return w;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (is(w))
            return w;
    return nullptr;
}

// A key that reacts to the touchscreen: Kobo's Qt sends touch events, and
// QPushButton only clicks on a left mouse button. Press/release work for
// touch and for mouse with any button; release inside the key = click().
class KtKey : public QPushButton {
public:
    KtKey(QString const& label, QFont const& font, int height) : QPushButton(label) {
        setFont(font);
        setFocusPolicy(Qt::NoFocus); // keep the keyboard attached to the field
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(height);
        setAttribute(Qt::WA_AcceptTouchEvents);
    }

protected:
    bool event(QEvent *e) override {
        static int logged = 0;
        switch (e->type()) {
        case QEvent::TouchBegin:
        case QEvent::MouseButtonPress:
            if (logged++ < 4)
                nh_log("key input event %d", e->type());
            e->accept(); // no pressed look: it costs two e-ink refreshes per tap
            return true;
        case QEvent::TouchEnd:
        case QEvent::MouseButtonRelease: {
            QPoint pos = e->type() == QEvent::TouchEnd && !static_cast<QTouchEvent*>(e)->touchPoints().isEmpty()
                ? static_cast<QTouchEvent*>(e)->touchPoints().first().pos().toPoint()
                : e->type() == QEvent::MouseButtonRelease ? static_cast<QMouseEvent*>(e)->pos() : QPoint(-1, -1);
            if (rect().contains(pos))
                click(); // toggles Ctrl/Alt, emits clicked()
            e->accept();
            return true;
        }
        case QEvent::TouchUpdate:
        case QEvent::TouchCancel:
        case QEvent::MouseMove:
            e->accept();
            return true;
        default:
            return QPushButton::event(e);
        }
    }
};

// The keys Nickel's keyboard lacks. No arrow keys: Nickel's keyboard ← → are
// forwarded (see the text field below), swiping scrolls; ⌨ shows/hides it.
static KtMods kt_keys(QWidget *box, KtTerm *term, int keyHeight, QWidget *dialog, QObject *field) {
    enum Kind { Text, Key, Ctrl, Alt, Kbd };
    static const struct { const char *label, *text; uint32_t keysym; Kind kind; } keys[2][5] = {
        {{"Esc", "\x1b", 0, Text}, {"/", "/", 0, Text}, {"-", "-", 0, Text}, {"~", "~", 0, Text}, {"⌨", 0, 0, Kbd}},
        {{"Tab", "\t", 0, Text}, {"Ctrl", 0, 0, Ctrl}, {"Alt", 0, 0, Alt}, {"Home", 0, XKB_KEY_Home, Key}, {"End", 0, XKB_KEY_End, Key}},
    };
    QGridLayout *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    // Nickel's button style has wide padding and minimum widths; override it.
    box->setStyleSheet(QStringLiteral("QPushButton { padding: 0px; margin: 0px; min-width: 0px; }"));
    int h = keyHeight;
    QFont font = kt_font(h * 9 / 20); // labels fill the key, the key itself stays the same size
    KtMods mods = {nullptr, nullptr};
    KtKey *b[2][5];
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 5; c++)
            grid->addWidget(b[r][c] = new KtKey(QString::fromUtf8(keys[r][c].label), font, h), r, c);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 5; c++)
            if (keys[r][c].kind == Ctrl)
                mods.ctrl = b[r][c];
            else if (keys[r][c].kind == Alt)
                mods.alt = b[r][c];
    mods.ctrl->setCheckable(true);
    mods.alt->setCheckable(true);
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 5; c++) {
            const char *text = keys[r][c].text;
            uint32_t keysym = keys[r][c].keysym;
            switch (keys[r][c].kind) {
            case Text:
                QObject::connect(b[r][c], &QPushButton::clicked, [term, mods, text]() { kt_type(term, mods, QString::fromUtf8(text)); });
                break;
            case Key:
                QObject::connect(b[r][c], &QPushButton::clicked, [term, mods, keysym]() { term->sendKey(keysym, mods.take()); });
                break;
            case Kbd:
                QObject::connect(b[r][c], &QPushButton::clicked, [dialog, field]() {
                    // Toggle Nickel's KeyboardFrame directly (what NickelDBus documents).
                    if (QWidget *kb = kt_find_keyboard(dialog))
                        kb->setVisible(!kb->isVisible());
                    else if (field)
                        QMetaObject::invokeMethod(field, "tapped"); // fallback: the field's own signal
                });
                break;
            default:
                break; // Ctrl/Alt just toggle; KtMods::take() reads them
            }
        }
    }
    return mods;
}

// Logs the dialog's widget tree once, to tune the layout from real devices.
static void kt_dump(QWidget *w, int depth = 0) {
    QRect g = w->geometry();
    QMargins m = w->contentsMargins(), lm = w->layout() ? w->layout()->contentsMargins() : QMargins();
    nh_log("layout: %*s%s '%s' %dx%d+%d+%d%s margins %d,%d,%d,%d layout %d,%d,%d,%d", depth * 2, "",
           w->metaObject()->className(), qPrintable(w->objectName()), g.width(), g.height(), g.x(), g.y(),
           w->isVisible() ? "" : " hidden", m.left(), m.top(), m.right(), m.bottom(), lm.left(), lm.top(), lm.right(), lm.bottom());
    for (QObject *o : w->children())
        if (o->isWidgetType())
            kt_dump(static_cast<QWidget*>(o), depth + 1);
}

// Sizes the terminal so the dialog's box fills the space above Nickel's
// keyboard, or the whole screen while the keyboard is hidden. Refits when the
// dialog or the keyboard changes, a few passes at most per change.
class KtFit : public QObject {
public:
    // box: the visible dialog box (Nickel's 'frame'), centred above the keyboard.
    // keys: the last widget in the box (the key bar).
    KtFit(QDialog *d, KtTerm *term, QWidget *box, QWidget *keys, int screenHeight)
        : QObject(d), d(d), term(term), box(box), keys(keys), screenHeight(screenHeight) {
        timer.setSingleShot(true);
        timer.setInterval(50); // let Nickel's layouts settle first
        connect(&timer, &QTimer::timeout, this, [this]() { fit(); });
        d->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *e) override {
        switch (e->type()) {
        case QEvent::Show:
        case QEvent::Hide:
            passes = 0; // e.g. the keyboard appeared: allow a fresh fit
            // fallthrough
        case QEvent::Resize:
        case QEvent::Move:
            timer.start();
        default:
            break;
        }
        return false;
    }

public:
    // Fit before the dialog is first shown, so it doesn't paint at a guessed
    // size and then again at the right one.
    void fitNow() {
        if (d->layout())
            d->layout()->activate();
        fit();
    }

private:
    QDialog *d;
    KtTerm *term;
    QWidget *box;
    QWidget *keys;
    int screenHeight;
    QTimer timer;
    QPointer<QWidget> keyboard;
    int passes = 0;

    void fit() {
        if (!keyboard && (keyboard = kt_find_keyboard(d)))
            keyboard->installEventFilter(this);
        if (!keyboard) { // without it, growing could push the prompt under the keyboard
            if (!passes++)
                nh_log("keyboard widget not found; keeping the default terminal height");
            return;
        }
        if (passes++ >= 4)
            return;
        box->setContentsMargins(0, 0, 0, 0); // again: a style re-polish may restore Nickel's inset
        // Every spare pixel goes to the terminal (rows = height / cell height;
        // the remainder is blank). Nickel centres the box above the keyboard, so
        // it fills the space exactly when box height == the keyboard's top: one
        // pass, one relayout, one e-ink refresh.
        // The keyboard is docked at the bottom: derive its top from its height. Its
        // reported position is stale right after it reappears (the box is still
        // screen-tall, so Nickel can't lay it out yet), which left the key bar
        // hidden behind it.
        // isVisibleTo(): also right before the dialog is first shown (see fitNow).
        if (keyboard->isVisibleTo(d) && keyboard->height() < screenHeight / 8)
            return; // not laid out yet (before the first show): the Show event refits
        int limit = keyboard->isVisibleTo(d) ? screenHeight - keyboard->height() : screenHeight;
        // What the content needs, not the box's current height: when the keyboard
        // reappears Nickel squeezes the box into the space above it at once,
        // the tall terminal overflows and the key bar is clipped, while the box
        // height alone already looks right.
        int bottomPad = box->height() - box->contentsRect().bottom() - 1 + (box->layout() ? box->layout()->contentsMargins().bottom() : 0);
        int need = keys->mapTo(box, QPoint(0, keys->height())).y() + bottomPad;
        int spare = limit - need;
        nh_log("fit: keyboard %s, limit %d, box %dx%d+%d, need %d, terminal %d -> %d",
               keyboard->isVisibleTo(d) ? "shown" : "hidden", limit, box->width(), box->height(),
               box->mapToGlobal(QPoint(0, 0)).y(), need, term->height(), qMax(3 * term->cellHeight(), term->height() + spare));
        int h = qMax(3 * term->cellHeight(), term->height() + spare);
        if (h != term->height()) {
            term->setFixedHeight(h);
            return;
        }
        static bool dumped = false; // once, when the layout has settled
        if (!dumped) {
            dumped = true;
            kt_dump(d);
        }
    }
};

// Whether the screen is colour e-ink (e.g. Kobo Libra/Clara Colour). On a
// black-and-white panel colours turn into greys that can be unreadable, so
// anything uncertain means black and white. Decided once, then cached.
static bool kt_colour_screen() {
    static int cached = -1;
    if (cached >= 0)
        return cached;
    cached = 0;
    // 1. Nickel's own answer, if this firmware has such a Device method. The
    //    names are educated guesses; the log shows which one (if any) resolved.
    static const char *const methods[] = {
        "_ZNK6Device14hasColorScreenEv", "_ZNK6Device15hasColourScreenEv", "_ZNK6Device15hasColorDisplayEv",
        "_ZNK6Device13isColorDeviceEv", "_ZNK6Device13supportsColorEv", "_ZNK6Device7isColorEv",
        "_ZNK6Device10hasKaleidoEv",
    };
    void *dev = Device__getCurrentDevice ? Device__getCurrentDevice() : nullptr;
    for (const char *m : methods) {
        if (void *fn = dev ? dlsym(RTLD_DEFAULT, m) : nullptr) {
            cached = reinterpret_cast<bool (*)(void *)>(fn)(dev);
            nh_log("colour: Nickel's %s says %s", m, cached ? "colour" : "black and white");
            return cached;
        }
    }
    // 2. The model's codename (what KOReader goes by): Libra Colour is
    //    "monza", Clara Colour "spaColour"; black-and-white models never match.
    QProcess p;
    p.start(QStringLiteral("/bin/kobo_config.sh"));
    QString codename = p.waitForFinished(2000) ? QString::fromLatin1(p.readAllStandardOutput()).trimmed() : QString();
    cached = codename.contains(QLatin1String("monza"), Qt::CaseInsensitive) ||
             codename.contains(QLatin1String("colour"), Qt::CaseInsensitive) ||
             codename.contains(QLatin1String("color"), Qt::CaseInsensitive);
    nh_log("colour: codename '%s' -> %s", qPrintable(codename), cached ? "colour" : "black and white");
    return cached;
}

// forceBW: the menu entry asked for black and white even on a colour screen.
static void kt_open(bool forceBW) {
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
    ConfirmationDialog__showCloseButton(d, false); // no X, no title: `exit` (or Ctrl+d, or USB) closes

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

    // ponytail: font size is a screen fraction; tune here if text is too small/big.
    // The height is only a first guess: KtFit grows it down to the keyboard.
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scr = screen ? screen->size() : QSize(1072, 1448); // Clara HD, if Qt has no screen yet
    QFont mono = kt_font(scr.width() / 45);
    QFontMetrics fm(mono);
    KtTerm *term = new KtTerm(mono, scr.width() * 9 / 10 / fm.width(QLatin1Char('M')), scr.height() * 3 / 10 / fm.height());
    term->exitTarget = d;
    term->colour = !forceBW && kt_colour_screen();
    ConfirmationDialog__addWidget(d, term);
    QWidget *row = new QWidget();
    // The field's tapped() signal is what opens the keyboard (taps on the terminal emit it).
    bool canTap = tle->metaObject()->indexOfSignal("tapped()") >= 0;
    KtMods mods = kt_keys(row, term, scr.height() / 24, d, canTap ? tle : nullptr);
    ConfirmationDialog__addWidget(d, row);

    // Layout by the names Nickel's ConfirmationDialog uses (see the layout dump
    // in the log): a full-screen transparent dialog whose visible box 'frame'
    // is centred above the keyboard, content in 'contentContainer'. Make the box
    // screen-wide with thin margins. If the names differ, keep Nickel's layout.
    KtFit *fitter = nullptr;
    QWidget *frame = d->findChild<QWidget*>(QStringLiteral("frame"));
    QWidget *content = d->findChild<QWidget*>(QStringLiteral("contentContainer"));
    if (frame && frame->layout() && content && content->layout()) {
        frame->setFixedWidth(scr.width());
        frame->layout()->setContentsMargins(4, 0, 4, 2);
        frame->layout()->setSpacing(0);
        // Nickel's stylesheet pads the box too; ours only overrides padding.
        frame->setStyleSheet(frame->styleSheet() + QStringLiteral(" #frame { padding: 0px; }"));
        frame->setContentsMargins(0, 0, 0, 0); // Nickel's ~58/82 px inset (not from the layout)
        content->setContentsMargins(0, 0, 0, 0);
        content->layout()->setContentsMargins(0, 0, 0, 0);
        content->layout()->setSpacing(2);
        fitter = new KtFit(d, term, frame, row, scr.height());
    } else {
        nh_log("dialog layout not recognised; keeping Nickel's sizes");
    }


    // The text field is only a keyboard receiver: collapse it, let a tap on the
    // terminal open the keyboard instead, and put our widgets above it.
    if (canTap) {
        tef->setFixedHeight(0);
        // A tap on the terminal shows the keyboard: KeyboardFrame::show() when the
        // frame exists (the field's tapped() signal alone doesn't open it).
        if (QWidget *kb = kt_find_keyboard(d)) {
            term->tapTarget = kb;
            term->tapMethod = "show";
        } else {
            term->tapTarget = tle;
            term->tapMethod = "tapped";
        }
        if (QBoxLayout *l = qobject_cast<QBoxLayout*>(tef->parentWidget() ? tef->parentWidget()->layout() : nullptr)) {
            int i = l->indexOf(tef);
            if (i >= 0 && l->indexOf(term) >= 0 && l->indexOf(row) >= 0) {
                QWidget *ours[] = {row, term}; // inserted in reverse at i
                for (QWidget *w : ours) {
                    l->removeWidget(w);
                    l->insertWidget(i, w);
                }
            }
        }
    } else {
        nh_log("TouchLineEdit has no tapped() signal; keeping the text field visible");
    }

    // Keys go to the shell as they are typed. The field always holds two
    // sentinel spaces with the cursor between them, so backspace still changes
    // the text and Nickel's ← → keys move the cursor (sent on as arrow keys).
    // A reentrancy guard (not blockSignals) lets Nickel's own listeners on the
    // field still see the reset.
    static const QString S = QStringLiteral("  ");
    static bool resetting = false;
    auto reset = [tle]() {
        resetting = true;
        tle->setText(S);
        tle->setCursorPosition(1);
        resetting = false;
    };
    reset();
    QObject::connect(tle, &QLineEdit::textChanged, term, [term, mods, reset](QString const& t) {
        if (resetting || t == S)
            return;
        if (t.size() < S.size())
            term->send("\x7f"); // backspace ate a sentinel
        else if (t.startsWith(QLatin1Char(' ')) && t.endsWith(QLatin1Char(' ')))
            kt_type(term, mods, t.mid(1, t.size() - 2));
        else
            kt_type(term, mods, t.trimmed()); // Nickel replaced the text: send what's new
        reset();
    });
    QObject::connect(tle, &QLineEdit::cursorPositionChanged, term, [tle, term, mods, reset](int from, int to) {
        if (resetting || tle->text() != S || from == to)
            return;
        term->sendKey(to < from ? XKB_KEY_Left : XKB_KEY_Right, mods.take());
        reset();
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
    if (fitter)
        fitter->fitNow();
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
        QByteArray msg;
        ssize_t n;
        while ((n = read(rfd, buf, sizeof(buf))) > 0) // drain; any number of taps opens one dialog
            msg.append(buf, n);
        kt_open(msg.contains("mono"));
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
    {.name = "_ZN6Device16getCurrentDeviceEv",                                             .out = nh_symoutptr(Device__getCurrentDevice),                                            .desc = 0, .optional = true},
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
