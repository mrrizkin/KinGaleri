#include "app/CliOptions.h"

#include <QTextStream>

namespace kg {

CliOptions parseCli(const QStringList &args)
{
    CliOptions opts;

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("kin-galeri - fast local photo & video library for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption optReset(
        QStringLiteral("reset-db"),
        QStringLiteral("Delete the existing library database before starting."));
    QCommandLineOption optIndex(
        QStringList{QStringLiteral("i"), QStringLiteral("index")},
        QStringLiteral("Index a folder (recursive). Repeatable."),
        QStringLiteral("folder"));
    QCommandLineOption optSeed(
        QStringLiteral("seed-benchmark"),
        QStringLiteral("Insert N synthetic media rows used for the scroll benchmark."),
        QStringLiteral("N"));
    QCommandLineOption optDb(
        QStringLiteral("db"),
        QStringLiteral("Override the library database file."),
        QStringLiteral("file"));
    QCommandLineOption optCache(
        QStringLiteral("cache"),
        QStringLiteral("Override the cache directory (thumbnails live here)."),
        QStringLiteral("dir"));
    QCommandLineOption optThumbSize(
        QStringLiteral("thumbnail-size"),
        QStringLiteral("Thumbnail edge length in pixels (default 256)."),
        QStringLiteral("px"));
    QCommandLineOption optThumbMem(
        QStringLiteral("thumb-memory-cache"),
        QStringLiteral("In-memory decoded thumbnail budget in MB (default: adaptive)."),
        QStringLiteral("MB"));
    QCommandLineOption optLimit(
        QStringLiteral("limit"),
        QStringLiteral("Only surface the newest N items in the grid (0 = no limit)."),
        QStringLiteral("N"));
    QCommandLineOption optPerf(
        QStringLiteral("perf-overlay"),
        QStringLiteral("Start with the frame-time overlay visible."));
    QCommandLineOption optBench(
        QStringLiteral("bench-scroll"),
        QStringLiteral("Run an automated scroll benchmark for N seconds, print frame "
                       "statistics and exit."),
        QStringLiteral("seconds"));
    QCommandLineOption optNoUi(
        QStringLiteral("no-ui"),
        QStringLiteral("Run headless (indexing/seeding only)."));
    QCommandLineOption optRebuildFts(
        QStringLiteral("rebuild-search-index"),
        QStringLiteral("Rebuild the full-text search index from the media table and exit."));
    QCommandLineOption optSoftware(
        QStringLiteral("software-render"),
        QStringLiteral("Force the Qt Quick software renderer (fallback for broken GPU drivers)."));
    QCommandLineOption optNoHomeScan(
        QStringLiteral("no-home-scan"),
        QStringLiteral("Do not seed a new library with the user's home directory."));

    parser.addOption(optReset);
    parser.addOption(optIndex);
    parser.addOption(optSeed);
    parser.addOption(optDb);
    parser.addOption(optCache);
    parser.addOption(optThumbSize);
    parser.addOption(optThumbMem);
    parser.addOption(optLimit);
    parser.addOption(optPerf);
    parser.addOption(optBench);
    parser.addOption(optNoUi);
    parser.addOption(optRebuildFts);
    parser.addOption(optSoftware);
    parser.addOption(optNoHomeScan);

    // parse() would abort the process on error; we want to report and continue.
    if (!parser.parse(args)) {
        opts.warnings.append(parser.errorText());
    }

    opts.helpRequested = parser.isSet(QStringLiteral("help"));
    opts.versionRequested = parser.isSet(QStringLiteral("version"));
    opts.resetDatabase = parser.isSet(optReset);
    opts.showPerfOverlay = parser.isSet(optPerf);
    opts.skipUi = parser.isSet(optNoUi);
    opts.rebuildSearchIndex = parser.isSet(optRebuildFts);
    opts.softwareRender = parser.isSet(optSoftware);
    opts.autoHomeScan = !parser.isSet(optNoHomeScan);
    opts.indexFolders = parser.values(optIndex);
    opts.databasePath = parser.value(optDb);
    opts.cachePath = parser.value(optCache);

    bool ok = false;
    const QString seedRaw = parser.value(optSeed);
    if (!seedRaw.isEmpty()) {
        const int count = seedRaw.toInt(&ok);
        if (ok && count > 0) {
            opts.seedBenchmarkCount = count;
        } else {
            opts.warnings.append(QStringLiteral("--seed-benchmark expects a positive integer, got '%1'")
                                     .arg(seedRaw));
        }
    }

    const QString thumbRaw = parser.value(optThumbSize);
    if (!thumbRaw.isEmpty()) {
        const int px = thumbRaw.toInt(&ok);
        if (ok && px >= 64 && px <= 4096) {
            opts.thumbnailSize = px;
        } else {
            opts.warnings.append(QStringLiteral("--thumbnail-size must be 64..4096, got '%1'")
                                     .arg(thumbRaw));
        }
    }

    const QString benchRaw = parser.value(optBench);
    if (!benchRaw.isEmpty()) {
        const double seconds = benchRaw.toDouble(&ok);
        if (ok && seconds > 0.5 && seconds <= 600.0) {
            opts.benchScrollSeconds = seconds;
            opts.showPerfOverlay = true;
        } else {
            opts.warnings.append(QStringLiteral("--bench-scroll expects 0.5..600 seconds, got '%1'")
                                     .arg(benchRaw));
        }
    }

    const QString thumbMemRaw = parser.value(optThumbMem);
    if (!thumbMemRaw.isEmpty()) {
        const int mb = thumbMemRaw.toInt(&ok);
        if (ok && mb >= 8 && mb <= 4096) {
            opts.thumbnailMemoryCacheMb = mb;
        } else {
            opts.warnings.append(
                QStringLiteral("--thumb-memory-cache must be 8..4096 MB, got '%1'").arg(thumbMemRaw));
        }
    }

    const QString limitRaw = parser.value(optLimit);
    if (!limitRaw.isEmpty()) {
        const int n = limitRaw.toInt(&ok);
        if (ok && n >= 0) {
            opts.gridLimit = n;
        } else {
            opts.warnings.append(QStringLiteral("--limit expects a non-negative integer, got '%1'")
                                     .arg(limitRaw));
        }
    }

    return opts;
}

void printHelp(const QString &programName)
{
    QTextStream out(stdout);
    out << "Usage: " << programName << " [options]\n\n";
    out << "Options:\n"
        << "  -h, --help                 Show this help.\n"
        << "  -v, --version              Show the version.\n"
        << "      --db <file>            Override the library database file.\n"
        << "      --cache <dir>          Override the cache directory.\n"
        << "      --reset-db             Delete the library database first.\n"
        << "  -i, --index <folder>       Index a folder (repeatable).\n"
        << "      --seed-benchmark <N>   Insert N synthetic rows for the scroll benchmark.\n"
        << "      --thumbnail-size <px>  Thumbnail edge length (default 256).\n"
        << "      --thumb-memory-cache <MB>  Decoded thumbnail budget in MB (default: adaptive).\n"
        << "      --limit <N>            Surface only the newest N items (0 = all).\n"
        << "      --perf-overlay         Show the frame-time overlay on start.\n"
        << "      --bench-scroll <s>     Automated scroll benchmark for <s> seconds, then exit.\n"
        << "      --no-ui                Headless mode.\n"
        << "      --rebuild-search-index Rebuild the full-text search index and exit.\n"
        << "      --software-render      Force the Qt Quick software renderer.\n"
        << "      --no-home-scan         Do not seed a fresh library with the home folder.\n\n";
    out << "Benchmark examples:\n"
        << "  # seed 50k synthetic rows, then measure sustained scrolling\n"
        << "  " << programName << " --db /tmp/kg-bench.db --cache /tmp/kg-cache \\\n"
        << "      --seed-benchmark 50000 --no-ui\n"
        << "  " << programName << " --db /tmp/kg-bench.db --cache /tmp/kg-cache \\\n"
        << "      --bench-scroll 12\n\n";
    out.flush();
}

} // namespace kg
