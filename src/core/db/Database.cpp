#include "core/db/Database.h"

#include "core/db/Migrations.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace kg {

namespace {
constexpr auto kConnectionPrefix = "kingaleri-";

QString describe(const QSqlError &error)
{
    return QStringLiteral("%1 (driver: %2, db: %3)")
        .arg(error.text(), error.driverText(), error.databaseText());
}
} // namespace

Database::Database(QString filePath)
    : m_filePath(std::move(filePath))
{
}

Database::~Database()
{
    closeThreadConnection();
}

QString Database::connectionNameForCurrentThread() const
{
    const Qt::HANDLE tid = QThread::currentThreadId();
    QMutexLocker lock(&m_mutex);

    const auto it = m_connectionNames.constFind(tid);
    if (it != m_connectionNames.constEnd()) {
        return *it;
    }

    const QString name = QStringLiteral("%1%2").arg(QLatin1String(kConnectionPrefix),
                                                    QString::number(reinterpret_cast<quintptr>(tid)));
    m_connectionNames.insert(tid, name);
    return name;
}

void Database::applyPragmas(const QSqlDatabase &db) const
{
    // PRD 5.3. Values tuned for read-mostly workloads with a single writer.
    static const char *pragmas[] = {
        "PRAGMA journal_mode = WAL",
        "PRAGMA synchronous = NORMAL",
        "PRAGMA foreign_keys = ON",
        "PRAGMA busy_timeout = 5000",
        "PRAGMA temp_store = MEMORY",
        "PRAGMA cache_size = -65536",      // 64 MB page cache
        "PRAGMA mmap_size = 268435456",    // 256 MB
        "PRAGMA wal_autocheckpoint = 2000",
    };

    QSqlQuery query(db);
    for (const char *sql : pragmas) {
        if (!query.exec(QLatin1String(sql))) {
            const QSqlError error = query.lastError();
            qWarning("kin-galeri: pragma failed: %s -> %s", sql,
                     qUtf8Printable(describe(error)));
        }
    }
}

bool Database::probeFts5(const QSqlDatabase &db) const
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT sqlite_compileoption_used('ENABLE_FTS5')"))) {
        return false;
    }
    if (!query.next()) {
        return false;
    }
    return query.value(0).toInt() != 0;
}

bool Database::open(QString *error)
{
    QSqlDatabase db = connection();
    if (!db.isValid()) {
        if (error) {
            *error = QStringLiteral("Could not create a SQLite connection for '%1'").arg(m_filePath);
        }
        return false;
    }
    if (!db.isOpen() && !db.open()) {
        if (error) {
            *error = QStringLiteral("Could not open '%1': %2").arg(m_filePath, describe(db.lastError()));
        }
        return false;
    }

    m_ftsAvailable = probeFts5(db);
    return true;
}

QString Database::takeConnectionNameForCurrentThread() const
{
    const Qt::HANDLE tid = QThread::currentThreadId();
    QMutexLocker lock(&m_mutex);
    return m_connectionNames.take(tid);
}

void Database::closeThreadConnection()
{
    const QString name = takeConnectionNameForCurrentThread();
    if (name.isEmpty()) {
        return;
    }

    if (QSqlDatabase::contains(name)) {
        {
            QSqlDatabase db = QSqlDatabase::database(name, false);
            if (db.isValid() && db.isOpen()) {
                // Force a WAL checkpoint so a worker thread never leaves the
                // write-ahead log dirty for the next one.
                QSqlQuery checkpoint(db);
                checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(PASSIVE)"));
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
    }
}

void Database::close()
{
    closeThreadConnection();
}

QSqlDatabase Database::connection() const
{
    const QString name = connectionNameForCurrentThread();

    if (!QSqlDatabase::contains(name)) {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(m_filePath);
        if (!db.open()) {
            qWarning("kin-galeri: failed to open database '%s': %s", qUtf8Printable(m_filePath),
                     qUtf8Printable(describe(db.lastError())));
            return db;
        }
        applyPragmas(db);
        return db;
    }

    QSqlDatabase db = QSqlDatabase::database(name, false);
    if (db.isValid() && !db.isOpen()) {
        db.open();
        applyPragmas(db);
    }
    return db;
}

bool Database::exec(const QString &sql, QString *error) const
{
    QSqlQuery query(connection());
    if (query.exec(sql)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("%1\n  while executing: %2").arg(describe(query.lastError()), sql);
    }
    return false;
}

int Database::schemaVersion() const
{
    QSqlQuery query(connection());
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

bool Database::migrate(QString *error, QStringList *warnings)
{
    QSqlDatabase db = connection();

    const int current = schemaVersion();
    const int target = latestSchemaVersion();

    if (current > target) {
        if (error) {
            *error = QStringLiteral(
                         "Database schema version %1 is newer than this build supports (%2). "
                         "Upgrade kin-galeri or point --db at a different file.")
                         .arg(current)
                         .arg(target);
        }
        return false;
    }

    if (current == target) {
        return true;
    }

    for (const Migration &migration : migrations()) {
        if (migration.version <= current) {
            continue;
        }

        // FTS5 is a compile-time option; migrations that need it are
        // best-effort. The schema version still advances so a database created
        // on a build without FTS5 does not retry the migration forever.
        if (migration.requiresFts5 && !m_ftsAvailable) {
            const QString note =
                QStringLiteral("migration '%1' skipped: SQLite was built without FTS5").arg(migration.name);
            qWarning("kin-galeri: %s", qUtf8Printable(note));
            if (warnings) {
                warnings->append(note);
            }
            if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(migration.version), error)) {
                return false;
            }
            continue;
        }

        if (!db.transaction()) {
            if (error) {
                *error = QStringLiteral("Could not start a transaction for migration '%1': %2")
                             .arg(migration.name, describe(db.lastError()));
            }
            return false;
        }

        bool ok = true;
        QString failure;
        for (const QString &statement : migration.statements) {
            QString statementError;
            if (!exec(statement, &statementError)) {
                ok = false;
                failure = statementError;
                break;
            }
        }

        // PRAGMA user_version is transactional in SQLite, so bumping it inside
        // the same transaction keeps schema and version in lockstep.
        if (ok) {
            QString versionError;
            if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(migration.version), &versionError)) {
                ok = false;
                failure = versionError;
            }
        }

        if (!ok) {
            db.rollback();
            if (migration.requiresFts5) {
                qWarning("kin-galeri: optional migration '%s' failed: %s",
                         qUtf8Printable(migration.name), qUtf8Printable(failure));
                if (warnings) {
                    warnings->append(QStringLiteral("optional migration '%1' failed: %2")
                                         .arg(migration.name, failure));
                }
                // Force the version forward so we do not retry forever.
                QString ignore;
                exec(QStringLiteral("PRAGMA user_version = %1").arg(migration.version), &ignore);
                continue;
            }
            if (error) {
                *error = QStringLiteral("Migration '%1' (v%2) failed: %3")
                             .arg(migration.name)
                             .arg(migration.version)
                             .arg(failure);
            }
            return false;
        }

        if (!db.commit()) {
            const QSqlError commitError = db.lastError();
            db.rollback();
            if (error) {
                *error = QStringLiteral("Could not commit migration '%1': %2")
                             .arg(migration.name, describe(commitError));
            }
            return false;
        }

        qInfo("kin-galeri: applied migration v%d '%s'", migration.version,
              qUtf8Printable(migration.name));
    }

    return true;
}

qint64 Database::lastInsertId() const
{
    QSqlQuery query(connection());
    if (query.exec(QStringLiteral("SELECT last_insert_rowid()")) && query.next()) {
        return query.value(0).toLongLong();
    }
    return -1;
}

bool Database::begin(QString *error) const
{
    QSqlQuery query(connection());
    if (query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        return true;
    }
    if (error) {
        *error = describe(query.lastError());
    }
    return false;
}

bool Database::commit(QString *error) const
{
    QSqlQuery query(connection());
    if (query.exec(QStringLiteral("COMMIT"))) {
        return true;
    }
    if (error) {
        *error = describe(query.lastError());
    }
    return false;
}

void Database::rollback() const
{
    QSqlQuery query(connection());
    query.exec(QStringLiteral("ROLLBACK"));
}

QString Database::metaValue(const QString &key, const QString &fallback) const
{
    QSqlQuery query(connection());
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    query.addBindValue(key);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return fallback;
}

bool Database::setMetaValue(const QString &key, const QString &value) const
{
    QSqlQuery query(connection());
    query.prepare(QStringLiteral(
        "INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    return query.exec();
}

qint64 Database::fileSizeBytes() const
{
    qint64 total = 0;
    for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const QFileInfo info(m_filePath + suffix);
        if (info.exists()) {
            total += info.size();
        }
    }
    return total;
}

QString Database::benchmarkRootPath()
{
    return QStringLiteral("<benchmark>");
}

} // namespace kg
