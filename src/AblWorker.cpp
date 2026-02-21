#include "AblWorker.h"
#include "FvhParser.h"

AblWorker::AblWorker(QObject *parent) : QObject(parent) {}

void AblWorker::extract(QByteArray ablData, FvhBlock block) {
    emit progress("Decompressing LZMA stream...");
    QString err;
    QByteArray result = FvhParser::decompress(block, err);
    if (!err.isEmpty()) {
        // decompress() returns raw bytes on failure with a warning — still usable
        emit progress("Warning: " + err);
    }
    if (result.isEmpty()) {
        emit error("Decompression returned empty result. File may be corrupted.");
    } else {
        emit progress(QString("Done: %1 bytes loaded into editor.").arg(result.size()));
        emit extractDone(result);
    }
}

void AblWorker::repack(QByteArray ablData, FvhBlock block, QByteArray patchedBinary) {
    emit progress("Compressing with original LZMA parameters...");
    QString err;
    QByteArray result = FvhParser::repack(ablData, block, patchedBinary, err);
    if (result.isEmpty()) {
        emit error(err);
    } else {
        emit progress(QString("Repack complete. Output size: %1 bytes.").arg(result.size()));
        emit repackDone(result);
    }
}
