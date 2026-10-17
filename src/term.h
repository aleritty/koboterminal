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
    // A key with modifiers (TSM_*_MASK), encoded by libtsm: honours app cursor mode,
    // Ctrl/Alt combos. For text, keysym = ASCII code (0 if none) and unicode = the char.
    void sendKey(uint32_t keysym, unsigned mods = 0, uint32_t unicode = 0xffffffff);
    QPointer<QObject> exitTarget;              // gets reject() when the shell exits
    int rows() const { return height() / ch; }
    int cellHeight() const { return ch; }
    void setRows(int rows) { setFixedHeight(rows * ch); } // resizeEvent updates the grid and the PTY
    QPointer<QObject> tapTarget;               // gets tapped() on a tap (opens Nickel's keyboard)

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    struct tsm_screen *screen = nullptr;
    struct tsm_vte *vte = nullptr;
    int master = -1;
    pid_t child = 0;
    QSocketNotifier *notifier = nullptr;     // PTY output
    int exitPipe = -1;                       // EOF when the shell has exited
    QSocketNotifier *exitNotifier = nullptr;
    QTimer flush;           // coalesces repaints (e-ink)
    uint_fast32_t lastAge = 0; // tsm_age_t at the last repaint
    QFont font, bold;
    int cw, ch;             // cell size in pixels
    QPoint pressPos;        // where the current touch started
    bool scrolled = false;  // viewing scrollback

    void writePty(const char *data, size_t len);
    void readPty();
    void shellExited();
    void repaintDirty();
};

#endif
