#include "core/ml/FaceClusterer.h"

#include "core/ml/VectorMath.h"

namespace kg::ml {

namespace {

/// Adds a member to a cluster and folds it into the running mean. The centroid is
/// re-normalised so it can still be compared by cosine.
void addToCluster(FaceCluster &cluster, const FaceSample &face)
{
    const int count = cluster.faceIds.size();
    if (count == 0) {
        cluster.centroid = face.embedding;
    } else {
        for (int i = 0; i < cluster.centroid.size(); ++i) {
            cluster.centroid[i] =
                (cluster.centroid.at(i) * count + face.embedding.at(i)) / (count + 1);
        }
        normalise(cluster.centroid);
    }
    cluster.faceIds.append(face.id);
    if (!cluster.mediaIds.contains(face.mediaId))
        cluster.mediaIds.append(face.mediaId);
}

} // namespace

QVector<FaceCluster> clusterFaces(const QVector<FaceSample> &faces, double threshold)
{
    QVector<FaceCluster> clusters;
    for (const FaceSample &face : faces) {
        int best = -1;
        double bestScore = threshold;
        for (int i = 0; i < clusters.size(); ++i) {
            const float score = cosineSimilarity(face.embedding, clusters.at(i).centroid);
            if (score > bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best < 0) {
            FaceCluster cluster;
            addToCluster(cluster, face);
            clusters.append(cluster);
        } else {
            addToCluster(clusters[best], face);
        }
    }
    return clusters;
}

} // namespace kg::ml
