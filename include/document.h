#pragma once

#include <QString>

struct Document {
    int id = -1;
    QString title;
    QString content;
    QString filePath;
    QString createdAt;
};