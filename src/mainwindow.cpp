#include "mainwindow.h"
#include "rag_policy.h"

#include "document.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
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
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      searchLineEdit_(nullptr),
      searchButton_(nullptr),
      documentListWidget_(nullptr),
      previewTextEdit_(nullptr) {
    setWindowTitle(QString::fromUtf8("离线知识库系统"));
    resize(1100, 700);
    setAcceptDrops(true);

    setupUi();
    setupMenus();

    embeddingStatusText_ = QString::fromUtf8("向量: 初始化中");
    if (embeddingStatusLabel_) {
        embeddingStatusLabel_->setText(embeddingStatusText_);
    }
    statusBar()->showMessage(embeddingStatusText_);

    kbService_ = new KbService();
    try {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString dictDir = QDir(appDir).absoluteFilePath("../resources/dict");
        kbService_->initialize(dictDir);
        embeddingStatusText_ = kbService_->embeddingStatusText();
    } catch (const std::exception& ex) {
        embeddingStatusText_ = QString::fromUtf8("向量: 初始化失败");
        QMessageBox::warning(this, QString::fromUtf8("引擎初始化失败"), QString::fromUtf8(ex.what()));
    }

    if (embeddingStatusLabel_) {
        embeddingStatusLabel_->setText(embeddingStatusText_);
    }
    statusBar()->showMessage(embeddingStatusText_);

    if (kbService_->isRagReady()) {
        ragWatcher_ = new QFutureWatcher<QString>(this);
        connect(ragWatcher_, &QFutureWatcher<QString>::finished, this, [this]() {
            if (!ragWatcher_) {
                return;
            }
            QString ans;
            try {
                ans = ragWatcher_->result();
            } catch (...) {
                ans = QString::fromUtf8("[错误] 获取结果失败");
            }
            onRagFinished(ans);
        });
    } else {
        QMessageBox::warning(this, QString::fromUtf8("RAG 初始化失败"),
                             QString::fromUtf8("本地大模型加载失败，聊天功能将不可用。"));
    }

    refreshDocumentList();
}

MainWindow::~MainWindow() {
    if (kbService_ && kbService_->isRagReady()) {
        kbService_->ragEngine()->stop();
    }
    if (ragWatcher_) {
        ragWatcher_->waitForFinished();
    }
    delete kbService_;
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

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

    chatWidget_ = new ChatWidget(centralWidget);

    tabWidget_ = new QTabWidget(centralWidget);
    tabWidget_->addTab(searchTab, QString::fromUtf8("文档检索"));
    tabWidget_->addTab(chatWidget_, QString::fromUtf8("AI 问答"));

    mainLayout->addWidget(tabWidget_);
    setCentralWidget(centralWidget);

    embeddingStatusLabel_ = new QLabel(QString::fromUtf8("向量: 未初始化"), this);
    embeddingStatusLabel_->setObjectName(QStringLiteral("embeddingStatusLabel"));
    embeddingStatusLabel_->setMinimumWidth(180);
    statusBar()->addPermanentWidget(embeddingStatusLabel_);
    statusBar()->showMessage(QString::fromUtf8("就绪"));

    connect(searchButton_, &QPushButton::clicked, this, &MainWindow::onSearch);
    connect(semanticBtn, &QPushButton::clicked, this, &MainWindow::onSemanticSearch);
    connect(searchLineEdit_, &QLineEdit::returnPressed, this, &MainWindow::onSearch);
    connect(documentListWidget_, &QListWidget::itemClicked, this, &MainWindow::onDocumentClicked);
    connect(chatWidget_, &ChatWidget::sendMessage, this, &MainWindow::onChatMessage);
    connect(chatWidget_, &ChatWidget::sourceClicked, this, &MainWindow::onSourceLinkClicked);
}

void MainWindow::setupMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    QAction* importAction = fileMenu->addAction(QString::fromUtf8("导入文档"));
    QAction* exitAction = fileMenu->addAction(QString::fromUtf8("退出"));

    QMenu* helpMenu = menuBar()->addMenu(QString::fromUtf8("帮助"));
    QAction* statusAction = helpMenu->addAction(QString::fromUtf8("当前模型状态"));
    QAction* aboutAction = helpMenu->addAction(QString::fromUtf8("关于"));

    connect(importAction, &QAction::triggered, this, &MainWindow::onImportDocuments);
    connect(exitAction, &QAction::triggered, this, &QCoreApplication::quit);
    connect(statusAction, &QAction::triggered, this, &MainWindow::onShowModelStatus);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);
}

void MainWindow::onImportDocuments() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, QString::fromUtf8("导入文档"), QString(),
        QString::fromUtf8("文本文档 (*.txt *.md)"));
    if (!files.isEmpty()) {
        importFiles(files);
    }
}

void MainWindow::onSearch() {
    QString kw = searchLineEdit_->text().trimmed();
    if (kw.isEmpty()) {
        refreshDocumentList();
        return;
    }
    if (!kbService_) {
        return;
    }
    try {
        refreshSearchResults(kbService_->searchDocumentsKeyword(kw));
    } catch (...) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("搜索失败"));
    }
}

void MainWindow::onSemanticSearch() {
    QString text = searchLineEdit_->text().trimmed();
    if (text.isEmpty() || !kbService_ || !kbService_->isEmbeddingReady()) {
        return;
    }

    try {
        const QVector<KbSearchHit> hits = kbService_->searchSemantic(text, 10);
        selectedDocumentId_ = -1;
        documentListWidget_->clear();
        previewTextEdit_->clear();
        previewTextEdit_->setExtraSelections({});

        for (const KbSearchHit& hit : hits) {
            try {
                Document doc = kbService_->getDocumentById(hit.documentId);
                QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
                title += QString(" (相似度: %1)").arg(hit.score, 0, 'f', 2);
                auto* item = new QListWidgetItem(title, documentListWidget_);
                item->setData(Qt::UserRole, doc.id);
            } catch (...) {
            }
        }
    } catch (...) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("语义搜索失败"));
    }
}

void MainWindow::onDocumentClicked(QListWidgetItem* item) {
    if (!item || !kbService_) {
        return;
    }
    int id = item->data(Qt::UserRole).toInt();

    if (selectedDocumentId_ == id) {
        selectedDocumentId_ = -1;
        documentListWidget_->clearSelection();
        documentListWidget_->setCurrentItem(nullptr);
        previewTextEdit_->clear();
        previewTextEdit_->setExtraSelections({});
        statusBar()->showMessage(QString::fromUtf8("已取消选中文档，AI 问答将基于全部文档"));
        return;
    }

    selectedDocumentId_ = id;
    try {
        Document doc = kbService_->getDocumentById(id);
        previewTextEdit_->setPlainText(doc.content);
        previewTextEdit_->setExtraSelections({});
        statusBar()->showMessage(QString::fromUtf8("已选中文档：%1，AI 问答将优先基于该文档").arg(item->text()));
    } catch (...) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("加载文档失败"));
    }
}

void MainWindow::onShowAbout() {
    QMessageBox::about(this, QString::fromUtf8("关于"),
                       QString::fromUtf8("离线知识库系统\n基于 Qt6 + SQLite + BM25 + 向量语义检索 + 本地 RAG"));
}

void MainWindow::onShowModelStatus() {
    if (!kbService_) {
        return;
    }
    QMessageBox::information(this, QString::fromUtf8("当前模型状态"), kbService_->statusSummary());
}

void MainWindow::onSourceLinkClicked(int index) {
    if (index < 0 || index >= lastRagSourceEntries_.size() || !kbService_) {
        return;
    }
    const RagSourceEntry& entry = lastRagSourceEntries_[index];

    if (tabWidget_) {
        tabWidget_->setCurrentIndex(0);
    }

    if (documentListWidget_) {
        bool foundItem = false;
        for (int i = 0; i < documentListWidget_->count(); ++i) {
            QListWidgetItem* item = documentListWidget_->item(i);
            if (item && item->data(Qt::UserRole).toInt() == entry.documentId) {
                documentListWidget_->setCurrentItem(item);
                selectedDocumentId_ = entry.documentId;
                foundItem = true;
                break;
            }
        }
        if (!foundItem) {
            refreshDocumentList();
            for (int i = 0; i < documentListWidget_->count(); ++i) {
                QListWidgetItem* item = documentListWidget_->item(i);
                if (item && item->data(Qt::UserRole).toInt() == entry.documentId) {
                    documentListWidget_->setCurrentItem(item);
                    selectedDocumentId_ = entry.documentId;
                    break;
                }
            }
        }
    }

    try {
        Document doc = kbService_->getDocumentById(entry.documentId);
        previewTextEdit_->setPlainText(doc.content);
    } catch (...) {
        return;
    }

    if (previewTextEdit_) {
        QTextDocument* doc = previewTextEdit_->document();
        const QString chunkText = entry.chunkContent.trimmed();

        QStringList candidates;
        if (chunkText.size() >= 80) candidates << chunkText.left(80).trimmed();
        if (chunkText.size() >= 40) candidates << chunkText.left(40).trimmed();
        if (chunkText.size() >= 20) candidates << chunkText.left(20).trimmed();
        candidates << chunkText.left(qMin(12, chunkText.size())).trimmed();

        QTextCursor found;
        for (const QString& s : candidates) {
            if (s.isEmpty()) {
                continue;
            }
            QTextCursor c = doc->find(s);
            if (!c.isNull()) {
                found = c;
                break;
            }
        }

        if (!found.isNull()) {
            const int start = found.selectionStart();
            const int end = qMin(start + chunkText.size(), doc->characterCount() - 1);
            QTextCursor sel(doc);
            sel.setPosition(start);
            sel.setPosition(end, QTextCursor::KeepAnchor);

            QList<QTextEdit::ExtraSelection> sels;
            QTextEdit::ExtraSelection es;
            es.cursor = sel;
            es.format.setBackground(QColor(255, 235, 59));
            es.format.setForeground(QColor(0, 0, 0));
            sels.append(es);
            previewTextEdit_->setExtraSelections(sels);

            previewTextEdit_->setTextCursor(sel);
            previewTextEdit_->ensureCursorVisible();
        } else {
            previewTextEdit_->setExtraSelections({});
        }
    }
}

void MainWindow::onChatMessage(const QString& msg) {
    if (!chatWidget_ || !kbService_) {
        return;
    }

    chatWidget_->appendMessage(QString::fromUtf8("我"), msg);

    if (!kbService_->isRagReady()) {
        lastRagSources_.clear();
        chatWidget_->setSources(QString());
        chatWidget_->appendMessage(QString::fromUtf8("系统"), QString::fromUtf8("RAG 引擎未初始化，无法回答"));
        return;
    }

    if (ragBusy_) {
        lastRagSources_.clear();
        chatWidget_->setSources(QString());
        chatWidget_->appendMessage(QString::fromUtf8("系统"), QString::fromUtf8("上一个问题仍在生成中，请稍候..."));
        return;
    }

    const KbRagResult ragContext = kbService_->buildContext(msg, selectedDocumentId_);
    lastRagSources_ = ragContext.sources;
    lastRagSourceEntries_.clear();
    lastRagSourceEntries_.reserve(ragContext.sourceEntries.size());
    for (const KbRagSource& e : ragContext.sourceEntries) {
        RagSourceEntry entry;
        entry.documentId = e.documentId;
        entry.chunkId = e.chunkId;
        entry.chunkIndex = e.chunkIndex;
        entry.title = e.title;
        entry.chunkContent = e.chunkContent;
        lastRagSourceEntries_.append(entry);
    }

    const QString focusHint = QStringLiteral("[") + ragContext.scopeLabel + QStringLiteral("]");
    chatWidget_->appendMessage(QString::fromUtf8("提示"), focusHint);

    QString sourcesHtml;
    for (int i = 0; i < ragContext.sourceEntries.size(); ++i) {
        const KbRagSource& e = ragContext.sourceEntries[i];
        sourcesHtml += QString::fromUtf8(
            "<a href=\"source:%1\" style=\"color:#0078d4;text-decoration:underline;\">"
            "[来源 %2] %3 / 片段 %4</a><br/>")
                           .arg(i)
                           .arg(i + 1)
                           .arg(e.title.toHtmlEscaped())
                           .arg(e.chunkIndex + 1);
    }
    chatWidget_->setSources(sourcesHtml);

    ragBusy_ = true;
    statusBar()->showMessage(QString::fromUtf8("AI 正在思考..."));

    QString effectiveQuestion = msg;
    if (ragContext.scopeLabel.contains(QStringLiteral("多文档覆盖"))) {
        effectiveQuestion = QString::fromUtf8(
            "下面【文档】中的每个【来源 N】对应系统里的一篇完整文档（已包含该文档的全部主要内容）。"
            "请为每篇文档各给出一段简要总结，要求覆盖该文档的主要内容；"
            "每篇文档只输出一段，按来源编号顺序排列，格式严格为：\n"
            "[来源 1] 文档名：一段总结\n"
            "[来源 2] 文档名：一段总结\n"
            "...\n"
            "不要回答\"无法回答\"。即使某篇文档是配置文件或测试代码，也要根据现有内容做出简要概括。\n\n"
            "用户原始问题：")
            + msg;
    }

    const std::string q = effectiveQuestion.toStdString();
    const std::string c = ragContext.context.toStdString();
    RagEngine* engine = kbService_->ragEngine();

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

    if (kbService_ && kbService_->isRagReady()) {
        const RagEngine::LastMetrics m = kbService_->ragEngine()->lastMetrics();
        if (m.generatedTokens > 0) {
            const QString msg = QString::fromUtf8(
                "生成完成: %1 tok / %2 ms 解码 (%3 tok/s)，prefill %4 tok / %5 ms")
                                    .arg(m.generatedTokens)
                                    .arg(QString::number(m.decodeMs, 'f', 0))
                                    .arg(QString::number(m.tokensPerSecond, 'f', 2))
                                    .arg(m.promptTokens)
                                    .arg(QString::number(m.prefillMs, 'f', 0));
            statusBar()->showMessage(msg, 10000);
            QTimer::singleShot(10050, this, [this]() {
                if (!ragBusy_) {
                    statusBar()->showMessage(QString::fromUtf8("就绪 | %1").arg(embeddingStatusText_));
                }
            });
        } else {
            statusBar()->showMessage(QString::fromUtf8("就绪 | %1").arg(embeddingStatusText_));
        }
    } else {
        statusBar()->showMessage(QString::fromUtf8("就绪 | %1").arg(embeddingStatusText_));
    }

    if (chatWidget_) {
        chatWidget_->appendMessage(QString::fromUtf8("AI"), answer.isEmpty() ? QString::fromUtf8("(空回答)") : answer);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            files << url.toLocalFile();
        }
    }
    importFiles(files);
    event->acceptProposedAction();
}

void MainWindow::refreshDocumentList(const QString& keyword) {
    selectedDocumentId_ = -1;
    documentListWidget_->clear();
    previewTextEdit_->clear();
    previewTextEdit_->setExtraSelections({});
    if (!kbService_) {
        return;
    }

    const QVector<Document> docs = kbService_->searchDocuments(keyword);
    for (const Document& doc : docs) {
        QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
        auto* item = new QListWidgetItem(title, documentListWidget_);
        item->setData(Qt::UserRole, doc.id);
    }
    updateStatusBarCount();
}

void MainWindow::refreshSearchResults(const std::vector<std::pair<int, double>>& results) {
    selectedDocumentId_ = -1;
    documentListWidget_->clear();
    previewTextEdit_->clear();
    previewTextEdit_->setExtraSelections({});
    if (!kbService_) {
        return;
    }

    for (const auto& [id, score] : results) {
        try {
            Document doc = kbService_->getDocumentById(id);
            QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
            title += QString(" (BM25: %1)").arg(score, 0, 'f', 2);
            auto* item = new QListWidgetItem(title, documentListWidget_);
            item->setData(Qt::UserRole, id);
        } catch (...) {
        }
    }
}

void MainWindow::updateStatusBarCount() {
    if (!kbService_) {
        return;
    }
    statusBar()->showMessage(QString::fromUtf8("文档总数：%1").arg(kbService_->getDocumentsCount()));
}

void MainWindow::importFiles(const QStringList& filePaths) {
    if (!kbService_) {
        return;
    }

    int ok = 0;
    QStringList skippedBlocked;
    for (const QString& path : filePaths) {
        QString blockReason;
        if (isImportBlocked(path, &blockReason)) {
            skippedBlocked << QFileInfo(path).fileName();
            continue;
        }

        if (!isSupportedDocument(path)) {
            continue;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QByteArray bytes = file.readAll();
        QString content = QString::fromUtf8(bytes);
        if (content.contains(QChar::ReplacementCharacter)) {
            content = QString::fromLocal8Bit(bytes);
        }

        const QString title = QFileInfo(path).completeBaseName();
        if (kbService_->importDocument(title, content, path)) {
            ok++;
        }
    }

    refreshDocumentList(searchLineEdit_->text().trimmed());

    QString message = QString::fromUtf8("成功导入 %1 个文档").arg(ok);
    if (!skippedBlocked.isEmpty()) {
        message += QString::fromUtf8("\n\n已跳过 %1 个 build/CMake 产物：\n%2")
                       .arg(skippedBlocked.size())
                       .arg(skippedBlocked.join(QStringLiteral("\n")));
    }
    QMessageBox::information(this, QString::fromUtf8("导入完成"), message);
}

bool MainWindow::isSupportedDocument(const QString& filePath) const {
    QString s = QFileInfo(filePath).suffix().toLower();
    return s == "txt" || s == "md";
}
