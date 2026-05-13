#include "mainwindow.h"

#include "document.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
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
#include <QRegularExpression>
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

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <exception>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr int kChunkTargetChars = 900;
constexpr int kChunkOverlapChars = 120;
constexpr int kRagContextMaxChars = 5200;
constexpr int kRagBm25TopK = 16;
constexpr int kRagVectorTopK = 16;
constexpr int kRagFinalTopK = 6;

QString normalizeTitle(const DocumentChunk& chunk) {
    if (!chunk.title.trimmed().isEmpty()) {
        return chunk.title.trimmed();
    }
    return QFileInfo(chunk.filePath).completeBaseName();
}

QString normalizeDocMatchText(QString s) {
    s = s.toLower().trimmed();
    s.remove(QRegularExpression(QStringLiteral("\\s+")));
    s.remove(QStringLiteral(".md"));
    s.remove(QStringLiteral(".txt"));
    s.replace(QStringLiteral("零"), QStringLiteral("0"));
    s.replace(QStringLiteral("一"), QStringLiteral("1"));
    s.replace(QStringLiteral("二"), QStringLiteral("2"));
    s.replace(QStringLiteral("三"), QStringLiteral("3"));
    s.replace(QStringLiteral("四"), QStringLiteral("4"));
    s.replace(QStringLiteral("五"), QStringLiteral("5"));
    s.replace(QStringLiteral("六"), QStringLiteral("6"));
    s.replace(QStringLiteral("七"), QStringLiteral("7"));
    s.replace(QStringLiteral("八"), QStringLiteral("8"));
    s.replace(QStringLiteral("九"), QStringLiteral("9"));
    return s;
}

QString normalizeRetrievalText(QString s) {
    s = normalizeDocMatchText(s);
    s.remove(QRegularExpression(QStringLiteral("[\\p{P}\\p{S}]+")));
    return s;
}

QStringList extractQuestionNumbers(const QString& question) {
    QStringList numbers;
    const QString normalized = normalizeDocMatchText(question);
    QRegularExpression re(QStringLiteral("\\d+"));
    QRegularExpressionMatchIterator it = re.globalMatch(normalized);
    while (it.hasNext()) {
        const QString n = it.next().captured(0);
        if (!n.isEmpty() && !numbers.contains(n)) {
            numbers << n;
        }
    }
    return numbers;
}

double lexicalQuestionScore(const QString& question, const QString& content) {
    const QString normalizedQuestion = normalizeRetrievalText(question);
    const QString normalizedContent = normalizeRetrievalText(content);
    double score = 0.0;

    for (const QString& n : extractQuestionNumbers(question)) {
        // 题号是强约束，“第3题/第三道”应优先命中包含 3 的片段。
        if (normalizedContent.contains(n)) {
            score += 30.0;
        }
    }

    const QStringList terms = question.split(
        QRegularExpression(QStringLiteral("[\\s,，。！？；；:：、（）()\\[\\]【】]+")),
        Qt::SkipEmptyParts);
    for (const QString& term : terms) {
        const QString t = normalizeRetrievalText(term);
        if (t.size() >= 2 && normalizedContent.contains(t)) {
            score += 12.0;
        }
    }

    // 中文短问题分词不稳定时，用连续 2 字短语辅助打分。
    for (int i = 0; i + 1 < normalizedQuestion.size(); ++i) {
        const QString gram = normalizedQuestion.mid(i, 2);
        if (gram.size() == 2 && normalizedContent.contains(gram)) {
            score += 1.5;
        }
    }

    return score;
}

SearchEngine::ChunkRecord toChunkRecord(const DocumentChunk& chunk) {
    SearchEngine::ChunkRecord rec;
    rec.id = chunk.id;
    rec.documentId = chunk.documentId;
    rec.title = chunk.title;
    rec.content = chunk.content;
    rec.filePath = chunk.filePath;
    return rec;
}

}  // namespace

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
    embeddingStatusText_ = QString::fromUtf8("向量: 初始化中");
    if (embeddingStatusLabel_) {
        embeddingStatusLabel_->setText(embeddingStatusText_);
    }
    statusBar()->showMessage(embeddingStatusText_);

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

        bool embeddingModelLoaded = false;
        QString embeddingStatusMessage;

        // 优先尝试加载真实中文 embedding 模型；任何失败都回退到哈希向量。
        // 候选 GGUF 文件（按优先级）：
        //   - bge-small-zh-v1.5-q4_k_m.gguf
        //   - bge-small-zh-v1.5.q4_k_m.gguf
        //   - bge-large-zh-v1.5-q4_k_m.gguf
        const QStringList embeddingCandidates = {
            QStringLiteral("bge-small-zh-v1.5-q4_k_m.gguf"),
            QStringLiteral("bge-small-zh-v1.5.q4_k_m.gguf"),
            QStringLiteral("bge-small-zh-v1.5-f16.gguf"),
            QStringLiteral("bge-large-zh-v1.5-q4_k_m.gguf"),
        };
        QString embeddingModelPath;
        for (const QString& name : embeddingCandidates) {
            const QString p = resolveModelPath(name);
            if (!p.isEmpty()) {
                embeddingModelPath = p;
                break;
            }
        }
        if (!embeddingModelPath.isEmpty()) {
            try {
                if (embeddingEngine_->initEmbeddingModel(
                        QDir::toNativeSeparators(embeddingModelPath).toStdString(),
                        /*nGpuLayers=*/99)) {
                    embeddingModelLoaded = true;
                    embeddingStatusMessage =
                        QString::fromUtf8("已加载真实语义向量模型:\n%1\n\n维度: %2\n路径: %3")
                            .arg(QFileInfo(embeddingModelPath).fileName())
                            .arg(embeddingEngine_->dimension())
                            .arg(QDir::toNativeSeparators(embeddingModelPath));
                }
            } catch (...) {
                // 回退到哈希向量
            }
        }
        if (!embeddingModelLoaded) {
            embeddingStatusMessage =
                QString::fromUtf8("未加载真实语义向量模型，当前使用哈希向量回退。\n\n"
                                  "如果你已经下载 bge-small-zh-v1.5 GGUF，请确认文件位于:\n"
                                  "F:\\OfflineKB\\build\\models\\bge-small-zh-v1.5-q4_k_m.gguf");
        }

        vectorIndex_ = new VectorIndex();
        vectorIndex_->init(embeddingEngine_->dimension(), 100000);

        ensureChunksForExistingDocuments(docs);
        rebuildChunkIndexes(/*tryLoadVectorIndex=*/true);

        embeddingModelLoaded_ = embeddingModelLoaded;
        embeddingStatusText_ = embeddingModelLoaded
            ? QString::fromUtf8("向量: BGE 真模型 (%1 维)").arg(embeddingEngine_->dimension())
            : QString::fromUtf8("向量: 哈希回退");
        if (embeddingStatusLabel_) {
            embeddingStatusLabel_->setText(embeddingStatusText_);
        }
        statusBar()->showMessage(embeddingStatusText_);
    } catch (const std::exception& ex) {
        embeddingStatusText_ = QString::fromUtf8("向量: 初始化失败");
        if (embeddingStatusLabel_) {
            embeddingStatusLabel_->setText(embeddingStatusText_);
        }
        QMessageBox::warning(this, QString::fromUtf8("引擎初始化失败"), QString::fromUtf8(ex.what()));
    }

    // ====================== RAG 引擎初始化 ======================
    // 模型路径相对于可执行文件目录，缺失时仅警告，不影响其他功能
    try {
        QStringList triedPaths;
        const QString modelPath =
            resolveModelPath(QStringLiteral("Qwen3-4B-Instruct-2507-Q4_K_M.gguf"), &triedPaths);
        if (modelPath.isEmpty()) {
            throw std::runtime_error(
                ("找不到模型文件，已尝试:\n" + triedPaths.join('\n')).toStdString());
        }

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

    embeddingStatusLabel_ = new QLabel(QString::fromUtf8("向量: 未初始化"), this);
    embeddingStatusLabel_->setObjectName(QStringLiteral("embeddingStatusLabel"));
    embeddingStatusLabel_->setMinimumWidth(180);
    statusBar()->addPermanentWidget(embeddingStatusLabel_);
    statusBar()->showMessage(QString::fromUtf8("就绪"));

    // 信号槽连接
    connect(searchButton_,       &QPushButton::clicked,        this, &MainWindow::onSearch);
    connect(semanticBtn,         &QPushButton::clicked,        this, &MainWindow::onSemanticSearch);
    connect(searchLineEdit_,     &QLineEdit::returnPressed,    this, &MainWindow::onSearch);
    connect(documentListWidget_, &QListWidget::itemClicked,    this, &MainWindow::onDocumentClicked);
    connect(chatWidget_,         &ChatWidget::sendMessage,     this, &MainWindow::onChatMessage);
    connect(chatWidget_,         &ChatWidget::sourceClicked,   this, &MainWindow::onSourceLinkClicked);
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

    // 再次点击已选中的同一文档时取消选中，AI 问答恢复为全库模式。
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
        Document doc = databaseManager_.getDocumentById(id);
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
    // 生成模型名
    QString ragModelName = QString::fromUtf8("未加载");
    int gpuLayers = 0;
    if (ragEngine_) {
        const RagEngine::LastMetrics m = ragEngine_->lastMetrics();
        if (!m.modelPath.empty()) {
            const QString mp = QString::fromStdString(m.modelPath);
            ragModelName = QFileInfo(mp).fileName();
        }
        gpuLayers = m.nGpuLayers;
    }

    // Embedding 模型名与维度
    QString embeddingName = QString::fromUtf8("未加载");
    int embDim = 0;
    if (embeddingEngine_) {
        embeddingName = QString::fromStdString(embeddingEngine_->modelName());
        embDim = embeddingEngine_->dimension();
    }

    // 索引 chunk 数
    int chunkCount = 0;
    if (vectorIndex_) {
        chunkCount = static_cast<int>(vectorIndex_->size());
    }

    // GPU/Vulkan 状态
    QString gpuStatus = gpuLayers > 0
        ? QString::fromUtf8("已启用 (offload %1 层)").arg(gpuLayers)
        : QString::fromUtf8("未启用 (CPU 推理)");

    const QString info = QString::fromUtf8(
        "生成模型: %1\n"
        "Embedding 模型: %2\n"
        "向量维度: %3\n"
        "索引 chunk 数: %4\n"
        "GPU/Vulkan: %5")
            .arg(ragModelName)
            .arg(embeddingName)
            .arg(embDim)
            .arg(chunkCount)
            .arg(gpuStatus);

    QMessageBox::information(this, QString::fromUtf8("当前模型状态"), info);
}

void MainWindow::onSourceLinkClicked(int index) {
    if (index < 0 || index >= lastRagSourceEntries_.size()) {
        return;
    }
    const RagSourceEntry& entry = lastRagSourceEntries_[index];

    // 切换到文档检索 Tab
    if (tabWidget_) {
        tabWidget_->setCurrentIndex(0);
    }

    // 在文档列表中选中对应文档
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
        Document doc = databaseManager_.getDocumentById(entry.documentId);
        previewTextEdit_->setPlainText(doc.content);
    } catch (...) {
        return;
    }

    // 在预览区定位并高亮对应 chunk 内容
    if (previewTextEdit_) {
        QTextDocument* doc = previewTextEdit_->document();
        const QString chunkText = entry.chunkContent.trimmed();

        // 多档子串兜底：80 -> 40 -> 20 -> 12 字
        QStringList candidates;
        if (chunkText.size() >= 80) candidates << chunkText.left(80).trimmed();
        if (chunkText.size() >= 40) candidates << chunkText.left(40).trimmed();
        if (chunkText.size() >= 20) candidates << chunkText.left(20).trimmed();
        candidates << chunkText.left(qMin(12, chunkText.size())).trimmed();

        QTextCursor found;
        for (const QString& s : candidates) {
            if (s.isEmpty()) continue;
            QTextCursor c = doc->find(s);
            if (!c.isNull()) {
                found = c;
                break;
            }
        }

        if (!found.isNull()) {
            // 把高亮范围扩展为整个 chunk 长度（从匹配起点向后）
            const int start = found.selectionStart();
            const int end = qMin(start + chunkText.size(), doc->characterCount() - 1);
            QTextCursor sel(doc);
            sel.setPosition(start);
            sel.setPosition(end, QTextCursor::KeepAnchor);

            // 用 ExtraSelections 显示鲜明的黄色高亮（不会被默认绘制覆盖）
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
            // 找不到时清掉旧高亮
            previewTextEdit_->setExtraSelections({});
        }
    }
}

void MainWindow::onChatMessage(const QString& msg) {
    if (!chatWidget_) return;

    // 1) 立刻把用户输入回显到聊天面板
    chatWidget_->appendMessage(QString::fromUtf8("我"), msg);

    if (!ragEngine_) {
        lastRagSources_.clear();
        chatWidget_->setSources(QString());
        chatWidget_->appendMessage(
            QString::fromUtf8("系统"),
            QString::fromUtf8("RAG 引擎未初始化，无法回答"));
        return;
    }

    if (ragBusy_) {
        lastRagSources_.clear();
        chatWidget_->setSources(QString());
        chatWidget_->appendMessage(
            QString::fromUtf8("系统"),
            QString::fromUtf8("上一个问题仍在生成中，请稍候..."));
        return;
    }

    // 2) 使用 chunk 级混合检索构造 RAG 上下文
    const RagContextBundle ragContext = buildRagContext(msg);
    lastRagSources_ = ragContext.sources;
    lastRagSourceEntries_ = ragContext.entries;

    // 给用户看清楚当前实际基于哪种范围回答。
    const QString focusHint = QStringLiteral("[") + ragContext.scopeLabel + QStringLiteral("]");
    chatWidget_->appendMessage(QString::fromUtf8("提示"), focusHint);

    // 构造带链接的 HTML 来源列表
    QString sourcesHtml;
    for (int i = 0; i < ragContext.entries.size(); ++i) {
        const RagSourceEntry& e = ragContext.entries[i];
        sourcesHtml += QString::fromUtf8(
            "<a href=\"source:%1\" style=\"color:#0078d4;text-decoration:underline;\">"
            "[来源 %2] %3 / 片段 %4</a><br/>")
                .arg(i)
                .arg(i + 1)
                .arg(e.title.toHtmlEscaped())
                .arg(e.chunkIndex + 1);
    }
    chatWidget_->setSources(sourcesHtml);

    // 3) 异步执行 RagEngine::ask，结果由 ragWatcher_ 的 finished 槽收回
    ragBusy_ = true;
    statusBar()->showMessage(QString::fromUtf8("AI 正在思考..."));

    const std::string q = msg.toStdString();
    const std::string c = ragContext.context.toStdString();
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

    if (ragEngine_) {
        const RagEngine::LastMetrics m = ragEngine_->lastMetrics();
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
        chatWidget_->appendMessage(
            QString::fromUtf8("AI"),
            answer.isEmpty() ? QString::fromUtf8("(空回答)") : answer);
        // 来源已经显示在底部专用面板（chatWidget_->setSources），不再重复 append 到聊天历史
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
    selectedDocumentId_ = -1;
    documentListWidget_->clear();
    previewTextEdit_->clear();
    previewTextEdit_->setExtraSelections({});
    QVector<Document> docs = databaseManager_.searchDocuments(keyword);
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
    selectedDocumentId_ = -1;
    documentListWidget_->clear();
    previewTextEdit_->clear();
    previewTextEdit_->setExtraSelections({});
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
                databaseManager_.replaceDocumentChunks(id, splitDocumentIntoChunks(content));
            }
            ok++;
        } catch (...) {}
    }

    try {
        rebuildChunkIndexes(/*tryLoadVectorIndex=*/false);
    } catch (...) {
        // 导入成功不应因索引刷新失败回滚；下次启动会尝试重建。
    }

    refreshDocumentList(searchLineEdit_->text().trimmed());
    QMessageBox::information(this, QString::fromUtf8("导入完成"),
                             QString::fromUtf8("成功导入 %1 个文档").arg(ok));
}

bool MainWindow::isSupportedDocument(const QString& filePath) const {
    QString s = QFileInfo(filePath).suffix().toLower();
    return s == "txt" || s == "md";
}

QString MainWindow::resolveModelPath(const QString& fileName, QStringList* triedPaths) const {
    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    dirs << QDir(appDir).absoluteFilePath("models");
    dirs << QDir(appDir).absoluteFilePath("../models");
    dirs << QDir(appDir).absoluteFilePath("../../models");
    dirs << QDir::current().absoluteFilePath("models");
    dirs << QDir(QStringLiteral("F:/OfflineKB")).absoluteFilePath("models");

    // 兼容旧模型下载目录。
    dirs << QDir(QStringLiteral("F:/OfflineKB/models/Qwen2.5-7B-Instruct-GGUF")).absolutePath();

    for (const QString& dir : dirs) {
        const QString candidate = QDir(dir).absoluteFilePath(fileName);
        if (triedPaths) {
            triedPaths->push_back(QDir::toNativeSeparators(candidate));
        }
        if (QFileInfo::exists(candidate) && QFileInfo(candidate).isFile()) {
            return candidate;
        }
    }
    return QString();
}

QVector<QString> MainWindow::splitDocumentIntoChunks(const QString& content) const {
    QVector<QString> chunks;
    const QString text = content.trimmed();
    if (text.isEmpty()) {
        return chunks;
    }

    const QStringList paragraphs = text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")),
                                              Qt::SkipEmptyParts);
    QString current;
    for (const QString& rawPara : paragraphs) {
        const QString para = rawPara.trimmed();
        if (para.isEmpty()) {
            continue;
        }
        if (para.size() > kChunkTargetChars * 2) {
            if (!current.trimmed().isEmpty()) {
                chunks.push_back(current.trimmed());
                current.clear();
            }
            for (int pos = 0; pos < para.size(); pos += (kChunkTargetChars - kChunkOverlapChars)) {
                chunks.push_back(para.mid(pos, kChunkTargetChars).trimmed());
            }
            continue;
        }

        if (!current.isEmpty() && current.size() + para.size() + 2 > kChunkTargetChars) {
            chunks.push_back(current.trimmed());
            const QString overlap = current.right(kChunkOverlapChars).trimmed();
            current = overlap.isEmpty() ? para : overlap + QStringLiteral("\n") + para;
        } else {
            if (!current.isEmpty()) {
                current += QStringLiteral("\n");
            }
            current += para;
        }
    }

    if (!current.trimmed().isEmpty()) {
        chunks.push_back(current.trimmed());
    }
    if (chunks.isEmpty()) {
        chunks.push_back(text.left(kChunkTargetChars));
    }
    return chunks;
}

void MainWindow::ensureChunksForExistingDocuments(const QVector<Document>& docs) {
    for (const Document& doc : docs) {
        const QVector<DocumentChunk> existing = databaseManager_.getChunksForDocument(doc.id);
        if (!existing.isEmpty()) {
            continue;
        }
        databaseManager_.replaceDocumentChunks(doc.id, splitDocumentIntoChunks(doc.content));
    }
}

QString MainWindow::vectorIndexPath() const {
    const QString dir = databaseManager_.appDataDir().isEmpty()
                            ? QDir(QCoreApplication::applicationDirPath()).absolutePath()
                            : databaseManager_.appDataDir();
    return QDir(dir).absoluteFilePath(QStringLiteral("chunks.hnsw"));
}

QString MainWindow::vectorIndexMetaPath() const {
    const QString dir = databaseManager_.appDataDir().isEmpty()
                            ? QDir(QCoreApplication::applicationDirPath()).absolutePath()
                            : databaseManager_.appDataDir();
    return QDir(dir).absoluteFilePath(QStringLiteral("chunks.meta.json"));
}

void MainWindow::rebuildChunkIndexes(bool tryLoadVectorIndex) {
    const QVector<DocumentChunk> chunks = databaseManager_.getAllChunks();

    std::vector<SearchEngine::ChunkRecord> records;
    records.reserve(static_cast<size_t>(chunks.size()));
    for (const DocumentChunk& chunk : chunks) {
        records.push_back(toChunkRecord(chunk));
    }
    if (searchEngine_) {
        searchEngine_->buildChunkIndex(records);
    }

    if (!embeddingEngine_ || !vectorIndex_) {
        return;
    }

    const int dim = embeddingEngine_->dimension();
    const int maxElements = std::max(100000, static_cast<int>(chunks.size()) + 1024);
    vectorIndex_->init(dim, maxElements);

    const QString indexPath = vectorIndexPath();
    const QString metaPath = vectorIndexMetaPath();
    const QString currentModelName = QString::fromStdString(embeddingEngine_->modelName());
    const int currentChunkCount = static_cast<int>(chunks.size());

    if (tryLoadVectorIndex && QFileInfo::exists(indexPath)) {
        bool metaValid = false;
        if (QFileInfo::exists(metaPath)) {
            QFile metaFile(metaPath);
            if (metaFile.open(QIODevice::ReadOnly)) {
                const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
                metaFile.close();
                if (doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    metaValid = obj.value(QStringLiteral("modelName")).toString() == currentModelName
                                && obj.value(QStringLiteral("dimension")).toInt() == dim
                                && obj.value(QStringLiteral("chunkCount")).toInt() == currentChunkCount;
                }
            }
        }
        if (metaValid) {
            try {
                vectorIndex_->load(QDir::toNativeSeparators(indexPath).toStdString());
                if (vectorIndex_->size() == static_cast<size_t>(currentChunkCount)) {
                    return;
                }
            } catch (...) {
                vectorIndex_->init(dim, maxElements);
            }
        }
    }

    for (const DocumentChunk& chunk : chunks) {
        const QByteArray utf8 = chunk.content.toUtf8();
        const std::string content(utf8.constData(), static_cast<size_t>(utf8.size()));
        std::vector<float> vec = embeddingEngine_->encode(content);
        vectorIndex_->addVectors(vec, chunk.id);
    }

    try {
        vectorIndex_->save(QDir::toNativeSeparators(indexPath).toStdString());
    } catch (...) {}

    // 写入 meta 文件，下次启动时校验
    try {
        QJsonObject meta;
        meta[QStringLiteral("modelName")] = currentModelName;
        meta[QStringLiteral("dimension")] = dim;
        meta[QStringLiteral("chunkCount")] = currentChunkCount;
        meta[QStringLiteral("updatedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        QFile metaFile(metaPath);
        if (metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
            metaFile.close();
        }
    } catch (...) {}
}

MainWindow::RagContextBundle MainWindow::buildRagContext(const QString& question) {
    RagContextBundle bundle;
    if (!searchEngine_) {
        return bundle;
    }

    // 聚焦模式：用户在文档检索 Tab 选中了某文档时，只用该文档的 chunks 作答。
    // 若问题里明确提到某个文档标题/文件名，则优先覆盖当前选中范围。
    int focusDocId = selectedDocumentId_;
    QString focusDocTitle;
    bool focusFromQuestion = false;
    if (focusDocId > 0) {
        try {
            const Document doc = databaseManager_.getDocumentById(focusDocId);
            focusDocTitle = doc.title.isEmpty()
                ? QFileInfo(doc.filePath).completeBaseName()
                : doc.title;
        } catch (...) {}
    }

    const QByteArray queryUtf8 = question.toUtf8();
    const std::string query(queryUtf8.constData(), static_cast<size_t>(queryUtf8.size()));

    // 每次都尝试从问题中识别文档名。这样即使当前选中了 README，
    // 用户问“习题5第三道选择题答案”时也会自动切到习题5。
    QString bestTitle;
    int bestDocId = -1;
    try {
        const QVector<Document> docs = databaseManager_.searchDocuments(QString());
        const QString normalizedQuestion = normalizeDocMatchText(question);
        for (const Document& doc : docs) {
            QString title = doc.title.trimmed();
            if (title.isEmpty()) {
                title = QFileInfo(doc.filePath).completeBaseName();
            }
            const QString base = QFileInfo(doc.filePath).completeBaseName();
            const bool titleHit = !title.isEmpty()
                && (question.contains(title, Qt::CaseInsensitive)
                    || normalizedQuestion.contains(normalizeDocMatchText(title)));
            const bool baseHit = !base.isEmpty()
                && (question.contains(base, Qt::CaseInsensitive)
                    || normalizedQuestion.contains(normalizeDocMatchText(base)));
            if ((titleHit || baseHit) && title.size() > bestTitle.size()) {
                bestTitle = title;
                bestDocId = doc.id;
            }
        }
    } catch (...) {}
    if (bestDocId > 0) {
        focusDocId = bestDocId;
        focusDocTitle = bestTitle;
        focusFromQuestion = true;
    }

    bundle.scopeLabel = focusDocId > 0
        ? (focusFromQuestion
               ? QString::fromUtf8("基于问题指定文档: %1").arg(focusDocTitle)
               : QString::fromUtf8("基于选中文档: %1").arg(focusDocTitle))
        : QString::fromUtf8("基于全部文档");

    std::vector<std::pair<int, double>> ranked;

    if (focusDocId > 0) {
        const QVector<DocumentChunk> focusChunks = databaseManager_.getChunksForDocument(focusDocId);
        const bool summaryQuery = question.contains(QRegularExpression(
            QStringLiteral("总结|概括|归纳|全文|整篇|本文|这篇|所有|全部|知识点")));

        ranked.reserve(static_cast<size_t>(focusChunks.size()));
        for (const DocumentChunk& chunk : focusChunks) {
            double score = 0.0;
            if (summaryQuery) {
                // 总结类问题需要尽量覆盖文档上下文，按原文片段顺序取来源。
                score = 100000.0 - chunk.chunkIndex;
            } else {
                score = lexicalQuestionScore(question, chunk.content);
                // 分数完全相同时保持更靠前片段优先。
                score += 1.0 / (chunk.chunkIndex + 1.0);
            }
            ranked.push_back({chunk.id, score});
        }

        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
    } else {
        std::unordered_map<int, double> fused;
        auto addRrf = [&fused](const auto& results, double weight) {
            for (int rank = 0; rank < static_cast<int>(results.size()); ++rank) {
                fused[results[static_cast<size_t>(rank)].first] += weight / (60.0 + rank + 1.0);
            }
        };

        try {
            addRrf(searchEngine_->searchChunks(query, kRagBm25TopK), 1.0);
        } catch (...) {}

        if (embeddingEngine_ && vectorIndex_) {
            try {
                std::vector<float> qv = embeddingEngine_->encode(query);
                addRrf(vectorIndex_->search(qv, kRagVectorTopK), 0.45);
            } catch (...) {}
        }

        ranked.reserve(fused.size());
        for (const auto& [chunkId, score] : fused) {
            double finalScore = score;
            try {
                const DocumentChunk chunk = databaseManager_.getChunkById(chunkId);
                finalScore += lexicalQuestionScore(question, chunk.content) * 0.02;
            } catch (...) {}
            ranked.push_back({chunkId, finalScore});
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    QStringList sourceLines;
    std::unordered_set<int> seenDocs;
    int used = 0;
    for (const auto& [chunkId, _] : ranked) {
        if (used >= kRagFinalTopK || bundle.context.size() >= kRagContextMaxChars) {
            break;
        }

        DocumentChunk chunk;
        try {
            chunk = databaseManager_.getChunkById(chunkId);
        } catch (...) {
            continue;
        }

        const QString title = normalizeTitle(chunk);
        const QString marker = QString::fromUtf8("[来源 %1] %2 / 片段 %3")
                                   .arg(used + 1)
                                   .arg(title)
                                   .arg(chunk.chunkIndex + 1);
        QString block = marker + QStringLiteral("\n") + chunk.content.trimmed();
        if (bundle.context.size() + block.size() > kRagContextMaxChars) {
            const int remainingChars =
                std::max(0, kRagContextMaxChars - static_cast<int>(bundle.context.size()));
            block = block.left(remainingChars);
        }
        if (!bundle.context.isEmpty()) {
            bundle.context += QStringLiteral("\n\n");
        }
        bundle.context += block;
        sourceLines << marker;

        RagSourceEntry entry;
        entry.documentId = chunk.documentId;
        entry.chunkId    = chunk.id;
        entry.chunkIndex = chunk.chunkIndex;
        entry.title      = title;
        entry.chunkContent = chunk.content.trimmed();
        bundle.entries.append(entry);

        seenDocs.insert(chunk.documentId);
        ++used;
    }

    if (bundle.context.trimmed().isEmpty() && documentListWidget_) {
        if (auto* item = documentListWidget_->currentItem()) {
            try {
                Document doc = databaseManager_.getDocumentById(item->data(Qt::UserRole).toInt());
                const QVector<QString> chunks = splitDocumentIntoChunks(doc.content);
                if (!chunks.isEmpty()) {
                    const QString title = doc.title.isEmpty()
                                              ? QFileInfo(doc.filePath).completeBaseName()
                                              : doc.title;
                    bundle.context = QString::fromUtf8("[来源 1] %1 / 当前选中文档\n%2")
                                         .arg(title, chunks.first().left(kRagContextMaxChars));
                    sourceLines << QString::fromUtf8("[来源 1] %1 / 当前选中文档").arg(title);
                }
            } catch (...) {}
        }
    }

    bundle.sources = sourceLines.join(QStringLiteral("\n"));
    return bundle;
}
