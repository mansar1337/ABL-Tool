#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ABL Tool");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("xendr4x");

    MainWindow w;
    w.show();

    // Support opening file from command line
    if (argc > 1) {
        QMetaObject::invokeMethod(&w, "loadFile",
            Qt::QueuedConnection,
            Q_ARG(QString, QString::fromLocal8Bit(argv[1])));
    }

    return app.exec();
}
