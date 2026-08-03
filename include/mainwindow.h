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
#include "kbservice.h"

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

    void onChatMessage(const QString &msg);
    void onRagFinished(const QString &answer);

private:
    struct RagSourceEntry {
        int documentId;
        int chunkId;
        int chunkIndex;
        QString title;
        QString chunkContent;
    };

    void setupUi();
    void setupMenus();
    void refreshDocumentList(const QString &keyword = QString());
    void refreshSearchResults(const std::vector<std::pair<int, double>> &results);
    void updateStatusBarCount();
    void importFiles(const QStringList &filePaths);
    bool isSupportedDocument(const QString &filePath) const;

    QLineEdit   *searchLineEdit_;
    QPushButton *searchButton_;
    QListWidget *documentListWidget_;
    QTextEdit   *previewTextEdit_;
    QLabel      *embeddingStatusLabel_ = nullptr;

    QTabWidget *tabWidget_  = nullptr;
    ChatWidget *chatWidget_ = nullptr;

    KbService *kbService_ = nullptr;

    QFutureWatcher<QString>  *ragWatcher_ = nullptr;
    bool                      ragBusy_    = false;
    QString                   lastRagSources_;
    QVector<RagSourceEntry>   lastRagSourceEntries_;
    int                       selectedDocumentId_ = -1;
    QString                   embeddingStatusText_ = QStringLiteral("向量: 未初始化");
};

#endif // MAINWINDOW_H
