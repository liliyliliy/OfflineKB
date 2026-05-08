#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFutureWatcher>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QPushButton>
#include <QString>
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

    // ===== RAG 问答相关槽 =====
    // 接收 ChatWidget::sendMessage 信号，组装上下文并异步调用 RagEngine
    void onChatMessage(const QString &msg);
    // QtConcurrent 任务完成后由 watcher 触发，回到 UI 线程显示 AI 回答
    void onRagFinished(const QString &answer);

private:
    void setupUi();
    void setupMenus();
    void refreshDocumentList(const QString &keyword = QString());
    void refreshSearchResults(const std::vector<std::pair<int, double>> &results);
    void refreshSemanticResults(const std::vector<std::pair<int, float>> &results);
    void updateStatusBarCount();
    void importFiles(const QStringList &filePaths);
    bool isSupportedDocument(const QString &filePath) const;

    // ===== 文档检索 Tab 控件 =====
    QLineEdit   *searchLineEdit_;
    QPushButton *searchButton_;
    QListWidget *documentListWidget_;
    QTextEdit   *previewTextEdit_;

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
};

#endif // MAINWINDOW_H
