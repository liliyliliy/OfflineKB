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
}  // namespace

DatabaseManager::DatabaseManager() : db_(nullptr) {}

DatabaseManager::~DatabaseManager() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void DatabaseManager::initialize() {
    const QString appDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/OfflineKB";
    QDir dir;
    if (!dir.mkpath(appDataDir)) {
        throw std::runtime_error(("Cannot create app data directory: " + appDataDir).toStdString());
    }

    dbFilePath_ = appDataDir + "/offlinekb.sqlite";
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

void DatabaseManager::ensureOpen() const {
    if (db_ == nullptr) {
        throw std::runtime_error("Database is not initialized.");
    }
}