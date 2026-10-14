#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/indexer/ScanPolicy.h"

#include <atomic>

class QFileInfo;

namespace kg {

class Database;

/**
 * Recursive folder scanner (PRD F-1). Synchronous by design: callers run it on a
 * worker thread (AppController uses QtConcurrent) and consume `progress` which Qt
 * queues onto the receiver's thread.
 *
 * M1 adds EXIF extraction through the dependency-free ExifReader, with the
 * PRD's fallback chain: EXIF -> filename pattern -> file mtime. `taken_at_src`
 * records which one won so the value can be audited and re-derived later.
 *
 * The scanner defaults to a home-directory library, so the walk prunes hidden
 * and junk directories (see ScanPolicy) and the expensive per-file metadata
 * reads are spread across a small worker pool before the (single-writer) upsert
 * loop. Ordering and the database stay on the caller's thread.
 */
class FolderIndexer : public QObject
{
    Q_OBJECT

public:
    struct Result
    {
        int scanned = 0;
        int upserted = 0;
        int unchanged = 0;
        int failed = 0;
        qint64 elapsedMs = 0;
        QString error;
        QString folder;

        bool ok() const { return error.isEmpty(); }
        int total() const { return upserted + unchanged; }
    };

    explicit FolderIndexer(Database *database, QObject *parent = nullptr);

    /// User-tunable walk behaviour, sourced from AppSettings by AppController.
    /// Aliases the shared pruning policy so the indexer and the watcher cannot
    /// drift apart.
    using ScanOptions = scan::Options;
    void setScanOptions(const ScanOptions &options) { m_scanOptions = options; }
    const ScanOptions &scanOptions() const { return m_scanOptions; }

    /// Scans `folder` and upserts every supported media file. `cancel` may be
    /// null; set it to true from another thread to abort early.
    Result run(const QString &folder, const std::atomic_bool *cancel = nullptr);

    /// Extensions we currently index, lower-case, without the leading dot.
    static QStringList supportedExtensions();
    static bool isVideoExtension(const QString &extension);
    static bool isSupportedExtension(const QString &extension);

    /// True when `directoryPath` should not be descended into (hidden or junk).
    /// Exposed so the watcher and tests share the exact same pruning rule.
    static bool isIgnoredDirectory(const QString &directoryPath);

    /// Overload honouring user-tunable pruning.
    static bool isIgnoredDirectory(const QString &directoryPath, const ScanOptions &options);

signals:
    void progress(int scanned, int inserted, const QString &currentFile);

private:
    struct TakenAt
    {
        qint64 epochMs = 0;
        QString source = QStringLiteral("mtime");
    };

    /// Everything the upsert needs for one file, filled off the DB thread.
    struct Extracted;

    /// Reads EXIF + dimensions for one file. Pure: safe to call concurrently.
    static Extracted extractOne(const QFileInfo &info);

    /// Capture date derived from the filename, or 0 when no pattern matches.
    static TakenAt deriveTakenAtFromFilename(const QString &fileName);

    static QString guessMimeType(const QString &extension);

    qint64 ensureRoot(const QString &folder, bool synthetic);

    Database *m_database = nullptr;
    QString m_error;
    ScanOptions m_scanOptions;
};

} // namespace kg
