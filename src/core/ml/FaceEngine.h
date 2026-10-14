#pragma once

#include "core/ml/VectorMath.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <memory>

#ifdef KG_ENABLE_ML
#include <onnxruntime_cxx_api.h>
#endif

namespace kg::ml {

/// One detected face plus, when embedded, its L2-normalised descriptor.
struct Face
{
    QRectF box;                 ///< in source-image pixels
    QVector<QPointF> landmarks; ///< 5 points: right eye, left eye, nose, mouth corners
    float score = 0.0f;
    QVector<float> embedding;   ///< 128 floats for SFace; empty until embed() runs
};

/**
 * YuNet face detection + SFace face embedding, both through ONNX Runtime.
 *
 * The two models are small CPU models chosen so the whole feature costs ~40 MB
 * of disk and needs no GPU. Detection follows OpenCV's `FaceDetectorYN` decode
 * exactly (raw cls/obj in [0,1], no sigmoid; box = (cell + delta) * stride,
 * size = exp(delta) * stride) and embedding follows `FaceRecognizerSF`
 * (5-point similarity alignment to the ArcFace reference, then a 112x112 RGB
 * crop). Both were checked against the OpenCV 4.x sources, which is the
 * reference implementation these ONNX files ship with.
 *
 * Thread-safety: one instance owns one ORT session and is not safe to call from
 * several threads at once. Each worker builds its own instance.
 */
class FaceEngine
{
public:
    /// True when this binary was compiled with the ML backend.
    static bool compiledIn();

    /// Model file names the runtime expects inside the model directory.
    static QString detectorFileName();
    static QString embedderFileName();
    /// Human-readable marker stored on each `faces` row. Bump when either model
    /// or the preprocessing changes, so old embeddings are recomputed.
    static QString modelMarker();

    FaceEngine();
    ~FaceEngine();
    FaceEngine(const FaceEngine &) = delete;
    FaceEngine &operator=(const FaceEngine &) = delete;

    /// Loads both models. `error` is filled on failure. Idempotent.
    bool load(const QString &detectorPath, const QString &embedderPath, QString *error);
    bool loaded() const;

    /// Runs detection on the full-size image. Coordinates are in source pixels.
    QVector<Face> detect(const QImage &image, float scoreThreshold = 0.6f,
                         float nmsThreshold = 0.3f) const;

    /// Adds an embedding to one face by aligning it to 112x112.
    bool embed(const QImage &image, Face &face) const;

private:
#ifdef KG_ENABLE_ML
    std::shared_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_detector;
    std::unique_ptr<Ort::Session> m_embedder;
    Ort::SessionOptions m_options;
    QString m_error;
#endif
};

} // namespace kg::ml
