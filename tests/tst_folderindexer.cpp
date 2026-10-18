#include "core/db/Database.h"
#include "core/indexer/FolderIndexer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace kg;

namespace {

/// Writes a real (tiny) PNG so the indexer exercises QImageReader and the
/// dimension columns, not just the file-name filters.
bool writePng(const QString &path, int edge)
{
    QImage image(edge, edge, QImage::Format_RGB32);
    image.fill(Qt::red);
    return image.save(path, "PNG");
}

int mediaCount(Database &db)
{
    QSqlQuery query(db.connection());
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM media")) && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

int pathCount(Database &db, const QString &path)
{
    QSqlQuery query(db.connection());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM media WHERE path = ?"));
    query.addBindValue(path);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

int rootCount(Database &db)
{
    QSqlQuery query(db.connection());
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM roots")) && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

} // namespace

/**
 * Incremental scanning is the difference between a library that stays current
 * and one that re-reads every file on every change. These tests pin the three
 * behaviours the watcher depends on: an unchanged file is skipped, a modified
 * file is re-upserted, and a file that disappeared is reconciled out of the
 * database.
 */
class TestFolderIndexer : public QObject
{
    Q_OBJECT

private slots:
    void supportedExtensionsCoverPhotosAndVideos();
    void indexCreatesRowsForEachMediaFile();
    void reindexSkipsUnchangedFiles();
    void reindexUpsertsModifiedFiles();
    void reindexReconcilesDeletedFiles();
    void emptyFolderReconcilesAllRows();
    void ignoredDirectoriesArePruned();
    void hiddenDirectoriesArePruned();
    void symlinkedDirectoriesAreNotDescended();
    void scanOptionsRelaxPruning();
    void followSymlinksOptionIndexesLinkedTree();
    void ignoredPathsPruneSubtree();
    void ignoredPatternsPruneByName();
    void junkPatternsAreEditable();
    void emptyFolderIsNotAnError();
    void sniffsUnknownExtensionsWhenEnabled();
    void ambiguousSniffListIsTheOnlyGate();
    void skipsUnknownExtensionsWhenDisabled();
    void shallowScanDoesNotRecurseOrReconcile();
    void shallowScanReusesCoveringRoot();
};

void TestFolderIndexer::supportedExtensionsCoverPhotosAndVideos()
{
    const QStringList extensions = FolderIndexer::supportedExtensions();
    QVERIFY(extensions.contains(QStringLiteral("png")));
    QVERIFY(extensions.contains(QStringLiteral("jpg")));
    QVERIFY(FolderIndexer::isSupportedExtension(QStringLiteral("png")));
    QVERIFY(FolderIndexer::isSupportedExtension(QStringLiteral("PNG")));
    QVERIFY(!FolderIndexer::isSupportedExtension(QStringLiteral("txt")));

    QVERIFY(FolderIndexer::isVideoExtension(QStringLiteral("mp4")));
    QVERIFY(!FolderIndexer::isVideoExtension(QStringLiteral("png")));
    // `.ts` is TypeScript, not MPEG-TS: a home scan must not vacuum up source.
    QVERIFY(!FolderIndexer::isSupportedExtension(QStringLiteral("ts")));
    QVERIFY(FolderIndexer::isSupportedExtension(QStringLiteral("mts")));
}

void TestFolderIndexer::indexCreatesRowsForEachMediaFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("a.png")), 4));
    QVERIFY(writePng(dir.filePath(QStringLiteral("b.png")), 6));
    // Unsupported files are ignored, not counted as failures.
    QFile text(dir.filePath(QStringLiteral("notes.txt")));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("hello");
    text.close();

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    const FolderIndexer::Result result = indexer.run(dir.path());

    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    // `scanned` counts supported media files; notes.txt is skipped, not counted.
    QCOMPARE(result.scanned, 2);
    QCOMPARE(result.upserted, 2);
    QCOMPARE(result.unchanged, 0);
    QCOMPARE(mediaCount(db), 2);
}

void TestFolderIndexer::reindexSkipsUnchangedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("a.png")), 4));
    QVERIFY(writePng(dir.filePath(QStringLiteral("b.png")), 6));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    QVERIFY(indexer.run(dir.path()).ok());

    const FolderIndexer::Result second = indexer.run(dir.path());
    QVERIFY(second.ok());
    // mtime + size match, so no EXIF/header work happens on the second pass.
    QCOMPARE(second.upserted, 0);
    QCOMPARE(second.unchanged, 2);
    QCOMPARE(mediaCount(db), 2);
}

void TestFolderIndexer::reindexUpsertsModifiedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString aPath = dir.filePath(QStringLiteral("a.png"));
    QVERIFY(writePng(aPath, 4));
    QVERIFY(writePng(dir.filePath(QStringLiteral("b.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    QVERIFY(indexer.run(dir.path()).ok());

    // Rewrite one file with a different size; that changes both size and mtime.
    QTest::qWait(20);
    QVERIFY(writePng(aPath, 9));

    const FolderIndexer::Result second = indexer.run(dir.path());
    QVERIFY(second.ok());
    QCOMPARE(second.upserted, 1);
    QCOMPARE(second.unchanged, 1);
    QCOMPARE(mediaCount(db), 2);
}

void TestFolderIndexer::reindexReconcilesDeletedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString aPath = dir.filePath(QStringLiteral("a.png"));
    QVERIFY(writePng(aPath, 4));
    QVERIFY(writePng(dir.filePath(QStringLiteral("b.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    QVERIFY(indexer.run(dir.path()).ok());
    QCOMPARE(mediaCount(db), 2);

    QVERIFY(QFile::remove(aPath));
    const FolderIndexer::Result second = indexer.run(dir.path());
    QVERIFY(second.ok());

    // The vanished row is gone; the survivor is still there.
    QCOMPARE(mediaCount(db), 1);
    QSqlQuery query(db.connection());
    QVERIFY(query.exec(QStringLiteral("SELECT filename FROM media")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("b.png"));
}

void TestFolderIndexer::emptyFolderReconcilesAllRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString aPath = dir.filePath(QStringLiteral("a.png"));
    QVERIFY(writePng(aPath, 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    QVERIFY(indexer.run(dir.path()).ok());
    QCOMPARE(mediaCount(db), 1);

    // Removing the last file leaves `seen` empty; the reconcile must still run,
    // otherwise an emptied folder keeps its ghosts forever.
    QVERIFY(QFile::remove(aPath));
    const FolderIndexer::Result second = indexer.run(dir.path());
    QVERIFY2(second.ok(), qUtf8Printable(second.error));
    QCOMPARE(mediaCount(db), 0);
}

void TestFolderIndexer::ignoredDirectoriesArePruned()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("keep.png")), 4));

    // Both are non-hidden, so only the junk-name policy can skip them.
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("node_modules/pkg"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("node_modules/pkg/icon.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("__pycache__"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("__pycache__/art.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    const FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 1);
    QCOMPARE(mediaCount(db), 1);

    QVERIFY(FolderIndexer::isIgnoredDirectory(dir.filePath(QStringLiteral("node_modules"))));
    QVERIFY(!FolderIndexer::isIgnoredDirectory(dir.filePath(QStringLiteral("Pictures"))));
}

void TestFolderIndexer::hiddenDirectoriesArePruned()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("visible.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral(".cache/thumbs"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral(".cache/thumbs/stale.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    const FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 1);
    QCOMPARE(mediaCount(db), 1);
}

void TestFolderIndexer::symlinkedDirectoriesAreNotDescended()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString realDir = dir.filePath(QStringLiteral("real"));
    QVERIFY(QDir().mkpath(realDir));
    QVERIFY(writePng(realDir + QStringLiteral("/photo.png"), 4));

    const QString linkPath = dir.filePath(QStringLiteral("link"));
    if (!QFile::link(realDir, linkPath)) {
        QSKIP("filesystem does not support symlinks");
    }

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    const FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    // The real file is counted once; the directory symlink is not followed.
    QCOMPARE(result.scanned, 1);
    QCOMPARE(mediaCount(db), 1);
}

void TestFolderIndexer::junkPatternsAreEditable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("keep.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("node_modules"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("node_modules/nm.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("MyJunk"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("MyJunk/junk.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);

    // Defaults prune node_modules but not MyJunk.
    indexer.setScanOptions(FolderIndexer::ScanOptions{});
    FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 2); // keep.png + MyJunk/junk.png
    QCOMPARE(pathCount(db, dir.filePath(QStringLiteral("node_modules/nm.png"))), 0);
    QCOMPARE(pathCount(db, dir.filePath(QStringLiteral("MyJunk/junk.png"))), 1);

    // A custom list replaces the defaults (node_modules is walked now) and
    // matches case-insensitively (MYJUNK prunes MyJunk).
    FolderIndexer::ScanOptions options;
    options.junkPatterns = {QStringLiteral("MYJUNK")};
    indexer.setScanOptions(options);
    result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 2); // keep.png + node_modules/nm.png
    QCOMPARE(pathCount(db, dir.filePath(QStringLiteral("node_modules/nm.png"))), 1);
    QCOMPARE(pathCount(db, dir.filePath(QStringLiteral("MyJunk/junk.png"))), 0);
    QCOMPARE(mediaCount(db), 2);
}

void TestFolderIndexer::sniffsUnknownExtensionsWhenEnabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("a.png")), 4));
    // A real PNG whose name carries no usable extension: only the header can
    // tell the indexer what it is.
    const QString mystery = dir.filePath(QStringLiteral("mystery.blob"));
    QVERIFY(QFile::copy(dir.filePath(QStringLiteral("a.png")), mystery));
    QFile text(dir.filePath(QStringLiteral("notes.txt")));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("hello");
    text.close();

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    FolderIndexer::ScanOptions options;
    options.sniffUnknownExtensions = true;
    options.sniffExtensions = {QStringLiteral("blob")};
    indexer.setScanOptions(options);
    const FolderIndexer::Result result = indexer.run(dir.path());

    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    // Both the named and the header-only PNG count; the text file does not.
    QCOMPARE(result.scanned, 2);
    QCOMPARE(mediaCount(db), 2);
    QCOMPARE(pathCount(db, mystery), 1);

    // The row carries the sniffed format, not the on-disk extension.
    QSqlQuery query(db.connection());
    query.prepare(QStringLiteral("SELECT ext, kind FROM media WHERE path = ?"));
    query.addBindValue(mystery);
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("png"));
    QCOMPARE(query.value(1).toString(), QStringLiteral("image"));
}

void TestFolderIndexer::ambiguousSniffListIsTheOnlyGate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("a.png")), 4));
    // In the sniff list: a media file hiding behind an ambiguous extension.
    const QString disguised = dir.filePath(QStringLiteral("clip.ts"));
    QVERIFY(QFile::copy(dir.filePath(QStringLiteral("a.png")), disguised));
    // NOT in the sniff list: never opened, even though the bytes are media.
    const QString unlisted = dir.filePath(QStringLiteral("mystery.blob"));
    QVERIFY(QFile::copy(dir.filePath(QStringLiteral("a.png")), unlisted));
    // In the sniff list but not media: rejected by the header check.
    QFile source(dir.filePath(QStringLiteral("app.ts")));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("export const x = 1;\n");
    source.close();

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    FolderIndexer::ScanOptions options;
    options.sniffUnknownExtensions = true;
    options.sniffExtensions = {QStringLiteral("ts")};
    indexer.setScanOptions(options);
    const FolderIndexer::Result result = indexer.run(dir.path());

    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 2);
    QCOMPARE(mediaCount(db), 2);
    QCOMPARE(pathCount(db, disguised), 1);
    QCOMPARE(pathCount(db, unlisted), 0);
    QCOMPARE(pathCount(db, dir.filePath(QStringLiteral("app.ts"))), 0);
}

void TestFolderIndexer::skipsUnknownExtensionsWhenDisabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("a.png")), 4));
    const QString mystery = dir.filePath(QStringLiteral("mystery.blob"));
    QVERIFY(QFile::copy(dir.filePath(QStringLiteral("a.png")), mystery));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    FolderIndexer::ScanOptions options;
    options.sniffUnknownExtensions = false;
    indexer.setScanOptions(options);
    const FolderIndexer::Result result = indexer.run(dir.path());

    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 1);
    QCOMPARE(mediaCount(db), 1);
    QCOMPARE(pathCount(db, mystery), 0);
}

void TestFolderIndexer::emptyFolderIsNotAnError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    const FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY(result.ok());
    QCOMPARE(result.upserted, 0);
    QCOMPARE(mediaCount(db), 0);
}

void TestFolderIndexer::scanOptionsRelaxPruning()
{
    QVERIFY(FolderIndexer::isIgnoredDirectory(QStringLiteral("/home/u/.cache")));
    QVERIFY(FolderIndexer::isIgnoredDirectory(QStringLiteral("/home/u/node_modules")));

    FolderIndexer::ScanOptions includeHidden;
    includeHidden.includeHidden = true;
    QVERIFY(!FolderIndexer::isIgnoredDirectory(QStringLiteral("/home/u/.cache"), includeHidden));

    FolderIndexer::ScanOptions keepJunk;
    keepJunk.ignoreJunk = false;
    QVERIFY(!FolderIndexer::isIgnoredDirectory(QStringLiteral("/home/u/node_modules"), keepJunk));
}

void TestFolderIndexer::followSymlinksOptionIndexesLinkedTree()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString realDir = dir.filePath(QStringLiteral("real"));
    QVERIFY(QDir().mkpath(realDir));
    QVERIFY(writePng(realDir + QStringLiteral("/photo.png"), 4));

    const QString linkPath = dir.filePath(QStringLiteral("link"));
    if (!QFile::link(realDir, linkPath)) {
        QSKIP("filesystem does not support symlinks");
    }

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    FolderIndexer::ScanOptions options;
    options.followSymlinks = true;
    indexer.setScanOptions(options);

    const FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    // The link resolves to the real directory, so it must be walked once - not
    // twice - thanks to the canonical-path guard.
    QCOMPARE(result.scanned, 1);
    QCOMPARE(mediaCount(db), 1);
}

void TestFolderIndexer::ignoredPathsPruneSubtree()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("keep.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub/secret"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("sub/secret/hidden.png")), 4));
    QVERIFY(writePng(dir.filePath(QStringLiteral("sub/visible.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    FolderIndexer::ScanOptions options;
    options.ignoredPaths.insert(dir.filePath(QStringLiteral("sub/secret")));
    indexer.setScanOptions(options);

    const FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 2);
    QCOMPARE(mediaCount(db), 2);
}

void TestFolderIndexer::ignoredPatternsPruneByName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("keep.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("Screenshots"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("Screenshots/phone.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("nested/Screenshots"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("nested/Screenshots/tab.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    FolderIndexer::ScanOptions options;
    options.ignoredPatterns = {"Screenshots"};
    indexer.setScanOptions(options);

    const FolderIndexer::Result result = indexer.run(dir.path());
    QVERIFY2(result.ok(), qUtf8Printable(result.error));
    QCOMPARE(result.scanned, 1);
    QCOMPARE(mediaCount(db), 1);
}

void TestFolderIndexer::shallowScanDoesNotRecurseOrReconcile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("a.png")), 4));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub"))));
    QVERIFY(writePng(dir.filePath(QStringLiteral("sub/b.png")), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    QVERIFY(indexer.run(dir.path()).ok());
    QCOMPARE(mediaCount(db), 2);
    QCOMPARE(rootCount(db), 1);

    // Touch the top-level file so the shallow pass actually has work.
    QTest::qWait(20);
    QVERIFY(writePng(dir.filePath(QStringLiteral("a.png")), 9));

    const FolderIndexer::Result shallow = indexer.runShallow(dir.path());
    QVERIFY2(shallow.ok(), qUtf8Printable(shallow.error));
    // Only the folder given: sub/b.png is neither scanned nor reconciled away.
    QCOMPARE(shallow.scanned, 1);
    QCOMPARE(mediaCount(db), 2);
    QCOMPARE(pathCount(db, dir.filePath(QStringLiteral("sub/b.png"))), 1);
    // The folder did not become a second root.
    QCOMPARE(rootCount(db), 1);
}

void TestFolderIndexer::shallowScanReusesCoveringRoot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writePng(dir.filePath(QStringLiteral("top.png")), 4));
    const QString album = dir.filePath(QStringLiteral("album"));
    QVERIFY(QDir().mkpath(album));
    QVERIFY(writePng(album + QStringLiteral("/x.png"), 4));

    Database db(dir.filePath(QStringLiteral("library.db")));
    QString error;
    QVERIFY2(db.open(&error), qUtf8Printable(error));
    QVERIFY2(db.migrate(&error), qUtf8Printable(error));

    FolderIndexer indexer(&db);
    QVERIFY(indexer.run(dir.path()).ok());
    QCOMPARE(rootCount(db), 1);

    // Refreshing a subfolder must reuse the root that contains it, not register
    // the subfolder as a new root.
    const FolderIndexer::Result shallow = indexer.runShallow(album, dir.path());
    QVERIFY2(shallow.ok(), qUtf8Printable(shallow.error));
    QCOMPARE(shallow.scanned, 1);
    QCOMPARE(rootCount(db), 1);
    QCOMPARE(mediaCount(db), 2);
}

QTEST_MAIN(TestFolderIndexer)
#include "tst_folderindexer.moc"
