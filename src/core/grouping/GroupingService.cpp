#include "core/grouping/GroupingService.h"

#include "core/db/Database.h"
#include "core/grouping/EventClusterer.h"
#include "core/grouping/GeoClusterer.h"

#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace kg {
namespace GroupingService {

namespace {

QString failure(const QSqlQuery &query)
{
    return query.lastError().text();
}

} // namespace

bool rebuildPlaces(Database &database, QString *error)
{
    QVector<GeoPoint> points;

    QSqlQuery read(database.connection());
    if (!read.exec(QStringLiteral(
            "SELECT id, gps_lat, gps_lon FROM media "
            "WHERE deleted_at IS NULL AND gps_lat IS NOT NULL AND gps_lon IS NOT NULL"))) {
        if (error)
            *error = failure(read);
        return false;
    }
    while (read.next()) {
        GeoPoint point;
        point.mediaId = read.value(0).toLongLong();
        point.lat = read.value(1).toDouble();
        point.lon = read.value(2).toDouble();
        points.append(point);
    }

    const QVector<PlaceCluster> clusters = GeoClusterer::cluster(points);

    if (!database.begin(error))
        return false;

    QSqlQuery clear(database.connection());
    // media_places cascades from places, but clearing both keeps the intent
    // explicit and works even if a future schema drops the cascade.
    if (!clear.exec(QStringLiteral("DELETE FROM media_places")) || !clear.exec(QStringLiteral("DELETE FROM places"))) {
        database.rollback();
        if (error)
            *error = failure(clear);
        return false;
    }

    QSqlQuery insertPlace(database.connection());
    if (!insertPlace.prepare(QStringLiteral(
            "INSERT INTO places (name, country, lat, lon, radius_m, media_count, cover_media_id) "
            "VALUES (:name, :country, :lat, :lon, :radius, :count, :cover)"))) {
        database.rollback();
        if (error)
            *error = failure(insertPlace);
        return false;
    }

    QSqlQuery insertLink(database.connection());
    if (!insertLink.prepare(QStringLiteral(
            "INSERT INTO media_places (media_id, place_id) VALUES (:media, :place)"))) {
        database.rollback();
        if (error)
            *error = failure(insertLink);
        return false;
    }

    for (const PlaceCluster &cluster : clusters) {
        insertPlace.bindValue(QStringLiteral(":name"), cluster.name);
        insertPlace.bindValue(QStringLiteral(":country"), cluster.country);
        insertPlace.bindValue(QStringLiteral(":lat"), cluster.lat);
        insertPlace.bindValue(QStringLiteral(":lon"), cluster.lon);
        insertPlace.bindValue(QStringLiteral(":radius"), cluster.radiusM);
        insertPlace.bindValue(QStringLiteral(":count"), cluster.mediaIds.size());
        insertPlace.bindValue(QStringLiteral(":cover"), cluster.coverMediaId);
        if (!insertPlace.exec()) {
            database.rollback();
            if (error)
                *error = failure(insertPlace);
            return false;
        }
        const qint64 placeId = insertPlace.lastInsertId().toLongLong();

        for (const qint64 mediaId : cluster.mediaIds) {
            insertLink.bindValue(QStringLiteral(":media"), mediaId);
            insertLink.bindValue(QStringLiteral(":place"), placeId);
            if (!insertLink.exec()) {
                database.rollback();
                if (error)
                    *error = failure(insertLink);
                return false;
            }
        }
    }

    if (!database.commit(error)) {
        database.rollback();
        return false;
    }
    return true;
}

bool rebuildEvents(Database &database, QString *error)
{
    QVector<EventPoint> points;

    QSqlQuery read(database.connection());
    if (!read.exec(QStringLiteral(
            "SELECT id, taken_at, gps_lat, gps_lon FROM media "
            "WHERE deleted_at IS NULL AND taken_at IS NOT NULL ORDER BY taken_at ASC, id ASC"))) {
        if (error)
            *error = failure(read);
        return false;
    }
    while (read.next()) {
        EventPoint point;
        point.mediaId = read.value(0).toLongLong();
        point.takenAt = read.value(1).toLongLong();
        point.hasGps = !read.value(2).isNull() && !read.value(3).isNull();
        point.lat = read.value(2).toDouble();
        point.lon = read.value(3).toDouble();
        points.append(point);
    }

    const QVector<EventCluster> events = EventClusterer::cluster(points);

    // A media id -> place id map, so an event can carry the place its first
    // geotagged member falls into. Only geotagged items are present.
    QHash<qint64, qint64> placeByMedia;
    QSqlQuery linkRead(database.connection());
    if (linkRead.exec(QStringLiteral("SELECT media_id, place_id FROM media_places"))) {
        while (linkRead.next())
            placeByMedia.insert(linkRead.value(0).toLongLong(), linkRead.value(1).toLongLong());
    }

    if (!database.begin(error))
        return false;

    QSqlQuery clear(database.connection());
    if (!clear.exec(QStringLiteral("DELETE FROM media_events")) || !clear.exec(QStringLiteral("DELETE FROM events"))) {
        database.rollback();
        if (error)
            *error = failure(clear);
        return false;
    }

    QSqlQuery insertEvent(database.connection());
    if (!insertEvent.prepare(QStringLiteral(
            "INSERT INTO events (name, start_at, end_at, place_id, media_count, cover_media_id) "
            "VALUES (:name, :start, :end, :place, :count, :cover)"))) {
        database.rollback();
        if (error)
            *error = failure(insertEvent);
        return false;
    }

    QSqlQuery insertLink(database.connection());
    if (!insertLink.prepare(QStringLiteral(
            "INSERT INTO media_events (media_id, event_id) VALUES (:media, :event)"))) {
        database.rollback();
        if (error)
            *error = failure(insertLink);
        return false;
    }

    for (const EventCluster &event : events) {
        // Auto title (localized) is a M5 idea, so the stored name stays empty and
        // the UI formats from start/end. A user rename will write here.
        insertEvent.bindValue(QStringLiteral(":name"), QVariant());
        insertEvent.bindValue(QStringLiteral(":start"), event.startAt);
        insertEvent.bindValue(QStringLiteral(":end"), event.endAt);

        qint64 placeId = 0;
        for (const qint64 mediaId : event.mediaIds) {
            const auto it = placeByMedia.constFind(mediaId);
            if (it != placeByMedia.constEnd()) {
                placeId = it.value();
                break;
            }
        }
        insertEvent.bindValue(QStringLiteral(":place"), placeId > 0 ? QVariant(placeId) : QVariant());
        insertEvent.bindValue(QStringLiteral(":count"), event.mediaIds.size());
        insertEvent.bindValue(QStringLiteral(":cover"), event.coverMediaId);
        if (!insertEvent.exec()) {
            database.rollback();
            if (error)
                *error = failure(insertEvent);
            return false;
        }
        const qint64 eventId = insertEvent.lastInsertId().toLongLong();

        for (const qint64 mediaId : event.mediaIds) {
            insertLink.bindValue(QStringLiteral(":media"), mediaId);
            insertLink.bindValue(QStringLiteral(":event"), eventId);
            if (!insertLink.exec()) {
                database.rollback();
                if (error)
                    *error = failure(insertLink);
                return false;
            }
        }
    }

    if (!database.commit(error)) {
        database.rollback();
        return false;
    }
    return true;
}

Result rebuild(Database &database)
{
    return rebuild(database, true, true);
}

Result rebuild(Database &database, bool places, bool events)
{
    Result result;
    QString error;
    // A disabled pass is cleared, not left stale: the user turned it off, so the
    // Places/Events pages should empty out rather than show yesterday's grouping.
    if (!places) {
        QSqlQuery clear(database.connection());
        if (!clear.exec(QStringLiteral("DELETE FROM media_places"))
            || !clear.exec(QStringLiteral("DELETE FROM places"))) {
            result.error = clear.lastError().text();
            return result;
        }
    } else if (!rebuildPlaces(database, &error)) {
        result.error = error;
        return result;
    }
    if (!events) {
        QSqlQuery clear(database.connection());
        if (!clear.exec(QStringLiteral("DELETE FROM media_events"))
            || !clear.exec(QStringLiteral("DELETE FROM events"))) {
            result.error = clear.lastError().text();
            return result;
        }
    } else if (!rebuildEvents(database, &error)) {
        result.error = error;
        return result;
    }

    QSqlQuery counts(database.connection());
    if (counts.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM places), (SELECT COUNT(*) FROM media_places), "
                                   "(SELECT COUNT(*) FROM events), (SELECT COUNT(*) FROM media_events)"))
        && counts.next()) {
        result.places = counts.value(0).toInt();
        result.mediaInPlaces = counts.value(1).toInt();
        result.events = counts.value(2).toInt();
        result.mediaInEvents = counts.value(3).toInt();
    }
    return result;
}

} // namespace GroupingService
} // namespace kg
