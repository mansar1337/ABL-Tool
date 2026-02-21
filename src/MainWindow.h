#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QThread>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QSplitter>
#include <QTextEdit>
#include <QProgressBar>
#include "HexEditor.h"
#include "FvhParser.h"
#include "AblWorker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openFile();
    void onBlockSelected(int index);
    void extractBlock();
    void repackBlock();
    void saveOutput();
    void copyFvhBlock();
    void onExtractDone(QByteArray decompressed);
    void onRepackDone(QByteArray newAbl);
    void onWorkerError(QString message);
    void onWorkerProgress(QString message);
    void goToOffset();
    void searchBytes();

private:
    void loadFile(const QString &path);
    void populateBlockList();
    void setUiBusy(bool busy);
    void log(const QString &msg);

    // Data
    QByteArray        m_ablData;
    QString           m_ablPath;
    QVector<FvhBlock> m_blocks;
    int               m_selectedBlock = -1;
    QByteArray        m_decompressed;
    QByteArray        m_repackedAbl;

    // Worker thread
    QThread    *m_thread = nullptr;
    AblWorker  *m_worker = nullptr;

    // UI
    QSplitter    *m_splitter    = nullptr;
    QListWidget  *m_blockList   = nullptr;
    HexEditor    *m_hexEditor   = nullptr;
    QTextEdit    *m_logView     = nullptr;
    QLabel       *m_statusLabel = nullptr;
    QProgressBar *m_progress    = nullptr;

    QPushButton  *m_btnOpen    = nullptr;
    QPushButton  *m_btnExtract = nullptr;
    QPushButton  *m_btnRepack  = nullptr;
    QPushButton  *m_btnSave    = nullptr;
    QPushButton  *m_btnCopyFvh = nullptr;
    QPushButton  *m_btnGoTo    = nullptr;
    QPushButton  *m_btnSearch  = nullptr;
};
