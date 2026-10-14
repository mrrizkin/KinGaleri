#pragma once

#include <QCommandLineParser>
#include <QString>
#include <QStringList>

namespace kg {

/**
 * Parsed command line. A deliberately thin wrapper around QCommandLineParser so
 * that tests and benchmarks can construct an option object without a QCoreApplication.
 */
struct CliOptions
{
    bool helpRequested = false;
    bool versionRequested = false;
    bool resetDatabase = false;
    bool showPerfOverlay = false;
    bool skipUi = false;

    /// --rebuild-search-index: rebuild media_fts from media and exit.
    bool rebuildSearchIndex = false;

    /// --software-render: force the Qt Quick software renderer (PRD risk table:
    /// broken Wayland/GPU drivers). Applied before QGuiApplication starts.
    bool softwareRender = false;

    /// Default home-directory library. On by default: a fresh library indexes
    /// the user's home folder unless --no-home-scan (or a benchmark/--index run)
    /// says otherwise.
    bool autoHomeScan = true;

    /// --seed-benchmark <n>: insert n synthetic rows and exit unless the UI runs.
    int seedBenchmarkCount = 0;

    /// --index <folder>: scan a real folder. Repeatable.
    QStringList indexFolders;

    /// --db <file>
    QString databasePath;

    /// --cache <dir>
    QString cachePath;

    /// --thumbnail-size <px>
    int thumbnailSize = 256;

    /// --thumb-memory-cache <MB>: in-memory decoded thumbnail budget.
    /// Negative means "adaptive" (sized from the visible grid, PRD 4.1.1).
    int thumbnailMemoryCacheMb = -1;

    /// --bench-scroll <seconds>: run an automated scroll benchmark and exit.
    /// 0 disables it.
    double benchScrollSeconds = 0.0;

    /// --limit <n>: only surface the newest n rows in the grid.
    int gridLimit = 0;

    /// Non-fatal parse problems, e.g. unknown flags or bad numbers.
    QStringList warnings;
};

/// Parse `args` **including** the application name at index 0, matching
/// QCoreApplication::arguments(). QCommandLineParser skips that first element
/// itself, so do not pre-trim it. Never exits the process.
CliOptions parseCli(const QStringList &args);

/// Human readable --help text body (QCommandLineParser adds the wrapper).
void printHelp(const QString &programName);

} // namespace kg
