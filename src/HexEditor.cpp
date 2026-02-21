#include "HexEditor.h"
#include <QPainter>
#include <QScrollBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QFont>

HexEditor::HexEditor(QWidget *parent) : QAbstractScrollArea(parent) {
    QFont font("Monospace", 10);
    font.setStyleHint(QFont::TypeWriter);
    setFont(font);
    setFocusPolicy(Qt::StrongFocus);
    updateGeometry();
}

void HexEditor::setData(const QByteArray &data) {
    m_data = data;
    m_cursorOffset = 0;
    m_modified = false;
    updateGeometry();
    viewport()->update();
}

void HexEditor::goTo(qint64 offset) {
    if (offset < 0 || offset >= m_data.size()) return;
    m_cursorOffset = offset;
    int row = offset / m_cols;
    verticalScrollBar()->setValue(row);
    viewport()->update();
}

void HexEditor::setHighlight(qint64 start, qint64 length) {
    m_hlStart = start;
    m_hlLen   = length;
    viewport()->update();
}

void HexEditor::updateGeometry() {
    QFontMetrics fm(font());
    m_charW = fm.horizontalAdvance('F');
    m_charH = fm.height();
    m_rowH  = m_charH + 4;
    m_cols  = 16;
    // Address column: 8 hex digits + 2 spaces
    m_addrW  = m_charW * 10;
    // Hex area: "XX " per byte
    m_hexX   = m_addrW;
    // ASCII area after hex
    m_asciiX = m_hexX + m_cols * (m_charW * 3) + m_charW;

    if (!m_data.isEmpty()) {
        int rows = (m_data.size() + m_cols - 1) / m_cols;
        verticalScrollBar()->setRange(0, qMax(0, rows - viewport()->height() / m_rowH));
        verticalScrollBar()->setPageStep(viewport()->height() / m_rowH);
    } else {
        verticalScrollBar()->setRange(0, 0);
    }
    horizontalScrollBar()->setRange(0, 0);
}

void HexEditor::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateGeometry();
}

void HexEditor::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), QColor(30, 30, 30));

    if (m_data.isEmpty()) return;

    QFontMetrics fm(font());
    p.setFont(font());

    int firstRow = verticalScrollBar()->value();
    int visRows  = viewport()->height() / m_rowH + 2;
    int totalRows = (m_data.size() + m_cols - 1) / m_cols;

    for (int row = firstRow; row < qMin(firstRow + visRows, totalRows); ++row) {
        int y = (row - firstRow) * m_rowH + m_charH;
        drawRow(p, row, y);
    }
}

void HexEditor::drawRow(QPainter &p, int row, int y) {
    qint64 baseOff = (qint64)row * m_cols;

    // Address
    p.setPen(QColor(100, 150, 200));
    p.drawText(0, y, QString("%1").arg(baseOff, 8, 16, QChar('0')).toUpper() + "  ");

    for (int col = 0; col < m_cols; ++col) {
        qint64 off = baseOff + col;
        if (off >= m_data.size()) break;

        quint8 byte = static_cast<quint8>(m_data[off]);
        int hexX = m_hexX + col * m_charW * 3;
        int ascX = m_asciiX + col * m_charW;

        // Highlight
        bool inHighlight = m_hlStart >= 0 && off >= m_hlStart && off < m_hlStart + m_hlLen;
        bool isCursor    = (off == m_cursorOffset);

        if (inHighlight) {
            p.fillRect(hexX - 1, y - m_charH + 2, m_charW * 2 + 1, m_rowH - 2,
                       QColor(60, 80, 40));
        }
        if (isCursor) {
            p.fillRect(hexX - 1, y - m_charH + 2, m_charW * 2 + 1, m_rowH - 2,
                       QColor(80, 120, 200));
        }

        // Hex bytes
        QString hex = QString("%1").arg(byte, 2, 16, QChar('0')).toUpper();
        p.setPen(isCursor ? Qt::white : inHighlight ? QColor(180, 230, 130) : QColor(200, 200, 200));
        p.drawText(hexX, y, hex);

        // Separator every 8 bytes
        if (col == 7) {
            p.setPen(QColor(80, 80, 80));
            p.drawText(m_hexX + 8 * m_charW * 3 - m_charW / 2, y, "|");
        }

        // ASCII
        QChar ch = (byte >= 0x20 && byte < 0x7F) ? QChar(byte) : QChar('.');
        p.setPen(isCursor ? Qt::white : QColor(130, 180, 130));
        p.drawText(ascX, y, ch);
    }
}

void HexEditor::mousePressEvent(QMouseEvent *event) {
    if (m_data.isEmpty()) return;
    int row = verticalScrollBar()->value() + event->pos().y() / m_rowH;
    int relX = event->pos().x() - m_hexX;
    if (relX < 0) return;
    int col = relX / (m_charW * 3);
    if (col >= m_cols) return;
    qint64 off = (qint64)row * m_cols + col;
    if (off >= m_data.size()) return;
    m_cursorOffset = off;
    m_cursorHiNib  = true;
    viewport()->update();
}

void HexEditor::keyPressEvent(QKeyEvent *event) {
    if (m_data.isEmpty()) return;

    auto move = [&](qint64 delta) {
        qint64 newOff = qBound(0LL, m_cursorOffset + delta, (qint64)m_data.size() - 1);
        m_cursorOffset = newOff;
        m_cursorHiNib  = true;
        // Scroll into view
        int row = newOff / m_cols;
        int firstRow = verticalScrollBar()->value();
        int visRows  = viewport()->height() / m_rowH;
        if (row < firstRow) verticalScrollBar()->setValue(row);
        if (row >= firstRow + visRows) verticalScrollBar()->setValue(row - visRows + 1);
        viewport()->update();
    };

    switch (event->key()) {
    case Qt::Key_Right: move(1);      break;
    case Qt::Key_Left:  move(-1);     break;
    case Qt::Key_Down:  move(m_cols); break;
    case Qt::Key_Up:    move(-m_cols);break;
    case Qt::Key_PageDown: move(m_cols * (viewport()->height() / m_rowH)); break;
    case Qt::Key_PageUp:   move(-m_cols * (viewport()->height() / m_rowH)); break;
    default: {
        QString txt = event->text().toUpper();
        if (txt.isEmpty()) break;
        QChar c = txt[0];
        int nibble = -1;
        if (c >= '0' && c <= '9') nibble = c.unicode() - '0';
        if (c >= 'A' && c <= 'F') nibble = c.unicode() - 'A' + 10;
        if (nibble < 0) break;

        quint8 cur = static_cast<quint8>(m_data[m_cursorOffset]);
        if (m_cursorHiNib) {
            cur = (cur & 0x0F) | (nibble << 4);
            m_data[m_cursorOffset] = static_cast<char>(cur);
            m_cursorHiNib = false;
        } else {
            cur = (cur & 0xF0) | nibble;
            m_data[m_cursorOffset] = static_cast<char>(cur);
            m_cursorHiNib = true;
            move(1);
        }
        m_modified = true;
        emit dataChanged();
        viewport()->update();
        break;
    }
    }
}
