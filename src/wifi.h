#ifndef KT_WIFI_H
#define KT_WIFI_H

#include <QObject>

// Nickel's type for the veto flag in WirelessWatchdog::aboutToKillWifi
// (same as NickelDBus). The name must match for the string-based connect.
typedef bool PermissionRequest;

// While alive (parented to the terminal dialog), denies Nickel's idle Wi-Fi
// shutdown, so SSH sessions and downloads started from the terminal survive.
class KtWifiKeepAlive : public QObject {
    Q_OBJECT
public:
    explicit KtWifiKeepAlive(QObject *watchdog, QObject *parent) : QObject(parent) {
        // Direct: the flag must be written before Nickel reads it back.
        connect(watchdog, SIGNAL(aboutToKillWifi(PermissionRequest*)), this, SLOT(deny(PermissionRequest*)), Qt::DirectConnection);
    }
public slots:
    void deny(PermissionRequest *allow) {
        if (allow)
            *allow = false;
    }
};

#endif
