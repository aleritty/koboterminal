#ifndef KT_TERM_H
#define KT_TERM_H

#include <sys/types.h>
#include <stdint.h>

#include <QFont>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#define KT_HISTFILE "/tmp/koboterminal_history"

class QSocketNotifier;
struct tsm_screen;
struct tsm_screen_attr;
struct tsm_vte;

// KtTerm is a terminal widget: a shell on a real PTY, parsed by libtsm, drawn
// as a character grid. Only changed cells are repainted, so e-ink refreshes
// stay small.
class KtTerm : public QWidget {
public:
    // cols x rows is the initial grid; font sets the cell size. The width then
    // follows the layout (cols = width / cell width), the height is set by the caller.
    KtTerm(QFont const& font, int cols, int rows, QWidget *parent = nullptr);
    ~KtTerm();

    bool start();                              // spawn the shell
    void send(QByteArray const& bytes);        // raw input to the PTY
    // A key with modifiers (TSM_*_MASK), encoded by libtsm: honours app cursor mode,
    // Ctrl/Alt combos. For text, keysym = ASCII code (0 if none) and unicode = the char.
    void sendKey(uint32_t keysym, unsigned mods = 0, uint32_t unicode = 0xffffffff);
    QPointer<QObject> exitTarget;              // gets reject() when the shell exits
    bool colour = false;                       // colour e-ink: draw ANSI colours (else black on white)

    // The colours a cell is drawn with. Colour mode keeps text dark and
    // backgrounds light (readable on washed-out colour e-ink); mono is black on
    // white, swapped for inverse video.
    static void cellColours(const tsm_screen_attr *attr, bool colour, QColor *fg, QColor *bg);
    int cellHeight() const { return ch; }
    QSize sizeHint() const override { return hint; }
    QPointer<QObject> tapTarget;               // tapMethod is invoked on it on a tap (opens a keyboard)
    const char *tapMethod = "show";

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool event(QEvent *) override;
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
    QElapsedTimer pending;  // since the first unpainted output
    uint_fast32_t lastAge = 0; // tsm_age_t at the last repaint
    QFont font, bold;
    int cw, ch;             // cell size in pixels
    QSize hint;             // initial grid size in pixels
    QPoint pressPos;        // where the current touch started
    bool scrolled = false;  // viewing scrollback

    void writePty(const char *data, size_t len);
    void release(QPoint pos); // end of a tap or swipe
    void readPty();
    void shellExited();
    void repaintDirty();
};

#endif
