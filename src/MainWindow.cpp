#include "MainWindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QGroupBox>
#include <QLabel>
#include <QSizePolicy>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("ABL Tool — Qualcomm Bootloader Editor");
    setMinimumSize(1100, 700);
    setAcceptDrops(true);

    // ── Toolbar ──────────────────────────────────────────────────
    QToolBar *tb = addToolBar("Main");
    tb->setMovable(false);

    m_btnOpen = new QPushButton("📂 Open ABL");
    m_btnOpen->setToolTip("Open abl.elf / abl.img file");
    tb->addWidget(m_btnOpen);
    tb->addSeparator();

    m_btnCopyFvh = new QPushButton("📋 Copy _FVH block");
    m_btnCopyFvh->setToolTip("Save raw FVH block to a separate file");
    m_btnCopyFvh->setEnabled(false);
    tb->addWidget(m_btnCopyFvh);
    tb->addSeparator();

    m_btnExtract = new QPushButton("⬇ Extract & Decompress");
    m_btnExtract->setEnabled(false);
    tb->addWidget(m_btnExtract);

    m_btnRepack = new QPushButton("⬆ Compress & Repack");
    m_btnRepack->setEnabled(false);
    tb->addWidget(m_btnRepack);
    tb->addSeparator();

    m_btnSave = new QPushButton("💾 Save patched ABL");
    m_btnSave->setEnabled(false);
    tb->addWidget(m_btnSave);
    tb->addSeparator();

    m_btnGoTo = new QPushButton("→ Go to offset");
    m_btnGoTo->setEnabled(false);
    tb->addWidget(m_btnGoTo);

    m_btnSearch = new QPushButton("🔍 Search bytes");
    m_btnSearch->setEnabled(false);
    tb->addWidget(m_btnSearch);

    // ── Central layout ────────────────────────────────────────────
    QWidget *central = new QWidget;
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // Top splitter: block list | hex editor
    m_splitter = new QSplitter(Qt::Horizontal);

    // Left: FVH block list
    QGroupBox *blockGroup = new QGroupBox("FVH Blocks");
    QVBoxLayout *blockLayout = new QVBoxLayout(blockGroup);
    m_blockList = new QListWidget;
    m_blockList->setMaximumWidth(260);
    m_blockList->setMinimumWidth(200);
    blockLayout->addWidget(m_blockList);
    m_splitter->addWidget(blockGroup);

    // Right: hex editor
    QGroupBox *hexGroup = new QGroupBox("Decompressed Binary (editable)");
    QVBoxLayout *hexLayout = new QVBoxLayout(hexGroup);
    m_hexEditor = new HexEditor;
    hexLayout->addWidget(m_hexEditor);
    m_splitter->addWidget(hexGroup);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(m_splitter, 1);

    // Bottom: log
    QGroupBox *logGroup = new QGroupBox("Log");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logView = new QTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(140);
    m_logView->setFont(QFont("Monospace", 9));
    m_logView->setStyleSheet("background: #1a1a1a; color: #aaffaa;");
    logLayout->addWidget(m_logView);
    mainLayout->addWidget(logGroup);

    // Status bar
    m_statusLabel = new QLabel("Drop an ABL file here or click Open.");
    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    m_progress->setMaximumWidth(150);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progress);

    // Dark theme
    qApp->setStyle("Fusion");
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(45,  45,  45));
    dark.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    dark.setColor(QPalette::Base,            QColor(30,  30,  30));
    dark.setColor(QPalette::AlternateBase,   QColor(50,  50,  50));
    dark.setColor(QPalette::Text,            QColor(220, 220, 220));
    dark.setColor(QPalette::Button,          QColor(60,  60,  60));
    dark.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
    dark.setColor(QPalette::Highlight,       QColor(80, 120, 200));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(dark);

    // ── Worker thread ─────────────────────────────────────────────
    m_thread = new QThread(this);
    m_worker = new AblWorker;
    m_worker->moveToThread(m_thread);
    m_thread->start();

    // ── Connections ───────────────────────────────────────────────
    connect(m_btnOpen,    &QPushButton::clicked, this, &MainWindow::openFile);
    connect(m_btnCopyFvh, &QPushButton::clicked, this, &MainWindow::copyFvhBlock);
    connect(m_btnExtract, &QPushButton::clicked, this, &MainWindow::extractBlock);
    connect(m_btnRepack,  &QPushButton::clicked, this, &MainWindow::repackBlock);
    connect(m_btnSave,    &QPushButton::clicked, this, &MainWindow::saveOutput);
    connect(m_btnGoTo,    &QPushButton::clicked, this, &MainWindow::goToOffset);
    connect(m_btnSearch,  &QPushButton::clicked, this, &MainWindow::searchBytes);

    connect(m_blockList, &QListWidget::currentRowChanged, this, &MainWindow::onBlockSelected);

    connect(m_worker, &AblWorker::extractDone, this, &MainWindow::onExtractDone);
    connect(m_worker, &AblWorker::repackDone,  this, &MainWindow::onRepackDone);
    connect(m_worker, &AblWorker::error,        this, &MainWindow::onWorkerError);
    connect(m_worker, &AblWorker::progress,     this, &MainWindow::onWorkerProgress);

    connect(m_hexEditor, &HexEditor::dataChanged, this, [this]() {
        m_btnRepack->setEnabled(true);
        setWindowTitle("ABL Tool — * unsaved changes");
    });

    log("ABL Tool ready. Drop or open an abl.elf / abl.img file.");
}

MainWindow::~MainWindow() {
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
}

// ── Drag & Drop ───────────────────────────────────────────────────

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) loadFile(urls.first().toLocalFile());
}

// ── File I/O ──────────────────────────────────────────────────────

void MainWindow::openFile() {
    QString path = QFileDialog::getOpenFileName(this, "Open ABL file", {},
        "ABL images (*.elf *.img *.bin);;All files (*)");
    if (!path.isEmpty()) loadFile(path);
}

void MainWindow::loadFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Cannot open file: " + path);
        return;
    }
    m_ablData = f.readAll();
    m_ablPath = path;
    m_decompressed.clear();
    m_repackedAbl.clear();
    m_hexEditor->setData({});
    m_btnExtract->setEnabled(false);
    m_btnCopyFvh->setEnabled(false);
    m_btnRepack->setEnabled(false);
    m_btnSave->setEnabled(false);
    m_btnGoTo->setEnabled(false);
    m_btnSearch->setEnabled(false);

    log(QString("Loaded: %1 (%2 bytes)").arg(QFileInfo(path).fileName()).arg(m_ablData.size()));

    FvhParser parser(m_ablData);
    m_blocks = parser.findBlocks();

    if (m_blocks.isEmpty()) {
        QMessageBox::warning(this, "No FVH blocks", "No _FVH blocks found in this file.\nMake sure it is a valid ABL ELF image.");
        log("No _FVH blocks found.");
        return;
    }

    populateBlockList();
    log(QString("Found %1 FVH block(s).").arg(m_blocks.size()));
    m_statusLabel->setText(QString("File: %1 | %2 FVH block(s)").arg(QFileInfo(path).fileName()).arg(m_blocks.size()));
    setWindowTitle(QString("ABL Tool — %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::populateBlockList() {
    m_blockList->clear();
    for (int i = 0; i < m_blocks.size(); ++i) {
        const auto &b = m_blocks[i];
        QString lzmaInfo = b.hasLzma
            ? QString("LZMA @ +0x%1").arg(b.lzmaOffset, 0, 16)
            : QString("⚠ No LZMA detected (raw extract)");
        QString label = QString("Block %1\n  FV @ 0x%2\n  Size: %3 KiB\n  %4")
            .arg(i + 1)
            .arg(b.fvStart, 8, 16, QChar('0'))
            .arg(b.fvSize / 1024)
            .arg(lzmaInfo);
        auto *item = new QListWidgetItem(label);
        item->setFont(QFont("Monospace", 9));
        if (!b.hasLzma)
            item->setForeground(QColor(255, 180, 60));
        m_blockList->addItem(item);
    }
    m_blockList->setCurrentRow(0);
}

void MainWindow::onBlockSelected(int index) {
    if (index < 0 || index >= m_blocks.size()) return;
    m_selectedBlock = index;
    m_btnExtract->setEnabled(true);
    m_btnCopyFvh->setEnabled(true);
    m_decompressed.clear();
    m_hexEditor->setData({});
    m_btnRepack->setEnabled(false);
    m_btnGoTo->setEnabled(false);
    m_btnSearch->setEnabled(false);

    const auto &b = m_blocks[index];
    log(QString("Selected block %1: FV start=0x%2 size=%3 bytes, LZMA offset=+0x%4 size=%5 bytes")
        .arg(index + 1)
        .arg(b.fvStart, 8, 16, QChar('0'))
        .arg(b.fvSize)
        .arg(b.lzmaOffset, 0, 16)
        .arg(b.lzmaSize));
}

// ── Extract ───────────────────────────────────────────────────────

void MainWindow::extractBlock() {
    if (m_selectedBlock < 0) return;
    setUiBusy(true);
    log("Extracting and decompressing...");
    QMetaObject::invokeMethod(m_worker, "extract",
        Q_ARG(QByteArray, m_ablData),
        Q_ARG(FvhBlock, m_blocks[m_selectedBlock]));
}

void MainWindow::onExtractDone(QByteArray decompressed) {
    m_decompressed = decompressed;
    const auto &b = m_blocks[m_selectedBlock];

    m_hexEditor->setData(decompressed);
    m_hexEditor->setHighlight(0, decompressed.size());
    m_btnGoTo->setEnabled(true);
    m_btnSearch->setEnabled(true);

    if (b.hasLzma) {
        log(QString("Decompressed OK. Size: %1 bytes (%.1f KiB)")
            .arg(decompressed.size())
            .arg(decompressed.size() / 1024.0));
        m_statusLabel->setText(QString("Decompressed %1 bytes — edit hex then Repack").arg(decompressed.size()));
    } else {
        log(QString("No LZMA found — showing raw FV bytes (%1 bytes). You can still inspect and edit.")
            .arg(decompressed.size()));
        m_statusLabel->setText(QString("Raw FV block: %1 bytes (no LZMA)").arg(decompressed.size()));
    }
    setUiBusy(false);
}

// ── Repack ────────────────────────────────────────────────────────

void MainWindow::repackBlock() {
    if (m_selectedBlock < 0 || m_decompressed.isEmpty()) {
        QMessageBox::information(this, "Nothing to repack", "Extract a block first, then edit bytes.");
        return;
    }

    auto reply = QMessageBox::question(this, "Repack",
        "Compress the edited binary and patch it back into the original ABL.\n\n"
        "This will replace the LZMA stream in the file. Continue?",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    setUiBusy(true);
    log("Compressing and repacking into ABL...");
    QMetaObject::invokeMethod(m_worker, "repack",
        Q_ARG(QByteArray, m_ablData),
        Q_ARG(FvhBlock, m_blocks[m_selectedBlock]),
        Q_ARG(QByteArray, m_hexEditor->data()));
}

void MainWindow::onRepackDone(QByteArray newAbl) {
    m_repackedAbl = newAbl;
    m_btnSave->setEnabled(true);
    log("Repack complete. Click 'Save patched ABL' to write to disk.");
    setUiBusy(false);
    m_statusLabel->setText("Repack done — save the patched ABL.");
    setWindowTitle(windowTitle().replace("* unsaved changes", "* ready to save"));
}

// ── Save ──────────────────────────────────────────────────────────

void MainWindow::saveOutput() {
    if (m_repackedAbl.isEmpty()) return;

    QString defaultName = QFileInfo(m_ablPath).baseName()
        + "_patched_"
        + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")
        + ".elf";

    QString path = QFileDialog::getSaveFileName(this, "Save patched ABL",
        QFileInfo(m_ablPath).dir().filePath(defaultName),
        "ABL images (*.elf *.img *.bin);;All files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Error", "Cannot write to: " + path);
        return;
    }
    f.write(m_repackedAbl);
    f.close();

    log(QString("Saved patched ABL → %1").arg(path));
    m_statusLabel->setText("Saved: " + QFileInfo(path).fileName());
    setWindowTitle(QString("ABL Tool — %1").arg(QFileInfo(path).fileName()));
}

// ── Copy FVH Block ────────────────────────────────────────────────

void MainWindow::copyFvhBlock() {
    if (m_selectedBlock < 0 || m_selectedBlock >= m_blocks.size()) return;

    const auto &block = m_blocks[m_selectedBlock];
    QString defaultName = QFileInfo(m_ablPath).baseName()
        + QString("_FVH_block%1").arg(m_selectedBlock + 1)
        + ".bin";

    QString path = QFileDialog::getSaveFileName(this, "Save _FVH block",
        QFileInfo(m_ablPath).dir().filePath(defaultName),
        "Binary files (*.bin);;All files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Error", "Cannot write to: " + path);
        return;
    }
    f.write(block.raw);
    f.close();

    log(QString("Saved _FVH block %1 → %2 (%3 bytes)")
        .arg(m_selectedBlock + 1)
        .arg(path)
        .arg(block.raw.size()));
    m_statusLabel->setText(QString("Copied FVH block %1 (%2 bytes)").arg(m_selectedBlock + 1).arg(block.raw.size()));
}

// ── Hex editor helpers ────────────────────────────────────────────

void MainWindow::goToOffset() {
    bool ok;
    QString text = QInputDialog::getText(this, "Go to offset",
        "Enter offset (hex, e.g. 0x1A3F or 1A3F):", QLineEdit::Normal, "", &ok);
    if (!ok || text.isEmpty()) return;
    text = text.trimmed().replace("0x", "", Qt::CaseInsensitive);
    qint64 off = text.toLongLong(&ok, 16);
    if (!ok) { log("Invalid offset."); return; }
    m_hexEditor->goTo(off);
    log(QString("Jumped to offset 0x%1").arg(off, 0, 16));
}

void MainWindow::searchBytes() {
    bool ok;
    QString text = QInputDialog::getText(this, "Search bytes",
        "Enter hex bytes to find (e.g. 5D 00 00 80 00):", QLineEdit::Normal, "", &ok);
    if (!ok || text.isEmpty()) return;

    QStringList parts = text.trimmed().split(QRegularExpression("\\s+"));
    QByteArray needle;
    for (const QString &p : parts) {
        bool cv;
        quint8 b = p.toUInt(&cv, 16);
        if (!cv) { log("Invalid hex byte: " + p); return; }
        needle.append(static_cast<char>(b));
    }

    const QByteArray &haystack = m_hexEditor->data();
    qint64 cur = m_hexEditor->property("cursorOffset").toLongLong();
    qint64 found = haystack.indexOf(needle, cur + 1);
    if (found < 0) found = haystack.indexOf(needle, 0); // wrap
    if (found < 0) {
        log(QString("Pattern not found: %1").arg(QString(text)));
        return;
    }
    m_hexEditor->goTo(found);
    m_hexEditor->setHighlight(found, needle.size());
    log(QString("Found at offset 0x%1").arg(found, 0, 16));
}

// ── Misc ──────────────────────────────────────────────────────────

void MainWindow::setUiBusy(bool busy) {
    m_btnOpen->setEnabled(!busy);
    m_btnCopyFvh->setEnabled(!busy && m_selectedBlock >= 0);
    m_btnExtract->setEnabled(!busy && m_selectedBlock >= 0);
    m_btnRepack->setEnabled(!busy && !m_decompressed.isEmpty());
    m_btnSave->setEnabled(!busy && !m_repackedAbl.isEmpty());
    m_progress->setVisible(busy);
}

void MainWindow::log(const QString &msg) {
    m_logView->append(QString("[%1] %2")
        .arg(QTime::currentTime().toString("HH:mm:ss"))
        .arg(msg));
}

void MainWindow::onWorkerError(QString message) {
    setUiBusy(false);
    log("ERROR: " + message);
    QMessageBox::critical(this, "Error", message);
    m_statusLabel->setText("Error — see log.");
}

void MainWindow::onWorkerProgress(QString message) {
    log(message);
    m_statusLabel->setText(message);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!m_repackedAbl.isEmpty() && windowTitle().contains("ready to save")) {
        auto reply = QMessageBox::question(this, "Unsaved changes",
            "You have a repacked ABL that hasn't been saved. Quit anyway?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) { event->ignore(); return; }
    }
    event->accept();
}
