// Host test for KtTerm: real PTY, input, ^C, shell exit, and session cleanup.
// Build and run with tests/run.sh (needs Qt5 dev headers; runs offscreen).
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <QTimer>
#include <libtsm.h>
#include "term.h"

extern "C" void nh_log(const char *fmt, ...) { (void)fmt; }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

template <typename F> static bool waitFor(F cond, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return cond();
}

static QByteArray slurp(QString const& p) {
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed() : QByteArray();
}

static bool alive(int pid) { // zombies count as dead (CI containers may lack an init that reaps)
    QByteArray s = slurp(QStringLiteral("/proc/%1/stat").arg(pid));
    int i = s.lastIndexOf(')');
    return !s.isEmpty() && i > 0 && s.at(i + 2) != 'Z';
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    char tmpl[] = "/tmp/kt_test_XXXXXX";
    QString dir = QString::fromLatin1(mkdtemp(tmpl));
    QByteArray d = dir.toUtf8();

    QDialog dlg;
    bool rejected = false;
    QObject::connect(&dlg, &QDialog::rejected, [&]() { rejected = true; });
    KtTerm *t = new KtTerm(QFont(QStringLiteral("monospace")), 60, 14, &dlg);
    t->exitTarget = &dlg;
    dlg.show();
    CHECK(t->start());

    // Input reaches the shell, and the shell knows the grid size.
    t->send("echo $((6*7)) $(stty size) $$ > " + d + "/a\r");
    CHECK(waitFor([&]() { return slurp(dir + "/a").startsWith("42 14 60 "); }));
    int shell = slurp(dir + "/a").split(' ').last().toInt();
    CHECK(alive(shell));

    // ^C interrupts the foreground job (signals were reset in the child).
    t->send("sh -c 'echo $$ > " + d + "/fg; exec sleep 100'\r");
    CHECK(waitFor([&]() { return !slurp(dir + "/fg").isEmpty(); }));
    t->sendKey('c', TSM_CONTROL_MASK, 'c'); // what Ctrl + typed "c" does
    t->send("echo after > " + d + "/b\r");
    CHECK(waitFor([&]() { return slurp(dir + "/b") == "after"; }, 3000));

    // A background job, to check it dies with the session.
    t->send("sleep 300 & echo $! > " + d + "/pid\r");
    CHECK(waitFor([&]() { return !slurp(dir + "/pid").isEmpty(); }));
    int bg = slurp(dir + "/pid").toInt();
    CHECK(alive(bg));

    t->grab(); // exercise the paint path

    // `exit` closes the dialog; deleting the widget kills the session.
    // `exit` closes the dialog; deleting the widget kills the session. Nothing
    // may survive: no shell (not even a zombie, we are its parent), no job,
    // and no cleanup timers left running in the app (no wakeups, no drain).
    t->send("exit\r");
    CHECK(waitFor([&]() { return rejected; }));
    delete t;
    CHECK(waitFor([&]() { return !alive(bg); }, 3000));
    CHECK(waitFor([&]() { return !QFile::exists(QStringLiteral("/proc/%1").arg(shell)); }, 3000));
    // (direct children only: the desktop Qt style may own timers of its own)
    CHECK(waitFor([&]() { return app.findChildren<QTimer*>(QString(), Qt::FindDirectChildrenOnly).isEmpty(); }, 12000));

    QDir(dir).removeRecursively();
    printf("term_test: OK\n");
    return 0;
}
