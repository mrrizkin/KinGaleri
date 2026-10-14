#pragma once

#include <QString>
#include <QVector>

namespace kg {

/// One geotagged media row, as read from `media`.
struct GeoPoint
{
    qint64 mediaId = 0;
    double lat = 0.0;
    double lon = 0.0;
};

/// A cluster of nearby geotagged media. Persisted into `places` + `media_places`.
struct PlaceCluster
{
    QString name;
    QString country;
    double lat = 0.0;
    double lon = 0.0;
    double radiusM = 0.0;
    qint64 coverMediaId = -1;
    QVector<qint64> mediaIds;
};

/**
 * Groups geotagged media into human-scale places.
 *
 * Two stages, both dependency-free:
 *   1. Union-find over a degree grid, joining any two points closer than
 *      `mergeMeters`. The grid only narrows the candidate set; the decision
 *      itself is a haversine distance, so the result does not depend on the
 *      grid origin.
 *   2. Each group is named from the bundled GeoNames city index (CC BY 4.0) or,
 *      when no city is near, from its coordinates.
 *
 * Deliberately no ML and no network: GPS EXIF is already stored by the indexer,
 * so Places works on a build without any model. See PRD F-2.3.
 */
class GeoClusterer
{
public:
    /// Great-circle distance in metres.
    static double haversineMeters(double lat1, double lon1, double lat2, double lon2);

    /// Clusters the points. Order of the output is stable (by first media id).
    static QVector<PlaceCluster> cluster(const QVector<GeoPoint> &points,
                                         double cellMeters = 300.0,
                                         double mergeMeters = 500.0);

    struct City
    {
        QString name;
        QString country;
        double lat = 0.0;
        double lon = 0.0;
        qint64 population = 0;
    };

    /// The bundled city table. Loaded once from `:/data/cities.tsv`, sorted by
    /// latitude. Empty when the resource is missing.
    static const QVector<City> &cities();

    /// Name of the nearest city within `maxKm`, or an empty string. `country`
    /// receives the ISO country code when a match is found.
    static QString nearestCityName(double lat, double lon, double maxKm, QString *country = nullptr);

    /// "City, CC" when a city is near, otherwise a "lat, lon" coordinate label.
    static QString labelFor(double lat, double lon, double maxKm = 50.0, QString *country = nullptr);
};

} // namespace kg
