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
      tokenizer_(nullptr),
      embeddingEngine_(nullptr),
      vectorIndex_(nullptr) {
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
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString dictDir = QDir(appDir).absoluteFilePath("../resources/dict");
        const std::string dictDirStd = QDir::toNativeSeparators(dictDir).toStdString();

        tokenizer_ = new Tokenizer(dictDirStd);
        searchEngine_ = new SearchEngine(dictDirStd);

        QVector<Document> docs = databaseManager_.searchDocuments(QString());
        std::vector<Document> allDocs;
        allDocs.reserve(docs.size());
        for (const auto& doc : docs) {
            allDocs.push_back(doc);
        }
        searchEngine_->buildIndex(allDocs);

        // ====================== 向量模块初始化 ======================
        embeddingEngine_ = new EmbeddingEngine();
        embeddingEngine_->init(tokenizer_);

        vectorIndex_ = new VectorIndex();
        vectorIndex_->init(1024, 100000);

        for (const Document& doc : docs) {
            std::string content = doc.content.toStdString();
            std::vector<float> vec = embeddingEngine_->encode(content);
            vectorIndex_->addVectors(vec, doc.id);
        }
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("引擎初始化失败"), QString::fromUtf8(ex.what()));
    }
}

MainWindow::~MainWindow() {
    delete searchEngine_;
    delete tokenizer_;
    delete embeddingEngine_;
    delete vectorIndex_;
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* searchLayout = new QHBoxLayout();
    searchLineEdit_ = new QLineEdit(centralWidget);
    searchLineEdit_->setPlaceholderText(QString::fromUtf8("输入关键词或语义查询"));
    searchButton_ = new QPushButton(QString::fromUtf8("关键词搜索"), centralWidget);
    QPushButton* semanticBtn = new QPushButton(QString::fromUtf8("语义搜索"), centralWidget);

    searchLayout->addWidget(searchLineEdit_);
    searchLayout->addWidget(searchButton_);
    searchLayout->addWidget(semanticBtn);

    auto* splitter = new QSplitter(Qt::Horizontal, centralWidget);
    documentListWidget_ = new QListWidget(splitter);
    previewTextEdit_ = new QTextEdit(splitter);
    previewTextEdit_->setReadOnly(true);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    mainLayout->addLayout(searchLayout);
    mainLayout->addWidget(splitter);
    setCentralWidget(centralWidget);

    statusBar()->showMessage(QString::fromUtf8("就绪"));

    connect(searchButton_, &QPushButton::clicked, this, &MainWindow::onSearch);
    connect(semanticBtn, &QPushButton::clicked, this, &MainWindow::onSemanticSearch);
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
    connect(exitAction, &QAction::triggered, this, &QCoreApplication::quit);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);
}

void MainWindow::onImportDocuments() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, QString::fromUtf8("导入文档"), QString(),
        QString::fromUtf8("文本文档 (*.txt *.md)")
    );
    if (!files.isEmpty()) importFiles(files);
}

void MainWindow::onSearch() {
    QString kw = searchLineEdit_->text().trimmed();
    if (kw.isEmpty()) {
        refreshDocumentList();
        return;
    }
    try {
        std::string query = kw.toStdString();
        auto res = searchEngine_->search(query);
        refreshSearchResults(res);
    } catch (...) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("搜索失败"));
    }
}

void MainWindow::onSemanticSearch() {
    QString text = searchLineEdit_->text().trimmed();
    if (text.isEmpty() || !embeddingEngine_ || !vectorIndex_) return;

    try {
        std::string q = text.toStdString();
        std::vector<float> vec = embeddingEngine_->encode(q);
        auto results = vectorIndex_->search(vec, 10);
        refreshSemanticResults(results);
    } catch (...) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("语义搜索失败"));
    }
}

void MainWindow::onDocumentClicked(QListWidgetItem* item) {
    if (!item) return;
    int id = item->data(Qt::UserRole).toInt();
    try {
        Document doc = databaseManager_.getDocumentById(id);
        previewTextEdit_->setPlainText(doc.content);
    } catch (...) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("加载文档失败"));
    }
}

void MainWindow::onShowAbout() {
    QMessageBox::about(this, QString::fromUtf8("关于"),
                       QString::fromUtf8("离线知识库系统\n基于 Qt6 + SQLite + BM25 + 向量语义检索"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) files << url.toLocalFile();
    }
    importFiles(files);
    event->acceptProposedAction();
}

void MainWindow::refreshDocumentList(const QString& keyword) {
    documentListWidget_->clear();
    previewTextEdit_->clear();
    QVector<Document> docs = databaseManager_.searchDocuments(keyword);
    for (const Document& doc : docs) {
        QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
        auto* item = new QListWidgetItem(title, documentListWidget_);
        item->setData(Qt::UserRole, doc.id);
    }
    updateStatusBarCount();
}

void MainWindow::refreshSearchResults(const std::vector<std::pair<int, double>>& results) {
    documentListWidget_->clear();
    for (const auto& [id, score] : results) {
        try {
            Document doc = databaseManager_.getDocumentById(id);
            QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
            title += QString(" (BM25: %1)").arg(score, 0, 'f', 2);
            auto* item = new QListWidgetItem(title, documentListWidget_);
            item->setData(Qt::UserRole, id);
        } catch (...) {}
    }
}

void MainWindow::refreshSemanticResults(const std::vector<std::pair<int, float>>& results) {
    documentListWidget_->clear();
    for (const auto& [id, sim] : results) {
        try {
            Document doc = databaseManager_.getDocumentById(id);
            QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
            title += QString(" (相似度: %1)").arg(sim, 0, 'f', 2);
            auto* item = new QListWidgetItem(title, documentListWidget_);
            item->setData(Qt::UserRole, id);
        } catch (...) {}
    }
}

void MainWindow::updateStatusBarCount() {
    int cnt = databaseManager_.getDocumentsCount();
    statusBar()->showMessage(QString::fromUtf8("文档总数：%1").arg(cnt));
}

void MainWindow::importFiles(const QStringList& filePaths) {
    int ok = 0;
    for (const QString& path : filePaths) {
        if (!isSupportedDocument(path)) continue;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QByteArray bytes = file.readAll();
        QString content = QString::fromUtf8(bytes);
        if (content.contains(QChar::ReplacementCharacter)) {
            content = QString::fromLocal8Bit(bytes);
        }

        QString title = QFileInfo(path).completeBaseName();
        try {
            int id = databaseManager_.insertDocument(title, content, path);

            if (searchEngine_) {
                Document d;
                d.id = id;
                d.title = title;
                d.content = content;
                d.filePath = path;
                searchEngine_->addDocument(d);
            }

            if (embeddingEngine_ && vectorIndex_) {
                std::string c = content.toStdString();
                std::vector<float> vec = embeddingEngine_->encode(c);
                vectorIndex_->addVectors(vec, id);
            }
            ok++;
        } catch (...) {}
    }

    refreshDocumentList(searchLineEdit_->text().trimmed());
    QMessageBox::information(this, QString::fromUtf8("导入完成"),
                             QString::fromUtf8("成功导入 %1 个文档").arg(ok));
}

bool MainWindow::isSupportedDocument(const QString& filePath) const {
    QString s = QFileInfo(filePath).suffix().toLower();
    return s == "txt" || s == "md";
}