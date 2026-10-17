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
    setFixedSize(cols * cw, rows * ch);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    tsm_screen_new(&screen, nullptr, nullptr);
    tsm_screen_set_max_sb(screen, 0); // no scrollback UI, so don't keep any
    tsm_screen_resize(screen, cols, rows);
    tsm_vte_new(&vte, screen, [](struct tsm_vte *, const char *u8, size_t len, void *self) {
        static_cast<KtTerm*>(self)->send(QByteArray::fromRawData(u8, len));
    }, this, nullptr, nullptr);

    flush.setSingleShot(true);
    flush.setInterval(150);
    connect(&flush, &QTimer::timeout, this, &KtTerm::repaintDirty);
}

KtTerm::~KtTerm() {
    if (child > 0) {
        // Not reaped yet, so its pid (= the session id) can't have been reused.
        kt_kill_session(child);
        kt_reap(child);
    }
    delete notifier; // unregister before the fd is closed
    if (master >= 0)
        ::close(master);
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

    pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        nh_log("forkpty: %m");
        master = -1;
        return false;
    }
    if (pid == 0) {
        // Nickel may block or ignore signals; exec keeps ignored ones, which
        // would break ^C. Also don't leak Nickel's fds (e.g. on /mnt/onboard).
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int s = 1; s < NSIG; s++)
            signal(s, SIG_DFL);
        for (int fd = 3; fd < maxfd; fd++)
            ::close(fd);
        if (chdir("/")) {} // can't fail for "/"; the braces silence warn_unused_result
        execve(sh, argv, envp.data());
        _exit(127);
    }
    child = pid;
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    fcntl(master, F_SETFD, FD_CLOEXEC);
    notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &KtTerm::readPty);
    // A background job keeps the PTY open after the shell exits, so EOF alone
    // can't detect `exit`. Poll the shell's status while the terminal is open.
    // ECHILD covers Nickel ignoring SIGCHLD (children are auto-reaped then).
    connect(&reaper, &QTimer::timeout, this, [this]() {
        pid_t r = waitpid(child, nullptr, WNOHANG);
        if (r == child || (r < 0 && errno == ECHILD)) {
            // Reaped: its pid may be reused from now on, so kill the leftover
            // jobs (they still pin the session id) now and forget the pid.
            kt_kill_session(child);
            child = 0;
            shellExited();
        }
    });
    reaper.start(1000);
    return true;
}

void KtTerm::shellExited() {
    reaper.stop();
    if (notifier)
        notifier->setEnabled(false);
    if (exitTarget)
        QMetaObject::invokeMethod(exitTarget, "reject", Qt::QueuedConnection);
    exitTarget = nullptr; // once
}

void KtTerm::send(QByteArray const& b) {
    // ponytail: drops the rest if the PTY input buffer (~4 KB) is full; only huge pastes hit it.
    for (int off = 0; master >= 0 && off < b.size();) {
        ssize_t n = write(master, b.constData() + off, b.size() - off);
        if (n > 0) {
            off += n;
        } else if (n < 0 && errno != EINTR) {
            if (errno != EAGAIN)
                nh_log("pty write: %m");
            break;
        }
    }
}

void KtTerm::sendKey(uint32_t keysym) {
    tsm_vte_handle_keyboard(vte, keysym, 0, 0, TSM_VTE_INVALID);
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
    if (!flush.isActive())
        flush.start();
}

namespace {
struct DrawCtx {
    QPainter *p;          // paint pass, or null for the dirty-collection pass
    QRect *dirty;
    tsm_age_t since;
    int cw, ch;
    QFont const *font, *bold;
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
    // e-ink: colours are ignored, only inverse (cursor, status bars) matters.
    QColor fg = attr->inverse ? Qt::white : Qt::black;
    c->p->fillRect(r, attr->inverse ? Qt::black : Qt::white);
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
    DrawCtx c = {nullptr, &dirty, lastAge, cw, ch, &font, &bold};
    lastAge = tsm_screen_draw(screen, kt_draw_cell, &c);
    if (!dirty.isEmpty())
        update(dirty);
}

void KtTerm::paintEvent(QPaintEvent *) {
    QPainter p(this); // clipped to the dirty rect, so only changed cells hit the e-ink
    DrawCtx c = {&p, nullptr, 0, cw, ch, &font, &bold};
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
