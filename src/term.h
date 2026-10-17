#ifndef KT_TERM_H
#define KT_TERM_H

#include <sys/types.h>
#include <stdint.h>

#include <QFont>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#define KT_HISTFILE "/tmp/koboterminal_history"

class QSocketNotifier;
struct tsm_screen;
struct tsm_vte;

// KtTerm is a terminal widget: a shell on a real PTY, parsed by libtsm, drawn
// as a character grid. Only changed cells are repainted, so e-ink refreshes
// stay small.
class KtTerm : public QWidget {
public:
    // cols x rows is the initial grid; font sets the cell size.
    KtTerm(QFont const& font, int cols, int rows, QWidget *parent = nullptr);
    ~KtTerm();

    bool start();                              // spawn the shell
    void send(QByteArray const& bytes);        // raw input to the PTY
    void sendKey(uint32_t keysym);             // XKB keysym (arrows etc.), honours app cursor mode
    QPointer<QObject> exitTarget;              // gets reject() when the shell exits

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct tsm_screen *screen = nullptr;
    struct tsm_vte *vte = nullptr;
    int master = -1;
    pid_t child = 0;
    QSocketNotifier *notifier = nullptr;
    QTimer flush;           // coalesces repaints (e-ink)
    QTimer reaper;          // notices the shell exiting
    uint32_t lastAge = 0;   // tsm age at the last repaint
    QFont font, bold;
    int cw, ch;             // cell size in pixels

    void readPty();
    void shellExited();
    void repaintDirty();
};

#endif
