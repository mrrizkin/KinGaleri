#pragma once

#include <QString>
#include <QVector>

namespace kg::ml {

/// One face as the clusterer needs it: its identity plus the L2-normalised
/// descriptor. Deliberately independent of FaceEngine so the grouping can be
/// tested (and reasoned about) without ONNX Runtime in the build.
struct FaceSample
{
    qint64 id = 0;
    qint64 mediaId = 0;
    QVector<float> embedding;
};

struct FaceCluster
{
    QVector<float> centroid;
    QVector<qint64> faceIds;
    QVector<qint64> mediaIds; ///< first entry is the highest-confidence cover
    QString name;
};

/**
 * Greedy cosine clustering: walk `faces` in order, join the nearest cluster
 * whose centroid similarity is above `threshold`, otherwise start a new one.
 *
 * Single pass on purpose. It is O(faces x clusters), and FacePipeline runs it on
 * the whole library at once because an identity is only defined by all of its
 * faces. The burden of precision therefore rests on `threshold`: with a loose
 * one, every additional face drags the running-mean centroid towards the
 * "average face" of the cluster, and large clusters of similar-looking people
 * start swallowing each other. See FacePipelineOptions::clusterThreshold for the
 * value and the LFW measurements behind it.
 */
QVector<FaceCluster> clusterFaces(const QVector<FaceSample> &faces, double threshold);

} // namespace kg::ml
