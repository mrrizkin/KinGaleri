#include "core/db/Database.h"
#include "core/db/LibraryMaintenance.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace kg;

namespace {

/// Minimal library: one root and the given media file names.
bool seedLibrary(Database &db, const QString &rootPath, const QStringList &names, QString *error)
{
    QSqlQuery root(db.connection());
    root.prepare(QStringLiteral(
        "INSERT INTO roots(path, enabled, is_synthetic, added_at) VALUES(?, 1, 0, ?)"));
    root.addBindValue(rootPath);
    root.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!root.exec()) {
        *error = root.lastError().text();
        return false;
    }
    const qint64 rootId = root.lastInsertId().toLongLong();

    for (const QString &name : names) {
        QSqlQuery media(db.connection());
        media.prepare(QStringLiteral(
            "INSERT INTO media(path, root_id, filename, ext, mime, source_album, taken_at) "
            "VALUES(?, ?, ?, 'jpg', 'image/jpeg', 'Camera', 1577836800000)"));
        media.addBindValue(rootPath + QLatin1Char('/') + name);
        media.addBindValue(rootId);
        media.addBindValue(name);
        if (!media.exec()) {
            *error = media.lastError().text();
            return false;
        }
    }
    return true;
}

/// Opens a fresh temporary library. The Database outlives the call site.
bool openLibrary(QTemporaryDir &dir, std::unique_ptr<Database> &db, QString *error)
{
    if (!dir.isValid()) {
        *error = QStringLiteral("temporary directory could not be created");
        return false;
    }
    db = std::make_unique<Database>(dir.filePath(QStringLiteral("library.db")));
    return db->open(error) && db->migrate(error);
}

int count(const Database &db, const QString &table)
{
    QSqlQuery query(db.connection());
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)) && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

} // namespace

/**
 * `ensureFtsPopulated` only reacts to a version bump, so a library whose index
 * was emptied or dropped while the version marker stayed current had no recovery
 * path at all. `rebuildFts` is that path; these tests cover both the "index out
 * of step" and the "virtual table missing entirely" cases.
 *
 * Each test opens its own temporary library: QTest reuses one test object across
 * slots, so a shared database file would leak rows and root paths between tests.
 */
class TestLibraryMaintenance : public QObject
{
    Q_OBJECT

private slots:
    void rebuildRepopulatesEmptiedIndex();
    void rebuildRecreatesMissingTableAndTriggers();
    void rebuildMakesSearchFindRows();
    void ensureFtsPopulatedIsNoOpAfterRebuild();
};

void TestLibraryMaintenance::rebuildRepopulatesEmptiedIndex()
{
    QTemporaryDir dir;
    std::unique_ptr<Database> db;
    QString error;
    QVERIFY2(openLibrary(dir, db, &error), qUtf8Printable(error));
    QVERIFY2(seedLibrary(*db, dir.path(),
                         {QStringLiteral("sunset.jpg"), QStringLiteral("forest.jpg")}, &error),
             qUtf8Printable(error));
    QCOMPARE(count(*db, QStringLiteral("media_fts")), 2);

    // Simulate a corrupted index: rows gone, version marker untouched.
    QSqlQuery wipe(db->connection());
    QVERIFY(wipe.exec(QStringLiteral("DELETE FROM media_fts")));
    QCOMPARE(count(*db, QStringLiteral("media_fts")), 0);

    QString rebuildError;
    QVERIFY2(LibraryMaintenance::rebuildFts(*db, &rebuildError), qUtf8Printable(rebuildError));
    QCOMPARE(count(*db, QStringLiteral("media_fts")), 2);
}

void TestLibraryMaintenance::rebuildRecreatesMissingTableAndTriggers()
{
    QTemporaryDir dir;
    std::unique_ptr<Database> db;
    QString error;
    QVERIFY2(openLibrary(dir, db, &error), qUtf8Printable(error));
    if (!db->ftsAvailable()) {
        QSKIP("SQLite was built without FTS5");
    }
    QVERIFY2(seedLibrary(*db, dir.path(), {QStringLiteral("sunset.jpg")}, &error),
             qUtf8Printable(error));

    QSqlQuery drop(db->connection());
    QVERIFY(drop.exec(QStringLiteral("DROP TABLE IF EXISTS media_fts")));

    QString rebuildError;
    QVERIFY2(LibraryMaintenance::rebuildFts(*db, &rebuildError), qUtf8Printable(rebuildError));
    QCOMPARE(count(*db, QStringLiteral("media_fts")), 1);

    // The recreated trigger must keep the index in step for new rows.
    QVERIFY2(seedLibrary(*db, dir.path() + QStringLiteral("/other"),
                         {QStringLiteral("beach.jpg")}, &error),
             qUtf8Printable(error));
    QCOMPARE(count(*db, QStringLiteral("media_fts")), 2);
}

void TestLibraryMaintenance::rebuildMakesSearchFindRows()
{
    QTemporaryDir dir;
    std::unique_ptr<Database> db;
    QString error;
    QVERIFY2(openLibrary(dir, db, &error), qUtf8Printable(error));
    if (!db->ftsAvailable()) {
        QSKIP("SQLite was built without FTS5");
    }
    QVERIFY2(seedLibrary(*db, dir.path(),
                         {QStringLiteral("sunset.jpg"), QStringLiteral("forest.jpg")}, &error),
             qUtf8Printable(error));

    QSqlQuery query(db->connection());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM media_fts WHERE media_fts MATCH :term"));
    query.addBindValue(QStringLiteral("sunset"));
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestLibraryMaintenance::ensureFtsPopulatedIsNoOpAfterRebuild()
{
    QTemporaryDir dir;
    std::unique_ptr<Database> db;
    QString error;
    QVERIFY2(openLibrary(dir, db, &error), qUtf8Printable(error));
    if (!db->ftsAvailable()) {
        QSKIP("SQLite was built without FTS5");
    }
    QVERIFY2(seedLibrary(*db, dir.path(), {QStringLiteral("sunset.jpg")}, &error),
             qUtf8Printable(error));

    QString rebuildError;
    QVERIFY2(LibraryMaintenance::rebuildFts(*db, &rebuildError), qUtf8Printable(rebuildError));

    // Version marker now matches, so the normal start path does nothing (and
    // must not fail).
    QVERIFY(LibraryMaintenance::ensureFtsPopulated(*db, &error));
    QCOMPARE(count(*db, QStringLiteral("media_fts")), 1);
}

QTEST_MAIN(TestLibraryMaintenance)
#include "tst_librarymaintenance.moc"
