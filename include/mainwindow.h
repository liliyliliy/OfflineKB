#pragma once

#include "databasemanager.h"
#include "searchengine.h"
#include "tokenizer.h"

#include <QMainWindow>
#include <vector>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTextEdit;
class QDragEnterEvent;
class QDropEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onImportDocuments();
    void onSearch();
    void onDocumentClicked(QListWidgetItem* item);
    void onShowAbout();

private:
    DatabaseManager databaseManager_;

    QLineEdit* searchLineEdit_;
    QPushButton* searchButton_;
    QListWidget* documentListWidget_;
    QTextEdit* previewTextEdit_;
    SearchEngine* searchEngine_;
    Tokenizer* tokenizer_;

    void setupUi();
    void setupMenus();
    void refreshDocumentList(const QString& keyword = QString());
    void refreshSearchResults(const std::vector<std::pair<int, double>>& results);
    void updateStatusBarCount();
    void importFiles(const QStringList& filePaths);
    bool isSupportedDocument(const QString& filePath) const;
};