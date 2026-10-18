#include "app/AppSettings.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace kg;

/**
 * AppSettings is tiny but it is the whole of the M4 "theme + language" feature:
 * a wrong normalisation would silently write a value the UI cannot select, and a
 * missing persistence round-trip would forget the choice on every restart.
 *
 * XDG_CONFIG_HOME is redirected to a temp directory so the test never touches the
 * developer's real preferences.
 */
class TestAppSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void normalisesTheme();
    void normalisesLanguage();
    void normalisesThumbnailSize();
    void normalisesAccent();
    void normalisesStartupPage();
    void normalisesNumericRanges();
    void normalisesIgnoredLists();
    void normalisesSniffExtensions();
    void explicitThemeDrivesDarkFlag();
    void choicePersistsAcrossInstances();
    void garbageFallsBackToSystem();

private:
    QTemporaryDir m_configDir;
};

void TestAppSettings::initTestCase()
{
    QVERIFY(m_configDir.isValid());
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toLocal8Bit());
    QCoreApplication::setOrganizationName(QStringLiteral("kin-test"));
    QCoreApplication::setApplicationName(QStringLiteral("Kin Galeri Test"));
}

void TestAppSettings::normalisesTheme()
{
    QCOMPARE(AppSettings::normaliseTheme(QStringLiteral("dark")), QStringLiteral("dark"));
    QCOMPARE(AppSettings::normaliseTheme(QStringLiteral("LIGHT")), QStringLiteral("light"));
    QCOMPARE(AppSettings::normaliseTheme(QStringLiteral("  Dark  ")), QStringLiteral("dark"));
    QCOMPARE(AppSettings::normaliseTheme(QStringLiteral("system")), QStringLiteral("system"));
    QCOMPARE(AppSettings::normaliseTheme(QString()), QStringLiteral("system"));
}

void TestAppSettings::normalisesLanguage()
{
    QCOMPARE(AppSettings::normaliseLanguage(QStringLiteral("id")), QStringLiteral("id"));
    QCOMPARE(AppSettings::normaliseLanguage(QStringLiteral("EN")), QStringLiteral("en"));
    QCOMPARE(AppSettings::normaliseLanguage(QStringLiteral("fr")), QStringLiteral("system"));
    QCOMPARE(AppSettings::normaliseLanguage(QString()), QStringLiteral("system"));
}

void TestAppSettings::normalisesThumbnailSize()
{
    QCOMPARE(AppSettings::normaliseThumbnailSize(168), 168);
    QCOMPARE(AppSettings::normaliseThumbnailSize(80), 80);
    QCOMPARE(AppSettings::normaliseThumbnailSize(384), 384);
    QCOMPARE(AppSettings::normaliseThumbnailSize(10), 80);    // clamped up, then snapped
    QCOMPARE(AppSettings::normaliseThumbnailSize(9999), 384); // clamped down
    QCOMPARE(AppSettings::normaliseThumbnailSize(163), 160);  // snapped to the 8px grid
    QCOMPARE(AppSettings::normaliseThumbnailSize(0), 80);
}

void TestAppSettings::normalisesAccent()
{
    QCOMPARE(AppSettings::normaliseAccent(QStringLiteral("teal")), QStringLiteral("teal"));
    QCOMPARE(AppSettings::normaliseAccent(QStringLiteral("PURPLE")), QStringLiteral("purple"));
    QCOMPARE(AppSettings::normaliseAccent(QStringLiteral("neon")), QStringLiteral("blue"));
    QCOMPARE(AppSettings::normaliseAccent(QString()), QStringLiteral("blue"));
    QVERIFY(AppSettings::accentNames().contains(QStringLiteral("orange")));
}

void TestAppSettings::normalisesStartupPage()
{
    QCOMPARE(AppSettings::normaliseStartupPage(QStringLiteral("albums")), QStringLiteral("albums"));
    QCOMPARE(AppSettings::normaliseStartupPage(QStringLiteral("photos")), QStringLiteral("photos"));
    QCOMPARE(AppSettings::normaliseStartupPage(QStringLiteral("settings")), QStringLiteral("settings"));
    QCOMPARE(AppSettings::normaliseStartupPage(QStringLiteral("last")), QStringLiteral("last"));
    QCOMPARE(AppSettings::normaliseStartupPage(QStringLiteral("nonsense")), QStringLiteral("last"));
}

void TestAppSettings::normalisesNumericRanges()
{
    QCOMPARE(AppSettings::normaliseGridSpacing(-3), 0);
    QCOMPARE(AppSettings::normaliseGridSpacing(999), 24);
    QCOMPARE(AppSettings::normaliseSidebarWidth(10), 160);
    QCOMPARE(AppSettings::normaliseSidebarWidth(4000), 360);
    QCOMPARE(AppSettings::normaliseThumbnailMemoryMb(0), 0);
    QCOMPARE(AppSettings::normaliseThumbnailMemoryMb(-5), 0);
    QCOMPARE(AppSettings::normaliseThumbnailMemoryMb(8), 32);
    QCOMPARE(AppSettings::normaliseThumbnailMemoryMb(99999), 2048);
    QCOMPARE(AppSettings::normaliseVideoVolume(-1), 0);
    QCOMPARE(AppSettings::normaliseVideoVolume(500), 100);
}

void TestAppSettings::normalisesIgnoredLists()
{
    const QStringList folders = AppSettings::normaliseIgnoredFolders(
        {"/home/u/photos/private", "/home/u/photos/private", "  ", "/"});
    QCOMPARE(folders, QStringList{"/home/u/photos/private"});
    // A trailing slash is cleaned so add/remove comparisons stay consistent.
    QCOMPARE(AppSettings::normaliseIgnoredFolders({"/home/u/a/"}), QStringList{"/home/u/a"});

    const QStringList patterns = AppSettings::normaliseIgnoredPatterns(
        {"Screenshots", " Screenshots ", "", "backup*"});
    const QStringList expectedPatterns = {"Screenshots", "backup*"};
    QCOMPARE(patterns, expectedPatterns);

    // The built-in junk list is a normalised pattern list too, and is expected
    // to ship with the toolchain globs (not just their base names).
    AppSettings settings;
    const QStringList junk = settings.junkPatterns();
    QVERIFY(junk.contains(QStringLiteral("node_modules")));
    QVERIFY(junk.contains(QStringLiteral("cmake-build-*")));
    QVERIFY(junk.contains(QStringLiteral("android-sdk-*")));
    QVERIFY(!junk.contains(QString()));

    settings.addJunkPattern(QStringLiteral("MyJunk"));
    settings.addJunkPattern(QStringLiteral("MyJunk")); // deduped
    QCOMPARE(settings.junkPatterns().count(QStringLiteral("MyJunk")), 1);
    settings.addJunkPattern(QStringLiteral("   ")); // ignored
    settings.removeJunkPattern(QStringLiteral("MyJunk"));
    QVERIFY(!settings.junkPatterns().contains(QStringLiteral("MyJunk")));
    settings.resetJunkPatterns();
    QCOMPARE(settings.junkPatterns(), junk);
}

void TestAppSettings::normalisesSniffExtensions()
{
    // Lower-cases, strips leading dots, trims, drops empties and de-duplicates.
    const QStringList normalised = AppSettings::normaliseExtensions(
        {".TS", " JFIF ", ".jfif", "", "  ", "JIF"});
    QCOMPARE(normalised, QStringList({"ts", "jfif", "jif"}));

    AppSettings settings;
    const QStringList defaults = settings.sniffExtensions();
    QVERIFY(defaults.contains(QStringLiteral("ts")));
    QVERIFY(defaults.contains(QStringLiteral("jfif")));
    QVERIFY(!defaults.contains(QString()));

    settings.addSniffExtension(QStringLiteral(".webm2"));
    QCOMPARE(settings.sniffExtensions().count(QStringLiteral("webm2")), 1);
    settings.removeSniffExtension(QStringLiteral("TS"));
    QVERIFY(!settings.sniffExtensions().contains(QStringLiteral("ts")));
    settings.resetSniffExtensions();
    QCOMPARE(settings.sniffExtensions(), defaults);
}

void TestAppSettings::explicitThemeDrivesDarkFlag()
{
    AppSettings settings;
    settings.setTheme(QStringLiteral("dark"));
    QCOMPARE(settings.theme(), QStringLiteral("dark"));
    QVERIFY(settings.dark());

    settings.setTheme(QStringLiteral("light"));
    QCOMPARE(settings.theme(), QStringLiteral("light"));
    QVERIFY(!settings.dark());
}

void TestAppSettings::choicePersistsAcrossInstances()
{
    {
        AppSettings first;
        first.setTheme(QStringLiteral("dark"));
        first.setLanguage(QStringLiteral("id"));
        first.setThumbnailSize(200);
        first.setAccent(QStringLiteral("purple"));
        first.setStartupPage(QStringLiteral("albums"));
        first.setLastPage(QStringLiteral("settings"));
        first.setGridSpacing(11);
        first.setShowSidebar(false);
        first.setSidebarWidth(280);
        first.setShowDateHeaders(false);
        first.setAnimateTransitions(false);
        first.setHoverEffects(false);
        first.setShowPerfOverlay(true);
        first.setConfirmDestructive(false);
        first.setAutoWatch(false);
        first.setRescanOnStartup(true);
        first.setFollowSymlinks(true);
        first.setIncludeHidden(true);
        first.setIgnoreJunk(false);
        first.setThumbnailMemoryMb(256);
        first.setVideoAutoplay(false);
        first.setVideoLoop(true);
        first.setVideoMute(true);
        first.setVideoVolume(40);
        first.setDeveloperMode(true);
        first.setSystemIcons(false);
        first.addIgnoredFolder(QStringLiteral("/home/u/photos/private"));
        first.addIgnoredFolder(QStringLiteral("/home/u/photos/private"));
        first.setIgnoredPatternsFromText(QStringLiteral("Screenshots, backup*"));
    }

    QSettings().sync();

    AppSettings second;
    QCOMPARE(second.theme(), QStringLiteral("dark"));
    QCOMPARE(second.language(), QStringLiteral("id"));
    QCOMPARE(second.thumbnailSize(), 200);
    QCOMPARE(second.accent(), QStringLiteral("purple"));
    QCOMPARE(second.startupPage(), QStringLiteral("albums"));
    QCOMPARE(second.lastPage(), QStringLiteral("settings"));
    QCOMPARE(second.gridSpacing(), 11);
    QVERIFY(!second.showSidebar());
    QCOMPARE(second.sidebarWidth(), 280);
    QVERIFY(!second.showDateHeaders());
    QVERIFY(!second.animateTransitions());
    QVERIFY(!second.hoverEffects());
    QVERIFY(second.showPerfOverlay());
    QVERIFY(!second.confirmDestructive());
    QVERIFY(!second.autoWatch());
    QVERIFY(second.rescanOnStartup());
    QVERIFY(second.followSymlinks());
    QVERIFY(second.includeHidden());
    QVERIFY(!second.ignoreJunk());
    QCOMPARE(second.thumbnailMemoryMb(), 256);
    QVERIFY(!second.videoAutoplay());
    QVERIFY(second.videoLoop());
    QVERIFY(second.videoMute());
    QCOMPARE(second.videoVolume(), 40);
    QVERIFY(second.developerMode());
    QVERIFY(!second.systemIcons());
    QCOMPARE(second.ignoredFolders(), QStringList{"/home/u/photos/private"});
    const QStringList expectedPatterns = {"Screenshots", "backup*"};
    QCOMPARE(second.ignoredPatterns(), expectedPatterns);

    second.removeIgnoredFolder(QStringLiteral("/home/u/photos/private"));
    QVERIFY(second.ignoredFolders().isEmpty());
}

void TestAppSettings::garbageFallsBackToSystem()
{
    AppSettings settings;
    // The setter normalises before storing, so an out-of-range request cannot
    // leave the UI in a state with no matching menu entry.
    settings.setTheme(QStringLiteral("neon"));
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    settings.setLanguage(QStringLiteral("klingon"));
    QCOMPARE(settings.language(), QStringLiteral("system"));
    settings.setThumbnailSize(-40);
    QCOMPARE(settings.thumbnailSize(), 80);
    settings.setAccent(QStringLiteral("neon"));
    QCOMPARE(settings.accent(), QStringLiteral("blue"));
    settings.setStartupPage(QStringLiteral("nowhere"));
    QCOMPARE(settings.startupPage(), QStringLiteral("last"));
    settings.setGridSpacing(1000);
    QCOMPARE(settings.gridSpacing(), 24);
    settings.setSidebarWidth(0);
    QCOMPARE(settings.sidebarWidth(), 160);
    settings.setThumbnailMemoryMb(-99);
    QCOMPARE(settings.thumbnailMemoryMb(), 0);
    settings.setVideoVolume(1000);
    QCOMPARE(settings.videoVolume(), 100);
    // Rejects dangerous/empty paths rather than ignoring the whole filesystem.
    settings.addIgnoredFolder(QStringLiteral("/"));
    QVERIFY(settings.ignoredFolders().isEmpty());
}

QTEST_MAIN(TestAppSettings)
#include "tst_appsettings.moc"
