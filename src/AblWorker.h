#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include "FvhParser.h"

// Runs heavy operations (decompress / repack) in a background thread.
class AblWorker : public QObject {
    Q_OBJECT
public:
    explicit AblWorker(QObject *parent = nullptr);

public slots:
    void extract(QByteArray ablData, FvhBlock block);
    void repack(QByteArray ablData, FvhBlock block, QByteArray patchedBinary);

signals:
    void extractDone(QByteArray decompressed);
    void repackDone(QByteArray newAbl);
    void error(QString message);
    void progress(QString message);
};
