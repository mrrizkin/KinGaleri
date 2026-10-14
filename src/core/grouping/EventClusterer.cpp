#include "core/grouping/EventClusterer.h"

#include "core/grouping/GeoClusterer.h"

#include <QDate>
#include <QDateTime>

#include <algorithm>

namespace kg {

namespace {

struct GpsSample
{
    bool valid = false;
    double lat = 0.0;
    double lon = 0.0;
};

} // namespace

QVector<EventCluster> EventClusterer::cluster(QVector<EventPoint> points,
                                              qint64 gapMs,
                                              double placeJumpMeters)
{
    QVector<EventCluster> events;
    if (gapMs <= 0)
        gapMs = kDefaultGapMs;
    if (placeJumpMeters <= 0.0)
        placeJumpMeters = kDefaultPlaceJumpMeters;

    points.erase(std::remove_if(points.begin(), points.end(),
                                [](const EventPoint &point) { return point.takenAt <= 0; }),
                 points.end());

    std::sort(points.begin(), points.end(), [](const EventPoint &a, const EventPoint &b) {
        if (a.takenAt != b.takenAt)
            return a.takenAt < b.takenAt;
        return a.mediaId < b.mediaId;
    });

    // GPS of the newest geotagged member of the event currently being built. A
    // photo without coordinates leaves it untouched, so a single untagged shot
    // never hides a real jump between two places.
    GpsSample anchor;

    for (const EventPoint &point : points) {
        bool startNew = events.isEmpty();
        if (!startNew) {
            const EventCluster &current = events.last();
            if (point.takenAt - current.endAt > gapMs)
                startNew = true;
            else if (point.hasGps && anchor.valid
                     && GeoClusterer::haversineMeters(point.lat, point.lon, anchor.lat, anchor.lon)
                            > placeJumpMeters)
                startNew = true;
        }

        if (startNew) {
            EventCluster event;
            event.startAt = point.takenAt;
            event.endAt = point.takenAt;
            event.coverMediaId = point.mediaId;
            event.mediaIds.append(point.mediaId);
            events.append(event);
            anchor = GpsSample{point.hasGps, point.lat, point.lon};
        } else {
            EventCluster &current = events.last();
            current.endAt = point.takenAt;
            current.mediaIds.append(point.mediaId);
            if (point.mediaId < current.coverMediaId)
                current.coverMediaId = point.mediaId;
            if (point.hasGps)
                anchor = GpsSample{true, point.lat, point.lon};
        }
    }

    return events;
}

QString EventClusterer::labelForRange(qint64 startMs, qint64 endMs)
{
    const QDate start = QDateTime::fromMSecsSinceEpoch(startMs).date();
    const QDate end = QDateTime::fromMSecsSinceEpoch(endMs).date();
    if (!start.isValid())
        return QString();
    if (!end.isValid() || start == end)
        return start.toString(QStringLiteral("d MMM yyyy"));
    if (start.year() == end.year() && start.month() == end.month()) {
        return QStringLiteral("%1–%2")
            .arg(start.day())
            .arg(end.toString(QStringLiteral("d MMM yyyy")));
    }
    return QStringLiteral("%1 – %2")
        .arg(start.toString(QStringLiteral("d MMM yyyy")), end.toString(QStringLiteral("d MMM yyyy")));
}

} // namespace kg
