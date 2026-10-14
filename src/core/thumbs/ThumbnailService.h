#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QVector>

class QThreadPool;

namespace kg {

/**
 * Two-tier thumbnail cache + asynchronous generator (PRD 5.4).
 *
 *   tier 1: in-memory LRU, keyed by media id, holds decoded QImages
 *   tier 2: on-disk WebP files under Paths::thumbnailCacheDir(), sharded
 *   tier 3: generate on demand from the source file, then populate 1 and 2
 *
 * `fetch()` is called from Qt Quick's image-loading thread and is therefore
 * fully thread-safe. When a thumbnail is not ready yet it returns a cheap
 * pre-rendered placeholder and enqueues generation; `thumbnailReady` is emitted
 * (queued to the main thread) once the real thumbnail lands in the disk cache.
 */
class ThumbnailService : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailService(int edgePx = 256, QObject *parent = nullptr);
    ~ThumbnailService() override;

    int edge() const { return m_edge; }

    /// Thumbnail container format actually in use, lower case ("webp" or "jpg").
    /// Chosen at construction from what the Qt image plugins on this system
    /// support, because WebP is not guaranteed to be installed.
    QString format() const { return m_format; }
    QString formatExtension() const { return m_extension; }

    /// Registers where a media id's pixels come from. Thread-safe.
    /// `video` routes generation through VideoFrameExtractor instead of
    /// QImageReader, which cannot open a video container at all.
    void registerSource(qint64 mediaId, const QString &sourcePath, bool synthetic,
                        bool video = false);
    void forgetSource(qint64 mediaId);
    void clearSources();

    /// Thread-safe. Returns a placeholder image when the thumbnail is not ready.
    /// `placeholder` (optional) is set to true when the returned image is a
    /// placeholder rather than the real thing.
    QImage fetch(qint64 mediaId, int requestedEdge, bool *placeholder = nullptr);

    /// True when a real thumbnail is already available (memory or disk).
    bool isReady(qint64 mediaId) const;

    int pendingCount() const;
    qint64 diskCacheBytes() const;
    int diskCacheFileCount() const;

    /// Statistics for the M0 benchmark readout.
    struct Stats
    {
        qint64 memoryHits = 0;
        qint64 diskHits = 0;
        qint64 misses = 0;
        qint64 failures = 0;
        qint64 generated = 0;
        qint64 generationMs = 0;
    };
    Stats stats() const;
    void resetStats();

    void setMemoryCacheLimitMb(int mb);
    int memoryCacheLimitMb() const;
    int memoryCacheCount() const;

    /// Deletes every file under the thumbnail cache directory.
    /// Number of media ids that failed generation this session.
    int failedCount() const;
    /// Forget all recorded failures so generation is attempted again. Useful after a new
    /// image-format plugin becomes available, or once video frame extraction exists.
    void retryFailed();

    bool clearDiskCache(QString *error = nullptr);
    void clearMemoryCache();

signals:
    void thumbnailReady(qint64 mediaId);

private:
    friend class ThumbnailTask;

    struct SourceEntry
    {
        QString path;
        bool synthetic = false;
        /// Video containers need libavformat; QImageReader cannot read them.
        bool video = false;
    };

    void requestGeneration(qint64 mediaId, int edge);
    void runGeneration(qint64 mediaId, int edge, SourceEntry entry);

    QImage renderSource(const SourceEntry &entry, int edge) const;
    static QImage renderSynthetic(qint64 mediaId, int edge);

    void insertMemory(qint64 mediaId, const QImage &image);
    QImage lookupMemory(qint64 mediaId) const;

    SourceEntry sourceFor(qint64 mediaId) const;
    void buildPlaceholders();
    /// Picks the best writable thumbnail format available on this system.
    static QString pickWriteFormat(QString *extension);

    int m_edge = 256;
    QString m_format;
    QString m_extension;
    QThreadPool *m_pool = nullptr;

    mutable QMutex m_mutex;
    QHash<qint64, SourceEntry> m_sources;
    QSet<qint64> m_pending;
    /// Media ids whose generation already failed this session (video, HEIC/RAW without
    /// a plugin, corrupt files). Without this, fetch() would reschedule the same doomed
    /// generation on every visible-cell request, and because runGeneration() used to emit
    /// thumbnailReady() even on failure, that bumped the caller's revision, changed the
    /// image URL, and made Qt re-request the cell forever: an infinite regenerate loop
    /// that made every video cell flicker. Cleared by clearDiskCache()/retryFailed().
    QSet<qint64> m_failed;
    /// Media ids that were asked for before their source path was registered. This is a
    /// not-yet-ready condition, not a failure: the grid requests images during model reset,
    /// which happens before registerThumbnailSources() has run. Parking the id here lets
    /// registerSource() retry it exactly once instead of leaving a permanent placeholder.
    QSet<qint64> m_awaitingSource;

    mutable QMutex m_memoryMutex;
    QHash<qint64, QImage> m_memory;
    QVector<qint64> m_memoryOrder; ///< LRU, oldest first
    qint64 m_memoryCostBytes = 0;
    /// In-memory thumbnail cache budget. Each 256 px RGB32 thumbnail costs
    /// 262 KB, so this is the dominant term in RSS and has to stay modest.
    qint64 m_memoryLimitBytes = 128LL * 1024 * 1024;

    QVector<QImage> m_placeholders;

    mutable QMutex m_statsMutex;
    Stats m_stats;
};

} // namespace kg
