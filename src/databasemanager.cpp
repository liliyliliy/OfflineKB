#include "databasemanager.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <sqlite3.h>

#include <memory>
#include <stdexcept>

namespace {
using StmtPtr = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

QString sqliteError(sqlite3* db, const QString& context) {
    const char* err = db ? sqlite3_errmsg(db) : "Unknown sqlite error";
    return context + ": " + QString::fromUtf8(err);
}

QString roamingConfigDir() {
    const QByteArray appData = qgetenv("APPDATA");
    if (!appData.isEmpty()) {
        return QString::fromLocal8Bit(appData);
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

// GUI 与 headless CLI 共用固定目录，避免 QApplication 名称不同导致读到空库。
QString resolveSharedAppDataDir() {
    const QString roaming = roamingConfigDir();
    const QString canonical = QDir(roaming).filePath(QStringLiteral("OfflineKB/OfflineKB"));
    if (QFileInfo::exists(QDir(canonical).filePath(QStringLiteral("offlinekb.sqlite")))) {
        return QDir(canonical).absolutePath();
    }

    const QString legacyCli = QDir(roaming).filePath(QStringLiteral("offlinekb-cli/OfflineKB"));
    if (QFileInfo::exists(QDir(legacyCli).filePath(QStringLiteral("offlinekb.sqlite")))) {
        return QDir(legacyCli).absolutePath();
    }

    const QString dynamic =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/OfflineKB";
    if (QFileInfo::exists(dynamic + "/offlinekb.sqlite")) {
        return dynamic;
    }

    return QDir(canonical).absolutePath();
}
}  // namespace

DatabaseManager::DatabaseManager() : db_(nullptr) {}

DatabaseManager::~DatabaseManager() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void DatabaseManager::initialize() {
    appDataDir_ = resolveSharedAppDataDir();
    QDir dir;
    if (!dir.mkpath(appDataDir_)) {
        throw std::runtime_error(("Cannot create app data directory: " + appDataDir_).toStdString());
    }

    dbFilePath_ = appDataDir_ + "/offlinekb.sqlite";
    const QByteArray dbPathUtf8 = dbFilePath_.toUtf8();
    const int openRc = sqlite3_open_v2(dbPathUtf8.constData(), &db_,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (openRc != SQLITE_OK) {
        const QString msg = sqliteError(db_, "Failed to open sqlite database");
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error(msg.toStdString());
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS documents ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "title TEXT,"
        "content TEXT,"
        "file_path TEXT,"
        "created_at TEXT"
        ");";
    char* errMsg = nullptr;
    const int execRc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (execRc != SQLITE_OK) {
        QString msg = "Failed to create documents table";
        if (errMsg != nullptr) {
            msg += ": " + QString::fromUtf8(errMsg);
            sqlite3_free(errMsg);
        }
        throw std::runtime_error(msg.toStdString());
    }

    const char* chunkSql =
        "CREATE TABLE IF NOT EXISTS document_chunks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "document_id INTEGER NOT NULL,"
        "chunk_index INTEGER NOT NULL,"
        "content TEXT NOT NULL,"
        "token_estimate INTEGER DEFAULT 0,"
        "created_at TEXT,"
        "FOREIGN KEY(document_id) REFERENCES documents(id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_document_chunks_document_id "
        "ON document_chunks(document_id);";
    errMsg = nullptr;
    const int chunkRc = sqlite3_exec(db_, chunkSql, nullptr, nullptr, &errMsg);
    if (chunkRc != SQLITE_OK) {
        QString msg = "Failed to create document_chunks table";
        if (errMsg != nullptr) {
            msg += ": " + QString::fromUtf8(errMsg);
            sqlite3_free(errMsg);
        }
        throw std::runtime_error(msg.toStdString());
    }
}

int DatabaseManager::insertDocument(const QString& title, const QString& content, const QString& filePath) {
    ensureOpen();

    const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    const char* sql = "INSERT INTO documents (title, content, file_path, created_at) VALUES (?, ?, ?, ?);";

    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare insertDocument statement").toStdString());
    }

    StmtPtr stmt(rawStmt, sqlite3_finalize);

    const QByteArray titleUtf8 = title.toUtf8();
    const QByteArray contentUtf8 = content.toUtf8();
    const QByteArray filePathUtf8 = filePath.toUtf8();
    const QByteArray createdAtUtf8 = createdAt.toUtf8();

    sqlite3_bind_text(stmt.get(), 1, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, filePathUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, createdAtUtf8.constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(sqliteError(db_, "Failed to execute insertDocument").toStdString());
    }

    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

QVector<Document> DatabaseManager::searchDocuments(const QString& keyword) const {
    ensureOpen();

    QVector<Document> docs;
    sqlite3_stmt* rawStmt = nullptr;

    QString sql;
    QByteArray likePattern;
    if (keyword.trimmed().isEmpty()) {
        sql = "SELECT id, title, content, file_path, created_at FROM documents ORDER BY id DESC;";
    } else {
        sql =
            "SELECT id, title, content, file_path, created_at "
            "FROM documents "
            "WHERE title LIKE ? OR content LIKE ? "
            "ORDER BY id DESC;";
        likePattern = ("%" + keyword + "%").toUtf8();
    }

    int rc = sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare searchDocuments statement").toStdString());
    }

    StmtPtr stmt(rawStmt, sqlite3_finalize);

    if (!keyword.trimmed().isEmpty()) {
        sqlite3_bind_text(stmt.get(), 1, likePattern.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, likePattern.constData(), -1, SQLITE_TRANSIENT);
    }

    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        Document doc;
        doc.id = sqlite3_column_int(stmt.get(), 0);
        doc.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1)));
        doc.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2)));
        doc.filePath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3)));
        doc.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        docs.push_back(doc);
    }

    if (rc != SQLITE_DONE) {
        throw std::runtime_error(sqliteError(db_, "Failed while iterating searchDocuments result").toStdString());
    }

    return docs;
}

Document DatabaseManager::getDocumentById(int id) const {
    ensureOpen();

    const char* sql =
        "SELECT id, title, content, file_path, created_at FROM documents WHERE id = ? LIMIT 1;";
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare getDocumentById statement").toStdString());
    }

    StmtPtr stmt(rawStmt, sqlite3_finalize);

    sqlite3_bind_int(stmt.get(), 1, id);

    rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        Document doc;
        doc.id = sqlite3_column_int(stmt.get(), 0);
        doc.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1)));
        doc.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2)));
        doc.filePath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3)));
        doc.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        return doc;
    }

    if (rc == SQLITE_DONE) {
        throw std::runtime_error(("Document not found, id=" + QString::number(id)).toStdString());
    }

    throw std::runtime_error(sqliteError(db_, "Failed to execute getDocumentById").toStdString());
}

void DatabaseManager::replaceDocumentChunks(int documentId, const QVector<QString>& chunks) {
    ensureOpen();

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        QString msg = "Failed to begin replaceDocumentChunks transaction";
        if (errMsg != nullptr) {
            msg += ": " + QString::fromUtf8(errMsg);
            sqlite3_free(errMsg);
        }
        throw std::runtime_error(msg.toStdString());
    }

    try {
        {
            sqlite3_stmt* rawStmt = nullptr;
            const char* sql = "DELETE FROM document_chunks WHERE document_id = ?;";
            int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
            if (rc != SQLITE_OK) {
                throw std::runtime_error(sqliteError(db_, "Failed to prepare delete chunks").toStdString());
            }
            StmtPtr stmt(rawStmt, sqlite3_finalize);
            sqlite3_bind_int(stmt.get(), 1, documentId);
            rc = sqlite3_step(stmt.get());
            if (rc != SQLITE_DONE) {
                throw std::runtime_error(sqliteError(db_, "Failed to delete chunks").toStdString());
            }
        }

        const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        const QByteArray createdAtUtf8 = createdAt.toUtf8();
        const char* sql =
            "INSERT INTO document_chunks "
            "(document_id, chunk_index, content, token_estimate, created_at) "
            "VALUES (?, ?, ?, ?, ?);";

        sqlite3_stmt* rawStmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error(sqliteError(db_, "Failed to prepare insert chunk").toStdString());
        }
        StmtPtr stmt(rawStmt, sqlite3_finalize);

        for (int i = 0; i < chunks.size(); ++i) {
            const QString chunk = chunks[i].trimmed();
            if (chunk.isEmpty()) {
                continue;
            }
            const QByteArray contentUtf8 = chunk.toUtf8();
            const int tokenEstimate = std::max(1, static_cast<int>(contentUtf8.size() / 3));

            sqlite3_reset(stmt.get());
            sqlite3_clear_bindings(stmt.get());
            sqlite3_bind_int(stmt.get(), 1, documentId);
            sqlite3_bind_int(stmt.get(), 2, i);
            sqlite3_bind_text(stmt.get(), 3, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 4, tokenEstimate);
            sqlite3_bind_text(stmt.get(), 5, createdAtUtf8.constData(), -1, SQLITE_TRANSIENT);

            rc = sqlite3_step(stmt.get());
            if (rc != SQLITE_DONE) {
                throw std::runtime_error(sqliteError(db_, "Failed to insert chunk").toStdString());
            }
        }

        if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            QString msg = "Failed to commit replaceDocumentChunks";
            if (errMsg != nullptr) {
                msg += ": " + QString::fromUtf8(errMsg);
                sqlite3_free(errMsg);
            }
            throw std::runtime_error(msg.toStdString());
        }
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

QVector<DocumentChunk> DatabaseManager::getAllChunks() const {
    ensureOpen();

    QVector<DocumentChunk> chunks;
    const char* sql =
        "SELECT c.id, c.document_id, c.chunk_index, d.title, c.content, "
        "d.file_path, c.created_at "
        "FROM document_chunks c "
        "JOIN documents d ON d.id = c.document_id "
        "ORDER BY c.id ASC;";
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare getAllChunks").toStdString());
    }
    StmtPtr stmt(rawStmt, sqlite3_finalize);

    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        DocumentChunk chunk;
        chunk.id = sqlite3_column_int(stmt.get(), 0);
        chunk.documentId = sqlite3_column_int(stmt.get(), 1);
        chunk.chunkIndex = sqlite3_column_int(stmt.get(), 2);
        chunk.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3)));
        chunk.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        chunk.filePath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 5)));
        chunk.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 6)));
        chunks.push_back(chunk);
    }
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(sqliteError(db_, "Failed while iterating getAllChunks").toStdString());
    }
    return chunks;
}

QVector<DocumentChunk> DatabaseManager::getChunksForDocument(int documentId) const {
    ensureOpen();

    QVector<DocumentChunk> chunks;
    const char* sql =
        "SELECT c.id, c.document_id, c.chunk_index, d.title, c.content, "
        "d.file_path, c.created_at "
        "FROM document_chunks c "
        "JOIN documents d ON d.id = c.document_id "
        "WHERE c.document_id = ? "
        "ORDER BY c.chunk_index ASC;";
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare getChunksForDocument").toStdString());
    }
    StmtPtr stmt(rawStmt, sqlite3_finalize);
    sqlite3_bind_int(stmt.get(), 1, documentId);

    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        DocumentChunk chunk;
        chunk.id = sqlite3_column_int(stmt.get(), 0);
        chunk.documentId = sqlite3_column_int(stmt.get(), 1);
        chunk.chunkIndex = sqlite3_column_int(stmt.get(), 2);
        chunk.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3)));
        chunk.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        chunk.filePath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 5)));
        chunk.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 6)));
        chunks.push_back(chunk);
    }
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(sqliteError(db_, "Failed while iterating getChunksForDocument").toStdString());
    }
    return chunks;
}

DocumentChunk DatabaseManager::getChunkById(int chunkId) const {
    ensureOpen();

    const char* sql =
        "SELECT c.id, c.document_id, c.chunk_index, d.title, c.content, "
        "d.file_path, c.created_at "
        "FROM document_chunks c "
        "JOIN documents d ON d.id = c.document_id "
        "WHERE c.id = ? LIMIT 1;";
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare getChunkById").toStdString());
    }
    StmtPtr stmt(rawStmt, sqlite3_finalize);
    sqlite3_bind_int(stmt.get(), 1, chunkId);

    rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        DocumentChunk chunk;
        chunk.id = sqlite3_column_int(stmt.get(), 0);
        chunk.documentId = sqlite3_column_int(stmt.get(), 1);
        chunk.chunkIndex = sqlite3_column_int(stmt.get(), 2);
        chunk.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3)));
        chunk.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        chunk.filePath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 5)));
        chunk.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 6)));
        return chunk;
    }
    if (rc == SQLITE_DONE) {
        throw std::runtime_error(("Chunk not found, id=" + QString::number(chunkId)).toStdString());
    }
    throw std::runtime_error(sqliteError(db_, "Failed to execute getChunkById").toStdString());
}

int DatabaseManager::getDocumentsCount() const {
    ensureOpen();

    const char* sql = "SELECT COUNT(*) FROM documents;";
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare getDocumentsCount statement").toStdString());
    }

    StmtPtr stmt(rawStmt, sqlite3_finalize);
    rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int(stmt.get(), 0);
    }
    throw std::runtime_error(sqliteError(db_, "Failed to execute getDocumentsCount").toStdString());
}

QString DatabaseManager::appDataDir() const {
    return appDataDir_;
}

void DatabaseManager::ensureOpen() const {
    if (db_ == nullptr) {
        throw std::runtime_error("Database is not initialized.");
    }
}