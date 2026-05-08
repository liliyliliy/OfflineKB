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
#include <QFuture>
#include <QFutureWatcher>
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
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent>

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

    // ====================== 分词 + BM25 + 向量模块 ======================
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

    // ====================== RAG 引擎初始化 ======================
    // 模型路径相对于可执行文件目录，缺失时仅警告，不影响其他功能
    try {
        // 默认模型：Qwen3-4B-Instruct-2507 Q4_K_M（约 2.5GB，4-6GB 显存可全量 offload）
        // 下载：https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF/resolve/main/Qwen3-4B-Instruct-2507-Q4_K_M.gguf
        const QString modelPath =
            QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath("models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf");

        ragEngine_ = new RagEngine();
        // 全量 GPU offload；如果 Vulkan 后端不可用或显存不足，
        // llama.cpp 会自动按层降级到 CPU。
        ragEngine_->setNGpuLayers(99);
        ragEngine_->init(QDir::toNativeSeparators(modelPath).toStdString());

        // QFutureWatcher 跟踪 worker 线程的 ask 结果，finished 信号回到 UI 线程
        ragWatcher_ = new QFutureWatcher<QString>(this);
        connect(ragWatcher_, &QFutureWatcher<QString>::finished, this, [this]() {
            if (!ragWatcher_) return;
            QString ans;
            try {
                ans = ragWatcher_->result();
            } catch (...) {
                ans = QString::fromUtf8("[错误] 获取结果失败");
            }
            onRagFinished(ans);
        });
    } catch (const std::exception& ex) {
        delete ragEngine_;
        ragEngine_ = nullptr;
        QMessageBox::warning(this, QString::fromUtf8("RAG 初始化失败"),
                             QString::fromUtf8("本地大模型加载失败：%1\n聊天功能将不可用。")
                                 .arg(QString::fromUtf8(ex.what())));
    } catch (...) {
        delete ragEngine_;
        ragEngine_ = nullptr;
        QMessageBox::warning(this, QString::fromUtf8("RAG 初始化失败"),
                             QString::fromUtf8("本地大模型加载失败（未知异常），聊天功能将不可用。"));
    }
}

MainWindow::~MainWindow() {
    // 1) 先请求中断推理循环，避免析构时 worker 仍在持有 ragEngine_
    if (ragEngine_) {
        ragEngine_->stop();
    }
    // 2) 等待 worker 线程退出
    if (ragWatcher_) {
        ragWatcher_->waitForFinished();
    }
    // 3) 释放业务引擎
    delete searchEngine_;
    delete tokenizer_;
    delete embeddingEngine_;
    delete vectorIndex_;
    delete ragEngine_;
    // ragWatcher_ 由 Qt 父子关系自动释放
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ============== Tab1：文档检索区 ==============
    auto* searchTab = new QWidget(centralWidget);
    auto* searchLayoutV = new QVBoxLayout(searchTab);

    auto* searchLayout = new QHBoxLayout();
    searchLineEdit_ = new QLineEdit(searchTab);
    searchLineEdit_->setPlaceholderText(QString::fromUtf8("输入关键词或语义查询"));
    searchButton_ = new QPushButton(QString::fromUtf8("关键词搜索"), searchTab);
    QPushButton* semanticBtn = new QPushButton(QString::fromUtf8("语义搜索"), searchTab);

    searchLayout->addWidget(searchLineEdit_);
    searchLayout->addWidget(searchButton_);
    searchLayout->addWidget(semanticBtn);

    auto* splitter = new QSplitter(Qt::Horizontal, searchTab);
    documentListWidget_ = new QListWidget(splitter);
    previewTextEdit_ = new QTextEdit(splitter);
    previewTextEdit_->setReadOnly(true);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    searchLayoutV->addLayout(searchLayout);
    searchLayoutV->addWidget(splitter);

    // ============== Tab2：AI 问答（ChatWidget） ==============
    chatWidget_ = new ChatWidget(centralWidget);

    // ============== 装入 QTabWidget ==============
    tabWidget_ = new QTabWidget(centralWidget);
    tabWidget_->addTab(searchTab,   QString::fromUtf8("文档检索"));
    tabWidget_->addTab(chatWidget_, QString::fromUtf8("AI 问答"));

    mainLayout->addWidget(tabWidget_);
    setCentralWidget(centralWidget);

    statusBar()->showMessage(QString::fromUtf8("就绪"));

    // 信号槽连接
    connect(searchButton_,       &QPushButton::clicked,        this, &MainWindow::onSearch);
    connect(semanticBtn,         &QPushButton::clicked,        this, &MainWindow::onSemanticSearch);
    connect(searchLineEdit_,     &QLineEdit::returnPressed,    this, &MainWindow::onSearch);
    connect(documentListWidget_, &QListWidget::itemClicked,    this, &MainWindow::onDocumentClicked);
    connect(chatWidget_,         &ChatWidget::sendMessage,     this, &MainWindow::onChatMessage);
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
        QString::fromUtf8("文本文档 (*.txt *.md)"));
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
                       QString::fromUtf8("离线知识库系统\n基于 Qt6 + SQLite + BM25 + 向量语义检索 + 本地 RAG"));
}

void MainWindow::onChatMessage(const QString& msg) {
    if (!chatWidget_) return;

    // 1) 立刻把用户输入回显到聊天面板
    chatWidget_->appendMessage(QString::fromUtf8("我"), msg);

    if (!ragEngine_) {
        chatWidget_->appendMessage(
            QString::fromUtf8("系统"),
            QString::fromUtf8("RAG 引擎未初始化，无法回答"));
        return;
    }

    if (ragBusy_) {
        chatWidget_->appendMessage(
            QString::fromUtf8("系统"),
            QString::fromUtf8("上一个问题仍在生成中，请稍候..."));
        return;
    }

            // 2) 取当前选中文档内容作为 context
    QString context;
    if (documentListWidget_) {
        if (auto* item = documentListWidget_->currentItem()) {
            try {
                Document doc = databaseManager_.getDocumentById(item->data(Qt::UserRole).toInt());
                context = doc.content.trimmed();
                // 长文档截断上限：4500 字符（≈ 3000-3500 token，
                // 加上 system + question + chat template 后仍稳定在 8K 上下文窗口内）
                constexpr int kMaxContextChars = 4500;
                if (context.length() > kMaxContextChars) {
                    context = context.left(kMaxContextChars)
                              + QString::fromUtf8("\n...(文档过长，已截断)");
                }
            } catch (...) {
                // 静默
            }
        }
    }         
    // 3) 异步执行 RagEngine::ask，结果由 ragWatcher_ 的 finished 槽收回
    ragBusy_ = true;
    statusBar()->showMessage(QString::fromUtf8("AI 正在思考..."));

    const std::string q = msg.toStdString();
    const std::string c = context.toStdString();
    RagEngine* engine = ragEngine_;

    QFuture<QString> fut = QtConcurrent::run([engine, q, c]() -> QString {
        try {
            return QString::fromStdString(engine->ask(q, c));
        } catch (const std::exception& ex) {
            return QString::fromUtf8("[错误] ") + QString::fromUtf8(ex.what());
        } catch (...) {
            return QString::fromUtf8("[错误] 推理过程中发生未知异常");
        }
    });

    if (ragWatcher_) {
        ragWatcher_->setFuture(fut);
    }
}

void MainWindow::onRagFinished(const QString& answer) {
    ragBusy_ = false;
    statusBar()->showMessage(QString::fromUtf8("就绪"));

    if (chatWidget_) {
        chatWidget_->appendMessage(
            QString::fromUtf8("AI"),
            answer.isEmpty() ? QString::fromUtf8("(空回答)") : answer);
    }
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
