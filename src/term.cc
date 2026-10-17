#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSocketNotifier>

#include <NickelHook.h>
#include <libtsm.h>

#include "term.h"

extern char **environ;

// Kills every process in the session (the shell, its jobs in their own
// process groups, and their children). `setsid cmd &` escapes on purpose.
static void kt_kill_session(pid_t sid) {
    DIR *d = opendir("/proc");
    if (!d)
        return;
    while (struct dirent *e = readdir(d)) {
        pid_t pid = atoi(e->d_name);
        if (pid <= 0)
            continue;
        char path[32], buf[512];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        char *p = strrchr(buf, ')'); // comm may contain spaces
        int sess;
        if (p && sscanf(p + 2, "%*c %*d %*d %d", &sess) == 1 && sess == sid)
            kill(pid, SIGKILL);
    }
    closedir(d);
}

// Reaps the shell without blocking Nickel's GUI thread.
static void kt_reap(pid_t pid) {
    if (waitpid(pid, nullptr, WNOHANG) != 0)
        return; // reaped, or ECHILD
    QTimer *t = new QTimer(QCoreApplication::instance());
    int *tries = new int(0);
    QObject::connect(t, &QTimer::timeout, [t, tries, pid]() {
        if (waitpid(pid, nullptr, WNOHANG) != 0 || ++*tries > 20) {
            delete tries;
            t->deleteLater();
        }
    });
    t->start(500);
}

KtTerm::KtTerm(QFont const& f, int cols, int rows, QWidget *parent) : QWidget(parent), font(f), bold(f) {
    bold.setBold(true);
    QFontMetrics fm(font);
    cw = fm.width(QLatin1Char('M'));
    ch = fm.height();
    hint = QSize(cols * cw, rows * ch);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(20 * cw);
    setFixedHeight(hint.height());
    resize(hint); // when not in a layout
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_AcceptTouchEvents);

    tsm_screen_new(&screen, nullptr, nullptr);
    tsm_screen_set_max_sb(screen, 1000); // ponytail: ~1000 lines of history; swipe to page through
    tsm_screen_resize(screen, cols, rows);
    tsm_vte_new(&vte, screen, [](struct tsm_vte *, const char *u8, size_t len, void *self) {
        static_cast<KtTerm*>(self)->writePty(u8, len); // terminal replies: don't leave scrollback
    }, this, nullptr, nullptr);

    flush.setSingleShot(true);
    flush.setInterval(100);
    connect(&flush, &QTimer::timeout, this, &KtTerm::repaintDirty);
}

KtTerm::~KtTerm() {
    if (child > 0) {
        // Not reaped yet, so its pid (= the session id) can't have been reused.
        kt_kill_session(child);
        kt_reap(child);
    }
    delete notifier; // unregister before the fds are closed
    delete exitNotifier;
    if (master >= 0)
        ::close(master);
    if (exitPipe >= 0)
        ::close(exitPipe);
    if (vte)
        tsm_vte_unref(vte);
    if (screen)
        tsm_screen_unref(screen);
}

bool KtTerm::start() {
    if (!screen || !vte)
        return false;

    // Nickel is multithreaded: after fork the child may only make
    // async-signal-safe calls, so build argv/envp here.
    QList<QByteArray> env;
    for (char **e = environ; *e; e++)
        if (strncmp(*e, "TERM=", 5) && strncmp(*e, "PS1=", 4) && strncmp(*e, "HISTFILE=", 9))
            env << QByteArray(*e);
    // History in tmpfs, not ~/.ash_history on the root filesystem (see PLAN.md footprint).
    env << "TERM=xterm" << "PS1=\\w # " << "HISTFILE=" KT_HISTFILE;
    QVector<char*> envp;
    for (QByteArray &e : env)
        envp << e.data();
    envp << nullptr;
    char sh[] = "/bin/sh", arg0[] = "-sh"; // login shell: reads /etc/profile
    char *argv[] = {arg0, nullptr};

    struct winsize ws = {};
    ws.ws_col = width() / cw;
    ws.ws_row = height() / ch;
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 65536)
        maxfd = 65536;

    // Detecting `exit` without polling: a background job may keep the PTY
    // open after the shell exits, so PTY EOF isn't enough. Instead the forkpty
    // child (the session leader) only waits for the shell and exits, closing
    // the write end of this pipe; Nickel's event loop sees EOF on the read end.
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC)) {
        nh_log("pipe2: %m");
        return false;
    }
    pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        nh_log("forkpty: %m");
        master = -1;
        ::close(pfd[0]);
        ::close(pfd[1]);
        return false;
    }
    if (pid == 0) {
        // Don't leak Nickel's fds (e.g. on /mnt/onboard) into the session.
        for (int fd = 3; fd < maxfd; fd++)
            if (fd != pfd[1])
                ::close(fd);
        // The watcher ignores terminal signals, so ^C can never end the session.
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGHUP, SIG_IGN);
        signal(SIGCHLD, SIG_DFL); // Nickel may ignore it, which would break waitpid
        pid_t shell = fork();
        if (shell == 0) {
            // Nickel may block or ignore signals; exec keeps ignored ones, which
            // would break ^C. pfd[1] is O_CLOEXEC, so the shell doesn't hold it.
            sigset_t none;
            sigemptyset(&none);
            sigprocmask(SIG_SETMASK, &none, nullptr);
            for (int s = 1; s < NSIG; s++)
                signal(s, SIG_DFL);
            if (chdir("/mnt/onboard") && chdir("/")) {} // user storage; the braces silence warn_unused_result
            execve(sh, argv, envp.data());
            _exit(127);
        }
        while (shell > 0 && waitpid(shell, nullptr, 0) < 0 && errno == EINTR) {}
        _exit(0);
    }
    ::close(pfd[1]);
    child = pid;
    exitPipe = pfd[0];
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    fcntl(master, F_SETFD, FD_CLOEXEC);
    notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &KtTerm::readPty);
    exitNotifier = new QSocketNotifier(exitPipe, QSocketNotifier::Read, this);
    connect(exitNotifier, &QSocketNotifier::activated, this, &KtTerm::shellExited);
    return true;
}

// The shell is gone. Kill its leftover jobs now, while the watcher's pid (the
// session id) is still pinned (it's unreaped), then reap and forget it.
void KtTerm::shellExited() {
    if (exitNotifier)
        exitNotifier->setEnabled(false);
    if (notifier)
        notifier->setEnabled(false);
    if (child > 0) {
        kt_kill_session(child);
        kt_reap(child);
        child = 0;
    }
    if (exitTarget)
        QMetaObject::invokeMethod(exitTarget, "reject", Qt::QueuedConnection);
    exitTarget = nullptr; // once
}

// User input jumps back from scrollback to the live screen, like any terminal.
void KtTerm::send(QByteArray const& b) {
    if (scrolled) {
        scrolled = false;
        tsm_screen_sb_reset(screen);
        repaintDirty();
    }
    writePty(b.constData(), b.size());
}

void KtTerm::writePty(const char *data, size_t len) {
    // ponytail: drops the rest if the PTY input buffer (~4 KB) is full; only huge pastes hit it.
    for (size_t off = 0; master >= 0 && off < len;) {
        ssize_t n = write(master, data + off, len - off);
        if (n > 0) {
            off += n;
        } else if (n < 0 && errno != EINTR) {
            if (errno != EAGAIN)
                nh_log("pty write: %m");
            break;
        }
    }
}

void KtTerm::sendKey(uint32_t keysym, unsigned mods, uint32_t unicode) {
    send(QByteArray()); // leave scrollback
    tsm_vte_handle_keyboard(vte, keysym, keysym < 0x80 ? keysym : 0, mods, unicode);
}

// Kobo's Qt delivers the touchscreen as touch events, not mouse clicks; take
// both. The first few input events are logged to see what a device sends.
bool KtTerm::event(QEvent *e) {
    static int logged = 0;
    switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::Gesture:
        if (logged++ < 6)
            nh_log("terminal input event %d", e->type());
    default:
        break;
    }
    if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchEnd) {
        QList<QTouchEvent::TouchPoint> const& pts = static_cast<QTouchEvent*>(e)->touchPoints();
        if (!pts.isEmpty()) {
            QPoint pos = pts.first().pos().toPoint();
            if (e->type() == QEvent::TouchBegin)
                pressPos = pos;
            else
                release(pos);
        }
        e->accept(); // accepting TouchBegin is what makes Qt send the rest
        return true;
    }
    if (e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchCancel) {
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void KtTerm::mousePressEvent(QMouseEvent *e) {
    pressPos = e->pos();
}

void KtTerm::mouseReleaseEvent(QMouseEvent *e) {
    release(e->pos());
}

// A tap (little movement in any direction) opens the keyboard. A vertical
// swipe pages through scrollback: down for older output, up for newer, one
// e-ink refresh per page. Other swipes do nothing.
void KtTerm::release(QPoint pos) {
    QPoint d = pos - pressPos;
    if (d.manhattanLength() < 2 * ch) {
        if (tapTarget)
            QMetaObject::invokeMethod(tapTarget, tapMethod);
        return;
    }
    int dy = d.y();
    if (qAbs(dy) < qAbs(d.x()))
        return;
    if (dy > 0)
        tsm_screen_sb_page_up(screen, 1);
    else
        tsm_screen_sb_page_down(screen, 1);
    scrolled = tsm_screen_sb_get_line_pos(screen) < tsm_screen_sb_get_line_count(screen);
    repaintDirty();
}

void KtTerm::readPty() {
    char buf[4096];
    // Cap the work per wakeup so a flood (`yes`) can't starve Nickel's GUI.
    for (int total = 0; total < 65536;) {
        ssize_t n = read(master, buf, sizeof(buf));
        if (n > 0) {
            tsm_vte_input(vte, buf, n);
            total += n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            break;
        // EIO/EOF: every process closed the slave side.
        shellExited();
        break;
    }
    // Repaint once output has been quiet for 100 ms (a command, its output and
    // the next prompt usually arrive in separate bursts), but never later than
    // ~0.5 s after the first unpainted byte: one e-ink refresh instead of several.
    if (!flush.isActive())
        pending.start();
    if (!flush.isActive() || pending.elapsed() < 400)
        flush.start();
}

// The 16 ANSI colours as text colours, darkened and saturated for colour
// e-ink. White and light grey would vanish on the white background, so they
// become greys dark enough to read.
static const QRgb kt_ansi[16] = {
    0x000000, 0xb00000, 0x007000, 0x8a6200, 0x0035c8, 0x9a009a, 0x006f8a, 0x505050,
    0x404040, 0xe00000, 0x009000, 0xa07800, 0x2050ff, 0xc000c0, 0x008fb0, 0x303030,
};

static int kt_luma(QColor c) { return (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000; }

// Darkens a colour until its luminance is at most `max` (0-255).
static QColor kt_darken(QColor c, int max) {
    int l = kt_luma(c);
    return l <= max ? c : QColor(c.red() * max / l, c.green() * max / l, c.blue() * max / l);
}

// A light tint (35% colour, 65% white): backgrounds stay light, text stays readable.
static QColor kt_tint(QColor c) {
    return QColor(255 - (255 - c.red()) * 35 / 100, 255 - (255 - c.green()) * 35 / 100, 255 - (255 - c.blue()) * 35 / 100);
}

void KtTerm::cellColours(const tsm_screen_attr *attr, bool colour, QColor *fg, QColor *bg) {
    if (!colour) {
        *fg = Qt::black;
        *bg = Qt::white;
    } else {
        int f = attr->fccode, b = attr->bccode;
        if (f >= 0 && f < 16)
            *fg = QColor(kt_ansi[f]);
        else if (f < 0)
            *fg = kt_darken(QColor(attr->fr, attr->fg, attr->fb), 110); // 256-colour / 24-bit
        else
            *fg = Qt::black; // default foreground
        if (b >= 0 && b < 16)
            *bg = kt_tint(QColor(kt_ansi[b]));
        else if (b < 0)
            *bg = kt_tint(QColor(attr->br, attr->bg, attr->bb));
        else
            *bg = Qt::white; // default background
    }
    if (attr->inverse)
        qSwap(*fg, *bg);
}

namespace {
struct DrawCtx {
    QPainter *p;          // paint pass, or null for the dirty-collection pass
    QRect *dirty;
    tsm_age_t since;
    int cw, ch;
    QFont const *font, *bold;
    bool colour;
};
}

static int kt_draw_cell(struct tsm_screen *, uint64_t, const uint32_t *ch, size_t len, unsigned int width,
                        unsigned int x, unsigned int y, const struct tsm_screen_attr *attr, tsm_age_t age, void *data) {
    DrawCtx *c = static_cast<DrawCtx*>(data);
    if (!width)
        return 0; // second half of a wide char
    QRect r(x * c->cw, y * c->ch, width * c->cw, c->ch);
    if (!c->p) {
        if (!age || !c->since || age > c->since)
            *c->dirty |= r;
        return 0;
    }
    QColor fg, bg;
    KtTerm::cellColours(attr, c->colour, &fg, &bg);
    c->p->fillRect(r, bg);
    if (len) {
        c->p->setFont(attr->bold ? *c->bold : *c->font);
        c->p->setPen(fg);
        c->p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, QString::fromUcs4(reinterpret_cast<const uint*>(ch), len));
    }
    if (attr->underline)
        c->p->fillRect(r.left(), r.bottom() - 1, r.width(), 2, fg);
    return 0;
}

void KtTerm::repaintDirty() {
    QRect dirty;
    DrawCtx c = {nullptr, &dirty, lastAge, cw, ch, &font, &bold, colour};
    lastAge = tsm_screen_draw(screen, kt_draw_cell, &c);
    if (!dirty.isEmpty())
        update(dirty);
}

void KtTerm::paintEvent(QPaintEvent *e) {
    QPainter p(this); // clipped to the dirty rect, so only changed cells hit the e-ink
    p.fillRect(e->rect(), Qt::white); // the strip right/below the last full cell
    DrawCtx c = {&p, nullptr, 0, cw, ch, &font, &bold, colour};
    tsm_screen_draw(screen, kt_draw_cell, &c);
}

void KtTerm::resizeEvent(QResizeEvent *) {
    struct winsize ws = {};
    ws.ws_col = width() / cw;
    ws.ws_row = height() / ch;
    tsm_screen_resize(screen, ws.ws_col, ws.ws_row);
    if (master >= 0)
        ioctl(master, TIOCSWINSZ, &ws); // kernel sends SIGWINCH to the foreground job
    lastAge = 0;
    update();
}
