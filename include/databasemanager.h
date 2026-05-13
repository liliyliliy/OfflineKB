#pragma once

#include "document.h"

#include <QString>
#include <QVector>

struct sqlite3;

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    // Open database and create schema if needed.
    void initialize();

    // Insert a document and return the inserted row id.
    int insertDocument(const QString& title, const QString& content, const QString& filePath);

    // Query all documents or filter by keyword.
    QVector<Document> searchDocuments(const QString& keyword) const;

    // Query one document by primary key id.
    Document getDocumentById(int id) const;

    // Replace all chunks for one document.
    void replaceDocumentChunks(int documentId, const QVector<QString>& chunks);

    // Query chunks used by retrieval.
    QVector<DocumentChunk> getAllChunks() const;
    QVector<DocumentChunk> getChunksForDocument(int documentId) const;
    DocumentChunk getChunkById(int chunkId) const;

    // Return total document count.
    int getDocumentsCount() const;

    // Directory that stores application data, indexes and sqlite DB.
    QString appDataDir() const;

private:
    sqlite3* db_;
    QString dbFilePath_;
    QString appDataDir_;

    void ensureOpen() const;
};