#pragma once

#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace kg {

/**
 * SQLite access layer.
 *
 * Connections are per-thread: `connection()` lazily creates (and caches) a
 * QSqlDatabase for the calling thread. SQLite is configured in WAL mode with a
 * single-writer assumption, which matches the indexer design in PRD 5.2.
 */
class Database
{
public:
    explicit Database(QString filePath);

    /// Closes the calling thread's connection. Releasing it in the destructor is
    /// what lets a second Database (a test, or a library switch) open a different
    /// file in the same thread: connections are named per-thread, so a leaked
    /// handle would otherwise be silently reused with the old file path.
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// Opens (creating if needed) the database on the calling thread and applies
    /// the mandatory pragmas from PRD 5.3.
    bool open(QString *error = nullptr);

    /// Closes every thread connection opened so far. Call before destruction.
    void close();

    /// Closes the connection belonging to the calling thread. QSqlDatabase
    /// connections are thread-affine, so worker threads must call this before
    /// exiting instead of relying on close() from the main thread.
    void closeThreadConnection();

    /// Applies all pending migrations inside a transaction. Non-fatal problems
    /// (e.g. FTS5 unavailable) are appended to `warnings`.
    bool migrate(QString *error = nullptr, QStringList *warnings = nullptr);

    int schemaVersion() const;

    /// QSqlDatabase for the calling thread. Never call from two threads at once
    /// with the same handle.
    QSqlDatabase connection() const;

    bool exec(const QString &sql, QString *error = nullptr) const;

    qint64 lastInsertId() const;

    bool begin(QString *error = nullptr) const;
    bool commit(QString *error = nullptr) const;
    void rollback() const;

    QString metaValue(const QString &key, const QString &fallback = QString()) const;
    bool setMetaValue(const QString &key, const QString &value) const;

    bool ftsAvailable() const { return m_ftsAvailable; }

    QString filePath() const { return m_filePath; }
    qint64 fileSizeBytes() const;

    /// Path used for the "synthetic benchmark" library root.
    static QString benchmarkRootPath();

private:
    QString connectionNameForCurrentThread() const;
    /// Returns and forgets the connection name for the calling thread.
    QString takeConnectionNameForCurrentThread() const;
    void applyPragmas(const QSqlDatabase &db) const;
    bool probeFts5(const QSqlDatabase &db) const;

    QString m_filePath;
    mutable QMutex m_mutex;
    mutable QHash<Qt::HANDLE, QString> m_connectionNames;
    mutable bool m_ftsAvailable = false;
};

} // namespace kg
