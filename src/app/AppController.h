#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <atomic>
#include <memory>

#include "core/indexer/FolderIndexer.h"
#include "core/indexer/LibraryWatcher.h"
#include "app/AppSettings.h"
#include "app/PerfMonitor.h"
#include "app/ShortcutRegistry.h"
#include "models/MediaGridModel.h"

namespace kg {

class Database;
class MediaGridModel;
class ShortcutRegistry;
class ThumbnailService;
class ThumbnailProvider;

/**
 * Application facade exposed to QML as `app`.
 *
 * Owns the storage layer, the thumbnail service and the grid model, and turns
 * long running work (indexing, seeding) into QtConcurrent jobs whose progress is
 * reported through queued signal emissions. Nothing here blocks the GUI thread
 * for longer than a database metadata query.
 */
class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(MediaGridModel *model READ model CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(int progressCurrent READ progressCurrent NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY progressChanged)
    Q_PROPERTY(bool initialPerfOverlay READ initialPerfOverlay CONSTANT)
    Q_PROPERTY(int thumbnailEdge READ thumbnailEdge CONSTANT)
    /** Effective in-memory decoded-thumbnail budget in MB. Adaptive unless the
     *  user pinned it with --thumb-memory-cache. */
    Q_PROPERTY(int thumbnailMemoryBudgetMb READ thumbnailMemoryBudgetMb NOTIFY thumbnailBudgetChanged)
    /** Which video backend this build uses: "mpv", "qtmultimedia" or "none".
     *  QML imports are static, so the mpv item lives in its own file that is only
     *  loaded when this reports "mpv" - otherwise a non-mpv build would fail to
     *  load the viewer at all. */
    Q_PROPERTY(QString videoBackend READ videoBackend CONSTANT)
    Q_PROPERTY(bool indexing READ indexing NOTIFY indexingChanged)
    Q_PROPERTY(bool watching READ watching NOTIFY watchingChanged)
    Q_PROPERTY(int watchedDirectories READ watchedDirectories NOTIFY watchingChanged)
    /** Library roots (user-managed folders) as {id, path, count} maps. */
    Q_PROPERTY(QVariantList libraryRoots READ libraryRoots NOTIFY rootsChanged)
    Q_PROPERTY(PerfMonitor *perf READ perf CONSTANT)
    /** Theme + language preferences (M4). Persisted with QSettings. */
    Q_PROPERTY(AppSettings *settings READ settings CONSTANT)
    /** Rebindable keyboard shortcuts (M5). Persisted with QSettings. */
    Q_PROPERTY(ShortcutRegistry *shortcuts READ shortcuts CONSTANT)
    /** Human-readable summary of the last smart-grouping pass. */
    Q_PROPERTY(QString smartStatus READ smartStatus NOTIFY smartStatusChanged)
    /** True when this build includes the optional face/ML backend. */
    Q_PROPERTY(bool smartFacesAvailable READ smartFacesAvailable CONSTANT)
    /** True while a grouping (or face) pass is running. */
    Q_PROPERTY(bool smartRunning READ smartRunning NOTIFY smartStatusChanged)
    /** True when the face model files are present on disk. */
    Q_PROPERTY(bool faceModelsReady READ faceModelsReady NOTIFY faceModelsChanged)
    /** True while the face models are being downloaded. */
    Q_PROPERTY(bool faceModelDownloading READ faceModelDownloading NOTIFY faceModelsChanged)
    /** Where the face models are expected to live. */
    Q_PROPERTY(QString faceModelDir READ faceModelDir CONSTANT)
    Q_PROPERTY(bool scrollBenchmarkEnabled READ scrollBenchmarkEnabled CONSTANT)
    Q_PROPERTY(double scrollBenchmarkSeconds READ scrollBenchmarkSeconds CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    /// Opens the database and applies migrations. Returns false on fatal error.
    bool initialise(const QString &databasePath, int thumbnailEdge, QString *error, QStringList *warnings);

    /// Unconditionally rebuilds the full-text search index (PRD F-4.1 recovery).
    /// Requires a successful initialise(). Returns false and fills `error` on
    /// failure, including when SQLite has no FTS5 support.
    bool rebuildSearchIndex(QString *error = nullptr);

    MediaGridModel *model() const { return m_model; }
    AppSettings *settings() const { return m_settings; }
    ShortcutRegistry *shortcuts() const { return m_shortcuts; }

    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    QString summary() const;
    int progressCurrent() const { return m_progressCurrent; }
    int progressTotal() const { return m_progressTotal; }
    bool initialPerfOverlay() const { return m_initialPerfOverlay; }
    void setInitialPerfOverlay(bool value) { m_initialPerfOverlay = value; }
    int thumbnailEdge() const;
    int thumbnailMemoryBudgetMb() const { return m_thumbnailMemoryBudgetMb; }
    QString videoBackend() const;
    bool indexing() const { return m_indexing; }
    /// True once the filesystem watcher is active on at least one root.
    bool watching() const;
    int watchedDirectories() const;

    QVariantList libraryRoots() const;
    /// Removes a root and every media row that belongs to it. Returns false when
    /// the path is not a known non-synthetic root. Files on disk are untouched.
    Q_INVOKABLE bool removeRoot(const QString &path);
    /// Re-indexes every enabled root (the Settings page "Rescan now" button).
    Q_INVOKABLE void rescanLibrary();

    /// Rebuilds Places + Events (and faces when ML is enabled) from the current
    /// library. Safe to call from QML; runs on a worker thread.
    Q_INVOKABLE void rebuildSmartGrouping();

    QString smartStatus() const { return m_smartStatus; }
    bool smartFacesAvailable() const;
    bool smartRunning() const { return m_smartRunning; }

    bool faceModelsReady() const;
    bool faceModelDownloading() const { return m_faceModelDownloading; }
    QString faceModelDir() const;
    /// Downloads yunet.onnx + sface.onnx into faceModelDir() (opt-in, PRD F-2.4).
    Q_INVOKABLE void downloadFaceModels();

    /// Registers the QML image provider and the model on the given engine.
    ThumbnailProvider *thumbnailProvider() const { return m_provider; }

    /// Frame pacing statistics for the scroll benchmark.
    PerfMonitor *perf() const { return m_perf; }

    /// True when --bench-scroll was passed: QML then drives the grid itself.
    bool scrollBenchmarkEnabled() const { return m_scrollBenchmarkSeconds > 0.0; }
    double scrollBenchmarkSeconds() const { return m_scrollBenchmarkSeconds; }
    void setScrollBenchmarkSeconds(double seconds) { m_scrollBenchmarkSeconds = seconds; }

    /// Called by the grid when the automated scroll finishes. Prints the result
    /// and emits scrollBenchmarkFinished() so a headless run can exit.
    Q_INVOKABLE void finishScrollBenchmark(double seconds, double distancePx, int itemsTraversed);

    /// Called by the grid just before the automated scroll starts, so the report
    /// captures the resting RSS as well as the peak during scrolling.
    Q_INVOKABLE void reportScrollBenchmarkStart();

    /// Overrides the decoded-thumbnail memory budget set at construction.
    /// A negative value re-enables the adaptive budget (PRD 4.1.1).
    void setThumbnailMemoryCacheMb(int mb);

    /// QML reports the current grid geometry so the thumbnail LRU budget can be
    /// sized to what is actually on screen (PRD 4.1.1 point 2). Ignored while a
    /// manual --thumb-memory-cache override is in force.
    Q_INVOKABLE void updateThumbnailBudget(int columns, int visibleRows);

    /// Caps how many items the grid loads. Applied on initialise(). 0 = no cap.
    void setGridLimit(int limit);

    /// Called from main() once the QML engine exists, using CLI options.
    void setStartupRequests(int seedRows, const QStringList &indexFolders);

    /// Enables/disables seeding a brand-new library with the home folder as its
    /// root. main() turns this off for benchmarks, seeding and explicit --index
    /// runs, where scanning a home directory would poison the measurement.
    void setAutoHomeScanEnabled(bool enabled) { m_autoHomeScanEnabled = enabled; }

    /// Called by the filesystem watcher when the library tree settles.
    void onLibraryChanged();

    /// Runs the queued startup work (seed + index + reload).
    Q_INVOKABLE void runStartupRequests();

    Q_INVOKABLE void reload();

    Q_INVOKABLE void seedBenchmark(int rows);
    Q_INVOKABLE void clearBenchmark();

    Q_INVOKABLE void indexFolder(const QString &folder);
    Q_INVOKABLE void indexFolders(const QStringList &folders);

    Q_INVOKABLE void cancelCurrentJob();

    Q_INVOKABLE void clearThumbnailCache();

    Q_INVOKABLE QString storageInfo() const;
    Q_INVOKABLE QString thumbnailInfo() const;
    Q_INVOKABLE QString databasePath() const;
    Q_INVOKABLE QString cachePath() const;

    /// RSS of this process in MB (Linux only). Used by the M0 benchmark report.
    double residentMemoryMb() const;
    /// Peak RSS of this process in MB (Linux only).
    double peakMemoryMb() const;

    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE void revealInFileManager(const QString &path) const;

    /// Builds the `image://full/...` URL the viewer uses for a local file path.
    /// Exposed here because the path has to be base64url-encoded, which is far
    /// less error-prone in C++ than in QML string handling.
    Q_INVOKABLE QString fullImageUrl(const QString &path, int maxEdge = 0) const;

signals:
    void busyChanged();
    void statusTextChanged();
    void summaryChanged();
    void progressChanged();
    void indexingChanged();
    void watchingChanged();
    void rootsChanged();
    void smartStatusChanged();
    void faceModelsChanged();
    void thumbnailBudgetChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);

    /// Emitted once the queued startup work (seed / index / reload) is done.
    /// `--no-ui` uses this to exit deterministically instead of guessing.
    void startupCompleted();

    /// Emitted after finishScrollBenchmark() has printed its report.
    void scrollBenchmarkFinished();

private:
    void setBusy(bool busy);
    void setIndexing(bool indexing);
    void setStatus(const QString &text);
    void setProgress(int current, int total);
    void registerThumbnailSources();
    QString formatIndexerResult(const FolderIndexer::Result &result) const;
    /// Snapshot of the library-related settings, safe to pass to a worker thread.
    FolderIndexer::ScanOptions currentScanOptions() const;
    /// Starts or stops the watcher according to settings.autoWatch().
    void applyWatcherSettings();
    /// Runs the smart-grouping pass when the preferences ask for it and one is
    /// not already in flight. Returns true when a pass is running (or was just
    /// started), so the startup chain knows to wait for it.
    bool maybeRunSmartGrouping();
    /// Cheap fingerprint (row count + newest indexed_at) of the live library. Used
    /// to skip a full Places/Events rebuild when nothing has changed.
    QString smartSourceSignature() const;
    void runSmartGrouping();
    void setSmartStatus(const QString &status);

    std::unique_ptr<Database> m_database;
    std::unique_ptr<ThumbnailService> m_thumbnails;
    std::unique_ptr<LibraryWatcher> m_watcher;
    ThumbnailProvider *m_provider = nullptr;
    MediaGridModel *m_model = nullptr;
    PerfMonitor *m_perf = nullptr;
    AppSettings *m_settings = nullptr;
    ShortcutRegistry *m_shortcuts = nullptr;

    std::atomic_bool m_cancel{false};

    bool m_busy = false;
    bool m_indexing = false;
    bool m_smartRunning = false;
    bool m_faceModelDownloading = false;
    QString m_smartStatus;
    bool m_initialPerfOverlay = false;
    QString m_statusText;
    int m_progressCurrent = 0;
    int m_progressTotal = 0;

    int m_startupSeedRows = 0;
    QStringList m_startupIndexFolders;
    /// True while the initial CLI-requested job chain is still running.
    bool m_startupChainActive = false;
    /// True once the startup chain has handed off to a smart-grouping pass, so
    /// finishStartupChain() does not start a second one when that pass ends.
    bool m_startupSmartStarted = false;
    /// True when this library should have the home folder added as first root.
    bool m_autoHomePending = false;
    /// Whether the home-root default is allowed at all (false for benchmarks).
    bool m_autoHomeScanEnabled = true;
    int m_thumbnailMemoryBudgetMb = 128;
    /// -1 = adaptive; >= 0 = pinned by --thumb-memory-cache.
    int m_thumbnailMemoryOverrideMb = -1;
    double m_scrollBenchmarkSeconds = 0.0;
    int m_pendingGridLimit = 0;

    /// Emits startupCompleted() and clears m_startupChainActive.
    void finishStartupChain();

    /// True when the library has no user roots yet and the one-time home-root
    /// seeding has not run. Read once during initialise().
    bool shouldSeedDefaultHomeRoot() const;

    /// Reads a `Vm*` field from /proc/self/status, in MB.
    double readMemoryStatMb(const char *key) const;
};

} // namespace kg
