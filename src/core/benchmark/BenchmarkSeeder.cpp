#include "core/benchmark/BenchmarkSeeder.h"

#include "core/db/Database.h"
#include "core/model/SourceAlbum.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include <random>

namespace kg {

namespace {

constexpr int kCommitEvery = 5000;

/// Album mix roughly matching a phone library. The first entry is the default.
struct AlbumProfile
{
    QString album;
    QString folder;
    int weight;
    QString prefix;
};

const QList<AlbumProfile> &albumProfiles()
{
    static const QList<AlbumProfile> profiles = {
        {QStringLiteral("Camera"), QStringLiteral("DCIM/Camera"), 58, QStringLiteral("IMG")},
        {QStringLiteral("Screenshots"), QStringLiteral("Pictures/Screenshots"), 16,
         QStringLiteral("Screenshot")},
        {QStringLiteral("WhatsApp"), QStringLiteral("WhatsApp Images"), 11, QStringLiteral("IMG")},
        {QStringLiteral("Telegram"), QStringLiteral("Telegram"), 7, QStringLiteral("photo")},
        {QStringLiteral("Downloads"), QStringLiteral("Downloads"), 5, QStringLiteral("download")},
        {QStringLiteral("Instagram"), QStringLiteral("Instagram"), 3, QStringLiteral("insta")},
    };
    return profiles;
}

const AlbumProfile &pickProfile(std::mt19937 &rng, const QList<AlbumProfile> &profiles)
{
    int total = 0;
    for (const AlbumProfile &p : profiles) {
        total += p.weight;
    }
    std::uniform_int_distribution<int> dist(0, total - 1);
    int roll = dist(rng);
    for (const AlbumProfile &p : profiles) {
        roll -= p.weight;
        if (roll < 0) {
            return p;
        }
    }
    return profiles.first();
}

} // namespace

BenchmarkSeeder::BenchmarkSeeder(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
}

int BenchmarkSeeder::countSyntheticRows(Database *database)
{
    if (!database) {
        return 0;
    }
    QSqlQuery query(database->connection());
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM media WHERE is_synthetic = 1")) && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

bool BenchmarkSeeder::clear(QString *error)
{
    if (!m_database) {
        if (error) {
            *error = QStringLiteral("No database attached");
        }
        return false;
    }
    if (!m_database->exec(QStringLiteral("DELETE FROM media WHERE is_synthetic = 1"), error)) {
        return false;
    }
    if (!m_database->exec(QStringLiteral("DELETE FROM roots WHERE is_synthetic = 1"), error)) {
        return false;
    }
    return true;
}

BenchmarkSeeder::Result BenchmarkSeeder::seed(int count)
{
    Result result;
    result.requested = count;

    if (!m_database) {
        result.error = QStringLiteral("No database attached");
        return result;
    }
    if (count <= 0) {
        result.error = QStringLiteral("Row count must be positive");
        return result;
    }

    QElapsedTimer timer;
    timer.start();

    // Idempotent: the benchmark library is disposable by definition.
    QString clearError;
    if (!clear(&clearError)) {
        result.error = QStringLiteral("Could not reset the benchmark library: %1").arg(clearError);
        return result;
    }

    // One root row for the whole synthetic library.
    const QString rootPath = Database::benchmarkRootPath();
    QSqlQuery rootInsert(m_database->connection());
    rootInsert.prepare(QStringLiteral(
        "INSERT INTO roots(path, enabled, is_synthetic, added_at) VALUES(?, 1, 1, ?)"));
    rootInsert.addBindValue(rootPath);
    rootInsert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!rootInsert.exec()) {
        result.error = QStringLiteral("Could not create the benchmark root: %1")
                           .arg(rootInsert.lastError().text());
        return result;
    }
    const qint64 rootId = rootInsert.lastInsertId().toLongLong();

    const QList<AlbumProfile> &profiles = albumProfiles();

    std::mt19937 rng(0xC0FFEEu); // fixed seed => reproducible benchmarks
    std::uniform_int_distribution<int> jitter(0, 4000);   // up to ~1h spread inside a day
    std::uniform_int_distribution<int> burst(2, 14);      // shots per "session"
    std::uniform_int_distribution<int> videoRoll(0, 99);
    std::uniform_int_distribution<int> dimRoll(0, 3);

    // Anchor at today's local midnight and walk backwards day by day.
    QDateTime cursor = QDateTime(QDate::currentDate().addDays(1), QTime(0, 0, 0), QTimeZone::LocalTime)
                           .addSecs(-1);
    const qint64 indexed = QDateTime::currentMSecsSinceEpoch();

    QSqlQuery insert(m_database->connection());
    insert.prepare(QStringLiteral(R"SQL(
INSERT INTO media(path, root_id, filename, ext, mime, size_bytes, mtime_ns, taken_at, taken_at_src,
                  width, height, duration_ms, kind, is_favorite, is_hidden, is_synthetic, indexed_at,
                  source_album)
VALUES(:path, :root, :filename, :ext, :mime, :size, :mtime, :taken, 'exif',
       :width, :height, :duration, :kind, :fav, 0, 1, :indexed,
       :album)
)SQL"));

    QString beginError;
    if (!m_database->begin(&beginError)) {
        result.error = QStringLiteral("Could not start a transaction: %1").arg(beginError);
        return result;
    }

    QString insertError;
    int inBatch = 0;

    for (int i = 0; i < count; ++i) {
        const AlbumProfile &profile = pickProfile(rng, profiles);
        const bool video = videoRoll(rng) < 8;
        const bool favourite = videoRoll(rng) < 6;

        const qint64 takenAt = cursor.toMSecsSinceEpoch() - jitter(rng) * 1000LL;

        const QString ext = video ? QStringLiteral("mp4") : QStringLiteral("jpg");
        const QString stem = QStringLiteral("%1_%2_%3")
                                 .arg(profile.prefix)
                                 .arg(QDateTime::fromMSecsSinceEpoch(takenAt).toString(QStringLiteral("yyyyMMdd_HHmmss")))
                                 .arg(i, 6, 10, QLatin1Char('0'));
        const QString filename = stem + QLatin1Char('.') + ext;
        const QString path = QStringLiteral("%1/%2/%3").arg(rootPath, profile.folder, filename);

        const int dimIndex = dimRoll(rng);
        const int width = dimIndex == 0 ? 4032 : (dimIndex == 1 ? 3000 : (dimIndex == 2 ? 1920 : 1440));
        const int height = width * 3 / 4;

        insert.bindValue(QStringLiteral(":path"), path);
        insert.bindValue(QStringLiteral(":root"), rootId);
        insert.bindValue(QStringLiteral(":filename"), filename);
        insert.bindValue(QStringLiteral(":ext"), ext);
        insert.bindValue(QStringLiteral(":mime"), video ? QStringLiteral("video/mp4") : QStringLiteral("image/jpeg"));
        insert.bindValue(QStringLiteral(":size"), video ? 8LL * 1024 * 1024 + (i % 500) * 4096
                                                        : 2LL * 1024 * 1024 + (i % 700) * 1024);
        insert.bindValue(QStringLiteral(":mtime"), takenAt * 1000000LL);
        insert.bindValue(QStringLiteral(":taken"), takenAt);
        insert.bindValue(QStringLiteral(":width"), width);
        insert.bindValue(QStringLiteral(":height"), height);
        insert.bindValue(QStringLiteral(":duration"), video ? 4000 + (i % 120) * 1000 : 0);
        insert.bindValue(QStringLiteral(":kind"), video ? QStringLiteral("video") : QStringLiteral("image"));
        // Derive it the same way the indexer does, rather than copying
        // profile.album, so the benchmark exercises real detection rules.
        insert.bindValue(QStringLiteral(":album"), SourceAlbum::detect(path, filename));
        insert.bindValue(QStringLiteral(":fav"), favourite ? 1 : 0);
        insert.bindValue(QStringLiteral(":indexed"), indexed);

        if (!insert.exec()) {
            if (insertError.isEmpty()) {
                insertError = insert.lastError().text();
            }
            break;
        }

        ++result.inserted;
        ++inBatch;

        if (inBatch >= kCommitEvery) {
            QString commitError;
            if (!m_database->commit(&commitError)) {
                insertError = QStringLiteral("Commit failed: %1").arg(commitError);
                break;
            }
            inBatch = 0;
            Q_EMIT progress(result.inserted, count);
            if (!m_database->begin(&beginError)) {
                insertError = QStringLiteral("Could not start a transaction: %1").arg(beginError);
                break;
            }
        }

        // Advance the clock by a "session" worth of shots. Bursts of 2..14 per
        // day keep the day-grouping realistic instead of one photo per day.
        if ((i + 1) % burst(rng) == 0) {
            cursor = cursor.addDays(-1);
            if (cursor.date().year() < QDate::currentDate().year() - 6) {
                cursor = QDateTime(QDate::currentDate().addDays(1), QTime(0, 0, 0), QTimeZone::LocalTime)
                             .addSecs(-1);
            }
        }
    }

    if (insertError.isEmpty() && inBatch > 0) {
        QString commitError;
        if (!m_database->commit(&commitError)) {
            insertError = QStringLiteral("Commit failed: %1").arg(commitError);
        }
    } else if (!insertError.isEmpty()) {
        m_database->rollback();
    }

    if (!insertError.isEmpty()) {
        result.error = insertError;
        result.inserted = countSyntheticRows(m_database);
    }

    result.elapsedMs = timer.elapsed();
    Q_EMIT progress(result.inserted, count);
    return result;
}

} // namespace kg
