#pragma once

#include <QString>

struct Document {
    int id = -1;
    QString title;
    QString content;
    QString filePath;
    QString createdAt;
};

struct DocumentChunk {
    int id = -1;
    int documentId = -1;
    int chunkIndex = -1;
    QString title;
    QString content;
    QString filePath;
    QString createdAt;
};