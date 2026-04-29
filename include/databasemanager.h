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

    // Return total document count.
    int getDocumentsCount() const;

private:
    sqlite3* db_;
    QString dbFilePath_;

    void ensureOpen() const;
};