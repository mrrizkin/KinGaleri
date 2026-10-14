#include "core/indexer/LibraryWatcher.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace kg;

/**
 * The watcher is what keeps the library fresh without a manual rescan (PRD
 * F-1.4). The parts worth pinning are the ones the UI depends on:
 *   - a change in a root produces exactly one debounced signal,
 *   - subdirectories created *after* setRoots() are picked up by refresh(),
 *   - stop() actually detaches the watches.
 */
class TestLibraryWatcher : public QObject
{
    Q_OBJECT

private slots:
    void changeEmitsDebouncedSignal();
    void subdirectoryIsPickedUpOnRefresh();
    void newDirectoryIsWatchedAfterRefresh();
    void stopDetachesWatches();
    void unchangedRootsAreIdempotent();

    static bool writeFile(const QString &path, const QByteArray &content = QByteArray("x"))
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(content);
        file.close();
        return true;
    }
};

void TestLibraryWatcher::changeEmitsDebouncedSignal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    LibraryWatcher watcher;
    watcher.setRoots({dir.path()});
    QVERIFY(watcher.watchedDirectoryCount() >= 1);

    QSignalSpy spy(&watcher, &LibraryWatcher::changed);
    QVERIFY(writeFile(dir.filePath(QStringLiteral("new.png"))));

    // Debounce window is 900 ms; allow generous headroom on a loaded CI box.
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

    // Nothing else may arrive after the burst settles.
    QTest::qWait(1500);
    QCOMPARE(spy.count(), 1);
}

void TestLibraryWatcher::subdirectoryIsPickedUpOnRefresh()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("nested/deeper")));

    LibraryWatcher watcher;
    watcher.setRoots({dir.path()});
    // The whole tree is walked up front, so nested directories count too.
    QVERIFY(watcher.watchedDirectoryCount() >= 3);

    QSignalSpy spy(&watcher, &LibraryWatcher::changed);
    QVERIFY(writeFile(dir.filePath(QStringLiteral("nested/deeper/x.png"))));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
}

void TestLibraryWatcher::newDirectoryIsWatchedAfterRefresh()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    LibraryWatcher watcher;
    watcher.setRoots({dir.path()});
    const int before = watcher.watchedDirectoryCount();

    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("created_later")));
    watcher.refresh();
    QCOMPARE(watcher.watchedDirectoryCount(), before + 1);

    // A file in the new directory must now produce a signal.
    QSignalSpy spy(&watcher, &LibraryWatcher::changed);
    QVERIFY(writeFile(dir.filePath(QStringLiteral("created_later/y.png"))));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
}

void TestLibraryWatcher::stopDetachesWatches()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    LibraryWatcher watcher;
    watcher.setRoots({dir.path()});
    QVERIFY(watcher.watchedDirectoryCount() >= 1);

    watcher.stop();
    QCOMPARE(watcher.watchedDirectoryCount(), 0);

    // With no watches there is nothing to debounce into a signal.
    QSignalSpy spy(&watcher, &LibraryWatcher::changed);
    QVERIFY(writeFile(dir.filePath(QStringLiteral("after_stop.png"))));
    QTest::qWait(1300);
    QCOMPARE(spy.count(), 0);
}

void TestLibraryWatcher::unchangedRootsAreIdempotent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    LibraryWatcher watcher;
    watcher.setRoots({dir.path()});
    const int count = watcher.watchedDirectoryCount();
    watcher.setRoots({dir.path()});
    QCOMPARE(watcher.watchedDirectoryCount(), count);
    QVERIFY(!watcher.truncated());
}

QTEST_MAIN(TestLibraryWatcher)
#include "tst_librarywatcher.moc"
