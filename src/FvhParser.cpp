#include "FvhParser.h"
#include <functional>
#include <lzma.h>
#include <QDebug>
#include <cstring>

static constexpr quint8  LZMA_MAGIC_BYTE  = 0x5D;
static constexpr quint32 MIN_FV_SIZE      = 32768;
static constexpr qint64  MAX_DECOMP_SIZE  = 256 * 1024 * 1024; // 256 MiB safety cap

FvhParser::FvhParser(const QByteArray &data) : m_data(data) {}

QVector<FvhBlock> FvhParser::findBlocks(quint32 minSize) const {
    QVector<FvhBlock> result;
    const QByteArray sig = "_FVH";
    int pos = 0;

    while (true) {
        pos = m_data.indexOf(sig, pos);
        if (pos < 0) break;

        qDebug() << "[FVH] Found _FVH at offset" << QString("0x%1").arg(pos, 8, 16, QChar('0'));

        // Try multiple FV start candidates and size offsets
        struct Candidate { qint64 fvStart; qint64 sizeOff; };
        QVector<Candidate> cands = {
            { pos - 0x28, (pos - 0x28) + 0x20 }, // UEFI spec: sig at +0x28, size at +0x20
            { pos - 0x10, (pos - 0x10) + 0x20 }, // Qualcomm variant
            { pos - 0x10, pos + 0x30 },            // original guess
            { pos,        pos + 0x10 },
        };

        bool added = false;
        for (auto &c : cands) {
            if (c.fvStart < 0) c.fvStart = 0;
            if (c.sizeOff + 4 > m_data.size()) continue;

            quint32 fvSize = 0;
            std::memcpy(&fvSize, m_data.constData() + c.sizeOff, 4);

            qDebug() << "  try fvStart=" << QString("0x%1").arg(c.fvStart,0,16)
                     << "sizeOff=" << QString("0x%1").arg(c.sizeOff,0,16)
                     << "fvSize=" << fvSize;

            if (fvSize == 0 || fvSize > 128u*1024*1024 || fvSize < minSize) continue;

            qint64 actualSize = qMin((qint64)fvSize, m_data.size() - c.fvStart);
            if (actualSize < (qint64)minSize) continue;

            FvhBlock blk;
            blk.fvhOffset  = pos;
            blk.fvStart    = c.fvStart;
            blk.fvSize     = (quint32)actualSize;
            blk.raw        = m_data.mid(c.fvStart, actualSize);
            blk.lzmaOffset = -1;
            blk.lzmaSize   = 0;
            blk.hasLzma    = false;
            LzmaParams params;
            blk.hasLzma = findLzmaStream(blk.raw, blk.lzmaOffset, blk.lzmaSize, params);
            qDebug() << "  -> ACCEPTED hasLzma=" << blk.hasLzma;
            result.append(blk);
            added = true;
            break;
        }

        if (!added) {
            // Fallback: add everything from nearest plausible FV start to EOF
            qint64 fvStart = qMax(0LL, (qint64)pos - 0x28);
            qint64 actualSize = m_data.size() - fvStart;
            if (actualSize >= (qint64)minSize) {
                qDebug() << "  -> fallback raw block";
                FvhBlock blk;
                blk.fvhOffset  = pos;
                blk.fvStart    = fvStart;
                blk.fvSize     = (quint32)actualSize;
                blk.raw        = m_data.mid(fvStart, actualSize);
                blk.lzmaOffset = -1;
                blk.lzmaSize   = 0;
                blk.hasLzma    = false;
                LzmaParams params;
                blk.hasLzma = findLzmaStream(blk.raw, blk.lzmaOffset, blk.lzmaSize, params);
                result.append(blk);
            } else {
                qDebug() << "  -> REJECTED";
            }
        }
        ++pos;
    }

    qDebug() << "[FVH] Total blocks:" << result.size();
    return result;
}

bool FvhParser::findLzmaStream(const QByteArray &fvRaw,
                                qint64 &offsetOut,
                                qint64 &sizeOut,
                                LzmaParams &paramsOut)
{
    const auto *d = reinterpret_cast<const quint8*>(fvRaw.constData());
    const qint64 sz = fvRaw.size();

    for (qint64 i = 0; i < sz - 13; ++i) {
        quint8 props = d[i];

        // Validate LZMA props: pb<=4, lp<=4, lc<=8, and total <= 224
        if (props > 224) continue;
        quint8 lc   = props % 9;
        quint8 rest = props / 9;
        quint8 lp   = rest % 5;
        quint8 pb   = rest / 5;
        if (pb > 4 || lp > 4 || lc > 8) continue;

        quint32 dictSize = 0;
        std::memcpy(&dictSize, d + i + 1, 4);

        // Accept any power-of-two-ish dict from 4KB to 256MB,
        // or the special values lzma_alone_encoder emits (2^n or 2^n+2^(n-1))
        if (dictSize < 4096 || dictSize > 256u * 1024 * 1024) continue;

        quint64 uncompSize = 0;
        std::memcpy(&uncompSize, d + i + 5, 8);

        // Sanity: either known size (< 512 MB) or unknown sentinel
        const quint64 UNKNOWN = 0xFFFFFFFFFFFFFFFFULL;
        if (uncompSize != UNKNOWN && uncompSize > 512ULL * 1024 * 1024) continue;

        // Quick sanity: try to validate first few bytes would decode (skip full decode here)
        paramsOut.uncompSize = uncompSize;
        std::memcpy(paramsOut.props, d + i, 5);

        offsetOut = i;
        sizeOut   = sz - i;
        return true;
    }
    return false;
}

// Try decompressing with a given decoder initializer
static QByteArray tryDecode(const uint8_t *inData, size_t inSize,
                             std::function<lzma_ret(lzma_stream*)> initFn,
                             size_t outBufSize, QString &errOut)
{
    QByteArray out(outBufSize, '\0');
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = initFn(&strm);
    if (ret != LZMA_OK) {
        errOut = QString("decoder init failed: %1").arg(ret);
        return {};
    }
    strm.next_in   = inData;
    strm.avail_in  = inSize;
    strm.next_out  = reinterpret_cast<uint8_t*>(out.data());
    strm.avail_out = outBufSize;
    ret = lzma_code(&strm, LZMA_FINISH);
    size_t outPos = outBufSize - strm.avail_out;
    lzma_end(&strm);
    if (ret != LZMA_STREAM_END && ret != LZMA_OK) {
        // LZMA error codes: 1=OK, 2=STREAM_END, 4=MEM_ERROR, 5=FORMAT_ERROR, 
        // 6=OPTIONS_ERROR, 7=PROG_ERROR, 8=DATA_ERROR, 9=BUF_ERROR
        const char* errNames[] = {"OK", "STREAM_END", "MEM_ERROR", "FORMAT_ERROR", 
                                  "OPTIONS_ERROR", "PROG_ERROR", "DATA_ERROR", "BUF_ERROR"};
        const char* errName = (ret >= 1 && ret <= 8) ? errNames[ret] : "UNKNOWN";
        errOut = QString("decode error: %1 (%2)").arg(ret).arg(errName);
        return {};
    }
    out.resize(outPos);
    return out;
}

QByteArray FvhParser::decompress(const FvhBlock &block, QString &errorOut) {
    if (!block.hasLzma || block.lzmaOffset < 0) {
        errorOut = "";
        return block.raw;
    }

    const auto *inData = reinterpret_cast<const uint8_t*>(
        block.raw.constData() + block.lzmaOffset);
    size_t inSize = static_cast<size_t>(block.lzmaSize);

    // Read declared uncompressed size from LZMA alone header (bytes 5..12)
    quint64 uncompSize = 0;
    if (inSize >= 13) std::memcpy(&uncompSize, inData + 5, 8);
    bool knownSize = (uncompSize != 0xFFFFFFFFFFFFFFFFULL) && (uncompSize > 0)
                     && (uncompSize < (quint64)MAX_DECOMP_SIZE);
    
    // For unknown size, start with a reasonable buffer and grow if needed
    size_t outBufSize = knownSize ? (size_t)uncompSize + 4096 : (size_t)(16 * 1024 * 1024);

    qDebug() << "[LZMA] lzmaOffset=" << Qt::hex << block.lzmaOffset
             << "inSize=" << inSize
             << "uncompSize=" << uncompSize
             << "props[0]=" << Qt::hex << (uint)inData[0]
             << "dictSize=" << *reinterpret_cast<const quint32*>(inData+1);

    QString err;
    QByteArray result;

    // 1. Try lzma_alone_decoder (LZMA1 with .lzma header: props+dictsize+uncompsize)
    result = tryDecode(inData, inSize,
        [](lzma_stream *s){ return lzma_alone_decoder(s, UINT64_MAX); },
        outBufSize, err);
    if (!result.isEmpty()) {
        qDebug() << "[LZMA] alone_decoder succeeded, size=" << result.size();
        return result;
    }
    qDebug() << "[LZMA] alone_decoder failed:" << err;

    // 2. Try auto_decoder (handles .lzma, .xz, raw)
    err.clear();
    result = tryDecode(inData, inSize,
        [](lzma_stream *s){ return lzma_auto_decoder(s, UINT64_MAX, 0); },
        outBufSize, err);
    if (!result.isEmpty()) {
        qDebug() << "[LZMA] auto_decoder succeeded, size=" << result.size();
        return result;
    }
    qDebug() << "[LZMA] auto_decoder failed:" << err;

    // 3. Scan forward up to 64 bytes to find a better LZMA header start
    for (int skip = 1; skip <= 64; ++skip) {
        if ((size_t)skip >= inSize - 13) break;
        const uint8_t *p = inData + skip;
        uint8_t props = p[0];
        if (props > 224) continue;
        uint8_t lc = props % 9, rest = props / 9;
        uint8_t lp = rest % 5, pb = rest / 5;
        if (pb > 4 || lp > 4 || lc > 8) continue;
        quint32 dict = 0; std::memcpy(&dict, p+1, 4);
        if (dict < 4096 || dict > 256u*1024*1024) continue;

        err.clear();
        result = tryDecode(p, inSize - skip,
            [](lzma_stream *s){ return lzma_alone_decoder(s, UINT64_MAX); },
            outBufSize, err);
        if (!result.isEmpty()) {
            qDebug() << "[LZMA] alone_decoder succeeded at skip=" << skip << "size=" << result.size();
            return result;
        }
    }

    // 4. Nothing worked — return raw bytes so user can inspect
    errorOut = QString("LZMA decompression failed with all methods. "
                       "Showing raw FV block bytes for manual inspection. "
                       "Last error: %1").arg(err);
    qDebug() << "[LZMA] all methods failed, returning raw bytes";
    return block.raw;
}

QByteArray FvhParser::repack(const QByteArray &originalData,
                              const FvhBlock   &block,
                              const QByteArray &patchedBinary,
                              QString          &errorOut)
{
    // 1. Compress patchedBinary with original LZMA props
    const auto *inData = reinterpret_cast<const uint8_t*>(patchedBinary.constData());
    size_t inSize = patchedBinary.size();

    // Get original props from the raw block
    const auto *origProps = reinterpret_cast<const uint8_t*>(
        block.raw.constData() + block.lzmaOffset);

    lzma_options_lzma opt;
    lzma_lzma_preset(&opt, LZMA_PRESET_DEFAULT);

    // Decode original props
    uint8_t props = origProps[0];
    opt.lc = props % 9;
    uint8_t rest = props / 9;
    opt.lp = rest % 5;
    opt.pb = rest / 5;
    std::memcpy(&opt.dict_size, origProps + 1, 4);

    lzma_filter filters[2];
    filters[0].id      = LZMA_FILTER_LZMA1;
    filters[0].options = &opt;
    filters[1].id      = LZMA_VLI_UNKNOWN;

    // Estimate output buffer (compressed is usually smaller, but allow 2x)
    size_t outBufSize = inSize + inSize / 2 + 65536;
    QByteArray compressed(outBufSize, '\0');

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_alone_encoder(&strm, &opt);
    if (ret != LZMA_OK) {
        errorOut = QString("lzma_alone_encoder init failed: %1").arg(ret);
        return {};
    }

    strm.next_in   = inData;
    strm.avail_in  = inSize;
    strm.next_out  = reinterpret_cast<uint8_t*>(compressed.data());
    strm.avail_out = outBufSize;

    ret = lzma_code(&strm, LZMA_FINISH);
    size_t compSize = outBufSize - strm.avail_out;
    lzma_end(&strm);

    if (ret != LZMA_STREAM_END) {
        errorOut = QString("LZMA compression failed: %1").arg(ret);
        return {};
    }
    compressed.resize(compSize);

    // 2. Check if compressed fits in original slot
    qint64 origLzmaSize = block.lzmaSize;
    if ((qint64)compSize > origLzmaSize) {
        errorOut = QString("Compressed size (%1 bytes) exceeds original LZMA slot (%2 bytes). "
                           "Patched binary is too large.")
                   .arg(compSize).arg(origLzmaSize);
        return {};
    }

    // 3. Patch: copy original file, replace LZMA bytes, zero-pad remainder
    QByteArray result = originalData;
    qint64 patchStart = block.fvStart + block.lzmaOffset;

    std::memcpy(result.data() + patchStart,
                compressed.constData(),
                compSize);

    // Zero-pad the rest of the original LZMA slot
    if ((qint64)compSize < origLzmaSize) {
        std::memset(result.data() + patchStart + compSize,
                    0x00,
                    origLzmaSize - compSize);
    }

    // 4. Update uncompressed size field in LZMA header (offset +5, 8 bytes LE)
    quint64 uncompSz = static_cast<quint64>(inSize);
    std::memcpy(result.data() + patchStart + 5, &uncompSz, 8);

    return result;
}
