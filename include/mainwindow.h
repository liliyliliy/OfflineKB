#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QPushButton>
#include <QTextEdit>
#include <QMainWindow>
#include <QListWidgetItem>
#include "databasemanager.h"
#include "searchengine.h"
#include "tokenizer.h"
#include "embeddingengine.h"
#include "vectorindex.h"

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

private:
    void setupUi();
    void setupMenus();
    void refreshDocumentList(const QString &keyword = QString());
    void refreshSearchResults(const std::vector<std::pair<int, double>> &results);
    void refreshSemanticResults(const std::vector<std::pair<int, float>> &results);
    void updateStatusBarCount();
    void importFiles(const QStringList &filePaths);
    bool isSupportedDocument(const QString &filePath) const;

    QLineEdit *searchLineEdit_;
    QPushButton *searchButton_;
    QListWidget *documentListWidget_;
    QTextEdit *previewTextEdit_;

    DatabaseManager databaseManager_;
    SearchEngine *searchEngine_;
    Tokenizer *tokenizer_;

    EmbeddingEngine *embeddingEngine_;
    VectorIndex *vectorIndex_;
};

#endif // MAINWINDOW_H