#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct FvhBlock {
    qint64  fvhOffset;      // offset of '_FVH' in original file
    qint64  fvStart;        // fvhOffset - 0x10 (real FV start)
    quint32 fvSize;         // size declared in FV header
    qint64  lzmaOffset;     // offset of LZMA stream inside the FV block (-1 if not found)
    qint64  lzmaSize;       // size of LZMA stream
    bool    hasLzma;        // whether a valid LZMA stream was detected
    QByteArray raw;         // raw bytes of the FV block (fvSize bytes from fvStart)
};

struct LzmaParams {
    quint8  props[5];       // LZMA properties (lc/lp/pb + dict size)
    quint64 uncompSize;     // uncompressed size from LZMA header (8 bytes LE, may be 0xFFFFFFFFFFFFFFFF)
};

class FvhParser {
public:
    explicit FvhParser(const QByteArray &data);

    QVector<FvhBlock> findBlocks(quint32 minSize = 32768) const;

    // Extract and decompress LZMA from a block
    // Returns decompressed bytes or empty on error
    static QByteArray decompress(const FvhBlock &block, QString &errorOut);

    // Compress data back with the same LZMA params, patch into original data
    // originalData is the full abl file; block is the original FvhBlock
    // Returns modified full abl bytes or empty on error
    static QByteArray repack(const QByteArray &originalData,
                             const FvhBlock   &block,
                             const QByteArray &patchedBinary,
                             QString          &errorOut);

private:
    static bool findLzmaStream(const QByteArray &fvRaw,
                               qint64 &offsetOut,
                               qint64 &sizeOut,
                               LzmaParams &paramsOut);

    const QByteArray &m_data;
};
