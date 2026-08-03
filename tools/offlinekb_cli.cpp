// offlinekb-cli: headless JSON Lines server for MCP integration.
//
// Usage:
//   offlinekb-cli server [--dict-dir PATH] [--models-dir PATH]
//
// Reads one JSON object per line from stdin, writes one JSON response per line to stdout.
// Logs go to stderr.

#include "kbservice.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <iostream>

namespace {

QJsonObject makeError(const QString& code, const QString& message) {
    QJsonObject err;
    err[QStringLiteral("code")] = code;
    err[QStringLiteral("message")] = message;
    return err;
}

QJsonObject makeResponse(int id, bool ok, const QJsonValue& result = QJsonValue(),
                         const QJsonObject& error = QJsonObject()) {
    QJsonObject resp;
    resp[QStringLiteral("id")] = id;
    resp[QStringLiteral("ok")] = ok;
    if (ok) {
        resp[QStringLiteral("result")] = result;
    } else {
        resp[QStringLiteral("error")] = error;
    }
    return resp;
}

void writeResponse(QTextStream& out, const QJsonObject& resp) {
    out << QString::fromUtf8(QJsonDocument(resp).toJson(QJsonDocument::Compact)) << '\n';
    out.flush();
}

QJsonArray documentsToJson(const QVector<Document>& docs) {
    QJsonArray arr;
    for (const Document& doc : docs) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = doc.id;
        obj[QStringLiteral("title")] = doc.title;
        obj[QStringLiteral("file_path")] = doc.filePath;
        obj[QStringLiteral("created_at")] = doc.createdAt;
        arr.append(obj);
    }
    return arr;
}

QJsonArray hitsToJson(const QVector<KbSearchHit>& hits) {
    QJsonArray arr;
    for (const KbSearchHit& hit : hits) {
        QJsonObject obj;
        obj[QStringLiteral("chunk_id")] = hit.chunkId;
        obj[QStringLiteral("document_id")] = hit.documentId;
        obj[QStringLiteral("chunk_index")] = hit.chunkIndex;
        obj[QStringLiteral("title")] = hit.title;
        obj[QStringLiteral("score")] = hit.score;
        obj[QStringLiteral("snippet")] = hit.snippet;
        arr.append(obj);
    }
    return arr;
}

QJsonArray sourcesToJson(const QVector<KbRagSource>& sources) {
    QJsonArray arr;
    for (const KbRagSource& src : sources) {
        QJsonObject obj;
        obj[QStringLiteral("document_id")] = src.documentId;
        obj[QStringLiteral("chunk_id")] = src.chunkId;
        obj[QStringLiteral("chunk_index")] = src.chunkIndex;
        obj[QStringLiteral("title")] = src.title;
        arr.append(obj);
    }
    return arr;
}

QJsonObject ragResultToJson(const KbRagResult& result) {
    QJsonObject obj;
    obj[QStringLiteral("answer")] = result.answer;
    obj[QStringLiteral("context")] = result.context;
    obj[QStringLiteral("sources_text")] = result.sources;
    obj[QStringLiteral("scope_label")] = result.scopeLabel;
    obj[QStringLiteral("sources")] = sourcesToJson(result.sourceEntries);
    return obj;
}

QJsonObject handleRequest(KbService& service, const QJsonObject& req) {
    const int id = req.value(QStringLiteral("id")).toInt(-1);
    const QString method = req.value(QStringLiteral("method")).toString();
    const QJsonObject params = req.value(QStringLiteral("params")).toObject();

    if (method == QStringLiteral("shutdown")) {
        QJsonObject result;
        result[QStringLiteral("status")] = QStringLiteral("bye");
        return makeResponse(id, true, result);
    }

    if (method == QStringLiteral("list_documents")) {
        QJsonObject result;
        result[QStringLiteral("app_data_dir")] = service.databaseManager().appDataDir();
        result[QStringLiteral("document_count")] = service.getDocumentsCount();
        result[QStringLiteral("documents")] = documentsToJson(service.listDocuments());
        return makeResponse(id, true, result);
    }

    if (method == QStringLiteral("search_kb")) {
        const QString query = params.value(QStringLiteral("query")).toString();
        const QString mode = params.value(QStringLiteral("mode")).toString(QStringLiteral("keyword"));
        const int topK = params.value(QStringLiteral("top_k")).toInt(10);

        if (query.trimmed().isEmpty()) {
            return makeResponse(id, false, QJsonValue(), makeError(QStringLiteral("INVALID_PARAMS"),
                                                                   QStringLiteral("query 不能为空")));
        }

        QVector<KbSearchHit> hits;
        if (mode == QStringLiteral("semantic")) {
            if (!service.isEmbeddingReady()) {
                return makeResponse(id, false, QJsonValue(),
                                    makeError(QStringLiteral("EMBEDDING_NOT_READY"),
                                              QStringLiteral("语义检索未就绪")));
            }
            hits = service.searchSemantic(query, topK);
        } else {
            if (!service.isSearchReady()) {
                return makeResponse(id, false, QJsonValue(),
                                    makeError(QStringLiteral("SEARCH_NOT_READY"),
                                              QStringLiteral("关键词检索未就绪")));
            }
            hits = service.searchKeyword(query, topK);
        }

        QJsonObject result;
        result[QStringLiteral("hits")] = hitsToJson(hits);
        return makeResponse(id, true, result);
    }

    if (method == QStringLiteral("get_chunk")) {
        const int chunkId = params.value(QStringLiteral("chunk_id")).toInt(-1);
        if (chunkId <= 0) {
            return makeResponse(id, false, QJsonValue(), makeError(QStringLiteral("INVALID_PARAMS"),
                                                                   QStringLiteral("chunk_id 无效")));
        }
        try {
            const DocumentChunk chunk = service.getChunk(chunkId);
            QJsonObject result;
            result[QStringLiteral("chunk_id")] = chunk.id;
            result[QStringLiteral("document_id")] = chunk.documentId;
            result[QStringLiteral("chunk_index")] = chunk.chunkIndex;
            result[QStringLiteral("title")] = chunk.title;
            result[QStringLiteral("content")] = chunk.content;
            result[QStringLiteral("file_path")] = chunk.filePath;
            return makeResponse(id, true, result);
        } catch (const std::exception& ex) {
            return makeResponse(id, false, QJsonValue(),
                                makeError(QStringLiteral("NOT_FOUND"), QString::fromUtf8(ex.what())));
        }
    }

    if (method == QStringLiteral("ask_rag")) {
        const QString question = params.value(QStringLiteral("question")).toString();
        if (question.trimmed().isEmpty()) {
            return makeResponse(id, false, QJsonValue(), makeError(QStringLiteral("INVALID_PARAMS"),
                                                                   QStringLiteral("question 不能为空")));
        }
        if (!service.isRagReady()) {
            return makeResponse(id, false, QJsonValue(),
                                makeError(QStringLiteral("RAG_NOT_READY"),
                                          QStringLiteral("RAG 引擎未初始化，请检查 GGUF 模型路径")));
        }

        int focusDocId = -1;
        if (params.contains(QStringLiteral("doc_id")) && !params.value(QStringLiteral("doc_id")).isNull()) {
            focusDocId = params.value(QStringLiteral("doc_id")).toInt(-1);
        }

        try {
            const KbRagResult result = service.ask(question, focusDocId);
            return makeResponse(id, true, ragResultToJson(result));
        } catch (const std::exception& ex) {
            return makeResponse(id, false, QJsonValue(),
                                makeError(QStringLiteral("RAG_ERROR"), QString::fromUtf8(ex.what())));
        }
    }

    if (method == QStringLiteral("status")) {
        QJsonObject result;
        result[QStringLiteral("app_data_dir")] = service.databaseManager().appDataDir();
        result[QStringLiteral("summary")] = service.statusSummary();
        result[QStringLiteral("embedding_status")] = service.embeddingStatusText();
        result[QStringLiteral("rag_ready")] = service.isRagReady();
        result[QStringLiteral("search_ready")] = service.isSearchReady();
        result[QStringLiteral("embedding_ready")] = service.isEmbeddingReady();
        return makeResponse(id, true, result);
    }

    return makeResponse(id, false, QJsonValue(),
                        makeError(QStringLiteral("UNKNOWN_METHOD"),
                                  QStringLiteral("未知 method: %1").arg(method)));
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OfflineKB"));
    QCoreApplication::setApplicationName(QStringLiteral("OfflineKB"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OfflineKB headless JSON Lines server"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption dictDirOpt({QStringLiteral("d"), QStringLiteral("dict-dir")},
                                  QStringLiteral("cppjieba dict directory"),
                                  QStringLiteral("path"));
    QCommandLineOption modelsDirOpt({QStringLiteral("m"), QStringLiteral("models-dir")},
                                    QStringLiteral("extra models search directory"),
                                    QStringLiteral("path"));
    parser.addOption(dictDirOpt);
    parser.addOption(modelsDirOpt);

    const QCommandLineOption serverOpt(QStringLiteral("server"), QStringLiteral("run JSON Lines server on stdio"));
    parser.addOption(serverOpt);
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("server"));

    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    const bool runServer = parser.isSet(serverOpt)
        || (positional.size() >= 1 && positional.first() == QStringLiteral("server"));

    if (!runServer) {
        std::cerr << "Usage: offlinekb-cli server [--dict-dir PATH] [--models-dir PATH]\n";
        return 1;
    }

    QString dictDir = parser.value(dictDirOpt);
    if (dictDir.isEmpty()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        dictDir = QDir(appDir).absoluteFilePath(QStringLiteral("../resources/dict"));
        if (!QFileInfo::exists(dictDir)) {
            dictDir = QDir(appDir).absoluteFilePath(QStringLiteral("resources/dict"));
        }
    }

    KbService service;
    const QString modelsDir = parser.value(modelsDirOpt);
    if (!modelsDir.isEmpty()) {
        service.addModelsSearchDir(modelsDir);
    }
    try {
        service.initialize(dictDir);
    } catch (const std::exception& ex) {
        std::cerr << "Initialize failed: " << ex.what() << '\n';
        return 2;
    }

    QTextStream in(stdin);
    QTextStream out(stdout);
    QTextStream err(stderr);
    err << "offlinekb-cli ready\n";
    err.flush();

    while (true) {
        const QString line = in.readLine();
        if (line.isNull()) {
            break;
        }
        const QByteArray lineBytes = line.toUtf8();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(lineBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            writeResponse(out, makeResponse(-1, false, QJsonValue(),
                                            makeError(QStringLiteral("INVALID_JSON"), parseError.errorString())));
            continue;
        }

        const QJsonObject req = doc.object();
        const QJsonObject resp = handleRequest(service, req);
        writeResponse(out, resp);

        if (req.value(QStringLiteral("method")).toString() == QStringLiteral("shutdown")) {
            break;
        }
    }

    return 0;
}
