#include "core/grouping/GeoClusterer.h"

#include <QFile>
#include <QHash>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace kg {

namespace {

constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
constexpr double kEarthRadiusM = 6371008.8;
constexpr double kMetersPerDegLat = 111320.0;
/// Coordinates are rounded to this many decimals for the fallback label, which
/// is ~11 m at the equator - far finer than the resolution of phone GPS.
constexpr int kLabelDecimals = 4;

struct CellKey
{
    qint64 lat = 0;
    qint64 lon = 0;

    bool operator==(const CellKey &other) const { return lat == other.lat && lon == other.lon; }
};

uint qHash(const CellKey &key, uint seed = 0)
{
    return ::qHash(key.lat, seed) ^ ::qHash(key.lon, seed + 0x9e3779b9u);
}

int findRoot(QVector<int> &parent, int index)
{
    while (parent.at(index) != index) {
        parent[index] = parent.at(parent.at(index));
        index = parent.at(index);
    }
    return index;
}

void unite(QVector<int> &parent, QVector<int> &rank, int a, int b)
{
    a = findRoot(parent, a);
    b = findRoot(parent, b);
    if (a == b)
        return;
    if (rank.at(a) < rank.at(b))
        std::swap(a, b);
    parent[b] = a;
    if (rank.at(a) == rank.at(b))
        ++rank[a];
}

} // namespace

double GeoClusterer::haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = (lat2 - lat1) * kDeg2Rad;
    const double dLon = (lon2 - lon1) * kDeg2Rad;
    const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0)
                     + std::cos(lat1 * kDeg2Rad) * std::cos(lat2 * kDeg2Rad)
                           * std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

QVector<PlaceCluster> GeoClusterer::cluster(const QVector<GeoPoint> &points,
                                            double cellMeters,
                                            double mergeMeters)
{
    QVector<PlaceCluster> result;
    if (points.isEmpty())
        return result;

    if (cellMeters <= 0.0)
        cellMeters = 300.0;
    if (mergeMeters <= 0.0)
        mergeMeters = 500.0;

    // A fixed degree cell whose latitude edge is `cellMeters`. Longitude edges
    // are the same in degrees, so a cell narrows with latitude; the neighbourhood
    // radius below compensates for that.
    const double cellDeg = cellMeters / kMetersPerDegLat;

    QHash<CellKey, QVector<int>> buckets;
    buckets.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const CellKey key{static_cast<qint64>(std::floor(points.at(i).lat / cellDeg)),
                          static_cast<qint64>(std::floor(points.at(i).lon / cellDeg))};
        buckets[key].append(i);
    }

    QVector<int> parent(points.size());
    QVector<int> rank(points.size(), 0);
    for (int i = 0; i < points.size(); ++i)
        parent[i] = i;

    for (int i = 0; i < points.size(); ++i) {
        const double lat = points.at(i).lat;
        const double cosLat = std::max(0.05, std::abs(std::cos(lat * kDeg2Rad)));
        // Enough rings that a cell at this latitude that is `mergeMeters` away in
        // longitude is still visited. The +1 covers the cell the point sits in.
        const int rings = static_cast<int>(std::ceil(mergeMeters / (kMetersPerDegLat * cosLat * cellDeg))) + 1;
        const qint64 baseLat = static_cast<qint64>(std::floor(lat / cellDeg));
        const qint64 baseLon = static_cast<qint64>(std::floor(points.at(i).lon / cellDeg));

        for (int dy = -rings; dy <= rings; ++dy) {
            for (int dx = -rings; dx <= rings; ++dx) {
                const CellKey key{baseLat + dy, baseLon + dx};
                const auto it = buckets.constFind(key);
                if (it == buckets.constEnd())
                    continue;
                for (const int j : *it) {
                    if (j <= i)
                        continue;
                    if (haversineMeters(lat, points.at(i).lon, points.at(j).lat, points.at(j).lon)
                        <= mergeMeters) {
                        unite(parent, rank, i, j);
                    }
                }
            }
        }
    }

    QHash<int, QVector<int>> groups;
    for (int i = 0; i < points.size(); ++i)
        groups[findRoot(parent, i)].append(i);

    result.reserve(groups.size());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QVector<int> &members = it.value();
        // Stable order so a rebuild produces the same clustering for the same
        // input, which matters for reproducible tests and for cover selection.
        std::sort(members.begin(), members.end(), [&points](int a, int b) {
            return points.at(a).mediaId < points.at(b).mediaId;
        });

        PlaceCluster cluster;
        double sumLat = 0.0;
        double sumLon = 0.0;
        for (const int index : members) {
            sumLat += points.at(index).lat;
            sumLon += points.at(index).lon;
        }
        cluster.lat = sumLat / members.size();
        cluster.lon = sumLon / members.size();

        double radius = 0.0;
        for (const int index : members) {
            radius = std::max(radius, haversineMeters(cluster.lat, cluster.lon,
                                                      points.at(index).lat, points.at(index).lon));
        }
        cluster.radiusM = radius;

        cluster.mediaIds.reserve(members.size());
        for (const int index : members) {
            const qint64 id = points.at(index).mediaId;
            cluster.mediaIds.append(id);
            if (cluster.coverMediaId < 0 || id < cluster.coverMediaId)
                cluster.coverMediaId = id;
        }

        QString country;
        cluster.name = labelFor(cluster.lat, cluster.lon, 50.0, &country);
        cluster.country = country;
        result.append(cluster);
    }

    return result;
}

const QVector<GeoClusterer::City> &GeoClusterer::cities()
{
    static const QVector<City> table = [] {
        QVector<City> cities;
        QFile file(QStringLiteral(":/data/cities.tsv"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return cities;

        while (!file.atEnd()) {
            const QByteArray line = file.readLine();
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            const QList<QByteArray> fields = line.trimmed().split('\t');
            if (fields.size() < 5)
                continue;
            City city;
            city.name = QString::fromUtf8(fields.at(0));
            city.country = QString::fromUtf8(fields.at(1));
            city.lat = fields.at(2).toDouble();
            city.lon = fields.at(3).toDouble();
            city.population = fields.at(4).toLongLong();
            cities.append(city);
        }

        std::sort(cities.begin(), cities.end(), [](const City &a, const City &b) {
            return a.lat < b.lat;
        });
        return cities;
    }();
    return table;
}

QString GeoClusterer::nearestCityName(double lat, double lon, double maxKm, QString *country)
{
    const QVector<City> &table = cities();
    if (table.isEmpty() || maxKm <= 0.0)
        return QString();

    // The table is sorted by latitude, so only a narrow band can be within range.
    const double latSpan = maxKm / (kMetersPerDegLat / 1000.0);
    const double minLat = lat - latSpan;
    const double maxLat = lat + latSpan;

    const auto begin = std::lower_bound(table.begin(), table.end(), minLat,
                                        [](const City &city, double value) { return city.lat < value; });

    QString best;
    QString bestCountry;
    double bestMeters = std::numeric_limits<double>::max();
    const double limit = maxKm * 1000.0;

    for (auto it = begin; it != table.end() && it->lat <= maxLat; ++it) {
        const double distance = haversineMeters(lat, lon, it->lat, it->lon);
        if (distance < bestMeters) {
            bestMeters = distance;
            best = it->name;
            bestCountry = it->country;
        }
    }

    if (best.isEmpty() || bestMeters > limit)
        return QString();

    if (country)
        *country = bestCountry;
    return best;
}

QString GeoClusterer::labelFor(double lat, double lon, double maxKm, QString *country)
{
    QString nearestCountry;
    const QString nearest = nearestCityName(lat, lon, maxKm, &nearestCountry);
    if (!nearest.isEmpty()) {
        if (country)
            *country = nearestCountry;
        if (nearestCountry.isEmpty() || nearestCountry == QLatin1String("ID"))
            return nearest;
        return QStringLiteral("%1, %2").arg(nearest, nearestCountry);
    }
    if (country)
        *country = QString();
    return QStringLiteral("%1, %2")
        .arg(lat, 0, 'f', kLabelDecimals)
        .arg(lon, 0, 'f', kLabelDecimals);
}

} // namespace kg
