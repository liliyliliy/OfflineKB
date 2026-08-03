#include "kbservice.h"
#include "rag_policy.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <exception>
#include <stdexcept>
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

KbSearchHit makeSearchHit(const DocumentChunk& chunk, double score) {
    KbSearchHit hit;
    hit.chunkId = chunk.id;
    hit.documentId = chunk.documentId;
    hit.chunkIndex = chunk.chunkIndex;
    hit.title = normalizeTitle(chunk);
    hit.score = score;
    hit.snippet = chunk.content.trimmed().left(200);
    return hit;
}

QString effectiveQuestionForAsk(const QString& msg, const QString& scopeLabel) {
    if (!scopeLabel.contains(QStringLiteral("多文档覆盖"))) {
        return msg;
    }
    return QString::fromUtf8(
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

}  // namespace

KbService::KbService() = default;

KbService::~KbService() {
    delete searchEngine_;
    delete tokenizer_;
    delete embeddingEngine_;
    delete vectorIndex_;
    delete ragEngine_;
}

void KbService::addModelsSearchDir(const QString& dir) {
    if (!dir.isEmpty()) {
        extraModelDirs_ << QDir(dir).absolutePath();
    }
}

void KbService::initialize(const QString& resourceDir) {
    resourceDir_ = resourceDir;

    databaseManager_.initialize();

    const std::string dictDirStd = QDir::toNativeSeparators(resourceDir).toStdString();
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

    embeddingModelLoaded_ = false;
    const QStringList embeddingCandidates = {
        QStringLiteral("bge-small-zh-v1.5-q4_k_m.gguf"),
        QStringLiteral("bge-small-zh-v1.5.q4_k_m.gguf"),
        QStringLiteral("bge-small-zh-v1.5-f16.gguf"),
        QStringLiteral("bge-large-zh-v1.5-q4_k_m.gguf"),
    };
    for (const QString& name : embeddingCandidates) {
        const QString p = resolveModelPath(name);
        if (p.isEmpty()) {
            continue;
        }
        try {
            if (embeddingEngine_->initEmbeddingModel(
                    QDir::toNativeSeparators(p).toStdString(), /*nGpuLayers=*/99)) {
                embeddingModelLoaded_ = true;
                break;
            }
        } catch (...) {
        }
    }

    vectorIndex_ = new VectorIndex();
    vectorIndex_->init(embeddingEngine_->dimension(), 100000);

    ensureChunksForExistingDocuments(docs);
    rebuildChunkIndexes(/*tryLoadVectorIndex=*/true);

    QStringList triedPaths;
    const QString modelPath =
        resolveModelPath(QStringLiteral("Qwen3-4B-Instruct-2507-Q4_K_M.gguf"), &triedPaths);
    if (!modelPath.isEmpty()) {
        try {
            ragEngine_ = new RagEngine();
            ragEngine_->setNGpuLayers(99);
            ragEngine_->init(QDir::toNativeSeparators(modelPath).toStdString());
        } catch (...) {
            delete ragEngine_;
            ragEngine_ = nullptr;
        }
    }
}

QVector<Document> KbService::listDocuments() const {
    return databaseManager_.searchDocuments(QString());
}

QVector<Document> KbService::searchDocuments(const QString& keyword) const {
    return databaseManager_.searchDocuments(keyword);
}

std::vector<std::pair<int, double>> KbService::searchDocumentsKeyword(const QString& query) const {
    if (!searchEngine_) {
        return {};
    }
    return searchEngine_->search(query.toStdString());
}

QVector<KbSearchHit> KbService::searchKeyword(const QString& query, int topK) const {
    QVector<KbSearchHit> hits;
    if (!searchEngine_ || query.trimmed().isEmpty()) {
        return hits;
    }

    const auto results = searchEngine_->searchChunks(query.toStdString(), topK);
    hits.reserve(static_cast<int>(results.size()));
    for (const auto& [chunkId, score] : results) {
        try {
            hits.push_back(makeSearchHit(databaseManager_.getChunkById(chunkId), score));
        } catch (...) {
        }
    }
    return hits;
}

QVector<KbSearchHit> KbService::searchSemantic(const QString& query, int topK) const {
    QVector<KbSearchHit> hits;
    if (!embeddingEngine_ || !vectorIndex_ || query.trimmed().isEmpty()) {
        return hits;
    }

    const std::vector<float> vec = embeddingEngine_->encode(query.toStdString());
    const auto results = vectorIndex_->search(vec, topK);
    hits.reserve(static_cast<int>(results.size()));
    for (const auto& [chunkId, score] : results) {
        try {
            hits.push_back(makeSearchHit(databaseManager_.getChunkById(chunkId), static_cast<double>(score)));
        } catch (...) {
        }
    }
    return hits;
}

KbRagResult KbService::buildContext(const QString& question, int focusDocId) const {
    KbRagResult result;
    if (!searchEngine_) {
        return result;
    }

    QString focusDocTitle;
    bool focusFromQuestion = false;
    if (focusDocId > 0) {
        try {
            const Document doc = databaseManager_.getDocumentById(focusDocId);
            focusDocTitle = doc.title.isEmpty() ? QFileInfo(doc.filePath).completeBaseName() : doc.title;
        } catch (...) {
        }
    }

    const QByteArray queryUtf8 = question.toUtf8();
    const std::string query(queryUtf8.constData(), static_cast<size_t>(queryUtf8.size()));

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
            if ((titleHit || baseHit) && title.size() > bestTitle.size()
                && title.size() >= 4 && base.size() >= 4) {
                bestTitle = title;
                bestDocId = doc.id;
            }
        }
    } catch (...) {
    }
    if (focusDocId <= 0 && bestDocId > 0 && !isConceptualQuery(question)) {
        focusDocId = bestDocId;
        focusDocTitle = bestTitle;
        focusFromQuestion = true;
    }

    const bool multiDocSummary = (focusDocId <= 0)
        && question.contains(QRegularExpression(
            QStringLiteral("这几篇|每篇|分别|几篇文档|所有文档|全部文档|各篇|各个文档")));

    result.scopeLabel = focusDocId > 0
        ? (focusFromQuestion
               ? QString::fromUtf8("基于问题指定文档: %1").arg(focusDocTitle)
               : QString::fromUtf8("基于选中文档: %1").arg(focusDocTitle))
        : (multiDocSummary ? QString::fromUtf8("基于全部文档（多文档覆盖）")
                           : QString::fromUtf8("基于全部文档"));

    std::vector<std::pair<int, double>> ranked;

    if (focusDocId > 0) {
        const QVector<DocumentChunk> focusChunks = databaseManager_.getChunksForDocument(focusDocId);
        const bool summaryQuery = question.contains(QRegularExpression(
            QStringLiteral("总结|概括|归纳|全文|整篇|本文|这篇|所有|全部|知识点")));

        ranked.reserve(static_cast<size_t>(focusChunks.size()));
        for (const DocumentChunk& chunk : focusChunks) {
            double score = 0.0;
            if (summaryQuery) {
                score = 100000.0 - chunk.chunkIndex;
            } else {
                score = lexicalQuestionScore(question, chunk.content);
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
        } catch (...) {
        }

        if (embeddingEngine_ && vectorIndex_) {
            try {
                const std::vector<float> qv = embeddingEngine_->encode(query);
                addRrf(vectorIndex_->search(qv, kRagVectorTopK), 0.45);
            } catch (...) {
            }
        }

        ranked.reserve(fused.size());
        for (const auto& [chunkId, score] : fused) {
            double finalScore = score;
            try {
                const DocumentChunk chunk = databaseManager_.getChunkById(chunkId);
                finalScore += lexicalQuestionScore(question, chunk.content) * 0.02;
                finalScore += conceptualQueryBoost(question, chunk);
            } catch (...) {
            }
            ranked.push_back({chunkId, finalScore});
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    QStringList sourceLines;
    int used = 0;
    const int maxChars = multiDocSummary ? kRagContextMaxChars * 2 : kRagContextMaxChars;

    if (multiDocSummary) {
        try {
            const QVector<Document> allDocs = databaseManager_.searchDocuments(QString());
            const int docCount = std::max(1, static_cast<int>(allDocs.size()));
            const int perDocMaxChars = std::max(400, maxChars / docCount);

            for (const Document& doc : allDocs) {
                if (!doc.filePath.isEmpty()) {
                    DocumentChunk probe;
                    probe.filePath = doc.filePath;
                    probe.title = doc.title;
                    if (isRagExcluded(probe)) {
                        continue;
                    }
                }

                const QVector<DocumentChunk> docChunks = databaseManager_.getChunksForDocument(doc.id);
                if (docChunks.isEmpty()) {
                    continue;
                }

                QString merged;
                for (const DocumentChunk& c : docChunks) {
                    if (merged.size() >= perDocMaxChars) {
                        break;
                    }
                    if (!merged.isEmpty()) {
                        merged += QStringLiteral("\n");
                    }
                    merged += c.content.trimmed();
                }
                merged = merged.left(perDocMaxChars).trimmed();
                if (merged.isEmpty()) {
                    continue;
                }

                QString title = doc.title.trimmed();
                if (title.isEmpty()) {
                    title = QFileInfo(doc.filePath).completeBaseName();
                }

                const QString marker = QString::fromUtf8("[来源 %1] %2").arg(used + 1).arg(title);
                const QString block = marker + QStringLiteral("\n") + merged;
                if (!result.context.isEmpty()) {
                    result.context += QStringLiteral("\n\n");
                }
                result.context += block;
                sourceLines << marker;

                KbRagSource entry;
                entry.documentId = doc.id;
                entry.chunkId = docChunks.first().id;
                entry.chunkIndex = docChunks.first().chunkIndex;
                entry.title = title;
                entry.chunkContent = docChunks.first().content.trimmed();
                result.sourceEntries.append(entry);
                ++used;
            }
        } catch (...) {
        }
    } else {
        for (const auto& [chunkId, _] : ranked) {
            if (used >= kRagFinalTopK || result.context.size() >= maxChars) {
                break;
            }

            DocumentChunk chunk;
            try {
                chunk = databaseManager_.getChunkById(chunkId);
            } catch (...) {
                continue;
            }

            if (focusDocId <= 0 && isRagExcluded(chunk)) {
                continue;
            }
            if (focusDocId <= 0 && isConceptualQuery(question) && isConceptualNoise(chunk)) {
                continue;
            }

            const QString title = normalizeTitle(chunk);
            const QString marker = QString::fromUtf8("[来源 %1] %2 / 片段 %3")
                                       .arg(used + 1)
                                       .arg(title)
                                       .arg(chunk.chunkIndex + 1);
            QString block = marker + QStringLiteral("\n") + chunk.content.trimmed();
            if (result.context.size() + block.size() > maxChars) {
                const int remainingChars =
                    std::max(0, maxChars - static_cast<int>(result.context.size()));
                block = block.left(remainingChars);
            }
            if (!result.context.isEmpty()) {
                result.context += QStringLiteral("\n\n");
            }
            result.context += block;
            sourceLines << marker;

            KbRagSource entry;
            entry.documentId = chunk.documentId;
            entry.chunkId = chunk.id;
            entry.chunkIndex = chunk.chunkIndex;
            entry.title = title;
            entry.chunkContent = chunk.content.trimmed();
            result.sourceEntries.append(entry);
            ++used;
        }

        if (used == 0 && isConceptualQuery(question) && focusDocId <= 0) {
            std::vector<std::pair<int, double>> fallbackRanked;
            try {
                const QVector<DocumentChunk> allChunks = databaseManager_.getAllChunks();
                fallbackRanked.reserve(static_cast<size_t>(allChunks.size()));
                for (const DocumentChunk& chunk : allChunks) {
                    if (!isPreferredConceptualSource(chunk)) {
                        continue;
                    }
                    const double score =
                        conceptualQueryBoost(question, chunk)
                        + lexicalQuestionScore(question, chunk.content) * 0.02;
                    fallbackRanked.push_back({chunk.id, score});
                }
                std::sort(fallbackRanked.begin(), fallbackRanked.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });
            } catch (...) {
            }

            for (const auto& [chunkId, _] : fallbackRanked) {
                if (used >= kRagFinalTopK || result.context.size() >= maxChars) {
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
                if (result.context.size() + block.size() > maxChars) {
                    const int remainingChars =
                        std::max(0, maxChars - static_cast<int>(result.context.size()));
                    block = block.left(remainingChars);
                }
                if (!result.context.isEmpty()) {
                    result.context += QStringLiteral("\n\n");
                }
                result.context += block;
                sourceLines << marker;

                KbRagSource entry;
                entry.documentId = chunk.documentId;
                entry.chunkId = chunk.id;
                entry.chunkIndex = chunk.chunkIndex;
                entry.title = title;
                entry.chunkContent = chunk.content.trimmed();
                result.sourceEntries.append(entry);
                ++used;
            }
        }
    }

    if (result.context.trimmed().isEmpty() && focusDocId > 0) {
        try {
            const Document doc = databaseManager_.getDocumentById(focusDocId);
            const QVector<QString> chunks = splitDocumentIntoChunks(doc.content);
            if (!chunks.isEmpty()) {
                const QString title =
                    doc.title.isEmpty() ? QFileInfo(doc.filePath).completeBaseName() : doc.title;
                result.context = QString::fromUtf8("[来源 1] %1 / 当前选中文档\n%2")
                                     .arg(title, chunks.first().left(kRagContextMaxChars));
                sourceLines << QString::fromUtf8("[来源 1] %1 / 当前选中文档").arg(title);
            }
        } catch (...) {
        }
    }

    result.sources = sourceLines.join(QStringLiteral("\n"));
    return result;
}

KbRagResult KbService::ask(const QString& question, int focusDocId) {
    KbRagResult result = buildContext(question, focusDocId);
    if (!ragEngine_) {
        throw std::runtime_error("RAG 引擎未初始化");
    }

    const QString effectiveQuestion = effectiveQuestionForAsk(question, result.scopeLabel);
    result.answer = QString::fromStdString(
        ragEngine_->ask(effectiveQuestion.toStdString(), result.context.toStdString()));
    return result;
}

Document KbService::getDocumentById(int id) const {
    return databaseManager_.getDocumentById(id);
}

DocumentChunk KbService::getChunk(int chunkId) const {
    return databaseManager_.getChunkById(chunkId);
}

int KbService::getDocumentsCount() const {
    return databaseManager_.getDocumentsCount();
}

bool KbService::importDocument(const QString& title, const QString& content, const QString& filePath) {
    if (isImportBlocked(filePath)) {
        return false;
    }

    try {
        const int id = databaseManager_.insertDocument(title, content, filePath);

        if (searchEngine_) {
            Document d;
            d.id = id;
            d.title = title;
            d.content = content;
            d.filePath = filePath;
            searchEngine_->addDocument(d);
        }

        if (embeddingEngine_ && vectorIndex_) {
            databaseManager_.replaceDocumentChunks(id, splitDocumentIntoChunks(content));
        }

        rebuildChunkIndexes(/*tryLoadVectorIndex=*/false);
        return true;
    } catch (...) {
        return false;
    }
}

QString KbService::embeddingStatusText() const {
    return embeddingModelLoaded_
        ? QString::fromUtf8("向量: BGE 真模型 (%1 维)").arg(embeddingEngine_ ? embeddingEngine_->dimension() : 0)
        : QString::fromUtf8("向量: 哈希回退");
}

QString KbService::statusSummary() const {
    QString ragModelName = QString::fromUtf8("未加载");
    int gpuLayers = 0;
    if (ragEngine_) {
        const RagEngine::LastMetrics m = ragEngine_->lastMetrics();
        if (!m.modelPath.empty()) {
            ragModelName = QFileInfo(QString::fromStdString(m.modelPath)).fileName();
        }
        gpuLayers = m.nGpuLayers;
    }

    QString embeddingName = QString::fromUtf8("未加载");
    int embDim = 0;
    if (embeddingEngine_) {
        embeddingName = QString::fromStdString(embeddingEngine_->modelName());
        embDim = embeddingEngine_->dimension();
    }

    int chunkCount = 0;
    if (vectorIndex_) {
        chunkCount = static_cast<int>(vectorIndex_->size());
    }

    const QString gpuStatus = gpuLayers > 0
        ? QString::fromUtf8("已启用 (offload %1 层)").arg(gpuLayers)
        : QString::fromUtf8("未启用 (CPU 推理)");

    return QString::fromUtf8(
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
}

QString KbService::resolveModelPath(const QString& fileName, QStringList* triedPaths) const {
    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    dirs << QDir(appDir).absoluteFilePath("models");
    dirs << QDir(appDir).absoluteFilePath("../models");
    dirs << QDir(appDir).absoluteFilePath("../../models");
    dirs << QDir::current().absoluteFilePath("models");
    dirs << QDir(QStringLiteral("F:/OfflineKB")).absoluteFilePath("models");
    dirs << QDir(QStringLiteral("F:/OfflineKB/models/Qwen2.5-7B-Instruct-GGUF")).absolutePath();
    dirs.append(extraModelDirs_);

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

QString KbService::vectorIndexPath() const {
    const QString dir = databaseManager_.appDataDir().isEmpty()
                            ? QDir(QCoreApplication::applicationDirPath()).absolutePath()
                            : databaseManager_.appDataDir();
    return QDir(dir).absoluteFilePath(QStringLiteral("chunks.hnsw"));
}

QString KbService::vectorIndexMetaPath() const {
    const QString dir = databaseManager_.appDataDir().isEmpty()
                            ? QDir(QCoreApplication::applicationDirPath()).absolutePath()
                            : databaseManager_.appDataDir();
    return QDir(dir).absoluteFilePath(QStringLiteral("chunks.meta.json"));
}

QVector<QString> KbService::splitDocumentIntoChunks(const QString& content) const {
    QVector<QString> chunks;
    const QString text = content.trimmed();
    if (text.isEmpty()) {
        return chunks;
    }

    const QStringList paragraphs =
        text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts);
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

void KbService::ensureChunksForExistingDocuments(const QVector<Document>& docs) {
    for (const Document& doc : docs) {
        const QVector<DocumentChunk> existing = databaseManager_.getChunksForDocument(doc.id);
        if (!existing.isEmpty()) {
            continue;
        }
        databaseManager_.replaceDocumentChunks(doc.id, splitDocumentIntoChunks(doc.content));
    }
}

void KbService::rebuildChunkIndexes(bool tryLoadVectorIndex) {
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
        const std::vector<float> vec = embeddingEngine_->encode(content);
        vectorIndex_->addVectors(vec, chunk.id);
    }

    try {
        vectorIndex_->save(QDir::toNativeSeparators(indexPath).toStdString());
    } catch (...) {
    }

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
    } catch (...) {
    }
}
