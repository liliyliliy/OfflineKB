#include "mainwindow.h"

#include "document.h"

#include <QAction>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QVBoxLayout>

#include <exception>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      searchLineEdit_(nullptr),
      searchButton_(nullptr),
      documentListWidget_(nullptr),
      previewTextEdit_(nullptr) {
    setWindowTitle(QString::fromUtf8("离线知识库系统"));
    resize(1100, 700);
    setAcceptDrops(true);

    try {
        databaseManager_.initialize();
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QString::fromUtf8("数据库错误"), QString::fromUtf8(ex.what()));
    }

    setupUi();
    setupMenus();
    refreshDocumentList();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* searchLayout = new QHBoxLayout();
    searchLineEdit_ = new QLineEdit(centralWidget);
    searchLineEdit_->setPlaceholderText(QString::fromUtf8("请输入关键词搜索标题或内容"));
    searchButton_ = new QPushButton(QString::fromUtf8("搜索"), centralWidget);
    searchLayout->addWidget(searchLineEdit_);
    searchLayout->addWidget(searchButton_);

    auto* splitter = new QSplitter(Qt::Horizontal, centralWidget);
    documentListWidget_ = new QListWidget(splitter);
    previewTextEdit_ = new QTextEdit(splitter);
    previewTextEdit_->setReadOnly(true);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    mainLayout->addLayout(searchLayout);
    mainLayout->addWidget(splitter);

    setCentralWidget(centralWidget);
    statusBar()->showMessage(QString::fromUtf8("文档总数: 0"));

    connect(searchButton_, &QPushButton::clicked, this, &MainWindow::onSearchDocuments);
    connect(searchLineEdit_, &QLineEdit::returnPressed, this, &MainWindow::onSearchDocuments);
    connect(documentListWidget_, &QListWidget::itemClicked, this, &MainWindow::onDocumentClicked);
}

void MainWindow::setupMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    QAction* importAction = fileMenu->addAction(QString::fromUtf8("导入文档"));
    QAction* exitAction = fileMenu->addAction(QString::fromUtf8("退出"));

    QMenu* helpMenu = menuBar()->addMenu(QString::fromUtf8("帮助"));
    QAction* aboutAction = helpMenu->addAction(QString::fromUtf8("关于"));

    connect(importAction, &QAction::triggered, this, &MainWindow::onImportDocuments);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);
}

void MainWindow::onImportDocuments() {
    const QStringList files =
        QFileDialog::getOpenFileNames(this, QString::fromUtf8("选择要导入的文档"), QString(),
                                      QString::fromUtf8("文档文件 (*.txt *.md)"));
    if (files.isEmpty()) {
        return;
    }
    importFiles(files);
}

void MainWindow::onSearchDocuments() {
    refreshDocumentList(searchLineEdit_->text().trimmed());
}

void MainWindow::onDocumentClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }

    const int documentId = item->data(Qt::UserRole).toInt();
    try {
        const Document doc = databaseManager_.getDocumentById(documentId);
        previewTextEdit_->setPlainText(doc.content);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("读取失败"), QString::fromUtf8(ex.what()));
    }
}

void MainWindow::onShowAbout() {
    QMessageBox::about(this, QString::fromUtf8("关于"),
                       QString::fromUtf8("离线知识库系统\n第一阶段骨架版本\n基于 Qt6 + SQLite"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList filePaths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (isSupportedDocument(path)) {
            filePaths.push_back(path);
        }
    }

    if (!filePaths.isEmpty()) {
        importFiles(filePaths);
        event->acceptProposedAction();
    }
}

void MainWindow::refreshDocumentList(const QString& keyword) {
    documentListWidget_->clear();
    previewTextEdit_->clear();

    try {
        const QVector<Document> docs = databaseManager_.searchDocuments(keyword);
        for (const Document& doc : docs) {
            QString displayTitle = doc.title.trimmed();
            if (displayTitle.isEmpty()) {
                displayTitle = QFileInfo(doc.filePath).fileName();
            }
            auto* item = new QListWidgetItem(displayTitle, documentListWidget_);
            item->setData(Qt::UserRole, doc.id);
            item->setToolTip(doc.filePath);
        }
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("查询失败"), QString::fromUtf8(ex.what()));
    }

    updateStatusBarCount();
}

void MainWindow::updateStatusBarCount() {
    try {
        const int count = databaseManager_.getDocumentsCount();
        statusBar()->showMessage(QString::fromUtf8("文档总数: %1").arg(count));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8("状态更新失败: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::importFiles(const QStringList& filePaths) {
    int successCount = 0;
    QStringList failedFiles;

    for (const QString& filePath : filePaths) {
        if (!isSupportedDocument(filePath)) {
            continue;
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            failedFiles.push_back(QFileInfo(filePath).fileName());
            continue;
        }

        const QByteArray bytes = file.readAll();
        const QString content = QString::fromUtf8(bytes);
        const QString title = QFileInfo(filePath).completeBaseName();

        try {
            databaseManager_.insertDocument(title, content, QDir::toNativeSeparators(filePath));
            ++successCount;
        } catch (const std::exception&) {
            failedFiles.push_back(QFileInfo(filePath).fileName());
        }
    }

    refreshDocumentList(searchLineEdit_->text().trimmed());

    QString message = QString::fromUtf8("成功导入 %1 个文档").arg(successCount);
    if (!failedFiles.isEmpty()) {
        message += QString::fromUtf8("，失败: %1").arg(failedFiles.join(", "));
    }
    QMessageBox::information(this, QString::fromUtf8("导入结果"), message);
}

bool MainWindow::isSupportedDocument(const QString& filePath) const {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == "txt" || suffix == "md";
}