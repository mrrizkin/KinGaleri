#include "core/db/Database.h"
#include "core/grouping/EventClusterer.h"
#include "core/grouping/GeoClusterer.h"
#include "core/grouping/GroupingService.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace kg;

namespace {

bool openLibrary(QTemporaryDir &dir, std::unique_ptr<Database> &db, QString *error)
{
    if (!dir.isValid()) {
        *error = QStringLiteral("temporary directory could not be created");
        return false;
    }
    db = std::make_unique<Database>(dir.filePath(QStringLiteral("library.db")));
    return db->open(error) && db->migrate(error);
}

/// Inserts one media row. `gpsValid` adds coordinates, `takenAt` a capture time.
bool insertMedia(Database &db, qint64 rootId, const QString &name, qint64 takenAt,
                 bool gpsValid, double lat, double lon, QString *error)
{
    QSqlQuery query(db.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO media(path, root_id, filename, ext, mime, source_album, taken_at, "
        "                  gps_lat, gps_lon) "
        "VALUES(?, ?, ?, 'jpg', 'image/jpeg', 'Camera', ?, ?, ?)"));
    query.addBindValue(QStringLiteral("/lib/") + name);
    query.addBindValue(rootId);
    query.addBindValue(name);
    query.addBindValue(takenAt);
    query.addBindValue(gpsValid ? QVariant(lat) : QVariant());
    query.addBindValue(gpsValid ? QVariant(lon) : QVariant());
    if (!query.exec()) {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

qint64 makeRoot(Database &db, QString *error)
{
    QSqlQuery root(db.connection());
    root.prepare(QStringLiteral(
        "INSERT INTO roots(path, enabled, is_synthetic, added_at) VALUES('/lib', 1, 0, ?)"));
    root.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!root.exec()) {
        *error = root.lastError().text();
        return -1;
    }
    return root.lastInsertId().toLongLong();
}

} // namespace

class TestGrouping : public QObject
{
    Q_OBJECT

private slots:
    void haversineMatchesKnownDistance();
    void clusterMergesNearbyPoints();
    void clusterSeparatesDistantPoints();
    void labelUsesBundledCity();
    void eventsSplitOnTimeGap();
    void eventsSplitOnPlaceJump();
    void eventsAreOrderedOldestFirst();
    void groupingServiceWritesPlacesAndEvents();
};

void TestGrouping::haversineMatchesKnownDistance()
{
    // One degree of latitude is ~111 km everywhere.
    const double oneDegree = GeoClusterer::haversineMeters(0.0, 0.0, 1.0, 0.0);
    QVERIFY(oneDegree > 110000.0 && oneDegree < 112000.0);
    QCOMPARE(GeoClusterer::haversineMeters(10.0, 10.0, 10.0, 10.0), 0.0);
}

void TestGrouping::clusterMergesNearbyPoints()
{
    // ~40 m apart: same place.
    const QVector<GeoPoint> points = {
        {1, -6.2000, 106.8167},
        {2, -6.2003, 106.8167},
    };
    const QVector<PlaceCluster> clusters = GeoClusterer::cluster(points);
    QCOMPARE(clusters.size(), 1);
    QCOMPARE(clusters.first().mediaIds.size(), 2);
    // The cover is the lowest media id, which is stable across rebuilds.
    QCOMPARE(clusters.first().coverMediaId, 1LL);
}

void TestGrouping::clusterSeparatesDistantPoints()
{
    const QVector<GeoPoint> points = {
        {1, -6.2000, 106.8167}, // Jakarta
        {2, -6.9175, 107.6191}, // Bandung, ~120 km away
    };
    const QVector<PlaceCluster> clusters = GeoClusterer::cluster(points);
    QCOMPARE(clusters.size(), 2);
    for (const PlaceCluster &cluster : clusters)
        QCOMPARE(cluster.mediaIds.size(), 1);
}

void TestGrouping::labelUsesBundledCity()
{
    QString country;
    const QString name = GeoClusterer::nearestCityName(-6.2, 106.8167, 50.0, &country);
    QCOMPARE(name, QStringLiteral("Jakarta"));
    QCOMPARE(country, QStringLiteral("ID"));

    // The fallback label carries the coordinates when nothing is near.
    const QString label = GeoClusterer::labelFor(-45.0, -120.0, 10.0);
    QVERIFY(label.contains(QLatin1Char(',')));
    QVERIFY(label.startsWith(QStringLiteral("-45")));
}

void TestGrouping::eventsSplitOnTimeGap()
{
    const qint64 base = QDateTime(QDate(2026, 7, 12), QTime(9, 0)).toMSecsSinceEpoch();
    QVector<EventPoint> points = {
        {1, base, false, 0, 0},
        {2, base + 60 * 60 * 1000, false, 0, 0},
        {3, base + 10 * 60 * 60 * 1000, false, 0, 0},
    };
    const QVector<EventCluster> events = EventClusterer::cluster(points);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).mediaIds.size(), 2);
    QCOMPARE(events.at(1).mediaIds.size(), 1);
}

void TestGrouping::eventsSplitOnPlaceJump()
{
    const qint64 base = QDateTime(QDate(2026, 7, 12), QTime(9, 0)).toMSecsSinceEpoch();
    QVector<EventPoint> points = {
        {1, base, true, -6.2, 106.8},
        {2, base + 60 * 60 * 1000, true, -6.3, 106.8},             // still Jakarta
        {3, base + 2 * 60 * 60 * 1000, true, -6.9175, 107.6191},   // Bandung, > 20 km jump
    };
    const QVector<EventCluster> events = EventClusterer::cluster(points);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).mediaIds.size(), 2);
    QCOMPARE(events.at(1).mediaIds.size(), 1);
}

void TestGrouping::eventsAreOrderedOldestFirst()
{
    const qint64 base = QDateTime(QDate(2026, 1, 1), QTime(8, 0)).toMSecsSinceEpoch();
    QVector<EventPoint> points = {
        {3, base + 48 * 60 * 60 * 1000, false, 0, 0},
        {1, base, false, 0, 0},
        {2, base + 24 * 60 * 60 * 1000, false, 0, 0},
    };
    const QVector<EventCluster> events = EventClusterer::cluster(points);
    QCOMPARE(events.size(), 3);
    QVERIFY(events.at(0).startAt < events.at(1).startAt);
    QVERIFY(events.at(1).startAt < events.at(2).startAt);
    QVERIFY(events.at(0).mediaIds.contains(1));
}

void TestGrouping::groupingServiceWritesPlacesAndEvents()
{
    QTemporaryDir dir;
    std::unique_ptr<Database> db;
    QString error;
    QVERIFY2(openLibrary(dir, db, &error), qUtf8Printable(error));

    const qint64 rootId = makeRoot(*db, &error);
    QVERIFY2(rootId > 0, qUtf8Printable(error));

    const qint64 base = QDateTime(QDate(2026, 7, 12), QTime(9, 0)).toMSecsSinceEpoch();
    QVERIFY2(insertMedia(*db, rootId, QStringLiteral("a.jpg"), base, true, -6.2000, 106.8167, &error),
             qUtf8Printable(error));
    QVERIFY2(insertMedia(*db, rootId, QStringLiteral("b.jpg"), base + 60000, true, -6.2003, 106.8167,
                         &error),
             qUtf8Printable(error));
    QVERIFY2(insertMedia(*db, rootId, QStringLiteral("c.jpg"), base + 60000, false, 0, 0, &error),
             qUtf8Printable(error));

    const GroupingService::Result result = GroupingService::rebuild(*db);
    QVERIFY2(result.error.isEmpty(), qUtf8Printable(result.error));
    QCOMPARE(result.places, 1);
    QCOMPARE(result.mediaInPlaces, 2);
    QCOMPARE(result.events, 1);
    QCOMPARE(result.mediaInEvents, 3);

    QSqlQuery name(db->connection());
    QVERIFY(name.exec(QStringLiteral("SELECT name FROM places")));
    QVERIFY(name.next());
    QCOMPARE(name.value(0).toString(), QStringLiteral("Jakarta"));

    // Rebuilding is idempotent: no duplicate rows pile up.
    const GroupingService::Result again = GroupingService::rebuild(*db);
    QVERIFY2(again.error.isEmpty(), qUtf8Printable(again.error));
    QCOMPARE(again.places, 1);
    QCOMPARE(again.events, 1);
}

QTEST_GUILESS_MAIN(TestGrouping)
#include "tst_grouping.moc"
