#include "core/thumbs/ThumbnailService.h"

#include "app/Paths.h"
#include "core/thumbs/VideoFrameExtractor.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

namespace kg {

namespace {

constexpr int kPlaceholderVariants = 64;
constexpr int kThumbnailQuality = 82;

/// Deterministic 32-bit mix so synthetic images are stable across runs.
inline quint32 mix32(quint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

QSize fitInside(const QSize &source, int box)
{
    if (!source.isValid() || source.isEmpty()) {
        return QSize(box, box);
    }
    return source.scaled(box, box, Qt::KeepAspectRatio);
}

} // namespace

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------
class ThumbnailTask : public QRunnable
{
public:
    ThumbnailTask(ThumbnailService *service, qint64 mediaId, int edge, ThumbnailService::SourceEntry entry)
        : m_service(service)
        , m_mediaId(mediaId)
        , m_edge(edge)
        , m_entry(std::move(entry))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        m_service->runGeneration(m_mediaId, m_edge, m_entry);
    }

private:
    ThumbnailService *m_service;
    qint64 m_mediaId;
    int m_edge;
    ThumbnailService::SourceEntry m_entry;
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
QString ThumbnailService::pickWriteFormat(QString *extension)
{
    // Preference order: smallest file first. WebP/AVIF are not shipped by every
    // Qt install (Artix being a live example), so probe rather than assume.
    static const QList<QPair<QString, QString>> candidates = {
        {QStringLiteral("webp"), QStringLiteral("webp")},
        {QStringLiteral("avif"), QStringLiteral("avif")},
        {QStringLiteral("jpg"), QStringLiteral("jpg")},
        {QStringLiteral("png"), QStringLiteral("png")},
    };

    const QList<QByteArray> supported = QImageWriter::supportedImageFormats();
    for (const auto &candidate : candidates) {
        if (supported.contains(candidate.first.toLatin1())) {
            if (extension) {
                *extension = candidate.second;
            }
            return candidate.first;
        }
    }

    // Absolutely nothing writable: PNG is compiled into QtGui, so this cannot
    // happen in practice, but keep the function total.
    if (extension) {
        *extension = QStringLiteral("png");
    }
    return QStringLiteral("png");
}

ThumbnailService::ThumbnailService(int edgePx, QObject *parent)
    : QObject(parent)
    , m_edge(std::clamp(edgePx, 64, 4096))
    , m_pool(new QThreadPool(this))
{
    // Resolved in the body, not the init list: m_format and m_extension are
    // initialised in declaration order, so probing into &m_extension from
    // m_format's initialiser would write to a not-yet-constructed QString.
    m_format = pickWriteFormat(&m_extension);
    // Decoding is CPU bound; leave at least one core for the UI thread.
    const int ideal = QThread::idealThreadCount();
    m_pool->setMaxThreadCount(std::clamp(ideal - 1, 2, 8));
    m_pool->setExpiryTimeout(30000);

    buildPlaceholders();
}

ThumbnailService::~ThumbnailService()
{
    if (m_pool) {
        m_pool->clear();
        m_pool->waitForDone();
    }
}

void ThumbnailService::buildPlaceholders()
{
    // Pre-render a small set of gradient tiles once. Serving a placeholder is
    // then a refcount bump instead of an allocation, so the benchmark measures
    // the delegate + image pipeline rather than placeholder synthesis.
    m_placeholders.reserve(kPlaceholderVariants);

    for (int i = 0; i < kPlaceholderVariants; ++i) {
        const quint32 h = mix32(static_cast<quint32>(i) * 2654435761u);

        const int r1 = 40 + static_cast<int>(h & 0x3F);
        const int g1 = 40 + static_cast<int>((h >> 8) & 0x3F);
        const int b1 = 48 + static_cast<int>((h >> 16) & 0x3F);

        const int r2 = std::min(255, r1 + 34);
        const int g2 = std::min(255, g1 + 30);
        const int b2 = std::min(255, b1 + 38);

        // 2x2 source smooth-scaled up gives a cheap two-axis gradient.
        QImage tiny(2, 2, QImage::Format_RGB32);
        tiny.setPixel(0, 0, qRgb(r1, g1, b1));
        tiny.setPixel(1, 0, qRgb(r2, g1, b1));
        tiny.setPixel(0, 1, qRgb(r1, g2, b2));
        tiny.setPixel(1, 1, qRgb(r2, g2, b2));

        m_placeholders.append(
            tiny.scaled(m_edge, m_edge, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
}

// ---------------------------------------------------------------------------
// Source registry
// ---------------------------------------------------------------------------
void ThumbnailService::registerSource(qint64 mediaId, const QString &sourcePath, bool synthetic,
                                     bool video)
{
    bool retry = false;
    {
        QMutexLocker lock(&m_mutex);
        m_sources.insert(mediaId, SourceEntry{sourcePath, synthetic, video});
        // A (re)registered source may now render where a previous attempt could not, so a
        // recorded failure must not be allowed to outlive it.
        m_failed.remove(mediaId);
        retry = m_awaitingSource.remove(mediaId);
    }

    // The grid asked for this image before the path was known. Now that it is, run the
    // generation that was skipped earlier; on success thumbnailReady() makes Qt re-request
    // the cell and the real thumbnail replaces the placeholder.
    if (retry) {
        requestGeneration(mediaId, m_edge);
    }
}

void ThumbnailService::forgetSource(qint64 mediaId)
{
    QMutexLocker lock(&m_mutex);
    m_sources.remove(mediaId);
}

void ThumbnailService::clearSources()
{
    QMutexLocker lock(&m_mutex);
    m_sources.clear();
}

ThumbnailService::SourceEntry ThumbnailService::sourceFor(qint64 mediaId) const
{
    QMutexLocker lock(&m_mutex);
    return m_sources.value(mediaId);
}

// ---------------------------------------------------------------------------
// Memory cache (LRU)
// ---------------------------------------------------------------------------
void ThumbnailService::insertMemory(qint64 mediaId, const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    const qint64 cost = static_cast<qint64>(image.sizeInBytes());

    QMutexLocker lock(&m_memoryMutex);

    if (m_memory.contains(mediaId)) {
        m_memoryCostBytes -= static_cast<qint64>(m_memory.value(mediaId).sizeInBytes());
        m_memoryOrder.removeAll(mediaId);
    }

    m_memory.insert(mediaId, image);
    m_memoryOrder.append(mediaId);
    m_memoryCostBytes += cost;

    while (m_memoryCostBytes > m_memoryLimitBytes && m_memoryOrder.size() > 1) {
        const qint64 victim = m_memoryOrder.takeFirst();
        const auto it = m_memory.constFind(victim);
        if (it == m_memory.constEnd()) {
            continue;
        }
        m_memoryCostBytes -= static_cast<qint64>(it->sizeInBytes());
        m_memory.remove(victim);
    }
}

QImage ThumbnailService::lookupMemory(qint64 mediaId) const
{
    QMutexLocker lock(&m_memoryMutex);
    return m_memory.value(mediaId);
}

void ThumbnailService::clearMemoryCache()
{
    QMutexLocker lock(&m_memoryMutex);
    m_memory.clear();
    m_memoryOrder.clear();
    m_memoryCostBytes = 0;
}

void ThumbnailService::setMemoryCacheLimitMb(int mb)
{
    QMutexLocker lock(&m_memoryMutex);
    m_memoryLimitBytes = qMax(16, mb) * 1024LL * 1024LL;

    // Shrinking the budget must release memory now, not on the next insert. The
    // whole point of the adaptive budget is a lower resident set while idle, so
    // waiting for the next generation to trigger eviction would defeat it.
    while (m_memoryCostBytes > m_memoryLimitBytes && !m_memoryOrder.isEmpty()) {
        const qint64 victim = m_memoryOrder.takeFirst();
        const auto it = m_memory.constFind(victim);
        if (it == m_memory.constEnd()) {
            continue;
        }
        m_memoryCostBytes -= static_cast<qint64>(it->sizeInBytes());
        m_memory.remove(victim);
    }
}

int ThumbnailService::memoryCacheLimitMb() const
{
    QMutexLocker lock(&m_memoryMutex);
    return static_cast<int>(m_memoryLimitBytes / (1024 * 1024));
}

int ThumbnailService::memoryCacheCount() const
{
    QMutexLocker lock(&m_memoryMutex);
    return m_memory.size();
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------
QImage ThumbnailService::fetch(qint64 mediaId, int requestedEdge, bool *isPlaceholder)
{
    if (isPlaceholder) {
        *isPlaceholder = false;
    }

    const int edge = requestedEdge > 0 ? std::clamp(requestedEdge, 64, 4096) : m_edge;

    // tier 1: memory
    if (const QImage cached = lookupMemory(mediaId); !cached.isNull()) {
        QMutexLocker lock(&m_statsMutex);
        ++m_stats.memoryHits;
        return cached;
    }

    // tier 2: disk
    const QString cacheFile = Paths::thumbnailPathFor(mediaId, m_extension);
    if (QFileInfo::exists(cacheFile)) {
        QImageReader reader(cacheFile);
        reader.setAutoTransform(true);
        QImage image = reader.read();
        if (!image.isNull()) {
            insertMemory(mediaId, image);
            QMutexLocker lock(&m_statsMutex);
            ++m_stats.diskHits;
            return image;
        }
        // Corrupt or unreadable cache entry: drop it and regenerate.
        QFile::remove(cacheFile);
    }

    // tier 3: schedule generation, serve a placeholder meanwhile.
    // Do not reschedule media whose generation already failed: the result will not
    // change, and retrying on every request is what caused the video flicker loop.
    bool alreadyFailed = false;
    {
        QMutexLocker lock(&m_mutex);
        alreadyFailed = m_failed.contains(mediaId);
    }
    if (!alreadyFailed) {
        requestGeneration(mediaId, edge);
    }

    if (isPlaceholder) {
        *isPlaceholder = true;
    }

    if (!m_placeholders.isEmpty()) {
        return m_placeholders.at(static_cast<int>(mediaId % m_placeholders.size()));
    }
    return QImage();
}

bool ThumbnailService::isReady(qint64 mediaId) const
{
    if (!lookupMemory(mediaId).isNull()) {
        return true;
    }
    return QFileInfo::exists(Paths::thumbnailPathFor(mediaId, m_extension));
}

void ThumbnailService::requestGeneration(qint64 mediaId, int edge)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_pending.contains(mediaId) || m_failed.contains(mediaId)) {
            return;
        }
        m_pending.insert(mediaId);
    }
    m_pool->start(new ThumbnailTask(this, mediaId, edge, sourceFor(mediaId)));
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------
QImage ThumbnailService::renderSource(const SourceEntry &entry, int edge) const
{
    if (entry.path.isEmpty()) {
        return QImage();
    }

    // A video container is not an image: QImageReader would fail, which is what used to
    // leave every video cell showing the placeholder forever. Decode a real poster frame
    // with libavformat instead (no GL context needed, so this also works headless).
    if (entry.video) {
        QImage frame = VideoFrameExtractor::extractPoster(entry.path, edge);
        if (!frame.isNull() && (frame.width() > edge || frame.height() > edge)) {
            frame = frame.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return frame;
    }

    QImageReader reader(entry.path);
    reader.setAutoTransform(true);

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && !sourceSize.isEmpty()) {
        // Ask the decoder for an already-downscaled image. For JPEG this uses
        // DCT scaling, which is several times faster than decode-then-resize.
        reader.setScaledSize(fitInside(sourceSize, edge));
    }
    // Always honour a bounded thumbnail; a huge or malformed image should not be
    // able to allocate unbounded memory.
    reader.setAllocationLimit(256);

    QImage image = reader.read();
    if (image.isNull()) {
        return image;
    }

    if (image.width() > edge || image.height() > edge) {
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

QImage ThumbnailService::renderSynthetic(qint64 mediaId, int edge)
{
    // Synthetic rows have no file on disk. We still produce real pixels through
    // the same resize/encode path so the benchmark exercises realistic cost.
    const quint32 h = mix32(static_cast<quint32>(mediaId));
    const int mediumWidth = edge * 2;
    const int mediumHeight = edge * 2;

    QImage image(mediumWidth, mediumHeight, QImage::Format_RGB32);

    const int baseR = 24 + static_cast<int>(h & 0x7F);
    const int baseG = 24 + static_cast<int>((h >> 7) & 0x7F);
    const int baseB = 32 + static_cast<int>((h >> 14) & 0x7F);

    for (int y = 0; y < mediumHeight; ++y) {
        const int shade = (y * 90) / mediumHeight;
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb rowColor = qRgb(std::min(255, baseR + shade),
                                   std::min(255, baseG + shade),
                                   std::min(255, baseB + shade));
        std::fill(line, line + mediumWidth, rowColor);
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int i = 0; i < 4; ++i) {
        const quint32 sh = mix32(h + static_cast<quint32>(i) * 9176u);
        const int x = static_cast<int>(sh % static_cast<quint32>(mediumWidth));
        const int w = mediumWidth / 6 + static_cast<int>((sh >> 8) % static_cast<quint32>(mediumWidth / 3));
        const int y = static_cast<int>((sh >> 16) % static_cast<quint32>(mediumHeight));
        const int ht = mediumHeight / 8;
        painter.fillRect(QRect(x, y, w, ht),
                         QColor(180 + static_cast<int>(sh & 0x3F), 90 + static_cast<int>((sh >> 6) & 0x7F),
                                60 + static_cast<int>((sh >> 12) & 0x7F), 190));
    }
    painter.end();

    if (image.width() > edge || image.height() > edge) {
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void ThumbnailService::runGeneration(qint64 mediaId, int edge, SourceEntry entry)
{
    QElapsedTimer timer;
    timer.start();


    // No path yet: the grid requested this cell during model reset, before
    // registerThumbnailSources() ran. Park it so registerSource() can retry, and do NOT
    // record a failure — that would leave the tile a permanent placeholder.
    if (!entry.synthetic && entry.path.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        m_pending.remove(mediaId);
        m_awaitingSource.insert(mediaId);
        return;
    }

    QImage image;
    if (entry.synthetic) {
        image = renderSynthetic(mediaId, edge);
    } else {
        image = renderSource(entry, edge);
    }

    if (image.isNull()) {
        // Nothing was produced (video, HEIC/RAW without a plugin, corrupt file).
        // Record the failure so fetch() stops rescheduling this id, and deliberately do
        // NOT emit thumbnailReady: that signal bumps the caller's revision, which changes
        // the image URL and makes Qt re-request the cell, which would schedule generation
        // again, fail again, and loop forever. That loop was the video flicker.
        {
            QMutexLocker lock(&m_mutex);
            m_failed.insert(mediaId);
            m_pending.remove(mediaId);
        }
        {
            QMutexLocker lock(&m_statsMutex);
            ++m_stats.failures;
        }
        return;
    }

    {
        const QString cacheFile = Paths::thumbnailPathFor(mediaId, m_extension);
        QDir().mkpath(QFileInfo(cacheFile).absolutePath());

        QImageWriter writer(cacheFile, m_format.toLatin1());
        writer.setQuality(kThumbnailQuality);
        if (!writer.write(image)) {
            qWarning("kin-galeri: thumbnail write failed for %lld (%s): %s",
                     static_cast<long long>(mediaId), qUtf8Printable(m_format),
                     qUtf8Printable(writer.errorString()));
        }
    }

    insertMemory(mediaId, image);

    {
        QMutexLocker lock(&m_statsMutex);
        ++m_stats.generated;
        m_stats.generationMs += timer.elapsed();
    }

    {
        QMutexLocker lock(&m_mutex);
        m_pending.remove(mediaId);
    }

    // Only announce success. Emitting this for a failed generation is what started the
    // re-request loop, so it must stay strictly on the success path.
    Q_EMIT thumbnailReady(mediaId);
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------
int ThumbnailService::pendingCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_pending.size());
}

qint64 ThumbnailService::diskCacheBytes() const
{
    qint64 total = 0;
    QDirIterator it(Paths::thumbnailCacheDir(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

int ThumbnailService::diskCacheFileCount() const
{
    int count = 0;
    QDirIterator it(Paths::thumbnailCacheDir(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

int ThumbnailService::failedCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_failed.size());
}

void ThumbnailService::retryFailed()
{
    QMutexLocker lock(&m_mutex);
    m_failed.clear();
}

ThumbnailService::Stats ThumbnailService::stats() const
{
    QMutexLocker lock(&m_statsMutex);
    return m_stats;
}

void ThumbnailService::resetStats()
{
    QMutexLocker lock(&m_statsMutex);
    m_stats = Stats{};
}

bool ThumbnailService::clearDiskCache(QString *error)
{
    const QString dir = Paths::thumbnailCacheDir();
    QDir root(dir);
    if (!root.exists()) {
        return true;
    }

    clearMemoryCache();

    {
        // A cleared cache invalidates the record of what cannot be generated, so the
        // user can retry after installing a missing image-format plugin.
        QMutexLocker lock(&m_mutex);
        m_failed.clear();
    }

    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files);
    for (const QString &entry : entries) {
        const QString path = dir + QLatin1Char('/') + entry;
        const QFileInfo info(path);
        const bool removed = info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!removed) {
            if (error) {
                *error = QStringLiteral("Could not remove '%1'").arg(path);
            }
            return false;
        }
    }
    return true;
}

} // namespace kg
