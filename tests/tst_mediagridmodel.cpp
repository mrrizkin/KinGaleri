#include "core/db/Database.h"
#include "models/MediaGridModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace kg;

namespace {

/// A throwaway SQLite library with one enabled root. Every test gets its own
/// temporary directory, so tests can create and move real files without
/// touching the user's library.
class LibraryFixture
{
public:
    QTemporaryDir dir;
    std::unique_ptr<Database> db;
    qint64 rootId = 0;

    bool open(QString *error)
    {
        if (!dir.isValid()) {
            *error = QStringLiteral("temporary directory could not be created");
            return false;
        }
        db = std::make_unique<Database>(dir.filePath(QStringLiteral("library.db")));
        if (!db->open(error) || !db->migrate(error)) {
            return false;
        }
        QSqlQuery query(db->connection());
        query.prepare(QStringLiteral(
            "INSERT INTO roots(path, enabled, is_synthetic, added_at) VALUES(?, 1, 0, ?)"));
        query.addBindValue(dir.path());
        query.addBindValue(QDateTime::currentSecsSinceEpoch());
        if (!query.exec()) {
            *error = query.lastError().text();
            return false;
        }
        rootId = query.lastInsertId().toLongLong();
        return rootId > 0;
    }

    QString pathOf(const QString &name, const QString &subdir = QString()) const
    {
        const QString base =
            subdir.isEmpty() ? dir.path() : dir.path() + QLatin1Char('/') + subdir;
        return base + QLatin1Char('/') + name;
    }

    /// Inserts a media row and, by default, the matching file on disk.
    qint64 addMedia(const QString &name, qint64 takenAt = 0, const QString &subdir = QString(),
                    bool favorite = false, const QString &sourceAlbum = QString())
    {
        if (!subdir.isEmpty()) {
            QDir().mkpath(pathOf(QString(), subdir));
        }
        const QString path = pathOf(name, subdir);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return -1;
        }
        file.write("data");
        file.close();

        const QFileInfo info(path);
        QSqlQuery query(db->connection());
        query.prepare(QStringLiteral(
            "INSERT INTO media(path, root_id, filename, ext, mime, size_bytes, mtime_ns, "
            "taken_at, taken_at_src, kind, is_favorite, source_album) "
            "VALUES(?, ?, ?, ?, 'image/png', 4, 0, ?, 'mtime', 'image', ?, ?)"));
        query.addBindValue(path);
        query.addBindValue(rootId);
        query.addBindValue(info.fileName());
        query.addBindValue(info.suffix().toLower());
        query.addBindValue(takenAt > 0 ? QVariant(takenAt) : QVariant());
        query.addBindValue(favorite ? 1 : 0);
        query.addBindValue(sourceAlbum.isEmpty() ? QVariant() : QVariant(sourceAlbum));
        if (!query.exec()) {
            return -1;
        }
        return query.lastInsertId().toLongLong();
    }

    QVariant scalar(const QString &sql, const QVariantList &binds = QVariantList())
    {
        QSqlQuery query(db->connection());
        query.prepare(sql);
        for (const QVariant &value : binds) {
            query.addBindValue(value);
        }
        if (query.exec() && query.next()) {
            return query.value(0);
        }
        return {};
    }
};

constexpr qint64 kJan2020 = 1577836800000LL;
constexpr qint64 kJun2020 = 1590969600000LL;
constexpr qint64 kDec2020 = 1606780800000LL;

} // namespace

/**
 * The grid model owns almost all of the M2 destructive behaviour: album
 * membership, multi-selection, batch recycle-bin moves, on-disk rename/move and
 * the undo stack. These tests exercise it against real temp files so a rollback
 * that forgets to put a file back is caught, not just the SQL state.
 */
class TestMediaGridModel : public QObject
{
    Q_OBJECT

private slots:
    void reloadLoadsRows();
    void dateRangeFiltersInclusive();
    void dateRangeSwapsReversedBounds();
    void clearFiltersResetsDateRange();

    void selectionApiIsConsistent();
    void leavingSelectionModeClearsSelection();
    void selectRangeCoversInclusiveSpan();

    void albumCrudAndFiltering();
    void albumNameIsCaseInsensitive();
    void albumCardsMergeManualAndSource();
    void albumCoverUrlTracksRevision();

    void renameMovesFileAndUndoRestoresIt();
    void renameRejectsPathSeparatorAndCollision();

    void moveUpdatesPathAndUndoRestoresIt();
    void moveBatchRollsBackOnCollision();

    void trashSelectionSoftDeletesAndUndoRestores();
    void batchFavoritePersists();

    void taggingFiltersAndIndexesFts();
};

void TestMediaGridModel::reloadLoadsRows()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    QVERIFY(fixture.addMedia(QStringLiteral("a.png"), kJan2020) > 0);
    QVERIFY(fixture.addMedia(QStringLiteral("b.png"), kJun2020) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    QCOMPARE(model.itemCount(), 2);
    QCOMPARE(model.rowCount(), 1);
}

void TestMediaGridModel::dateRangeFiltersInclusive()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    QVERIFY(fixture.addMedia(QStringLiteral("jan.png"), kJan2020) > 0);
    QVERIFY(fixture.addMedia(QStringLiteral("jun.png"), kJun2020) > 0);
    QVERIFY(fixture.addMedia(QStringLiteral("dec.png"), kDec2020) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();
    QCOMPARE(model.itemCount(), 3);

    // Bounds are inclusive on both ends, so the June row is inside the window.
    model.setDateRange(kJun2020, kJun2020);
    QCOMPARE(model.itemCount(), 1);
    QCOMPARE(model.dateFrom(), kJun2020);
    QCOMPARE(model.dateTo(), kJun2020);
    QVERIFY(!model.dateRangeLabel().isEmpty());

    model.setDateRange(0, kJun2020);
    QCOMPARE(model.itemCount(), 2);

    model.clearDateRange();
    QCOMPARE(model.itemCount(), 3);
    QVERIFY(model.dateRangeLabel().isEmpty());
}

void TestMediaGridModel::dateRangeSwapsReversedBounds()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    QVERIFY(fixture.addMedia(QStringLiteral("jan.png"), kJan2020) > 0);
    QVERIFY(fixture.addMedia(QStringLiteral("dec.png"), kDec2020) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    model.setDateRange(kDec2020, kJan2020);
    QCOMPARE(model.dateFrom(), kJan2020);
    QCOMPARE(model.dateTo(), kDec2020);
    QCOMPARE(model.itemCount(), 2);
}

void TestMediaGridModel::clearFiltersResetsDateRange()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    QVERIFY(fixture.addMedia(QStringLiteral("jan.png"), kJan2020) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.setDateRange(kJan2020, kJan2020);
    model.reload();
    QCOMPARE(model.itemCount(), 1);

    model.clearFilters();
    QCOMPARE(model.dateFrom(), 0);
    QCOMPARE(model.dateTo(), 0);
    QCOMPARE(model.itemCount(), 1);
}

void TestMediaGridModel::selectionApiIsConsistent()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 a = fixture.addMedia(QStringLiteral("a.png"), kJan2020);
    const qint64 b = fixture.addMedia(QStringLiteral("b.png"), kJun2020);
    QVERIFY(a > 0 && b > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    QSignalSpy spy(&model, &MediaGridModel::selectionChanged);

    model.selectAll();
    QCOMPARE(model.selectionCount(), 2);
    QVERIFY(model.isSelected(a));
    QCOMPARE(model.selectedIds().size(), 2);

    model.clearSelection();
    QCOMPARE(model.selectionCount(), 0);

    model.toggleSelected(a);
    QCOMPARE(model.selectionCount(), 1);
    QVERIFY(model.isSelected(a));
    model.toggleSelected(a);
    QCOMPARE(model.selectionCount(), 0);

    model.setSelected(b, true);
    QVERIFY(model.isSelected(b));
    model.setSelected(b, false);
    QVERIFY(!model.isSelected(b));

    QVERIFY(spy.count() > 0);
}

void TestMediaGridModel::leavingSelectionModeClearsSelection()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 a = fixture.addMedia(QStringLiteral("a.png"), kJan2020);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    model.setSelectionMode(true);
    model.setSelected(a, true);
    QCOMPARE(model.selectionCount(), 1);

    model.setSelectionMode(false);
    QVERIFY(!model.selectionMode());
    QCOMPARE(model.selectionCount(), 0);
}

void TestMediaGridModel::selectRangeCoversInclusiveSpan()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    for (int i = 0; i < 4; ++i) {
        QVERIFY(fixture.addMedia(QStringLiteral("m%1.png").arg(i), kJan2020 + i) > 0);
    }

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    model.selectRange(1, 2, false);
    QCOMPARE(model.selectionCount(), 2);

    model.selectRange(0, 0, true);
    QCOMPARE(model.selectionCount(), 3);
}

void TestMediaGridModel::albumCrudAndFiltering()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 a = fixture.addMedia(QStringLiteral("a.png"), kJan2020);
    const qint64 b = fixture.addMedia(QStringLiteral("b.png"), kJun2020);
    QVERIFY(a > 0 && b > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    const qint64 album = model.createAlbum(QStringLiteral("Trip"));
    QVERIFY(album > 0);

    model.addToAlbum(album, {a, b});
    QCOMPARE(model.albums().size(), 1);
    QCOMPARE(model.albums().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Trip"));
    QCOMPARE(model.albums().first().toMap().value(QStringLiteral("count")).toInt(), 2);

    model.setAlbumId(album);
    model.reload();
    QCOMPARE(model.itemCount(), 2);

    model.renameAlbum(album, QStringLiteral("Holiday"));
    QCOMPARE(model.albums().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Holiday"));

    model.removeFromAlbum(album, {a});
    model.reload();
    QCOMPARE(model.itemCount(), 1);

    model.deleteAlbum(album);
    QCOMPARE(model.albums().size(), 0);
    // Deleting the album being viewed clears the filter.
    QCOMPARE(model.albumId(), 0);
    QCOMPARE(model.itemCount(), 2);
}

void TestMediaGridModel::albumNameIsCaseInsensitive()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    QVERIFY(fixture.addMedia(QStringLiteral("a.png"), kJan2020) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    const qint64 first = model.createAlbum(QStringLiteral("Trip"));
    const qint64 second = model.createAlbum(QStringLiteral("  trip  "));
    QCOMPARE(second, first);
    QCOMPARE(model.albums().size(), 1);
    QCOMPARE(model.createAlbum(QStringLiteral("   ")), qint64(0));
}

void TestMediaGridModel::albumCardsMergeManualAndSource()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 camA = fixture.addMedia(QStringLiteral("cam-a.png"), kJan2020,
                                         QString(), false, QStringLiteral("Camera"));
    const qint64 camB = fixture.addMedia(QStringLiteral("cam-b.png"), kJun2020,
                                         QString(), false, QStringLiteral("Camera"));
    const qint64 shot = fixture.addMedia(QStringLiteral("shot.png"), kDec2020,
                                         QString(), false, QStringLiteral("Screenshots"));
    QVERIFY(camA > 0 && camB > 0 && shot > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    const qint64 album = model.createAlbum(QStringLiteral("Trip"));
    model.addToAlbum(album, {camA, shot});

    const QVariantList cards = model.albumCards();
    QCOMPARE(cards.size(), 3); // one manual album + two source albums

    // Manual albums are listed first and count live members only.
    const QVariantMap manual = cards.first().toMap();
    QCOMPARE(manual.value(QStringLiteral("kind")).toString(), QStringLiteral("manual"));
    QCOMPARE(manual.value(QStringLiteral("name")).toString(), QStringLiteral("Trip"));
    QCOMPARE(manual.value(QStringLiteral("count")).toInt(), 2);
    // Cover is the newest member (shot, Dec 2020), and carries a real thumb URL.
    QCOMPARE(manual.value(QStringLiteral("coverId")).toLongLong(), shot);
    QVERIFY(manual.value(QStringLiteral("coverUrl")).toString()
                .startsWith(QStringLiteral("image://thumbs/")));

    int cameraCount = -1;
    int screenshotsCount = -1;
    QString cameraKind;
    for (const QVariant &card : cards) {
        const QVariantMap map = card.toMap();
        const QString name = map.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("Camera")) {
            cameraCount = map.value(QStringLiteral("count")).toInt();
            cameraKind = map.value(QStringLiteral("kind")).toString();
            QCOMPARE(map.value(QStringLiteral("id")).toLongLong(), qint64(0));
        } else if (name == QLatin1String("Screenshots")) {
            screenshotsCount = map.value(QStringLiteral("count")).toInt();
        }
    }
    QCOMPARE(cameraCount, 2);
    QCOMPARE(cameraKind, QStringLiteral("source"));
    QCOMPARE(screenshotsCount, 1);
}

void TestMediaGridModel::albumCoverUrlTracksRevision()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 id = fixture.addMedia(QStringLiteral("a.png"), kJan2020);
    QVERIFY(id > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    const QString before = model.albumCoverUrl(id);
    QVERIFY(before.contains(QStringLiteral("v=0")));
    // A finished thumbnail must move the URL forward so QML drops the placeholder.
    model.onThumbnailReady(id);
    QVERIFY(model.albumCoverUrl(id) != before);
    QVERIFY(model.albumCoverUrl(id).contains(QStringLiteral("v=1")));
    QCOMPARE(model.albumCoverUrl(0), QString());
}

void TestMediaGridModel::renameMovesFileAndUndoRestoresIt()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 id = fixture.addMedia(QStringLiteral("orig.png"), kJan2020);
    QVERIFY(id > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    const QString oldPath = fixture.pathOf(QStringLiteral("orig.png"));
    const QString newPath = fixture.pathOf(QStringLiteral("renamed.png"));
    QVERIFY(QFileInfo::exists(oldPath));
    QVERIFY(!QFileInfo::exists(newPath));

    const QString renameError = model.renameMedia(id, QStringLiteral("renamed.png"));
    QVERIFY2(renameError.isEmpty(), qUtf8Printable(renameError));
    QVERIFY(!QFileInfo::exists(oldPath));
    QVERIFY(QFileInfo::exists(newPath));
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT path FROM media WHERE id = ?"), {id}).toString(),
             newPath);
    QVERIFY(model.canUndo());
    QVERIFY(model.undoLabel().contains(QStringLiteral("renamed.png")));

    model.undo();
    QVERIFY(QFileInfo::exists(oldPath));
    QVERIFY(!QFileInfo::exists(newPath));
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT path FROM media WHERE id = ?"), {id}).toString(),
             oldPath);
    QVERIFY(!model.canUndo());
}

void TestMediaGridModel::renameRejectsPathSeparatorAndCollision()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 id = fixture.addMedia(QStringLiteral("a.png"), kJan2020);
    QVERIFY(fixture.addMedia(QStringLiteral("b.png"), kJun2020) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    QVERIFY(!model.renameMedia(id, QStringLiteral("sub/a.png")).isEmpty());
    QVERIFY(!model.renameMedia(id, QStringLiteral("b.png")).isEmpty());
    QVERIFY(!model.renameMedia(id, QStringLiteral("  ")).isEmpty());
    // Nothing was touched.
    QVERIFY(QFileInfo::exists(fixture.pathOf(QStringLiteral("a.png"))));
    QVERIFY(!model.canUndo());
}

void TestMediaGridModel::moveUpdatesPathAndUndoRestoresIt()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 id = fixture.addMedia(QStringLiteral("a.png"), kJan2020);
    QVERIFY(id > 0);
    QVERIFY(QDir(fixture.dir.path()).mkpath(QStringLiteral("target")));

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    const QString oldPath = fixture.pathOf(QStringLiteral("a.png"));
    const QString targetDir = fixture.pathOf(QString(), QStringLiteral("target"));
    const QString newPath = fixture.pathOf(QStringLiteral("a.png"), QStringLiteral("target"));

    // Passing a file:// URL mirrors exactly what QML's FolderDialog hands over.
    const QString moveError =
        model.moveMedia({id}, QStringLiteral("file://") + targetDir);
    QVERIFY2(moveError.isEmpty(), qUtf8Printable(moveError));
    QVERIFY(!QFileInfo::exists(oldPath));
    QVERIFY(QFileInfo::exists(newPath));

    model.undo();
    QVERIFY(QFileInfo::exists(oldPath));
    QVERIFY(!QFileInfo::exists(newPath));
}

void TestMediaGridModel::moveBatchRollsBackOnCollision()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 first = fixture.addMedia(QStringLiteral("a.png"), kJan2020);
    const qint64 second = fixture.addMedia(QStringLiteral("b.png"), kJun2020);
    QVERIFY(first > 0 && second > 0);
    // A file already sitting at the destination for the *second* item.
    QVERIFY(fixture.addMedia(QStringLiteral("b.png"), kDec2020, QStringLiteral("target")) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    const QString targetDir = fixture.pathOf(QStringLiteral("target"));
    const QString moveError = model.moveMedia({first, second}, targetDir);
    QVERIFY(!moveError.isEmpty());

    // All-or-nothing: the first file must have been rolled back.
    QVERIFY(QFileInfo::exists(fixture.pathOf(QStringLiteral("a.png"))));
    QVERIFY(QFileInfo::exists(fixture.pathOf(QStringLiteral("b.png"))));
    QVERIFY(!QFileInfo::exists(fixture.pathOf(QStringLiteral("a.png"), QStringLiteral("target"))));
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT path FROM media WHERE id = ?"), {first}).toString(),
             fixture.pathOf(QStringLiteral("a.png")));
    QVERIFY(!model.canUndo());
}

void TestMediaGridModel::trashSelectionSoftDeletesAndUndoRestores()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    QVERIFY(fixture.addMedia(QStringLiteral("a.png"), kJan2020) > 0);
    QVERIFY(fixture.addMedia(QStringLiteral("b.png"), kJun2020) > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    model.selectAll();
    model.trashSelection();

    QCOMPARE(model.itemCount(), 0);
    QCOMPARE(model.trashCount(), 2);
    QVERIFY(model.canUndo());
    // No file on disk is touched by a soft delete (PRD 4.3).
    QVERIFY(QFileInfo::exists(fixture.pathOf(QStringLiteral("a.png"))));

    model.undo();
    QCOMPARE(model.itemCount(), 2);
    QCOMPARE(model.trashCount(), 0);
}

void TestMediaGridModel::batchFavoritePersists()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 a = fixture.addMedia(QStringLiteral("a.png"), kJan2020);
    QVERIFY(a > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();

    model.setSelected(a, true);
    model.setSelectionFavorite(true);
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT is_favorite FROM media WHERE id = ?"), {a}).toInt(),
             1);

    model.setSelectionFavorite(false);
    QCOMPARE(fixture.scalar(QStringLiteral("SELECT is_favorite FROM media WHERE id = ?"), {a}).toInt(),
             0);
}

void TestMediaGridModel::taggingFiltersAndIndexesFts()
{
    LibraryFixture fixture;
    QString error;
    QVERIFY2(fixture.open(&error), qUtf8Printable(error));
    const qint64 first = fixture.addMedia(QStringLiteral("one.png"), kJan2020);
    const qint64 second = fixture.addMedia(QStringLiteral("two.png"), kJun2020);
    QVERIFY(first > 0 && second > 0);

    MediaGridModel model;
    model.attachDatabase(fixture.db.get());
    model.reload();
    QCOMPARE(model.itemCount(), 2);

    model.addTagToMedia({first, second}, QStringLiteral("Family"));
    model.addTagToMedia({first}, QStringLiteral("Holiday"));
    QCOMPARE(model.tags().length(), 2);
    QCOMPARE(model.mediaTags(first), QStringList({QStringLiteral("Family"), QStringLiteral("Holiday")}));

    // The tag filter is a case-insensitive membership test.
    model.setTag(QStringLiteral("holiday"));
    QCOMPARE(model.itemCount(), 1);
    model.setTag(QStringLiteral("FAMILY"));
    QCOMPARE(model.itemCount(), 2);
    model.setTag(QString());
    QCOMPARE(model.itemCount(), 2);

    // The FTS index carries the tag list (migration v8).
    auto ftsMatches = [&fixture](const QString &term) {
        QSqlQuery query(fixture.db->connection());
        query.prepare(QStringLiteral("SELECT rowid FROM media_fts WHERE media_fts MATCH ?"));
        query.addBindValue(term);
        if (!query.exec()) {
            return QList<qint64>{};
        }
        QList<qint64> ids;
        while (query.next()) {
            ids.append(query.value(0).toLongLong());
        }
        return ids;
    };
    QCOMPARE(ftsMatches(QStringLiteral("Holiday")), QList<qint64>{first});
    const QList<qint64> familyExpected = {first, second};
    QCOMPARE(ftsMatches(QStringLiteral("Family")), familyExpected);

    // Renaming rewrites the indexed text through the tags trigger.
    qint64 holidayId = 0;
    for (const QVariant &entry : model.tags()) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("name")).toString() == QLatin1String("Holiday")) {
            holidayId = map.value(QStringLiteral("id")).toLongLong();
        }
    }
    QVERIFY(holidayId > 0);
    model.renameTag(holidayId, QStringLiteral("Vacation"));
    QVERIFY(ftsMatches(QStringLiteral("Holiday")).isEmpty());
    QCOMPARE(ftsMatches(QStringLiteral("Vacation")), QList<qint64>{first});

    // Removing the last use drops the tag and clears it from the index.
    model.removeTagFromMedia({first}, QStringLiteral("Vacation"));
    QCOMPARE(model.tags().length(), 1);
    QVERIFY(ftsMatches(QStringLiteral("Vacation")).isEmpty());
    QCOMPARE(model.mediaTags(first), QStringList({QStringLiteral("Family")}));
}

QTEST_MAIN(TestMediaGridModel)
#include "tst_mediagridmodel.moc"
