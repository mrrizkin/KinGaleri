#include "core/ml/FaceClusterer.h"
#include "core/ml/FacePipeline.h"

#include "core/db/Database.h"
#include "core/ml/FaceEngine.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <cstring>

namespace kg {
namespace ml {

namespace {

/// 128 floats as a little blob. Same-machine round-trip only, so endianness is
/// deliberately not normalised.
QByteArray toBlob(const QVector<float> &values)
{
    return QByteArray(reinterpret_cast<const char *>(values.constData()),
                      static_cast<qsizetype>(values.size() * sizeof(float)));
}

QVector<float> fromBlob(const QByteArray &blob)
{
    const int count = static_cast<int>(blob.size() / sizeof(float));
    QVector<float> values(count);
    if (count > 0)
        memcpy(values.data(), blob.constData(), static_cast<size_t>(count) * sizeof(float));
    return values;
}

} // namespace

FacePipeline::FacePipeline(Database &database)
    : m_database(database)
{
}

FacePipelineResult FacePipeline::run(const FacePipelineOptions &options,
                                     const std::function<bool()> &cancelled)
{
    FacePipelineResult result;

    FaceEngine engine;
    QString error;
    if (!engine.load(options.detectorPath, options.embedderPath, &error)) {
        result.error = error;
        return result;
    }
    const QString marker = FaceEngine::modelMarker();

    // ---- 1. images that have not been scanned under this model marker -------
    struct Pending
    {
        qint64 id;
        QString path;
    };
    QVector<Pending> pending;
    {
        QSqlQuery query(m_database.connection());
        query.prepare(QStringLiteral(
            "SELECT m.id, m.path FROM media m "
            "LEFT JOIN face_scans fs ON fs.media_id = m.id AND fs.model_marker = :marker "
            "WHERE m.deleted_at IS NULL AND m.is_synthetic = 0 AND m.kind = 'image' "
            "AND fs.media_id IS NULL ORDER BY m.id"));
        query.bindValue(QStringLiteral(":marker"), marker);
        if (!query.exec()) {
            result.error = query.lastError().text();
            return result;
        }
        while (query.next())
            pending.append({query.value(0).toLongLong(), query.value(1).toString()});
    }

    // ---- 2. detect + embed, one committed transaction per image -------------
    for (const Pending &item : pending) {
        if (cancelled && cancelled())
            break;

        QVector<Face> faces;
        QImage image;
        QImageReader reader(item.path);
        reader.setAutoTransform(true);
        image = reader.read();
        if (!image.isNull()) {
            if (image.width() > options.maxEdge || image.height() > options.maxEdge) {
                image = image.scaled(options.maxEdge, options.maxEdge, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
            }
            faces = engine.detect(image);
            for (Face &face : faces) {
                if (!engine.embed(image, face))
                    face.embedding.clear();
            }
        }

        if (!m_database.begin()) {
            result.error = QStringLiteral("could not start a transaction");
            return result;
        }
        QSqlQuery clear(m_database.connection());
        clear.prepare(QStringLiteral("DELETE FROM faces WHERE media_id = ?"));
        clear.addBindValue(item.id);
        if (!clear.exec()) {
            m_database.rollback();
            result.error = clear.lastError().text();
            return result;
        }

        int stored = 0;
        for (const Face &face : faces) {
            if (face.embedding.isEmpty())
                continue;
            QSqlQuery insert(m_database.connection());
            insert.prepare(QStringLiteral(
                "INSERT INTO faces(media_id, person_id, bbox_x, bbox_y, bbox_w, bbox_h, "
                "                  embedding, confidence, model_marker) "
                "VALUES(?, NULL, ?, ?, ?, ?, ?, ?, ?)"));
            insert.addBindValue(item.id);
            insert.addBindValue(face.box.x());
            insert.addBindValue(face.box.y());
            insert.addBindValue(face.box.width());
            insert.addBindValue(face.box.height());
            insert.addBindValue(toBlob(face.embedding));
            insert.addBindValue(face.score);
            insert.addBindValue(marker);
            if (!insert.exec()) {
                m_database.rollback();
                result.error = insert.lastError().text();
                return result;
            }
            ++stored;
        }

        QSqlQuery scan(m_database.connection());
        scan.prepare(QStringLiteral(
            "INSERT INTO face_scans(media_id, model_marker, face_count, analyzed_at) "
            "VALUES(?, ?, ?, ?) "
            "ON CONFLICT(media_id) DO UPDATE SET model_marker = excluded.model_marker, "
            "face_count = excluded.face_count, analyzed_at = excluded.analyzed_at"));
        scan.addBindValue(item.id);
        scan.addBindValue(marker);
        scan.addBindValue(stored);
        scan.addBindValue(QDateTime::currentMSecsSinceEpoch());
        if (!scan.exec()) {
            m_database.rollback();
            result.error = scan.lastError().text();
            return result;
        }
        if (!m_database.commit()) {
            m_database.rollback();
            result.error = QStringLiteral("could not commit a face batch");
            return result;
        }
        ++result.analyzed;
        result.detected += stored;
    }

    // ---- 3. load every face (old and new) and cluster ----------------------
    QVector<FaceSample> all;
    {
        QSqlQuery query(m_database.connection());
        query.prepare(QStringLiteral(
            "SELECT f.id, f.media_id, f.embedding FROM faces f "
            "JOIN media m ON m.id = f.media_id "
            "WHERE f.model_marker = :marker AND m.deleted_at IS NULL "
            "ORDER BY f.confidence DESC, f.id ASC"));
        query.bindValue(QStringLiteral(":marker"), marker);
        if (!query.exec()) {
            result.error = query.lastError().text();
            return result;
        }
        while (query.next()) {
            all.append({query.value(0).toLongLong(), query.value(1).toLongLong(),
                        fromBlob(query.value(2).toByteArray())});
        }
    }

    QVector<FaceCluster> clusters = clusterFaces(all, options.clusterThreshold);

    // ---- 4. carry user-assigned names across the re-cluster ----------------
    {
        QSqlQuery named(m_database.connection());
        if (named.exec(QStringLiteral(
                "SELECT p.name, f.embedding FROM people p JOIN faces f ON f.person_id = p.id "
                "WHERE p.name IS NOT NULL AND p.name <> '' GROUP BY p.id"))) {
            while (named.next()) {
                const QString name = named.value(0).toString();
                const QVector<float> embedding = fromBlob(named.value(1).toByteArray());
                if (name.isEmpty() || embedding.isEmpty())
                    continue;
                int best = -1;
                double bestScore = 0.5;
                for (int i = 0; i < clusters.size(); ++i) {
                    const float score = cosineSimilarity(embedding, clusters.at(i).centroid);
                    if (score > bestScore) {
                        bestScore = score;
                        best = i;
                    }
                }
                if (best >= 0 && clusters.at(best).name.isEmpty())
                    clusters[best].name = name;
            }
        }
    }

    // ---- 5. persist clusters ----------------------------------------------
    if (!m_database.begin()) {
        result.error = QStringLiteral("could not start a transaction");
        return result;
    }
    QSqlQuery reset(m_database.connection());
    if (!reset.exec(QStringLiteral("UPDATE faces SET person_id = NULL"))
        || !reset.exec(QStringLiteral("DELETE FROM people"))) {
        m_database.rollback();
        result.error = reset.lastError().text();
        return result;
    }

    QSqlQuery insertPerson(m_database.connection());
    insertPerson.prepare(QStringLiteral(
        "INSERT INTO people(name, cover_face_id, auto_cluster_key, media_count, cover_media_id) "
        "VALUES(?, NULL, NULL, ?, ?)"));
    QSqlQuery assign(m_database.connection());
    assign.prepare(QStringLiteral("UPDATE faces SET person_id = ? WHERE id = ?"));

    int peopleCount = 0;
    for (const FaceCluster &cluster : clusters) {
        if (cluster.faceIds.isEmpty())
            continue;
        const qint64 coverMedia = cluster.mediaIds.isEmpty() ? 0 : cluster.mediaIds.first();
        insertPerson.addBindValue(cluster.name.isEmpty() ? QVariant() : QVariant(cluster.name));
        insertPerson.addBindValue(cluster.mediaIds.size());
        insertPerson.addBindValue(coverMedia > 0 ? QVariant(coverMedia) : QVariant());
        if (!insertPerson.exec()) {
            m_database.rollback();
            result.error = insertPerson.lastError().text();
            return result;
        }
        const qint64 personId = insertPerson.lastInsertId().toLongLong();
        for (const qint64 faceId : cluster.faceIds) {
            assign.addBindValue(personId);
            assign.addBindValue(faceId);
            if (!assign.exec()) {
                m_database.rollback();
                result.error = assign.lastError().text();
                return result;
            }
        }
        ++peopleCount;
    }
    if (!m_database.commit()) {
        m_database.rollback();
        result.error = QStringLiteral("could not commit the people cluster");
        return result;
    }

    result.ok = true;
    result.totalFaces = all.size();
    result.people = peopleCount;
    return result;
}

} // namespace ml
} // namespace kg
