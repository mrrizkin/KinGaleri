#include "core/indexer/FolderIndexer.h"

#include "core/db/Database.h"
#include "core/indexer/ScanPolicy.h"
#include "core/metadata/ExifReader.h"
#include "core/model/SourceAlbum.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QSize>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QThreadPool>
#include <QVariant>
#include <QtConcurrent>

#include <algorithm>

namespace kg {

namespace {

/// Rows per database transaction. Large enough that commit overhead disappears,
/// small enough that a crash loses at most a second of indexing work.
constexpr int kBatchSize = 1000;

/// Files whose metadata is decoded in parallel before the next serial upsert
/// pass. Keeps peak memory bounded on a home directory with 100k+ files.
constexpr int kExtractBatchSize = 256;

/// Cap on metadata worker threads (PRD 4.3 names min(hw_concurrency, 8)).
constexpr int kMaxExtractThreads = 8;

/**
 * Renders an EXIF exposure time the way photographers read it: reciprocals
 * below a second ("1/250"), decimals above it ("2.5"). Returns a null QVariant
 * when there is no usable value, so the column stays NULL.
 */
QVariant formatExposure(double seconds)
{
    if (seconds <= 0.0)
        return QVariant();
    if (seconds >= 1.0)
        return QString::number(seconds, 'g', 3);
    return QStringLiteral("1/%1").arg(qRound(1.0 / seconds));
}

const QStringList &imageExtensions()
{
    static const QStringList list = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),  QStringLiteral("webp"),
        QStringLiteral("gif"),  QStringLiteral("bmp"),  QStringLiteral("tif"),  QStringLiteral("tiff"),
        QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"), QStringLiteral("jxl"),
        QStringLiteral("dng"),  QStringLiteral("cr2"),  QStringLiteral("nef"),  QStringLiteral("arw"),
        QStringLiteral("raf"),  QStringLiteral("orf"),  QStringLiteral("rw2"),  QStringLiteral("sr2"),
        QStringLiteral("svg"),
    };
    return list;
}

const QStringList &videoExtensions()
{
    // `.ts` is deliberately absent: it is overwhelmingly TypeScript source in a
    // developer's home, while MPEG transport streams use `.mts`/`.m2ts`. The PRD
    // format list does not name `.ts` either.
    static const QStringList list = {
        QStringLiteral("mp4"),  QStringLiteral("mov"),  QStringLiteral("mkv"), QStringLiteral("webm"),
        QStringLiteral("avi"),  QStringLiteral("m4v"),  QStringLiteral("3gp"), QStringLiteral("mpg"),
        QStringLiteral("mpeg"), QStringLiteral("mts"),  QStringLiteral("m2ts"), QStringLiteral("wmv"),
        QStringLiteral("flv"),
    };
    return list;
}

} // namespace

/// Plain-data result of the concurrent metadata pass. Defined here so the header
/// does not have to pull in ExifReader/QFileInfo.
struct FolderIndexer::Extracted
{
    QFileInfo info;
    bool video = false;
    qint64 takenMs = 0;
    QString takenSource;
    int width = 0;
    int height = 0;
    int orientation = 0;
    QString make;
    QString model;
    QString lens;
    int iso = 0;
    double fNumber = 0.0;
    QVariant exposure;
    double focal = 0.0;
    bool hasGps = false;
    double lat = 0.0;
    double lon = 0.0;
};

FolderIndexer::FolderIndexer(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
}

QStringList FolderIndexer::supportedExtensions()
{
    QStringList all = imageExtensions();
    all += videoExtensions();
    all.sort();
    all.removeDuplicates();
    return all;
}

bool FolderIndexer::isVideoExtension(const QString &extension)
{
    return videoExtensions().contains(extension.toLower());
}

bool FolderIndexer::isSupportedExtension(const QString &extension)
{
    const QString lower = extension.toLower();
    return imageExtensions().contains(lower) || videoExtensions().contains(lower);
}

bool FolderIndexer::isIgnoredDirectory(const QString &directoryPath)
{
    return scan::isIgnoredDirectory(directoryPath);
}

bool FolderIndexer::isIgnoredDirectory(const QString &directoryPath, const ScanOptions &options)
{
    return scan::isIgnoredDirectory(directoryPath, options);
}

QString FolderIndexer::guessMimeType(const QString &extension)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("jpg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("png"), QStringLiteral("image/png")},
        {QStringLiteral("webp"), QStringLiteral("image/webp")},
        {QStringLiteral("gif"), QStringLiteral("image/gif")},
        {QStringLiteral("bmp"), QStringLiteral("image/bmp")},
        {QStringLiteral("tif"), QStringLiteral("image/tiff")},
        {QStringLiteral("tiff"), QStringLiteral("image/tiff")},
        {QStringLiteral("heic"), QStringLiteral("image/heic")},
        {QStringLiteral("heif"), QStringLiteral("image/heif")},
        {QStringLiteral("avif"), QStringLiteral("image/avif")},
        {QStringLiteral("jxl"), QStringLiteral("image/jxl")},
        {QStringLiteral("svg"), QStringLiteral("image/svg+xml")},
        {QStringLiteral("mp4"), QStringLiteral("video/mp4")},
        {QStringLiteral("m4v"), QStringLiteral("video/mp4")},
        {QStringLiteral("mov"), QStringLiteral("video/quicktime")},
        {QStringLiteral("mkv"), QStringLiteral("video/x-matroska")},
        {QStringLiteral("webm"), QStringLiteral("video/webm")},
        {QStringLiteral("avi"), QStringLiteral("video/x-msvideo")},
        {QStringLiteral("3gp"), QStringLiteral("video/3gpp")},
        {QStringLiteral("mts"), QStringLiteral("video/mp2t")},
        {QStringLiteral("m2ts"), QStringLiteral("video/mp2t")},
        {QStringLiteral("ts"), QStringLiteral("video/mp2t")},
    };
    return map.value(extension.toLower(), QStringLiteral("application/octet-stream"));
}

FolderIndexer::TakenAt FolderIndexer::deriveTakenAtFromFilename(const QString &fileName)
{
    // Phone cameras encode the capture time in the filename. This is a much
    // better signal than mtime for files copied off a device, and it is what we
    // fall back to when a file carries no usable EXIF.
    static const QRegularExpression patterns[] = {
        // IMG_20250712_183045.jpg, VID_20250712_183045.mp4
        QRegularExpression(QStringLiteral("(?:IMG|VID|PANO|PXL|MVIMG|Screenshot)[_-](\\d{8})[_-](\\d{6})"),
                           QRegularExpression::CaseInsensitiveOption),
        // 20250712_183045.jpg
        QRegularExpression(QStringLiteral("(\\d{8})[_-](\\d{6})")),
        // Screenshot_2025-07-12-18-30-45.png
        QRegularExpression(QStringLiteral("(\\d{4})-(\\d{2})-(\\d{2})[-_ ](\\d{2})[-_](\\d{2})[-_](\\d{2})")),
    };

    QRegularExpressionMatch match = patterns[0].match(fileName);
    if (match.hasMatch()) {
        const QDateTime parsed =
            QDateTime::fromString(match.captured(1) + match.captured(2), QStringLiteral("yyyyMMddHHmmss"));
        if (parsed.isValid()) {
            return {parsed.toMSecsSinceEpoch(), QStringLiteral("filename")};
        }
    }

    match = patterns[1].match(fileName);
    if (match.hasMatch()) {
        const QDateTime parsed =
            QDateTime::fromString(match.captured(1) + match.captured(2), QStringLiteral("yyyyMMddHHmmss"));
        if (parsed.isValid()) {
            return {parsed.toMSecsSinceEpoch(), QStringLiteral("filename")};
        }
    }

    match = patterns[2].match(fileName);
    if (match.hasMatch()) {
        const QDateTime parsed =
            QDateTime::fromString(match.captured(1) + match.captured(2) + match.captured(3)
                                      + QLatin1Char('-') + match.captured(4) + match.captured(5)
                                      + match.captured(6),
                                  QStringLiteral("yyyyMMdd-HHmmss"));
        if (parsed.isValid()) {
            return {parsed.toMSecsSinceEpoch(), QStringLiteral("filename")};
        }
    }

    return {0, QString()};
}

FolderIndexer::Extracted FolderIndexer::extractOne(const QFileInfo &info)
{
    Extracted out;
    out.info = info;
    out.video = isVideoExtension(info.suffix());

    // PRD 8 fallback chain: EXIF -> filename pattern -> file mtime. The winning
    // source is persisted so the value can be audited later.
    ExifReader::Metadata exif;
    if (!out.video)
        exif = ExifReader::read(info.absoluteFilePath());

    TakenAt taken;
    if (exif.takenAtMs != 0) {
        taken = {exif.takenAtMs, QStringLiteral("exif")};
    } else {
        taken = deriveTakenAtFromFilename(info.fileName());
        if (taken.epochMs == 0) {
            taken = {info.lastModified().toMSecsSinceEpoch(), QStringLiteral("mtime")};
        }
    }
    out.takenMs = taken.epochMs;
    out.takenSource = taken.source;

    // Dimensions come from EXIF when present; otherwise read just the header.
    // This is also what gives PNG/WebP/HEIC rows a width and height at all.
    int width = exif.width;
    int height = exif.height;
    if (width <= 0 || height <= 0) {
        QImageReader header(info.absoluteFilePath());
        header.setDecideFormatFromContent(true);
        const QSize size = header.size();
        if (size.isValid() && !size.isEmpty()) {
            width = size.width();
            height = size.height();
        }
    }

    out.width = width;
    out.height = height;
    out.orientation = exif.orientation;
    out.make = exif.cameraMake;
    out.model = exif.cameraModel;
    out.lens = exif.lensModel;
    out.iso = exif.iso;
    out.fNumber = exif.fNumber;
    out.exposure = formatExposure(exif.exposureTimeSeconds);
    out.focal = exif.focalLengthMm;
    out.hasGps = exif.hasGps;
    out.lat = exif.latitude;
    out.lon = exif.longitude;
    return out;
}

qint64 FolderIndexer::ensureRoot(const QString &folder, bool synthetic)
{
    const QString normalised = QDir::cleanPath(folder);

    QSqlQuery select(m_database->connection());
    select.prepare(QStringLiteral("SELECT id FROM roots WHERE path = ?"));
    select.addBindValue(normalised);
    if (select.exec() && select.next()) {
        return select.value(0).toLongLong();
    }

    QSqlQuery insert(m_database->connection());
    insert.prepare(QStringLiteral(
        "INSERT INTO roots(path, enabled, is_synthetic, added_at) VALUES(?, 1, ?, ?)"));
    insert.addBindValue(normalised);
    insert.addBindValue(synthetic ? 1 : 0);
    insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!insert.exec()) {
        qWarning("kin-galeri: could not create library root '%s': %s", qUtf8Printable(normalised),
                 qUtf8Printable(insert.lastError().text()));
        return -1;
    }
    return insert.lastInsertId().toLongLong();
}

FolderIndexer::Result FolderIndexer::run(const QString &folder, const std::atomic_bool *cancel)
{
    Result result;
    result.folder = folder;

    QElapsedTimer timer;
    timer.start();

    if (!m_database) {
        result.error = QStringLiteral("No database attached");
        return result;
    }

    const QFileInfo folderInfo(folder);
    if (!folderInfo.exists() || !folderInfo.isDir()) {
        result.error = QStringLiteral("'%1' is not a directory").arg(folder);
        return result;
    }

    const qint64 rootId = ensureRoot(folderInfo.absoluteFilePath(), false);
    if (rootId < 0) {
        result.error = QStringLiteral("Could not register library root '%1'").arg(folder);
        return result;
    }

    QString lastFile;
    bool inTransaction = false;
    QSet<QString> seen;

    auto flushBatch = [&]() -> bool {
        if (!inTransaction) {
            return true;
        }
        QString error;
        const bool ok = m_database->commit(&error);
        inTransaction = false;
        if (!ok) {
            result.error = QStringLiteral("Commit failed: %1").arg(error);
        }
        return ok;
    };

    auto beginIfNeeded = [&]() -> bool {
        if (inTransaction) {
            return true;
        }
        QString beginError;
        if (!m_database->begin(&beginError)) {
            result.error = QStringLiteral("Could not start a transaction: %1").arg(beginError);
            return false;
        }
        inTransaction = true;
        return true;
    };

    QString upsertError;
    QSqlQuery upsert(m_database->connection());
    upsert.prepare(QStringLiteral(R"SQL(
INSERT INTO media(path, root_id, filename, ext, mime, size_bytes, mtime_ns, taken_at, taken_at_src,
                  width, height, orientation, camera_make, camera_model, lens_model,
                  iso, f_number, exposure_time, focal_length, gps_lat, gps_lon,
                  kind, source_album, is_synthetic, indexed_at)
VALUES(:path, :root, :filename, :ext, :mime, :size, :mtime, :taken, :takensrc,
       :width, :height, :orientation, :make, :model, :lens,
       :iso, :fnumber, :exposure, :focal, :lat, :lon,
       :kind, :album, 0, :indexed)
ON CONFLICT(path) DO UPDATE SET
    size_bytes    = excluded.size_bytes,
    mtime_ns      = excluded.mtime_ns,
    taken_at      = excluded.taken_at,
    taken_at_src  = excluded.taken_at_src,
    ext           = excluded.ext,
    mime          = excluded.mime,
    width         = excluded.width,
    height        = excluded.height,
    orientation   = excluded.orientation,
    camera_make   = excluded.camera_make,
    camera_model  = excluded.camera_model,
    lens_model    = excluded.lens_model,
    iso           = excluded.iso,
    f_number      = excluded.f_number,
    exposure_time = excluded.exposure_time,
    focal_length  = excluded.focal_length,
    gps_lat       = excluded.gps_lat,
    gps_lon       = excluded.gps_lon,
    source_album  = excluded.source_album,
    indexed_at    = excluded.indexed_at
WHERE media.mtime_ns <> excluded.mtime_ns OR media.size_bytes <> excluded.size_bytes
)SQL"));

    // Cheap existence+recency probe used to skip unchanged files entirely.
    QSqlQuery unchangedQuery(m_database->connection());
    unchangedQuery.prepare(
        QStringLiteral("SELECT mtime_ns, size_bytes FROM media WHERE path = ?"));

    // Metadata decoding (EXIF + image header) is the expensive part of a cold
    // scan and is independent per file, so it runs on a small pool. Only the
    // upserts stay on this thread: SQLite is single-writer by design here.
    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, std::min(kMaxExtractThreads, QThread::idealThreadCount())));

    QVector<QFileInfo> pending;
    pending.reserve(kExtractBatchSize);
    int writesSinceFlush = 0;
    bool cancelled = false;

    auto drainPending = [&]() -> bool {
        if (pending.isEmpty()) {
            return true;
        }

        QVector<QFileInfo> batch;
        batch.swap(pending);
        pending.reserve(kExtractBatchSize);

        const QList<Extracted> extracted =
            QtConcurrent::mapped(&pool, batch,
                                 [](const QFileInfo &info) { return extractOne(info); })
                .results();

        if (!beginIfNeeded()) {
            return false;
        }

        for (const Extracted &item : extracted) {
            const QFileInfo &info = item.info;

            upsert.bindValue(QStringLiteral(":path"), info.absoluteFilePath());
            upsert.bindValue(QStringLiteral(":root"), rootId);
            upsert.bindValue(QStringLiteral(":filename"), info.fileName());
            upsert.bindValue(QStringLiteral(":ext"), info.suffix().toLower());
            upsert.bindValue(QStringLiteral(":mime"), guessMimeType(info.suffix()));
            upsert.bindValue(QStringLiteral(":size"), info.size());
            upsert.bindValue(QStringLiteral(":mtime"), info.lastModified().toMSecsSinceEpoch() * 1000000LL);
            upsert.bindValue(QStringLiteral(":taken"), item.takenMs);
            upsert.bindValue(QStringLiteral(":takensrc"), item.takenSource);
            upsert.bindValue(QStringLiteral(":kind"),
                             item.video ? QStringLiteral("video") : QStringLiteral("image"));
            upsert.bindValue(QStringLiteral(":album"),
                             SourceAlbum::detect(info.absoluteFilePath(), info.fileName()));
            upsert.bindValue(QStringLiteral(":indexed"), QDateTime::currentMSecsSinceEpoch());

            upsert.bindValue(QStringLiteral(":width"), item.width);
            upsert.bindValue(QStringLiteral(":height"), item.height);
            // The schema defaults orientation to 1 ("normal"); 0 would mean
            // "unknown" and is not what downstream consumers expect.
            upsert.bindValue(QStringLiteral(":orientation"), item.orientation > 0 ? item.orientation : 1);
            upsert.bindValue(QStringLiteral(":make"), item.make.isEmpty() ? QVariant() : item.make);
            upsert.bindValue(QStringLiteral(":model"), item.model.isEmpty() ? QVariant() : item.model);
            upsert.bindValue(QStringLiteral(":lens"), item.lens.isEmpty() ? QVariant() : item.lens);
            upsert.bindValue(QStringLiteral(":iso"), item.iso > 0 ? QVariant(item.iso) : QVariant());
            upsert.bindValue(QStringLiteral(":fnumber"), item.fNumber > 0.0 ? QVariant(item.fNumber) : QVariant());
            upsert.bindValue(QStringLiteral(":exposure"), item.exposure);
            upsert.bindValue(QStringLiteral(":focal"), item.focal > 0.0 ? QVariant(item.focal) : QVariant());
            upsert.bindValue(QStringLiteral(":lat"), item.hasGps ? QVariant(item.lat) : QVariant());
            upsert.bindValue(QStringLiteral(":lon"), item.hasGps ? QVariant(item.lon) : QVariant());

            if (!upsert.exec()) {
                ++result.failed;
                if (upsertError.isEmpty()) {
                    upsertError = upsert.lastError().text();
                }
                continue;
            }

            // ON CONFLICT ... DO UPDATE ... WHERE yields 0 affected rows when the
            // file is unchanged, and 1 for both insert and update. SQLite folds
            // those two together here, so we report them as one bucket.
            if (upsert.numRowsAffected() == 0) {
                ++result.unchanged;
            } else {
                ++result.upserted;
                ++writesSinceFlush;
            }
        }

        if (writesSinceFlush >= kBatchSize) {
            if (!flushBatch()) {
                return false;
            }
            writesSinceFlush = 0;
            Q_EMIT progress(result.scanned, result.upserted, lastFile);
        }
        return true;
    };

    // Explicit stack instead of QDirIterator because the walk has to *prune*
    // ignored subtrees, which QDirIterator cannot do. Symlinked directories are
    // never descended into: a home directory can contain links back into itself
    // or to other roots, which would duplicate rows and never terminate.
    QStringList directoryStack;
    directoryStack.append(folderInfo.absoluteFilePath());

    // Only maintained when following symlinks; guards against a link that points
    // back into an already-walked tree (which would otherwise loop forever).
    QSet<QString> visitedDirectories;
    if (m_scanOptions.followSymlinks) {
        visitedDirectories.insert(folderInfo.canonicalFilePath());
    }
    const scan::Options prune = m_scanOptions;

    while (!directoryStack.isEmpty()) {
        if (cancel && cancel->load()) {
            cancelled = true;
            result.error = QStringLiteral("cancelled");
            break;
        }

        const QString directory = directoryStack.takeLast();
        const QFileInfoList entries =
            QDir(directory).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QFileInfo &entry : entries) {
            if (cancel && cancel->load()) {
                cancelled = true;
                result.error = QStringLiteral("cancelled");
                break;
            }

            if (entry.isSymLink()) {
                // QFileInfo follows the link, so this catches both dangling links
                // and links to directories. File links are indexed under their
                // link path so the row matches what the user sees; directory
                // links are only descended into when the user opted in.
                if (!entry.exists()) {
                    continue;
                }
                if (entry.isDir()) {
                    if (!m_scanOptions.followSymlinks) {
                        continue;
                    }
                } else if (!entry.isFile()) {
                    continue;
                }
            }

            if (entry.isDir()) {
                if (scan::isIgnoredDirectory(entry.absoluteFilePath(), prune)) {
                    continue;
                }
                if (m_scanOptions.followSymlinks) {
                    const QString canonical = entry.canonicalFilePath();
                    if (canonical.isEmpty() || visitedDirectories.contains(canonical)) {
                        continue;
                    }
                    visitedDirectories.insert(canonical);
                }
                directoryStack.append(entry.absoluteFilePath());
                continue;
            }

            if (!entry.isFile()) {
                continue;
            }

            if (!isSupportedExtension(entry.suffix())) {
                continue;
            }

            ++result.scanned;
            lastFile = entry.absoluteFilePath();

            // `seen` drives the deletion reconcile after the loop, so every
            // supported file that still exists has to land here, even the cheap
            // skipped ones.
            seen.insert(entry.absoluteFilePath());

            // Incremental scan (PRD F-1.3): an unchanged mtime+size means none of
            // the expensive work (EXIF parse, header read) is needed. Without this
            // a watcher-triggered rescan would re-read every file's metadata.
            const qint64 mtimeNs = entry.lastModified().toMSecsSinceEpoch() * 1000000LL;
            unchangedQuery.bindValue(0, entry.absoluteFilePath());
            if (unchangedQuery.exec() && unchangedQuery.next()
                && unchangedQuery.value(0).toLongLong() == mtimeNs
                && unchangedQuery.value(1).toLongLong() == entry.size()) {
                ++result.unchanged;
                continue;
            }

            pending.append(entry);

            if (pending.size() >= kExtractBatchSize && !drainPending()) {
                cancelled = true;
                break;
            }

            // Keep the UI's "seen / indexed" counter moving during a long walk,
            // not only on transaction boundaries.
            if (result.scanned % 2048 == 0) {
                Q_EMIT progress(result.scanned, result.upserted, lastFile);
            }
        }

        if (cancelled) {
            break;
        }
    }

    if (!cancelled) {
        drainPending();
    }

    // A cancelled run flushes what it has but must not reconcile deletions: it
    // has not seen the whole tree.
    flushBatch();

    // Reconcile deletions: rows whose file no longer exists under this root are
    // dropped so the library stops showing ghosts (PRD F-1.3 "hilang"). Only run
    // it after a complete scan.
    //
    // A TEMP table of seen paths beats the previous "load every row, then DELETE
    // one by one" pass: it is O(rows) instead of O(rows * delete), and it stays
    // in SQLite's page cache. Reconcile also runs when the tree became empty -
    // otherwise an emptied folder would keep its ghosts forever.
    if (result.error.isEmpty()) {
        QString reconcileError;
        if (!m_database->begin(&reconcileError)) {
            result.error = QStringLiteral("Could not start reconciliation: %1").arg(reconcileError);
        } else {
            m_database->exec(QStringLiteral(
                "CREATE TEMP TABLE IF NOT EXISTS scan_seen(path TEXT PRIMARY KEY)"));

            if (!seen.isEmpty()) {
                QSqlQuery insertSeen(m_database->connection());
                insertSeen.prepare(QStringLiteral("INSERT OR IGNORE INTO scan_seen(path) VALUES(?)"));
                for (const QString &path : seen) {
                    insertSeen.bindValue(0, path);
                    insertSeen.exec();
                }
            }

            QSqlQuery dropGone(m_database->connection());
            dropGone.prepare(QStringLiteral(
                "DELETE FROM media WHERE root_id = ? AND is_synthetic = 0 "
                "AND path NOT IN (SELECT path FROM scan_seen)"));
            dropGone.addBindValue(rootId);
            if (!dropGone.exec()) {
                result.error = QStringLiteral("Reconciliation failed: %1")
                                   .arg(dropGone.lastError().text());
            }

            m_database->exec(QStringLiteral("DROP TABLE IF EXISTS scan_seen"));
            m_database->commit(&reconcileError);
        }
    }

    if (!upsertError.isEmpty()) {
        result.error = upsertError;
    }

    result.elapsedMs = timer.elapsed();
    Q_EMIT progress(result.scanned, result.upserted, lastFile);
    return result;
}

} // namespace kg
