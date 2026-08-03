#ifdef __cplusplus
#include <QImageIOHandler>
#include <QObject>
#include <QtPlugin>

/*
 * Fake image plugin used only to make Qt auto-load the shared library.
 * NickelHook logic is executed by a constructor in C code.
 */
class NHPlugin : public QImageIOPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE
                        "nhplugin.json")
public:
  Capabilities capabilities(QIODevice *, QByteArray const &) const {
    return Capabilities();
  }
  QImageIOHandler *create(QIODevice *,
                          QByteArray const & = QByteArray()) const {
    return 0;
  }
};

#endif
