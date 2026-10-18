#include "app/CliOptions.h"

#include <QtTest>

using namespace kg;

/**
 * The command line is the only configuration surface (there is no settings
 * dialog), so the defaults and the validation ranges are behaviour, not
 * implementation detail. These tests pin:
 *   - the adaptive thumbnail budget default (PRD 4.1.1),
 *   - the documented ranges for the numeric options,
 *   - that a bad value warns instead of aborting the process.
 *
 * parseCli() never exits, which is exactly what makes it testable.
 */
class TestCliOptions : public QObject
{
    Q_OBJECT

private slots:
    void defaultsPreserveAdaptiveThumbnailBudget();
    void parsesIndexFoldersAndFlags();
    void parsesNumericOptions();
    void benchScrollEnablesPerfOverlay();
    void warnsOnOutOfRangeValues();
    void warnsOnUnknownOption();
    void resetAndRebuildFlags();
    void homeScanIsOnByDefaultAndOptOut();
    void openPathsComeFromPositionalArguments();
};

void TestCliOptions::defaultsPreserveAdaptiveThumbnailBudget()
{
    const CliOptions opts = parseCli({QStringLiteral("kin-galeri")});

    QCOMPARE(opts.thumbnailSize, 256);
    // -1 means "size the LRU from the visible grid", a deliberate M2 behaviour.
    QCOMPARE(opts.thumbnailMemoryCacheMb, -1);
    QCOMPARE(opts.gridLimit, 0);
    QCOMPARE(opts.seedBenchmarkCount, 0);
    QVERIFY(!opts.skipUi);
    QVERIFY(!opts.resetDatabase);
    QVERIFY(!opts.rebuildSearchIndex);
    QVERIFY(opts.autoHomeScan);
    QVERIFY(opts.indexFolders.isEmpty());
    QVERIFY(opts.warnings.isEmpty());
}

void TestCliOptions::parsesIndexFoldersAndFlags()
{
    const CliOptions opts = parseCli({QStringLiteral("kin-galeri"), QStringLiteral("--no-ui"),
                                      QStringLiteral("--perf-overlay"),
                                      QStringLiteral("--index"), QStringLiteral("/a"),
                                      QStringLiteral("-i"), QStringLiteral("/b")});

    QVERIFY(opts.skipUi);
    QVERIFY(opts.showPerfOverlay);
    QCOMPARE(opts.indexFolders, QStringList({QStringLiteral("/a"), QStringLiteral("/b")}));
}

void TestCliOptions::parsesNumericOptions()
{
    const CliOptions opts =
        parseCli({QStringLiteral("kin-galeri"), QStringLiteral("--thumbnail-size"),
                  QStringLiteral("512"), QStringLiteral("--thumb-memory-cache"),
                  QStringLiteral("64"), QStringLiteral("--limit"),
                  QStringLiteral("100"), QStringLiteral("--seed-benchmark"),
                  QStringLiteral("50000")});

    QCOMPARE(opts.thumbnailSize, 512);
    QCOMPARE(opts.thumbnailMemoryCacheMb, 64);
    QCOMPARE(opts.gridLimit, 100);
    QCOMPARE(opts.seedBenchmarkCount, 50000);
    QVERIFY(opts.warnings.isEmpty());
}

void TestCliOptions::benchScrollEnablesPerfOverlay()
{
    // The benchmark runs unattended, so the overlay has to come on by itself.
    const CliOptions opts = parseCli({QStringLiteral("kin-galeri"), QStringLiteral("--bench-scroll"),
                                      QStringLiteral("5")});

    QCOMPARE(opts.benchScrollSeconds, 5.0);
    QVERIFY(opts.showPerfOverlay);
    QVERIFY(opts.warnings.isEmpty());
}

void TestCliOptions::warnsOnOutOfRangeValues()
{
    const auto warningCount = [](const QStringList &args) {
        return parseCli(args).warnings.size();
    };

    QCOMPARE(warningCount({QStringLiteral("kin-galeri"), QStringLiteral("--thumbnail-size"),
                           QStringLiteral("10")}),
             1);
    QCOMPARE(warningCount({QStringLiteral("kin-galeri"), QStringLiteral("--thumbnail-size"),
                           QStringLiteral("9999")}),
             1);
    QCOMPARE(warningCount({QStringLiteral("kin-galeri"), QStringLiteral("--thumb-memory-cache"),
                           QStringLiteral("4")}),
             1);
    QCOMPARE(warningCount({QStringLiteral("kin-galeri"), QStringLiteral("--seed-benchmark"),
                           QStringLiteral("abc")}),
             1);
    QCOMPARE(warningCount({QStringLiteral("kin-galeri"), QStringLiteral("--limit"),
                           QStringLiteral("-1")}),
             1);
    QCOMPARE(warningCount({QStringLiteral("kin-galeri"), QStringLiteral("--bench-scroll"),
                           QStringLiteral("0.1")}),
             1);
}

void TestCliOptions::warnsOnUnknownOption()
{
    const CliOptions opts =
        parseCli({QStringLiteral("kin-galeri"), QStringLiteral("--definitely-not-a-flag")});
    QVERIFY(!opts.warnings.isEmpty());
}

void TestCliOptions::resetAndRebuildFlags()
{
    const CliOptions opts = parseCli({QStringLiteral("kin-galeri"), QStringLiteral("--reset-db"),
                                      QStringLiteral("--rebuild-search-index"),
                                      QStringLiteral("--software-render")});
    QVERIFY(opts.resetDatabase);
    QVERIFY(opts.rebuildSearchIndex);
    QVERIFY(opts.softwareRender);
}

void TestCliOptions::homeScanIsOnByDefaultAndOptOut()
{
    // A fresh library indexes the home folder unless explicitly told not to.
    QVERIFY(parseCli({QStringLiteral("kin-galeri")}).autoHomeScan);
    QVERIFY(!parseCli({QStringLiteral("kin-galeri"), QStringLiteral("--no-home-scan")})
                 .autoHomeScan);
}

void TestCliOptions::openPathsComeFromPositionalArguments()
{
    // A file manager invokes `kin-galeri %F`, which arrives as plain positional
    // arguments. They must be collected without being mistaken for options.
    const CliOptions opts = parseCli({QStringLiteral("kin-galeri"),
                                      QStringLiteral("/tmp/photo one.jpg"),
                                      QStringLiteral("/tmp/clip.mp4")});

    QCOMPARE(opts.openPaths,
             QStringList({QStringLiteral("/tmp/photo one.jpg"), QStringLiteral("/tmp/clip.mp4")}));
    QVERIFY(opts.warnings.isEmpty());

    // Options and files can be mixed in any order.
    const CliOptions mixed = parseCli({QStringLiteral("kin-galeri"), QStringLiteral("/tmp/a.png"),
                                       QStringLiteral("--no-home-scan")});
    QCOMPARE(mixed.openPaths, QStringList({QStringLiteral("/tmp/a.png")}));
    QVERIFY(!mixed.autoHomeScan);

    QVERIFY(parseCli({QStringLiteral("kin-galeri")}).openPaths.isEmpty());
}

QTEST_MAIN(TestCliOptions)
#include "tst_clioptions.moc"
