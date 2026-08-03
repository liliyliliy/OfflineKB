#include "rag_policy.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

QString normalizedPathLower(const QString& filePath) {
    return QDir::fromNativeSeparators(filePath).toLower();
}

bool pathMatchesNoisePatterns(const QString& filePath) {
    if (filePath.trimmed().isEmpty()) {
        return false;
    }

    const QString normalized = normalizedPathLower(filePath);
    const QString fileName = QFileInfo(filePath).fileName().toLower();

    if (fileName.startsWith(QStringLiteral("cmakecache"))) {
        return true;
    }

    static const QStringList kBuildDirMarkers = {
        QStringLiteral("/build/"),
        QStringLiteral("\\build\\"),
        QStringLiteral("/cmake-build/"),
        QStringLiteral("\\cmake-build\\"),
        QStringLiteral("/cmakefiles/"),
        QStringLiteral("\\cmakefiles\\"),
    };
    for (const QString& marker : kBuildDirMarkers) {
        if (normalized.contains(marker)) {
            return true;
        }
    }

    if (fileName.endsWith(QStringLiteral(".cmake"))
        && (normalized.contains(QStringLiteral("/build/"))
            || normalized.contains(QStringLiteral("\\build\\"))
            || normalized.contains(QStringLiteral("/cmake-build/"))
            || normalized.contains(QStringLiteral("\\cmake-build\\")))) {
        return true;
    }

    return false;
}

const QRegularExpression& conceptualQueryPattern() {
    static const QRegularExpression re(
        QStringLiteral("流程|架构|技术栈|怎么实现|工作原理|整体设计|offlinekb|rag"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

bool pathContainsDocsDir(const QString& normalizedPath) {
    return normalizedPath.contains(QStringLiteral("/docs/"))
        || normalizedPath.contains(QStringLiteral("\\docs\\"));
}

}  // namespace

bool isConceptualNoise(const DocumentChunk& chunk) {
    const QString path = normalizedPathLower(chunk.filePath);
    static const QStringList kNoiseMarkers = {
        QStringLiteral("面经"),
        QStringLiteral("笔试面试"),
        QStringLiteral("项目流程讲解"),
    };
    for (const QString& marker : kNoiseMarkers) {
        if (path.contains(marker)) {
            return true;
        }
    }
    return false;
}

bool isPreferredConceptualSource(const DocumentChunk& chunk) {
    if (isRagExcluded(chunk) || isConceptualNoise(chunk)) {
        return false;
    }
    const QString path = normalizedPathLower(chunk.filePath);
    return path.contains(QStringLiteral("readme.md"))
        || (pathContainsDocsDir(path) && path.endsWith(QStringLiteral(".md")));
}

bool isImportBlocked(const QString& filePath, QString* reason) {
    if (!pathMatchesNoisePatterns(filePath)) {
        return false;
    }
    if (reason) {
        *reason = QString::fromUtf8("已跳过 build/CMake 产物，默认不参与知识库");
    }
    return true;
}

bool isRagExcluded(const DocumentChunk& chunk) {
    if (pathMatchesNoisePatterns(chunk.filePath)) {
        return true;
    }
    return false;
}

bool isConceptualQuery(const QString& question) {
    return conceptualQueryPattern().match(question).hasMatch();
}

double conceptualQueryBoost(const QString& question, const DocumentChunk& chunk) {
    if (!isConceptualQuery(question)) {
        return 0.0;
    }

    const QString path = normalizedPathLower(chunk.filePath);
    const QString title = chunk.title.trimmed().toLower();

    double boost = 0.0;
    if (path.contains(QStringLiteral("readme.md"))) {
        boost += 0.30;
    }
    if (pathContainsDocsDir(path) && path.endsWith(QStringLiteral(".md"))) {
        boost += 0.25;
    }
    if (title == QStringLiteral("readme") || title.startsWith(QStringLiteral("docs/"))) {
        boost += 0.10;
    }
    if (path.contains(QStringLiteral("offlinekb"))) {
        boost += 0.20;
    }
    return boost;
}
