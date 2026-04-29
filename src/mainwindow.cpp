#include "mainwindow.h"

#include "document.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
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
      previewTextEdit_(nullptr),
      searchEngine_(nullptr),
      tokenizer_(nullptr) {
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

    try {
        // ===== 词典路径自检日志 =====
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString dictDir = QDir(appDir).absoluteFilePath("../resources/dict");

        qDebug() << "应用目录 appDir =" << appDir;
        qDebug() << "词典目录 dictDir =" << dictDir;
        qDebug() << "词典目录存在 =" << QDir(dictDir).exists();

        const QString dictPath = QDir(dictDir).filePath("jieba.dict.utf8");
        const QString hmmPath = QDir(dictDir).filePath("hmm_model.utf8");
        const QString userDictPath = QDir(dictDir).filePath("user.dict.utf8");
        const QString idfPath = QDir(dictDir).filePath("idf.utf8");
        const QString stopWordPath = QDir(dictDir).filePath("stop_words.utf8");

        qDebug() << "[词典检查] jieba.dict.utf8 =" << QFileInfo::exists(dictPath) << dictPath;
        qDebug() << "[词典检查] hmm_model.utf8 =" << QFileInfo::exists(hmmPath) << hmmPath;
        qDebug() << "[词典检查] user.dict.utf8 =" << QFileInfo::exists(userDictPath) << userDictPath;
        qDebug() << "[词典检查] idf.utf8 =" << QFileInfo::exists(idfPath) << idfPath;
        qDebug() << "[词典检查] stop_words.utf8 =" << QFileInfo::exists(stopWordPath) << stopWordPath;
        // ===== 词典路径自检日志结束 =====

        const std::string dictDirStd = QDir::toNativeSeparators(dictDir).toStdString();

        tokenizer_ = new Tokenizer(dictDirStd);
        searchEngine_ = new SearchEngine(dictDirStd);

        const QVector<Document> docs = databaseManager_.searchDocuments(QString());
        std::vector<Document> allDocs;
        allDocs.reserve(docs.size());
        for (const auto& doc : docs) {
            allDocs.push_back(doc);
        }
        searchEngine_->buildIndex(allDocs);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("检索初始化失败"), QString::fromUtf8(ex.what()));
    }
}

MainWindow::~MainWindow() {
    delete searchEngine_;
    searchEngine_ = nullptr;
    delete tokenizer_;
    tokenizer_ = nullptr;
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

    connect(searchButton_, &QPushButton::clicked, this, &MainWindow::onSearch);
    connect(searchLineEdit_, &QLineEdit::returnPressed, this, &MainWindow::onSearch);
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

void MainWindow::onSearch() {
    const QString keyword = searchLineEdit_->text().trimmed();
    if (keyword.isEmpty()) {
        refreshDocumentList();
        return;
    }

    if (searchEngine_ == nullptr) {
        refreshDocumentList(keyword);
        return;
    }

    try {
        // 查询词使用 UTF-8，确保中文查询分词正确
        const QByteArray queryUtf8 = keyword.toUtf8();
        const std::string query(queryUtf8.constData(), static_cast<size_t>(queryUtf8.size()));

        const auto results = searchEngine_->search(query);
        refreshSearchResults(results);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("搜索失败"), QString::fromUtf8(ex.what()));
    }
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

void MainWindow::refreshSearchResults(const std::vector<std::pair<int, double>>& results) {
    documentListWidget_->clear();
    previewTextEdit_->clear();

    for (const auto& [docId, score] : results) {
        try {
            const Document doc = databaseManager_.getDocumentById(docId);
            QString displayTitle = doc.title.trimmed();
            if (displayTitle.isEmpty()) {
                displayTitle = QFileInfo(doc.filePath).fileName();
            }
            displayTitle += QString::fromUtf8("  (BM25: %1)").arg(score, 0, 'f', 3);
            auto* item = new QListWidgetItem(displayTitle, documentListWidget_);
            item->setData(Qt::UserRole, doc.id);
            item->setToolTip(doc.filePath);
        } catch (const std::exception&) {
            // 忽略不存在或讀取失敗的文檔，保證結果列表穩定展示
        }
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
        QString content = QString::fromUtf8(bytes);

// 如果出现替换字符，说明 UTF-8 解码失败，回退到系统本地编码（Windows 常见 GBK）
if (content.contains(QChar::ReplacementCharacter)) {
    content = QString::fromLocal8Bit(bytes);
}
        const QString title = QFileInfo(filePath).completeBaseName();

        try {
            const int newId =
                databaseManager_.insertDocument(title, content, QDir::toNativeSeparators(filePath));
            if (searchEngine_ != nullptr) {
                Document newDoc;
                newDoc.id = newId;
                newDoc.title = title;
                newDoc.content = content;
                newDoc.filePath = QDir::toNativeSeparators(filePath);
                searchEngine_->addDocument(newDoc);
            }
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