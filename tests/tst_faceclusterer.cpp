#include "core/ml/FaceClusterer.h"
#include "core/ml/VectorMath.h"

#include <QtTest>

#include <cmath>

using namespace kg::ml;

namespace {

/// Unit vector at `degrees` in the XY plane. Handy because the cosine of two of
/// them is just the cosine of the angle between them, so a test can state the
/// similarity it means instead of hand-writing coordinates.
QVector<float> atDegrees(double degrees)
{
    const double radians = degrees * M_PI / 180.0;
    return {static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians)), 0.0f, 0.0f};
}

/// Unit vector whose cosine with atDegrees(0) is the asked-for similarity, so a
/// test can say "faces this similar" without hand-solving for the angle.
QVector<float> atCosine(double cosine)
{
    const double radians = std::acos(cosine);
    return {static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians)), 0.0f, 0.0f};
}

FaceSample sample(qint64 id, qint64 mediaId, QVector<float> embedding)
{
    return FaceSample{id, mediaId, std::move(embedding)};
}

double norm(const QVector<float> &v)
{
    double sum = 0.0;
    for (float x : v)
        sum += static_cast<double>(x) * x;
    return std::sqrt(sum);
}

} // namespace

class TestFaceClusterer : public QObject
{
    Q_OBJECT

private slots:
    void unrelatedFacesGetTheirOwnCluster();
    void samePersonMergesAboveTheThreshold();
    void thresholdDecidesWhetherSimilarFacesMerge();
    void joinsTheNearestClusterNotTheFirst();
    void centroidStaysUnitLength();
    void mediaIdsAreUniqueAndKeepTheCoverFirst();
};

void TestFaceClusterer::unrelatedFacesGetTheirOwnCluster()
{
    const QVector<FaceSample> faces{
        sample(1, 10, atDegrees(0)),
        sample(2, 20, atDegrees(90)),
        sample(3, 30, atDegrees(180)),
    };
    const auto clusters = clusterFaces(faces, 0.50);
    QCOMPARE(clusters.size(), 3);
    for (const auto &cluster : clusters)
        QCOMPARE(cluster.faceIds.size(), 1);
}

void TestFaceClusterer::samePersonMergesAboveTheThreshold()
{
    // Two photos of the same face with cosine 0.80. This is the order of the
    // real cross-year pair in the smoke library (0.802), so it keeps the merge
    // the threshold exists to allow.
    const QVector<FaceSample> pair{sample(1, 10, atDegrees(0)),
                                   sample(2, 20, atCosine(0.80))};
    QVERIFY(qAbs(cosineSimilarity(pair.at(0).embedding, pair.at(1).embedding) - 0.80f) < 1e-5f);
    const auto clusters = clusterFaces(pair, 0.50);
    QCOMPARE(clusters.size(), 1);
    QCOMPARE(clusters.at(0).faceIds.size(), 2);
}

void TestFaceClusterer::thresholdDecidesWhetherSimilarFacesMerge()
{
    // Cosine 0.45 is the band the default threshold moved through: under 0.40 the
    // pair merged, at 0.50 it no longer does. On LFW that single change is what
    // stopped large clusters of similar-looking people from absorbing each other
    // (purity 76.4% -> 98.4%, pairwise F1 90.5% -> 96.3%): see
    // scripts/evaluate-lfw.py and docs/M3-REPORT.md.
    const QVector<FaceSample> pair{sample(1, 10, atDegrees(0)),
                                   sample(2, 20, atCosine(0.45))};
    QVERIFY(qAbs(cosineSimilarity(pair.at(0).embedding, pair.at(1).embedding) - 0.45f) < 1e-5f);

    QCOMPARE(clusterFaces(pair, 0.40).size(), 1);
    QCOMPARE(clusterFaces(pair, 0.50).size(), 2);
}

void TestFaceClusterer::joinsTheNearestClusterNotTheFirst()
{
    // 0 -> cluster 0; 80 (cosine 0.17 with 0) -> cluster 1; 55 is cosine 0.90
    // from the first face of cluster 1 and 0.57 from cluster 0, so it must land
    // in cluster 1 even though cluster 0 comes first in the scan.
    const auto clusters = clusterFaces({sample(1, 10, atDegrees(0)),
                                        sample(2, 20, atDegrees(80)),
                                        sample(3, 30, atDegrees(55))},
                                       0.50);
    QCOMPARE(clusters.size(), 2);
    QCOMPARE(clusters.at(0).faceIds, QVector<qint64>{1});
    QCOMPARE(clusters.at(1).faceIds, QVector<qint64>({2, 3}));
}

void TestFaceClusterer::centroidStaysUnitLength()
{
    // The centroid is only ever compared by cosine, so folding in a member must
    // re-normalise it; a drifting norm would silently change every later score.
    const auto clusters = clusterFaces({sample(1, 10, atDegrees(0)),
                                        sample(2, 20, atDegrees(10)),
                                        sample(3, 30, atDegrees(-10)),
                                        sample(4, 40, atDegrees(5))},
                                       0.50);
    QCOMPARE(clusters.size(), 1);
    QVERIFY(qAbs(norm(clusters.at(0).centroid) - 1.0) < 1e-6);
}

void TestFaceClusterer::mediaIdsAreUniqueAndKeepTheCoverFirst()
{
    // Two faces from one image (a group photo) and one from another: the image
    // must be listed once, and the first face in - the highest-confidence one,
    // because FacePipeline sorts before clustering - becomes the cover.
    const auto clusters = clusterFaces({sample(7, 100, atDegrees(0)),
                                        sample(8, 100, atDegrees(5)),
                                        sample(9, 200, atDegrees(-5))},
                                       0.50);
    QCOMPARE(clusters.size(), 1);
    QCOMPARE(clusters.at(0).faceIds, QVector<qint64>({7, 8, 9}));
    QCOMPARE(clusters.at(0).mediaIds, QVector<qint64>({100, 200}));
}

QTEST_GUILESS_MAIN(TestFaceClusterer)
#include "tst_faceclusterer.moc"
