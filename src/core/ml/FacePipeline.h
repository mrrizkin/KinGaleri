#pragma once

#include <QString>

#include <functional>

namespace kg {

class Database;

namespace ml {

struct FacePipelineOptions
{
    QString detectorPath;
    QString embedderPath;
    /// Longest edge the detector sees. Photos are downscaled to this first, which
    /// bounds per-image cost without losing faces (YuNet itself works at 640).
    int maxEdge = 1280;
    /// Cosine similarity above which two faces are considered the same person.
    ///
    /// Measured on LFW (13,233 images, 5,749 identities, clustered as one
    /// library because that is how the pipeline runs): 0.40 grouped too eagerly.
    /// A cluster's running-mean centroid drifts towards the "average face" of
    /// that identity, so once a cluster held hundreds of faces it started
    /// matching other similar-looking people - the largest ended up with 527
    /// faces covering 12 identities, and overall purity was 76.4%. At 0.50
    /// purity is 98.4% and pairwise F1 96.3%, while the merges that should
    /// happen still do: real same-person pairs sit far above the line (the
    /// cross-year Obama pair in the smoke library scores 0.80). Reproduce with
    /// `scripts/evaluate-lfw.py`, reasoning in docs/M3-REPORT.md.
    double clusterThreshold = 0.50;
};

struct FacePipelineResult
{
    bool ok = false;
    int analyzed = 0; ///< images scanned this pass
    int detected = 0; ///< faces found this pass
    int totalFaces = 0;
    int people = 0;
    QString error;
};

/**
 * Detects and embeds faces into `faces`, then clusters them into `people`.
 *
 * Incremental: a `face_scans` row records that an image was looked at under the
 * current model marker (even when it held zero faces), so a second pass only
 * touches new photos. Clustering itself is global, because a person is defined
 * by all their faces at once.
 *
 * Renames survive a rebuild: before re-clustering, named people are matched to
 * the nearest new cluster by their cover face's embedding, and the name is
 * carried over.
 *
 * Must run on a worker thread with its own Database connection. Needs the ML
 * backend; on a build without it `run` returns immediately with an error.
 */
class FacePipeline
{
public:
    explicit FacePipeline(Database &database);

    FacePipelineResult run(const FacePipelineOptions &options,
                           const std::function<bool()> &cancelled = {});

private:
    Database &m_database;
};

} // namespace ml
} // namespace kg
