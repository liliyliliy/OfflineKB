#pragma once

#include "databasemanager.h"

#include <QMainWindow>

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
    ~MainWindow() override = default;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onImportDocuments();
    void onSearchDocuments();
    void onDocumentClicked(QListWidgetItem* item);
    void onShowAbout();

private:
    DatabaseManager databaseManager_;

    QLineEdit* searchLineEdit_;
    QPushButton* searchButton_;
    QListWidget* documentListWidget_;
    QTextEdit* previewTextEdit_;

    void setupUi();
    void setupMenus();
    void refreshDocumentList(const QString& keyword = QString());
    void updateStatusBarCount();
    void importFiles(const QStringList& filePaths);
    bool isSupportedDocument(const QString& filePath) const;
};