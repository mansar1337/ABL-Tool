#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>

// Lightweight hex editor widget.
// Displays bytes as hex + ASCII side by side.
// Supports editing individual bytes by clicking a hex cell and typing two hex digits.
// Emits dataChanged() when any byte is modified.

class HexEditor : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit HexEditor(QWidget *parent = nullptr);

    void setData(const QByteArray &data);
    const QByteArray &data() const { return m_data; }
    bool isModified() const { return m_modified; }
    void clearModified() { m_modified = false; }

    // Jump to byte offset
    void goTo(qint64 offset);

    // Highlight a range (e.g., LZMA stream)
    void setHighlight(qint64 start, qint64 length);

signals:
    void dataChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateGeometry();
    qint64 posToOffset(int x, int y) const;
    void drawRow(QPainter &p, int row, int y);

    QByteArray m_data;
    qint64     m_cursorOffset  = 0;
    bool       m_cursorHiNib   = true;   // editing high nibble first
    bool       m_modified      = false;
    qint64     m_hlStart       = -1;
    qint64     m_hlLen         = 0;

    // Layout constants (computed in updateGeometry)
    int m_charW   = 10;
    int m_charH   = 16;
    int m_cols    = 16;  // bytes per row
    int m_addrW   = 0;
    int m_hexX    = 0;
    int m_asciiX  = 0;
    int m_rowH    = 0;
};
