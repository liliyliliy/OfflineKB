#pragma once

#include "databasemanager.h"
#include "document.h"
#include "embeddingengine.h"
#include "ragengine.h"
#include "searchengine.h"
#include "tokenizer.h"
#include "vectorindex.h"

#include <QString>
#include <QVector>

struct KbSearchHit {
    int chunkId = -1;
    int documentId = -1;
    int chunkIndex = -1;
    QString title;
    double score = 0.0;
    QString snippet;
};

struct KbRagSource {
    int documentId = -1;
    int chunkId = -1;
    int chunkIndex = -1;
    QString title;
    QString chunkContent;
};

struct KbRagResult {
    QString answer;
    QString context;
    QString sources;
    QString scopeLabel;
    QVector<KbRagSource> sourceEntries;
};

class KbService {
public:
    KbService();
    ~KbService();

    KbService(const KbService&) = delete;
    KbService& operator=(const KbService&) = delete;

    // resourceDir: cppjieba dict directory (e.g. .../resources/dict)
    void initialize(const QString& resourceDir);
    void addModelsSearchDir(const QString& dir);

    QVector<Document> listDocuments() const;
    QVector<Document> searchDocuments(const QString& keyword) const;

    std::vector<std::pair<int, double>> searchDocumentsKeyword(const QString& query) const;
    QVector<KbSearchHit> searchKeyword(const QString& query, int topK = 10) const;
    QVector<KbSearchHit> searchSemantic(const QString& query, int topK = 10) const;

    KbRagResult buildContext(const QString& question, int focusDocId = -1) const;
    KbRagResult ask(const QString& question, int focusDocId = -1);

    Document getDocumentById(int id) const;
    DocumentChunk getChunk(int chunkId) const;
    int getDocumentsCount() const;

    bool importDocument(const QString& title, const QString& content, const QString& filePath);

    bool isSearchReady() const { return searchEngine_ != nullptr; }
    bool isEmbeddingReady() const { return embeddingEngine_ != nullptr && vectorIndex_ != nullptr; }
    bool isRagReady() const { return ragEngine_ != nullptr; }
    bool embeddingModelLoaded() const { return embeddingModelLoaded_; }

    QString embeddingStatusText() const;
    QString statusSummary() const;

    RagEngine* ragEngine() { return ragEngine_; }
    DatabaseManager& databaseManager() { return databaseManager_; }

private:
    QString resolveModelPath(const QString& fileName, QStringList* triedPaths = nullptr) const;
    QString vectorIndexPath() const;
    QString vectorIndexMetaPath() const;

    QVector<QString> splitDocumentIntoChunks(const QString& content) const;
    void ensureChunksForExistingDocuments(const QVector<Document>& docs);
    void rebuildChunkIndexes(bool tryLoadVectorIndex);

    DatabaseManager databaseManager_;
    SearchEngine* searchEngine_ = nullptr;
    Tokenizer* tokenizer_ = nullptr;
    EmbeddingEngine* embeddingEngine_ = nullptr;
    VectorIndex* vectorIndex_ = nullptr;
    RagEngine* ragEngine_ = nullptr;

    bool embeddingModelLoaded_ = false;
    QString resourceDir_;
    QStringList extraModelDirs_;
};
