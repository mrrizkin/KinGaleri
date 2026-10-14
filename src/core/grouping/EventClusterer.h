#pragma once

#include <QString>
#include <QVector>

namespace kg {

/// One media row considered for event clustering.
struct EventPoint
{
    qint64 mediaId = 0;
    qint64 takenAt = 0; ///< epoch milliseconds
    bool hasGps = false;
    double lat = 0.0;
    double lon = 0.0;
};

/// A run of media that belongs to one occasion.
struct EventCluster
{
    QString name;
    qint64 startAt = 0;
    qint64 endAt = 0;
    qint64 coverMediaId = -1;
    QVector<qint64> mediaIds;
};

/**
 * Splits a timeline into events.
 *
 * A new event starts when either
 *   - the gap since the previous item exceeds `gapMs`, or
 *   - both items carry GPS and they are further apart than `placeJumpMeters`,
 *     which catches a morning and an evening shoot on the same day but in two
 *     different cities.
 *
 * Pure time + distance, so it needs no model and no network (PRD F-2.10).
 */
class EventClusterer
{
public:
    /// Default: a six-hour gap ends an event; a 20 km GPS jump does too.
    static constexpr qint64 kDefaultGapMs = 6LL * 60 * 60 * 1000;
    static constexpr double kDefaultPlaceJumpMeters = 20000.0;

    /// Points are sorted internally; the caller may pass them in any order.
    /// Events are returned oldest first. Items without a valid timestamp are
    /// skipped.
    static QVector<EventCluster> cluster(QVector<EventPoint> points,
                                         qint64 gapMs = kDefaultGapMs,
                                         double placeJumpMeters = kDefaultPlaceJumpMeters);

    /// "12 Jul 2026" for a single day, "12–14 Jul 2026" for a span.
    static QString labelForRange(qint64 startMs, qint64 endMs);
};

} // namespace kg
