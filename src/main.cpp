#include "app/AppController.h"
#include "app/AppSettings.h"
#include "app/CliOptions.h"
#include "app/Paths.h"
#include "models/MediaGridModel.h"
#include "ui/quick/ThumbnailProvider.h"
#include "ui/quick/FullImageProvider.h"
#include "ui/quick/ThemeIconProvider.h"

#ifdef KG_HAS_MPV
#include "ui/quick/KinMpvItem.h"
#include <QQmlEngine>
#endif

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

using namespace kg;

namespace {

/// Environment overrides let the benchmark harness run without CLI parsing
/// (useful when launching through a profiler or the QML test runner).
void applyEnvironmentOverrides()
{
    const QByteArray cacheDir = qgetenv("KG_CACHE_DIR");
    if (!cacheDir.isEmpty()) {
        Paths::setCacheOverride(QString::fromLocal8Bit(cacheDir));
    }
    const QByteArray dbFile = qgetenv("KG_DB_FILE");
    if (!dbFile.isEmpty()) {
        Paths::setDatabaseOverride(QString::fromLocal8Bit(dbFile));
    }
    // Video playback must use the FFmpeg backend (PRD 4.4). Several distros
    // still default to GStreamer, which behaves differently for phone video.
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND")) {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }
}

/**
 * Environment that must be in place *before* QGuiApplication is constructed.
 *
 * The scenegraph backend and the high-DPI rounding policy are both read during
 * QGuiApplication's constructor, so parsing them out of Qt's own option handling
 * (which runs later) would be too late.
 */
void applyPreAppEnvironment(int argc, char *argv[])
{
    // Fractional scaling (PRD F-3.10). The default rounding policy makes a 1.25x
    // or 1.5x desktop scale look chunky; PassThrough keeps the fractional factor.
    // Left alone if the user already set the variable (or runs under Wayland,
    // where the compositor drives it).
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR_ROUNDING_POLICY")) {
        qputenv("QT_SCALE_FACTOR_ROUNDING_POLICY", "PassThrough");
    }

    // --software-render: PRD risk mitigation for broken Wayland/GPU drivers.
    // Read straight from argv because the variable has to be set before
    // QGuiApplication is constructed, and that happens before the parsed options
    // are available.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--software-render") == 0) {
            qputenv("QT_QUICK_BACKEND", "software");
            break;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Started before anything else so the reported cold-start latency covers
    // process spawn, DB open, migrations, model load and the first frame.
    QElapsedTimer processClock;
    processClock.start();

    applyPreAppEnvironment(argc, argv);

    // --help and --version are parsed *before* QGuiApplication exists so they work
    // where no platform plugin ever could: a build container, an SSH session, a CI
    // step. Constructing the application first made `kin-galeri --version` abort
    // with "could not connect to display" - which is exactly how CI found this.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        rawArgs << QString::fromLocal8Bit(argv[i]);
    }
    const CliOptions options = parseCli(rawArgs);

    if (options.helpRequested) {
        printHelp(QStringLiteral(KG_APP_NAME));
        return 0;
    }
    if (options.versionRequested) {
        QTextStream out(stdout);
        // The optional features are part of the version line on purpose: they are
        // what a bug report needs, and they let a packaged artifact prove which
        // video backend and whether face grouping actually came with it.
        out << KG_APP_NAME << ' ' << KG_APP_VERSION << " (Qt " << QT_VERSION_STR
            << ", video: " << QStringLiteral(KG_VIDEO_BACKEND)
#ifdef KG_ENABLE_ML
            << ", ml: on"
#else
            << ", ml: off"
#endif
            << ")\n";
        out.flush();
        return 0;
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(KG_APP_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(KG_APP_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("kin"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.mrrizkin"));

    for (const QString &warning : options.warnings) {
        std::fprintf(stderr, "kin-galeri: %s\n", qUtf8Printable(warning));
    }

    applyEnvironmentOverrides();

    if (qEnvironmentVariableIsSet("KG_DEBUG")) {
        std::fprintf(stderr,
                     "kin-galeri[debug]: parsed db='%s' cache='%s' seed=%d index=%d limit=%d thumb=%d thumbMem=%d warnings=%d\n",
                     qUtf8Printable(options.databasePath), qUtf8Printable(options.cachePath),
                     options.seedBenchmarkCount, static_cast<int>(options.indexFolders.size()),
                     options.gridLimit, options.thumbnailSize, options.thumbnailMemoryCacheMb,
                     static_cast<int>(options.warnings.size()));
    }

    if (!options.databasePath.isEmpty()) {
        Paths::setDatabaseOverride(options.databasePath);
    }
    if (!options.cachePath.isEmpty()) {
        Paths::setCacheOverride(options.cachePath);
    }

    if (!Paths::databaseOverride().isEmpty()) {
        const QString parent = QFileInfo(Paths::databaseOverride()).absolutePath();
        QDir().mkpath(parent);
    }
    Paths::initialise(true);

    if (options.resetDatabase) {
        const QString db = Paths::databaseFile();
        for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            const QString path = db + suffix;
            if (QFile::exists(path) && !QFile::remove(path)) {
                std::fprintf(stderr, "kin-galeri: could not remove '%s'\n", qUtf8Printable(path));
                return 2;
            }
        }
        QDir(Paths::thumbnailCacheDir()).removeRecursively();
        QDir().mkpath(Paths::thumbnailCacheDir());
    }

    std::fprintf(stderr, "kin-galeri: database %s\n", qUtf8Printable(Paths::databaseFile()));
    std::fprintf(stderr, "kin-galeri: cache    %s\n", qUtf8Printable(Paths::thumbnailCacheDir()));

    AppController controller;
    controller.setInitialPerfOverlay(options.showPerfOverlay);
    // Must be set before initialise() so the model's first load honours it. An
    // explicit --limit always wins; otherwise the persisted setting decides, which
    // is how a 100k library keeps a fast first frame without a command line.
    controller.setGridLimit(options.gridLimitSet ? options.gridLimit
                                                 : controller.settings()->gridDisplayLimit());

    // A fresh library defaults to the home folder, but only for an interactive
    // launch: benchmarks, explicit --index runs and KG_NO_HOME_SCAN keep it off
    // so a measurement never turns into an accidental 100k-file scan.
    const bool autoHomeScan =
        options.autoHomeScan && options.seedBenchmarkCount == 0 && options.indexFolders.isEmpty()
        && options.benchScrollSeconds <= 0.0 && !qEnvironmentVariableIsSet("KG_NO_HOME_SCAN");
    controller.setAutoHomeScanEnabled(autoHomeScan);

    QString error;
    QStringList migrationWarnings;
    if (!controller.initialise(Paths::databaseFile(), options.thumbnailSize, &error, &migrationWarnings)) {
        std::fprintf(stderr, "kin-galeri: %s\n", qUtf8Printable(error));
        return 3;
    }
    for (const QString &warning : migrationWarnings) {
        std::fprintf(stderr, "kin-galeri: %s\n", qUtf8Printable(warning));
    }

    if (options.rebuildSearchIndex) {
        QString rebuildError;
        if (!controller.rebuildSearchIndex(&rebuildError)) {
            std::fprintf(stderr, "kin-galeri: search index rebuild failed: %s\n",
                         qUtf8Printable(rebuildError));
            return 5;
        }
        std::fprintf(stderr, "kin-galeri: search index rebuilt for %s\n",
                     qUtf8Printable(Paths::databaseFile()));
        return 0;
    }

    // The counterpart of --seed-benchmark. The developer-mode button in the empty
    // library view seeds into whatever library is open - the real one - and until
    // this existed there was no way to undo that from the interface at all.
    if (options.clearBenchmark) {
        QString clearError;
        if (!controller.clearBenchmarkLibrary(&clearError)) {
            std::fprintf(stderr, "kin-galeri: could not remove the benchmark rows: %s\n",
                         qUtf8Printable(clearError));
            return 6;
        }
        std::fprintf(stderr, "kin-galeri: synthetic benchmark rows removed from %s\n",
                     qUtf8Printable(Paths::databaseFile()));
        return 0;
    }

    controller.setStartupRequests(options.seedBenchmarkCount, options.indexFolders);
    controller.setScrollBenchmarkSeconds(options.benchScrollSeconds);
    // Precedence: explicit --thumb-memory-cache pins the budget, otherwise the
    // persisted setting wins (0 = adaptive, sized from the visible grid).
    const int persistedThumbMem = controller.settings()->thumbnailMemoryMb();
    const int effectiveThumbMem = options.thumbnailMemoryCacheMb >= 0
                                      ? options.thumbnailMemoryCacheMb
                                      : (persistedThumbMem > 0 ? persistedThumbMem : -1);
    controller.setThumbnailMemoryCacheMb(effectiveThumbMem);

    if (options.skipUi) {
        // Headless: run the queued work and exit as soon as it completes.
        QObject::connect(&controller, &AppController::startupCompleted, &app,
                         &QCoreApplication::quit, Qt::QueuedConnection);
        QTimer::singleShot(0, &controller, &AppController::runStartupRequests);
        return app.exec();
    }

    // Qt Quick Controls style. Fusion is the most predictable across distros and
    // we do not depend on a platform style being installed.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // AppIcon resolves names with QIcon::fromTheme, which needs a theme name. A
    // bare window-manager session can leave it unset, so fall back to the first
    // well-known theme that is actually installed; otherwise every icon would
    // silently disappear and the text-glyph fallback would look like a bug.
    auto themeInstalled = [](const QString &name) {
        const QStringList searchPaths = QIcon::themeSearchPaths();
        for (const QString &base : searchPaths) {
            if (QDir(base + QLatin1Char('/') + name).exists()) {
                return true;
            }
        }
        return false;
    };
    QString iconTheme = QIcon::themeName();
    if (iconTheme.isEmpty() || !themeInstalled(iconTheme)) {
        iconTheme.clear();
        const QStringList candidates = { QStringLiteral("breeze"), QStringLiteral("Adwaita"),
                                         QStringLiteral("hicolor") };
        for (const QString &candidate : candidates) {
            if (themeInstalled(candidate)) {
                iconTheme = candidate;
                break;
            }
        }
    }
    if (!iconTheme.isEmpty()) {
        QIcon::setThemeName(iconTheme);
        QIcon::setFallbackThemeName(iconTheme);
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QString::fromLatin1(ThumbnailProvider::kProviderId),
                            controller.thumbnailProvider());
    // Owned by the engine once registered.
    engine.addImageProvider(QString::fromLatin1(FullImageProvider::kProviderId),
                            new FullImageProvider);
    engine.addImageProvider(QString::fromLatin1(ThemeIconProvider::kProviderId),
                            new ThemeIconProvider);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // Switching language at runtime replaces the QTranslator, but a QQuickWindow
    // does not reliably receive QEvent::LanguageChange, so every qsTr() binding
    // would keep its old text until restart. Nudge the engine explicitly.
    QObject::connect(controller.settings(), &AppSettings::languageChanged, &engine,
                     &QQmlEngine::retranslate);

    // Changing the thumbnail memory budget from the settings page re-pins the
    // LRU (or hands control back to the adaptive sizing when set to 0).
    QObject::connect(controller.settings(), &AppSettings::thumbnailMemoryMbChanged, &controller,
                     [&controller]() {
                         const int mb = controller.settings()->thumbnailMemoryMb();
                         controller.setThumbnailMemoryCacheMb(mb > 0 ? mb : -1);
                     });

#ifdef KG_HAS_MPV
    // mpvqt ships libMpvQt.so and headers but no qmldir, plugin or QML type, so
    // the type has to be registered by hand. A distinct URI is used instead of
    // "KinGaleri" because that module already has a generated qmldir and
    // registering into it at runtime produces a conflicting definition.
    qmlRegisterType<KinMpvItem>("KinGaleri.Mpv", 1, 0, "MpvItem");
#endif

    // `app` is handed to QML as a required property instead of a context
    // property, so the QML side stays statically analysable (qmllint friendly).
    engine.setInitialProperties({
        {QStringLiteral("app"), QVariant::fromValue(&controller)},
    });

    engine.loadFromModule("KinGaleri", "Main");
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "kin-galeri: failed to load the QML module 'KinGaleri'\n");
        return 4;
    }

    // Kick off seeding/indexing after the first frame so the window is visible.
    QTimer::singleShot(0, &controller, &AppController::runStartupRequests);

    if (controller.scrollBenchmarkEnabled()) {
        // The benchmark drives itself from QML and reports back when done.
        QObject::connect(&controller, &AppController::scrollBenchmarkFinished, &app,
                         &QCoreApplication::quit, Qt::QueuedConnection);
    }

    if (qEnvironmentVariableIsSet("KG_DEBUG") || options.benchScrollSeconds > 0.0) {
        // Cold start = process spawn to first presented frame (PRD 4.1).
        QObject::connect(controller.perf(), &PerfMonitor::firstFramePresented, &app,
                         [&processClock]() {
                             std::fprintf(stderr, "kin-galeri[bench] startup: ms=%lld\n",
                                          static_cast<long long>(processClock.elapsed()));
                             std::fflush(stderr);
                         });
    }

    return app.exec();
}
