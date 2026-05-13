#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFutureWatcher>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTextEdit>

#include "chatwidget.h"
#include "databasemanager.h"
#include "embeddingengine.h"
#include "ragengine.h"
#include "searchengine.h"
#include "tokenizer.h"
#include "vectorindex.h"

class QLineEdit;
class QListWidget;
class QLabel;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onImportDocuments();
    void onSearch();
    void onSemanticSearch();
    void onDocumentClicked(QListWidgetItem *item);
    void onShowAbout();
    void onShowModelStatus();
    void onSourceLinkClicked(int index);

    // ===== RAG 问答相关槽 =====
    // 接收 ChatWidget::sendMessage 信号，组装上下文并异步调用 RagEngine
    void onChatMessage(const QString &msg);
    // QtConcurrent 任务完成后由 watcher 触发，回到 UI 线程显示 AI 回答
    void onRagFinished(const QString &answer);

private:
    struct RagSourceEntry {
        int documentId;
        int chunkId;
        int chunkIndex;
        QString title;
        QString chunkContent;
    };

    struct RagContextBundle {
        QString context;
        QString sources;
        QString scopeLabel;
        QVector<RagSourceEntry> entries;
    };

    void setupUi();
    void setupMenus();
    void refreshDocumentList(const QString &keyword = QString());
    void refreshSearchResults(const std::vector<std::pair<int, double>> &results);
    void refreshSemanticResults(const std::vector<std::pair<int, float>> &results);
    void updateStatusBarCount();
    void importFiles(const QStringList &filePaths);
    bool isSupportedDocument(const QString &filePath) const;
    QString resolveModelPath(const QString& fileName, QStringList* triedPaths = nullptr) const;
    QVector<QString> splitDocumentIntoChunks(const QString& content) const;
    void ensureChunksForExistingDocuments(const QVector<Document>& docs);
    void rebuildChunkIndexes(bool tryLoadVectorIndex);
    RagContextBundle buildRagContext(const QString& question);
    QString vectorIndexPath() const;
    QString vectorIndexMetaPath() const;

    // ===== 文档检索 Tab 控件 =====
    QLineEdit   *searchLineEdit_;
    QPushButton *searchButton_;
    QListWidget *documentListWidget_;
    QTextEdit   *previewTextEdit_;
    QLabel      *embeddingStatusLabel_ = nullptr;

    // ===== 顶层 Tab 容器（Tab1: 检索，Tab2: AI 问答） =====
    QTabWidget *tabWidget_  = nullptr;
    ChatWidget *chatWidget_ = nullptr;

    // ===== 业务引擎 =====
    DatabaseManager  databaseManager_;
    SearchEngine    *searchEngine_   = nullptr;
    Tokenizer       *tokenizer_      = nullptr;
    EmbeddingEngine *embeddingEngine_= nullptr;
    VectorIndex     *vectorIndex_    = nullptr;

    // ===== RAG 引擎与异步执行 =====
    RagEngine                *ragEngine_  = nullptr;
    QFutureWatcher<QString>  *ragWatcher_ = nullptr;
    bool                      ragBusy_    = false;
    QString                   lastRagSources_;
    QVector<RagSourceEntry>   lastRagSourceEntries_;
    int                       selectedDocumentId_ = -1;
    QString                   embeddingStatusText_ = QStringLiteral("向量: 未初始化");
    bool                      embeddingModelLoaded_ = false;
};

#endif // MAINWINDOW_H
