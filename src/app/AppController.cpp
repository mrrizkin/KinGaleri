#include "app/AppController.h"

#include "app/PathUtils.h"
#include "app/Paths.h"
#include "core/benchmark/BenchmarkSeeder.h"
#include "core/db/Database.h"
#include "core/db/LibraryMaintenance.h"
#include "core/thumbs/ThumbnailService.h"
#include "models/MediaGridModel.h"
#include "ui/quick/ThumbnailProvider.h"
#include "ui/quick/FullImageProvider.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kg {

namespace {

// PRD 4.1.1: the decoded-thumbnail LRU is sized from the visible grid rather
// than pinned to a flat 128 MB. `cells = columns * visibleRows * 2.5` gives the
// LRU a screen-and-a-bit of headroom so a fast scroll does not evict everything
// it just decoded, and the result is clamped to keep both extremes sane.
constexpr int kAdaptiveThumbFloorMb = 32;
constexpr int kAdaptiveThumbCeilingMb = 256;

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_model(new MediaGridModel(this))
    , m_perf(new PerfMonitor(this))
    , m_settings(new AppSettings(this))
{
}

AppController::~AppController()
{
    m_cancel.store(true);
    if (m_database) {
        m_database->close();
    }
}

bool AppController::initialise(const QString &databasePath, int thumbnailEdge, QString *error,
                               QStringList *warnings)
{
    m_database = std::make_unique<Database>(databasePath);

    if (!m_database->open(error)) {
        return false;
    }
    if (!m_database->migrate(error, warnings)) {
        return false;
    }

    // One-off data passes that schema DDL cannot express, because they call
    // SourceAlbum::detect() or need to walk existing rows. Both are no-ops after
    // the first successful run and are guarded by values in `meta`.
    //
    // Order matters: the album backfill UPDATE is what fires the v4 FTS triggers,
    // so running it first means the reconcile step finds nothing left to do on an
    // upgraded library.
    QString maintenanceError;
    if (!LibraryMaintenance::ensureSourceAlbum(*m_database, &maintenanceError)) {
        if (error) {
            *error = maintenanceError;
        }
        return false;
    }
    if (!LibraryMaintenance::ensureFtsPopulated(*m_database, &maintenanceError)) {
        if (error) {
            *error = maintenanceError;
        }
        return false;
    }

    m_thumbnails = std::make_unique<ThumbnailService>(thumbnailEdge);
    m_provider = new ThumbnailProvider(m_thumbnails.get());

    connect(m_thumbnails.get(), &ThumbnailService::thumbnailReady, m_model,
            &MediaGridModel::onThumbnailReady);

    m_model->attachDatabase(m_database.get());

    if (m_pendingGridLimit > 0) {
        // Must land before the first real reload so the model never materialises
        // more rows than the caller asked for.
        m_model->setLimit(m_pendingGridLimit);
    }
    connect(m_model, &MediaGridModel::reloadFinished, this, &AppController::registerThumbnailSources);
    connect(m_model, &MediaGridModel::countChanged, this, &AppController::summaryChanged);
    connect(m_model, &MediaGridModel::summaryChanged, this, &AppController::summaryChanged);

    // Filesystem watcher (PRD F-1.4). It is created here but only gets roots after
    // the first reload, when the model has read the `roots` table.
    m_watcher = std::make_unique<LibraryWatcher>();
    connect(m_watcher.get(), &LibraryWatcher::changed, this, &AppController::onLibraryChanged);
    connect(m_model, &MediaGridModel::reloadFinished, this, [this]() {
        // setRoots() is a no-op when the list is unchanged and watches are live;
        // refresh() (inside it) picks up directories created since the last pass.
        applyWatcherSettings();
        Q_EMIT rootsChanged();
    });
    connect(m_settings, &AppSettings::autoWatchChanged, this, &AppController::applyWatcherSettings);
    // Changing any pruning knob must re-evaluate what the watcher watches.
    connect(m_settings, &AppSettings::includeHiddenChanged, this, &AppController::applyWatcherSettings);
    connect(m_settings, &AppSettings::ignoreJunkChanged, this, &AppController::applyWatcherSettings);
    connect(m_settings, &AppSettings::junkPatternsChanged, this, &AppController::applyWatcherSettings);
    connect(m_settings, &AppSettings::followSymlinksChanged, this, &AppController::applyWatcherSettings);
    connect(m_settings, &AppSettings::ignoredFoldersChanged, this, &AppController::applyWatcherSettings);
    connect(m_settings, &AppSettings::ignoredPatternsChanged, this, &AppController::applyWatcherSettings);

    m_database->setMetaValue(QStringLiteral("last_opened"),
                             QString::number(QDateTime::currentMSecsSinceEpoch()));

    if (m_autoHomeScanEnabled) {
        m_autoHomePending = shouldSeedDefaultHomeRoot();
    }

    return true;
}

bool AppController::shouldSeedDefaultHomeRoot() const
{
    if (!m_database) {
        return false;
    }

    // A library that has ever been seeded with the home folder stays seeded: if
    // the user later removes that root we must not silently add it back.
    if (m_database->metaValue(QStringLiteral("default_home_root_added"))
        == QLatin1String("1")) {
        return false;
    }

    // Any real root means the user already chose their library; respect it.
    QSqlQuery query(m_database->connection());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM roots WHERE is_synthetic = 0"))) {
        return false;
    }
    if (!query.next()) {
        return false;
    }
    return query.value(0).toInt() == 0;
}

bool AppController::rebuildSearchIndex(QString *error)
{
    if (!m_database) {
        if (error) {
            *error = QStringLiteral("The database is not open");
        }
        return false;
    }
    return LibraryMaintenance::rebuildFts(*m_database, error);
}

int AppController::thumbnailEdge() const
{
    return m_thumbnails ? m_thumbnails->edge() : 256;
}

QString AppController::videoBackend() const
{
#ifdef KG_VIDEO_BACKEND
    return QString::fromLatin1(KG_VIDEO_BACKEND);
#else
    return QStringLiteral("none");
#endif
}

QString AppController::summary() const
{
    return m_model ? m_model->summary() : QString();
}

void AppController::setBusy(bool busy)
{
    if (busy == m_busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void AppController::setIndexing(bool indexing)
{
    if (indexing == m_indexing) {
        return;
    }
    m_indexing = indexing;
    Q_EMIT indexingChanged();
}

void AppController::setStatus(const QString &text)
{
    if (text == m_statusText) {
        return;
    }
    m_statusText = text;
    Q_EMIT statusTextChanged();
}

void AppController::setProgress(int current, int total)
{
    if (current == m_progressCurrent && total == m_progressTotal) {
        return;
    }
    m_progressCurrent = current;
    m_progressTotal = total;
    Q_EMIT progressChanged();
}

void AppController::setStartupRequests(int seedRows, const QStringList &indexFolders)
{
    m_startupSeedRows = seedRows;
    m_startupIndexFolders = indexFolders;
}

bool AppController::watching() const
{
    return m_watcher && m_watcher->watchedDirectoryCount() > 0;
}

int AppController::watchedDirectories() const
{
    return m_watcher ? m_watcher->watchedDirectoryCount() : 0;
}

void AppController::applyWatcherSettings()
{
    if (!m_watcher || !m_model || !m_settings) {
        return;
    }
    if (!m_settings->autoWatch()) {
        m_watcher->stop();
        Q_EMIT watchingChanged();
        return;
    }
    m_watcher->setScanOptions(currentScanOptions());
    m_watcher->setRoots(m_model->rootPaths());
    Q_EMIT watchingChanged();
}

FolderIndexer::ScanOptions AppController::currentScanOptions() const
{
    FolderIndexer::ScanOptions options;
    if (m_settings) {
        options.followSymlinks = m_settings->followSymlinks();
        options.includeHidden = m_settings->includeHidden();
        options.ignoreJunk = m_settings->ignoreJunk();
        options.junkPatterns = m_settings->junkPatterns();
        const QStringList ignoredFolders = m_settings->ignoredFolders();
        for (const QString &path : ignoredFolders) {
            options.ignoredPaths.insert(path);
        }
        options.ignoredPatterns = m_settings->ignoredPatterns();
    }
    return options;
}

QVariantList AppController::libraryRoots() const
{
    QVariantList list;
    if (!m_database) {
        return list;
    }
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral(
        "SELECT r.id, r.path, (SELECT COUNT(*) FROM media m WHERE m.root_id = r.id "
        "AND m.deleted_at IS NULL) FROM roots r WHERE r.is_synthetic = 0 ORDER BY r.path"));
    if (!query.exec()) {
        qWarning("kin-galeri: could not list library roots: %s",
                 qUtf8Printable(query.lastError().text()));
        return list;
    }
    while (query.next()) {
        list.append(QVariantMap{{"id", query.value(0)}, {"path", query.value(1)},
                                {"count", query.value(2)}});
    }
    return list;
}

bool AppController::removeRoot(const QString &path)
{
    if (m_busy || !m_database) {
        return false;
    }
    const QString normalised = QDir::cleanPath(path);

    QSqlQuery select(m_database->connection());
    select.prepare(QStringLiteral("SELECT id FROM roots WHERE path = ? AND is_synthetic = 0"));
    select.addBindValue(normalised);
    if (!select.exec() || !select.next()) {
        return false;
    }
    const qint64 rootId = select.value(0).toLongLong();

    QString error;
    if (!m_database->begin(&error)) {
        Q_EMIT errorOccurred(QStringLiteral("Could not remove folder: %1").arg(error));
        return false;
    }
    QSqlQuery removeMedia(m_database->connection());
    removeMedia.prepare(QStringLiteral("DELETE FROM media WHERE root_id = ?"));
    removeMedia.addBindValue(rootId);
    QSqlQuery removeRootQuery(m_database->connection());
    removeRootQuery.prepare(QStringLiteral("DELETE FROM roots WHERE id = ?"));
    removeRootQuery.addBindValue(rootId);
    if (!removeMedia.exec() || !removeRootQuery.exec() || !m_database->commit(&error)) {
        m_database->rollback();
        Q_EMIT errorOccurred(QStringLiteral("Could not remove folder: %1").arg(error));
        return false;
    }

    Q_EMIT infoMessage(QStringLiteral("Removed '%1' from the library").arg(normalised));
    Q_EMIT rootsChanged();
    reload();
    return true;
}

void AppController::rescanLibrary()
{
    if (!m_model) {
        return;
    }
    const QStringList roots = m_model->rootPaths();
    if (roots.isEmpty()) {
        Q_EMIT infoMessage(QStringLiteral("No folders to rescan"));
        return;
    }
    indexFolders(roots);
}

void AppController::onLibraryChanged()
{
    // A rescan is already running; its own reloadFinished will refresh the watch
    // set, so there is nothing useful to do with this event.
    if (m_busy || m_indexing || !m_model) {
        return;
    }

    const QStringList roots = m_model->rootPaths();
    if (roots.isEmpty()) {
        return;
    }
    setStatus(QStringLiteral("Library changed — rescanning…"));
    indexFolders(roots);
}

void AppController::finishStartupChain()
{
    if (!m_startupChainActive) {
        return;
    }
    m_startupChainActive = false;
    Q_EMIT startupCompleted();
}

void AppController::runStartupRequests()
{
    if (m_startupSeedRows > 0) {
        const int rows = m_startupSeedRows;
        m_startupSeedRows = 0;
        m_startupChainActive = true;
        seedBenchmark(rows);
        return;
    }
    if (!m_startupIndexFolders.isEmpty()) {
        const QStringList folders = m_startupIndexFolders;
        m_startupIndexFolders.clear();
        m_startupChainActive = true;
        indexFolders(folders);
        return;
    }
    // Fresh library with no user roots: index the home folder by default so the
    // first launch shows photos immediately instead of an empty gallery. The
    // marker is written before scanning so a crash mid-scan does not re-trigger
    // it, and so the auto-added root is treated like any user-added root.
    if (m_autoHomePending) {
        m_autoHomePending = false;
        const QString home = QDir::homePath();
        if (!home.isEmpty() && home != QLatin1String("/") && QFileInfo(home).isDir()) {
            m_database->setMetaValue(QStringLiteral("default_home_root_added"),
                                     QStringLiteral("1"));
            setStatus(QStringLiteral("First run — indexing %1…").arg(home));
            m_startupChainActive = true;
            indexFolders({home});
            return;
        }
    }
    // Rescan every root on launch when the user asked for it. Runs after the
    // first-run home seeding and after any explicit --index request, both of
    // which already perform a scan.
    if (m_settings && m_settings->rescanOnStartup() && m_model) {
        const QStringList roots = m_model->rootPaths();
        if (!roots.isEmpty()) {
            setStatus(QStringLiteral("Rescanning library…"));
            m_startupChainActive = true;
            indexFolders(roots);
            return;
        }
    }
    // The flag has to be raised before reload(), because reload() runs
    // synchronously and calls finishStartupChain() before this function returns.
    // Without it the guard in finishStartupChain() sees an inactive chain and
    // returns early, so startupCompleted() is never emitted - which silently
    // hangs --no-ui on a library that needs no indexing.
    m_startupChainActive = true;
    reload();
    finishStartupChain();
}

void AppController::reload()
{
    m_thumbnails->clearSources();
    m_model->reload();
    setStatus(m_model->currentError().isEmpty() ? QStringLiteral("Ready") : m_model->currentError());
}

void AppController::registerThumbnailSources()
{
    if (!m_thumbnails) {
        return;
    }
    m_thumbnails->clearSources();

    const QVector<qint64> ids = m_model->mediaIds();
    for (int i = 0; i < ids.size(); ++i) {
        const MediaGridModel::Entry *entry = m_model->entryAt(i);
        if (!entry) {
            continue;
        }
        m_thumbnails->registerSource(ids.at(i), entry->item.path, entry->item.isSynthetic,
                                     entry->item.kind == MediaKind::Video);
    }

    // Album covers are not necessarily part of the current grid filter (the album
    // page can be open while the model is filtered to something else), so resolve
    // their source files explicitly or their thumbnails would never generate.
    const QVariantList cards = m_model->albumCards();
    for (const QVariant &variant : cards) {
        const qint64 coverId = variant.toMap().value(QStringLiteral("coverId")).toLongLong();
        if (coverId <= 0) {
            continue;
        }
        const QVariantMap detail = m_model->mediaDetail(coverId);
        const QString path = detail.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        m_thumbnails->registerSource(coverId, path,
                                     detail.value(QStringLiteral("isSynthetic")).toBool(),
                                     detail.value(QStringLiteral("kind")).toString()
                                         == QLatin1String("video"));
    }
}

// ---------------------------------------------------------------------------
// Benchmark seeding
// ---------------------------------------------------------------------------
void AppController::seedBenchmark(int rows)
{
    if (m_busy || !m_database) {
        return;
    }

    setBusy(true);
    setIndexing(true);
    setStatus(QStringLiteral("Generating %1 synthetic rows…").arg(rows));
    setProgress(0, rows);

    auto *watch = new QFutureWatcher<BenchmarkSeeder::Result>(this);
    connect(watch, &QFutureWatcher<BenchmarkSeeder::Result>::finished, this, [this, watch]() {
        const BenchmarkSeeder::Result result = watch->result();
        watch->deleteLater();

        setBusy(false);
        setIndexing(false);
        setProgress(0, 0);

        if (!result.ok()) {
            setStatus(QStringLiteral("Benchmark seeding failed"));
            Q_EMIT errorOccurred(result.error);
            return;
        }

        setStatus(QStringLiteral("Seeded %1 rows in %2 ms (%3 rows/s)")
                      .arg(result.inserted)
                      .arg(result.elapsedMs)
                      .arg(result.rowsPerSecond(), 0, 'f', 0));
        Q_EMIT infoMessage(m_statusText);
        reload();
        finishStartupChain();
    });

    Database *database = m_database.get();
    watch->setFuture(QtConcurrent::run([this, database, rows]() {
        BenchmarkSeeder seeder(database);
        QObject::connect(&seeder, &BenchmarkSeeder::progress, this,
                         [this](int inserted, int total) {
                             QMetaObject::invokeMethod(
                                 this,
                                 [this, inserted, total]() { setProgress(inserted, total); },
                                 Qt::QueuedConnection);
                         },
                         Qt::DirectConnection);
        const BenchmarkSeeder::Result result = seeder.seed(rows);
        // QSqlDatabase connections are thread-affine; tear ours down here rather
        // than leaving it for the main thread (see Database::closeThreadConnection).
        database->closeThreadConnection();
        return result;
    }));
}

void AppController::clearBenchmark()
{
    if (m_busy || !m_database) {
        return;
    }
    QString error;
    BenchmarkSeeder seeder(m_database.get());
    if (!seeder.clear(&error)) {
        Q_EMIT errorOccurred(QStringLiteral("Could not clear the benchmark library: %1").arg(error));
        return;
    }
    Q_EMIT infoMessage(QStringLiteral("Benchmark library cleared"));
    reload();
}

// ---------------------------------------------------------------------------
// Indexing
// ---------------------------------------------------------------------------
void AppController::indexFolder(const QString &folder)
{
    indexFolders({localPath(folder)});
}

void AppController::indexFolders(const QStringList &folders)
{
    if (m_busy || folders.isEmpty() || !m_database) {
        return;
    }

    setBusy(true);
    setIndexing(true);
    setStatus(QStringLiteral("Scanning %1 folder(s)…").arg(folders.size()));
    setProgress(0, 0);
    m_cancel.store(false);

    auto *watch = new QFutureWatcher<FolderIndexer::Result>(this);
    connect(watch, &QFutureWatcher<FolderIndexer::Result>::finished, this, [this, watch]() {
        const FolderIndexer::Result result = watch->result();
        watch->deleteLater();

        setBusy(false);
        setIndexing(false);
        setProgress(0, 0);

        if (!result.ok()) {
            setStatus(QStringLiteral("Indexing stopped"));
            Q_EMIT errorOccurred(result.error);
        } else {
            setStatus(formatIndexerResult(result));
            Q_EMIT infoMessage(m_statusText);
        }
        reload();
        finishStartupChain();
    });

    Database *database = m_database.get();
    const FolderIndexer::ScanOptions scanOptions = currentScanOptions();
    watch->setFuture(QtConcurrent::run([this, database, folders, scanOptions]() -> FolderIndexer::Result {
        FolderIndexer indexer(database);
        indexer.setScanOptions(scanOptions);

        // Progress is emitted from this worker thread; the direct connection
        // below hops to the GUI thread through a queued invocation.
        QObject::connect(
            &indexer, &FolderIndexer::progress, this,
            [this](int scanned, int upserted, const QString &current) {
                QMetaObject::invokeMethod(
                    this,
                    [this, scanned, upserted, current]() {
                        setProgress(scanned, 0);
                        setStatus(QStringLiteral("Scanning… %1 seen, %2 indexed\n%3")
                                      .arg(scanned)
                                      .arg(upserted)
                                      .arg(current));
                    },
                    Qt::QueuedConnection);
            },
            Qt::DirectConnection);

        FolderIndexer::Result last;
        for (const QString &folder : folders) {
            last = indexer.run(folder, &m_cancel);
            if (!last.ok()) {
                break;
            }
        }
        // QSqlDatabase connections are thread-affine; tear ours down here.
        database->closeThreadConnection();
        return last;
    }));
}

void AppController::cancelCurrentJob()
{
    m_cancel.store(true);
    setStatus(QStringLiteral("Cancelling…"));
}

double AppController::readMemoryStatMb(const char *key) const
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly)) {
        return 0.0;
    }

    // readAll + split rather than readLine: QFile::atEnd() on /proc files is not
    // reliable across kernels, while a single read always is.
    const QList<QByteArray> lines = status.readAll().split('\n');
    const QByteArray wanted(key);

    for (const QByteArray &line : lines) {
        if (!line.startsWith(wanted)) {
            continue;
        }
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() >= 2) {
            bool ok = false;
            const double kb = parts.at(1).toDouble(&ok);
            if (ok) {
                return kb / 1024.0;
            }
        }
    }
    return 0.0;
}

double AppController::residentMemoryMb() const
{
    return readMemoryStatMb("VmRSS:");
}

double AppController::peakMemoryMb() const
{
    return readMemoryStatMb("VmHWM:");
}

void AppController::setThumbnailMemoryCacheMb(int mb)
{
    m_thumbnailMemoryOverrideMb = mb;
    if (!m_thumbnails) {
        return;
    }

    if (mb >= 0) {
        m_thumbnails->setMemoryCacheLimitMb(mb);
    } else {
        // Auto: start at the floor until QML reports the real grid geometry, then
        // updateThumbnailBudget() refines it. Without this the service would sit
        // on its 128 MB construction default until the first layout pass.
        m_thumbnails->setMemoryCacheLimitMb(kAdaptiveThumbFloorMb);
    }

    const int effective = m_thumbnails->memoryCacheLimitMb();
    if (effective != m_thumbnailMemoryBudgetMb) {
        m_thumbnailMemoryBudgetMb = effective;
        Q_EMIT thumbnailBudgetChanged();
    }
}

void AppController::updateThumbnailBudget(int columns, int visibleRows)
{
    if (m_thumbnailMemoryOverrideMb >= 0 || !m_thumbnails) {
        return;
    }

    const int edge = m_thumbnails->edge();
    const double cells = std::max(1, columns) * std::max(1, visibleRows) * 2.5;
    const double bytes = cells * static_cast<double>(edge) * static_cast<double>(edge) * 4.0;
    const int mb = std::clamp(static_cast<int>(std::ceil(bytes / (1024.0 * 1024.0))),
                              kAdaptiveThumbFloorMb, kAdaptiveThumbCeilingMb);

    if (mb == m_thumbnails->memoryCacheLimitMb()) {
        return;
    }
    m_thumbnails->setMemoryCacheLimitMb(mb);

    const int effective = m_thumbnails->memoryCacheLimitMb();
    if (effective != m_thumbnailMemoryBudgetMb) {
        m_thumbnailMemoryBudgetMb = effective;
        Q_EMIT thumbnailBudgetChanged();
    }
}

void AppController::setGridLimit(int limit)
{
    m_pendingGridLimit = std::max(0, limit);
}

void AppController::reportScrollBenchmarkStart()
{
    std::fprintf(stderr, "kin-galeri[bench] resting: rss=%.0fMB thumbs=%d\n", residentMemoryMb(),
                 m_thumbnails ? m_thumbnails->memoryCacheCount() : 0);
    std::fflush(stderr);
}

void AppController::finishScrollBenchmark(double seconds, double distancePx, int itemsTraversed)
{
    if (!m_perf) {
        return;
    }

    const PerfMonitor::Stats snapshot = m_perf->snapshot();
    const double budgetMs = 1000.0 / snapshot.targetFps;
    const double droppedPercent = snapshot.frames > 0
                                      ? 100.0 * static_cast<double>(snapshot.droppedFrames) / snapshot.frames
                                      : 0.0;

    // Machine readable on purpose: this line is the M0 acceptance evidence.
    std::fprintf(stderr,
                 "kin-galeri[bench] scroll: seconds=%.1f distance=%.0fpx items=%d "
                 "frames=%lld dropped=%lld (%.2f%%) fps=%.2f avg=%.2fms worst=%.1fms "
                 "budget=%.2fms target=%dfps rss=%.0fMB rssPeak=%.0fMB libitems=%d "
                 "thumbs=%d thumbcache=%.0fMB\n",
                 seconds, distancePx, itemsTraversed,
                 static_cast<long long>(snapshot.frames),
                 static_cast<long long>(snapshot.droppedFrames), droppedPercent,
                 snapshot.fps, snapshot.frameMs, snapshot.worstFrameMs, budgetMs,
                 snapshot.targetFps, residentMemoryMb(), peakMemoryMb(),
                 m_model ? m_model->itemCount() : 0,
                 m_thumbnails ? m_thumbnails->diskCacheFileCount() : 0,
                 m_thumbnails ? m_thumbnails->diskCacheBytes() / (1024.0 * 1024.0) : 0.0);
    std::fflush(stderr);

    setStatus(QStringLiteral("Scroll benchmark: %1 fps, avg %2 ms, worst %3 ms, %4/%5 frames over budget")
                  .arg(snapshot.fps, 0, 'f', 1)
                  .arg(snapshot.frameMs, 0, 'f', 2)
                  .arg(snapshot.worstFrameMs, 0, 'f', 1)
                  .arg(static_cast<long long>(snapshot.droppedFrames))
                  .arg(static_cast<long long>(snapshot.frames)));

    Q_EMIT scrollBenchmarkFinished();
}

QString AppController::formatIndexerResult(const FolderIndexer::Result &result) const
{
    return QStringLiteral("Indexed %1 files (%2 changed, %3 unchanged, %4 failed) in %5 s")
        .arg(result.scanned)
        .arg(result.upserted)
        .arg(result.unchanged)
        .arg(result.failed)
        .arg(result.elapsedMs / 1000.0, 0, 'f', 1);
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------
void AppController::clearThumbnailCache()
{
    if (!m_thumbnails) {
        return;
    }
    QString error;
    if (!m_thumbnails->clearDiskCache(&error)) {
        Q_EMIT errorOccurred(QStringLiteral("Could not clear the thumbnail cache: %1").arg(error));
        return;
    }
    m_thumbnails->resetStats();
    Q_EMIT infoMessage(QStringLiteral("Thumbnail cache cleared"));
    reload();
}

QString AppController::thumbnailInfo() const
{
    if (!m_thumbnails) {
        return {};
    }
    const ThumbnailService::Stats stats = m_thumbnails->stats();
    const QString avg = stats.generated > 0
                            ? QString::number(static_cast<double>(stats.generationMs) / stats.generated, 'f', 1)
                            : QStringLiteral("0.0");

    return QStringLiteral("%1 files · %2 · %3 · mem %4 items · hits mem %5 / disk %6 · miss %7 · gen %8 (avg %9 ms) · pending %10 · failed %11")
        .arg(m_thumbnails->diskCacheFileCount())
        .arg(Paths::sizeForLog(m_thumbnails->diskCacheBytes()))
        .arg(m_thumbnails->format())
        .arg(m_thumbnails->memoryCacheCount())
        .arg(stats.memoryHits)
        .arg(stats.diskHits)
        .arg(stats.misses)
        .arg(stats.generated)
        .arg(avg)
        .arg(m_thumbnails->pendingCount())
        .arg(m_thumbnails->failedCount());
}

QString AppController::storageInfo() const
{
    if (!m_database) {
        return {};
    }
    return QStringLiteral("%1 · %2").arg(Paths::sizeForLog(m_database->fileSizeBytes()),
                                         m_database->ftsAvailable() ? QStringLiteral("FTS5 on")
                                                                    : QStringLiteral("FTS5 off"));
}

QString AppController::databasePath() const
{
    return m_database ? m_database->filePath() : QString();
}

QString AppController::cachePath() const
{
    return Paths::thumbnailCacheDir();
}

// ---------------------------------------------------------------------------
// Desktop integration
// ---------------------------------------------------------------------------
void AppController::copyToClipboard(const QString &text) const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QString AppController::fullImageUrl(const QString &path, int maxEdge) const
{
    return FullImageProvider::urlForPath(path, maxEdge);
}

void AppController::revealInFileManager(const QString &path) const
{
    const QFileInfo info(path);
    const QString dir = info.exists() && info.isFile() ? info.absolutePath() : path;

    // Try the usual desktops in order; the first one that starts wins.
    const QStringList candidates = {
        QStringLiteral("xdg-open"),
        QStringLiteral("dolphin"),
    };

    for (const QString &program : candidates) {
        if (QProcess::startDetached(program, {dir})) {
            return;
        }
    }
}

} // namespace kg
