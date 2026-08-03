// eval_retrieval: retrieval-quality regression tests (no LLM generation).
//
// Usage:
//   ./eval_retrieval [--dict-dir PATH] [--models-dir PATH]
//
// Runs rag_policy unit tests always; integration tests against the live DB
// when document_count > 0.

#include "kbservice.h"
#include "rag_policy.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

namespace {

int gFailures = 0;

void expectTrue(bool condition, const char* label) {
    if (condition) {
        std::cout << "[PASS] " << label << '\n';
    } else {
        std::cout << "[FAIL] " << label << '\n';
        ++gFailures;
    }
}

QString resolveDictDir(const QCommandLineParser& parser, const QCommandLineOption& dictDirOpt) {
    QString dictDir = parser.value(dictDirOpt);
    if (!dictDir.isEmpty()) {
        return dictDir;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    dictDir = QDir(appDir).absoluteFilePath(QStringLiteral("../resources/dict"));
    if (!QFileInfo::exists(dictDir)) {
        dictDir = QDir(appDir).absoluteFilePath(QStringLiteral("resources/dict"));
    }
    if (!QFileInfo::exists(dictDir)) {
        dictDir = QDir(QStringLiteral("F:/OfflineKB/resources/dict")).absolutePath();
    }
    return dictDir;
}

bool topSourcesMatch(const KbRagResult& result, KbService& service, int topN,
                     const std::function<bool(const QString& path)>& predicate) {
    const int count = std::min(topN, static_cast<int>(result.sourceEntries.size()));
    for (int i = 0; i < count; ++i) {
        try {
            const DocumentChunk chunk = service.getChunk(result.sourceEntries[i].chunkId);
            if (predicate(chunk.filePath)) {
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

bool topSourcesAllMatch(const KbRagResult& result, KbService& service, int topN,
                        const std::function<bool(const QString& path)>& predicate) {
    const int count = std::min(topN, static_cast<int>(result.sourceEntries.size()));
    if (count == 0) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        try {
            const DocumentChunk chunk = service.getChunk(result.sourceEntries[i].chunkId);
            if (!predicate(chunk.filePath)) {
                return false;
            }
        } catch (...) {
            return false;
        }
    }
    return true;
}

void runPolicyUnitTests() {
    DocumentChunk cmakeChunk;
    cmakeChunk.filePath = QStringLiteral("F:/OfflineKB/build/CMakeCache.txt");
    cmakeChunk.title = QStringLiteral("CMakeCache");
    expectTrue(isRagExcluded(cmakeChunk), "isRagExcluded blocks CMakeCache");
    expectTrue(isImportBlocked(cmakeChunk.filePath), "isImportBlocked blocks CMakeCache");

    DocumentChunk readmeChunk;
    readmeChunk.filePath = QStringLiteral("F:/OfflineKB/README.md");
    readmeChunk.title = QStringLiteral("README");
    expectTrue(!isRagExcluded(readmeChunk), "README is not excluded");
    expectTrue(!isImportBlocked(readmeChunk.filePath), "README import is allowed");

    const QString conceptualQuestion = QString::fromUtf8("OfflineKB 的 RAG 流程是什么？");
    expectTrue(isConceptualQuery(conceptualQuestion), "conceptual query detected");
    expectTrue(conceptualQueryBoost(conceptualQuestion, readmeChunk) >= 0.30,
               "README gets conceptual boost");

    const QString stackQuestionText = QString::fromUtf8("项目用了哪些技术栈？");
    expectTrue(isConceptualQuery(stackQuestionText), "tech stack query is conceptual");

    const QString factualQuestion = QString::fromUtf8("CMakeCache 里有什么？");
    expectTrue(!isConceptualQuery(factualQuestion), "factual query is not conceptual");
    expectTrue(conceptualQueryBoost(factualQuestion, readmeChunk) == 0.0,
               "no boost for factual query");
}

void runIntegrationTests(KbService& service) {
    if (service.getDocumentsCount() <= 0) {
        std::cout << "[SKIP] integration tests (empty knowledge base)\n";
        return;
    }

    const KbRagResult conceptual = service.buildContext(QString::fromUtf8("OfflineKB 的 RAG 流程是什么？"));
    expectTrue(!conceptual.sourceEntries.isEmpty(), "conceptual query returns sources");
    expectTrue(topSourcesMatch(conceptual, service, 3, [](const QString& path) {
                   const QString lower = path.toLower();
                   return lower.contains(QStringLiteral("readme.md"))
                       || (lower.contains(QStringLiteral("/docs/")) && lower.endsWith(QStringLiteral(".md")));
               }),
               "conceptual Top-3 includes README or docs/*.md");
    expectTrue(topSourcesAllMatch(conceptual, service, 3, [](const QString& path) {
                   return !path.contains(QStringLiteral("cmakecache"), Qt::CaseInsensitive);
               }),
               "conceptual Top-3 excludes CMakeCache");

    const KbRagResult stackQuestion =
        service.buildContext(QString::fromUtf8("项目用了哪些技术栈？"));
    const auto stackPathOk = [](const QString& path) {
        const QString lower = QDir::fromNativeSeparators(path).toLower();
        return lower.contains(QStringLiteral("readme.md"))
            || (lower.contains(QStringLiteral("/docs/")) && lower.endsWith(QStringLiteral(".md")));
    };
    const bool stackOk = topSourcesMatch(stackQuestion, service, 3, stackPathOk);
    expectTrue(stackOk, "tech stack Top-3 includes README or docs/*.md");

    const KbRagResult factual = service.buildContext(QString::fromUtf8("CMakeCache 里有什么？"));
    expectTrue(topSourcesMatch(factual, service, 5, [](const QString& path) {
                   return path.contains(QStringLiteral("CMakeCache"), Qt::CaseInsensitive);
               }),
               "factual CMakeCache query can still recall CMakeCache when present");
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OfflineKB"));
    QCoreApplication::setApplicationName(QStringLiteral("eval_retrieval"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OfflineKB retrieval regression tests"));
    parser.addHelpOption();
    QCommandLineOption dictDirOpt({QStringLiteral("d"), QStringLiteral("dict-dir")},
                                  QStringLiteral("cppjieba dict directory"),
                                  QStringLiteral("path"));
    QCommandLineOption modelsDirOpt({QStringLiteral("m"), QStringLiteral("models-dir")},
                                    QStringLiteral("extra models search directory"),
                                    QStringLiteral("path"));
    parser.addOption(dictDirOpt);
    parser.addOption(modelsDirOpt);
    parser.process(app);

    runPolicyUnitTests();

    const QString dictDir = resolveDictDir(parser, dictDirOpt);
    if (!QFileInfo::exists(dictDir)) {
        std::cout << "[WARN] dict dir not found: " << dictDir.toStdString()
                  << " — skipping integration tests\n";
        return gFailures == 0 ? 0 : 1;
    }

    KbService service;
    const QString modelsDir = parser.value(modelsDirOpt);
    if (!modelsDir.isEmpty()) {
        service.addModelsSearchDir(modelsDir);
    }

    try {
        service.initialize(dictDir);
    } catch (const std::exception& ex) {
        std::cout << "[WARN] initialize failed: " << ex.what()
                  << " — skipping integration tests\n";
        return gFailures == 0 ? 0 : 1;
    }

    runIntegrationTests(service);

    if (gFailures == 0) {
        std::cout << "All retrieval tests passed.\n";
        return 0;
    }
    std::cout << gFailures << " test(s) failed.\n";
    return 1;
}
