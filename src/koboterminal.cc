#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QSocketNotifier>
#include <QString>

#include <NickelHook.h>

#define KT_FIFO "/tmp/koboterminal.fifo"

static void (*ConfirmationDialogFactory_showOKDialog)(QString const& title, QString const& body);

static void kt_open() {
    ConfirmationDialogFactory_showOKDialog(QStringLiteral("KoboTerminal"), QStringLiteral("Hello from KoboTerminal."));
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
    QSocketNotifier *n = new QSocketNotifier(fd, QSocketNotifier::Read);
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

static struct nh_dlsym KoboTerminalDlsym[] = {
    {.name = "_ZN25ConfirmationDialogFactory12showOKDialogERK7QStringS2_", .out = nh_symoutptr(ConfirmationDialogFactory_showOKDialog)},
    {0},
};

NickelHook(
    .init  = kt_init,
    .info  = &KoboTerminal,
    .hook  = KoboTerminalHook,
    .dlsym = KoboTerminalDlsym,
)
