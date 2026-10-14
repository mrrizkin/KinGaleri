#include "core/ml/FaceEngine.h"

#include <QFileInfo>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kg::ml {

namespace {

#ifdef KG_ENABLE_ML

constexpr int kDetInput = 640;
constexpr int kEncInput = 112;
constexpr int kEmbedDim = 128;
constexpr int kStrideCount = 3;
constexpr int kStride[3] = {8, 16, 32};

/// ArcFace reference landmarks, the same five points OpenCV's FaceRecognizerSF
/// aligns to (right eye, left eye, nose, right mouth corner, left mouth corner).
constexpr float kRefLandmarks[5][2] = {
    {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f},
    {41.5493f, 92.3655f}, {70.7299f, 92.2041f},
};

float clamp01(float value)
{
    return std::min(1.0f, std::max(0.0f, value));
}

float mean(const QVector<QPointF> &points, bool y)
{
    float sum = 0.0f;
    for (const QPointF &point : points)
        sum += static_cast<float>(y ? point.y() : point.x());
    return sum / static_cast<float>(points.size());
}

/// Bilinear RGB sample; returns false when the point is outside the image.
bool samplePixel(const QImage &rgb, double x, double y, float out[3])
{
    if (x < 0.0 || y < 0.0 || x > rgb.width() - 1.001 || y > rgb.height() - 1.001)
        return false;
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, rgb.width() - 1);
    const int y1 = std::min(y0 + 1, rgb.height() - 1);
    const double fx = x - x0;
    const double fy = y - y0;

    const uchar *const p = rgb.constBits();
    const auto channel = [&](int px, int py, int c) -> double {
        return p[static_cast<qsizetype>(py) * rgb.bytesPerLine() + px * 3 + c];
    };
    for (int c = 0; c < 3; ++c) {
        const double top = channel(x0, y0, c) * (1.0 - fx) + channel(x1, y0, c) * fx;
        const double bottom = channel(x0, y1, c) * (1.0 - fx) + channel(x1, y1, c) * fx;
        out[c] = static_cast<float>(top * (1.0 - fy) + bottom * fy);
    }
    return true;
}

float intersectionOverUnion(const QRectF &a, const QRectF &b)
{
    const QRectF inter = a.intersected(b);
    if (inter.isEmpty())
        return 0.0f;
    const float area = static_cast<float>(inter.width() * inter.height());
    const float total = static_cast<float>(a.width() * a.height() + b.width() * b.height()) - area;
    return total > 0.0f ? area / total : 0.0f;
}

QVector<int> nonMaxSuppression(const QVector<Face> &faces, float threshold)
{
    QVector<int> order(faces.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&faces](int a, int b) {
        return faces.at(a).score > faces.at(b).score;
    });

    QVector<int> keep;
    QVector<bool> suppressed(faces.size(), false);
    for (const int index : order) {
        if (suppressed.at(index))
            continue;
        keep.append(index);
        for (const int other : order) {
            if (other == index || suppressed.at(other))
                continue;
            if (intersectionOverUnion(faces.at(index).box, faces.at(other).box) > threshold)
                suppressed[other] = true;
        }
    }
    return keep;
}

#endif // KG_ENABLE_ML

} // namespace

#ifdef KG_ENABLE_ML

bool FaceEngine::compiledIn()
{
    return true;
}

QString FaceEngine::detectorFileName()
{
    return QStringLiteral("yunet.onnx");
}

QString FaceEngine::embedderFileName()
{
    return QStringLiteral("sface.onnx");
}

QString FaceEngine::modelMarker()
{
    // Bump when the preprocessing, alignment or model files change.
    return QStringLiteral("yunet-2023mar+sface-2021dec-v1");
}

FaceEngine::FaceEngine() = default;

FaceEngine::~FaceEngine() = default;

bool FaceEngine::load(const QString &detectorPath, const QString &embedderPath, QString *error)
{
    if (m_detector && m_embedder)
        return true;
    if (!QFileInfo::exists(detectorPath) || !QFileInfo::exists(embedderPath)) {
        if (error)
            *error = QStringLiteral("Face model files are missing");
        return false;
    }

    try {
        if (!m_env)
            m_env = std::make_shared<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "kin-galeri");
        // Two threads is the tuned default: inference runs in the background and
        // must not starve thumbnailing or the GUI thread.
        m_options.SetIntraOpNumThreads(2);
        m_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        m_detector = std::make_unique<Ort::Session>(*m_env, detectorPath.toStdString().c_str(),
                                                    m_options);
        m_embedder = std::make_unique<Ort::Session>(*m_env, embedderPath.toStdString().c_str(),
                                                    m_options);
    } catch (const Ort::Exception &exception) {
        m_detector.reset();
        m_embedder.reset();
        if (error)
            *error = QString::fromUtf8(exception.what());
        return false;
    }
    return true;
}

bool FaceEngine::loaded() const
{
    return m_detector && m_embedder;
}

QVector<Face> FaceEngine::detect(const QImage &image, float scoreThreshold,
                                 float nmsThreshold) const
{
    QVector<Face> result;
    if (!m_detector || image.isNull())
        return result;

    // Letterbox so the face geometry is not stretched. The detector expects BGR
    // (OpenCV's FaceDetectorYN feeds the Mat straight through, no channel swap).
    const QImage bgr = image.convertToFormat(QImage::Format_BGR888);
    const double scale = std::min(static_cast<double>(kDetInput) / bgr.width(),
                                  static_cast<double>(kDetInput) / bgr.height());
    const int scaledW = std::max(1, static_cast<int>(std::lround(bgr.width() * scale)));
    const int scaledH = std::max(1, static_cast<int>(std::lround(bgr.height() * scale)));
    const int padX = (kDetInput - scaledW) / 2;
    const int padY = (kDetInput - scaledH) / 2;

    QImage canvas(kDetInput, kDetInput, QImage::Format_BGR888);
    canvas.fill(Qt::black);
    {
        QPainter painter(&canvas);
        painter.drawImage(QRect(padX, padY, scaledW, scaledH), bgr);
    }

    QVector<float> input(1 * 3 * kDetInput * kDetInput, 0.0f);
    for (int y = 0; y < kDetInput; ++y) {
        const uchar *line = canvas.constScanLine(y);
        for (int x = 0; x < kDetInput; ++x) {
            for (int c = 0; c < 3; ++c) {
                input[c * kDetInput * kDetInput + y * kDetInput + x] =
                    static_cast<float>(line[x * 3 + c]);
            }
        }
    }

    const int64_t shape[4] = {1, 3, kDetInput, kDetInput};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape, 4);

    static const char *const kInputNames[] = {"input"};
    static const char *const kOutputNames[] = {
        "cls_8", "cls_16", "cls_32", "obj_8", "obj_16", "obj_32",
        "bbox_8", "bbox_16", "bbox_32", "kps_8", "kps_16", "kps_32",
    };

    std::vector<Ort::Value> outputs;
    try {
        outputs = m_detector->Run(Ort::RunOptions{nullptr}, kInputNames, &tensor, 1, kOutputNames,
                                  std::size(kOutputNames));
    } catch (const Ort::Exception &) {
        return result;
    }

    const auto tensorData = [&outputs](size_t index) -> const float * {
        return outputs.at(index).GetTensorData<float>();
    };

    const double invScale = 1.0 / scale;
    for (int s = 0; s < kStrideCount; ++s) {
        const int cols = kDetInput / kStride[s];
        const int rows = kDetInput / kStride[s];
        const float *cls = tensorData(static_cast<size_t>(s));
        const float *obj = tensorData(static_cast<size_t>(s + kStrideCount));
        const float *bbox = tensorData(static_cast<size_t>(s + kStrideCount * 2));
        const float *kps = tensorData(static_cast<size_t>(s + kStrideCount * 3));

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const size_t index = static_cast<size_t>(r) * cols + c;
                const float score = std::sqrt(clamp01(cls[index]) * clamp01(obj[index]));
                if (score < scoreThreshold)
                    continue;

                const float cx = (static_cast<float>(c) + bbox[index * 4 + 0]) * kStride[s];
                const float cy = (static_cast<float>(r) + bbox[index * 4 + 1]) * kStride[s];
                const float w = std::exp(bbox[index * 4 + 2]) * kStride[s];
                const float h = std::exp(bbox[index * 4 + 3]) * kStride[s];

                Face face;
                face.score = score;
                // Map from letterbox space back to the source image.
                const double x1 = (cx - w / 2.0f - padX) * invScale;
                const double y1 = (cy - h / 2.0f - padY) * invScale;
                const double bw = w * invScale;
                const double bh = h * invScale;
                face.box = QRectF(x1, y1, bw, bh);
                for (int n = 0; n < 5; ++n) {
                    const double px = (static_cast<float>(c) + kps[index * 10 + n * 2]) * kStride[s];
                    const double py = (static_cast<float>(r) + kps[index * 10 + n * 2 + 1]) * kStride[s];
                    face.landmarks.append(QPointF((px - padX) * invScale, (py - padY) * invScale));
                }
                result.append(face);
            }
        }
    }

    const QVector<int> keep = nonMaxSuppression(result, nmsThreshold);
    QVector<Face> suppressed;
    suppressed.reserve(keep.size());
    for (const int index : keep)
        suppressed.append(result.at(index));
    return suppressed;
}

bool FaceEngine::embed(const QImage &image, Face &face) const
{
    if (!m_embedder || image.isNull() || face.landmarks.size() != 5)
        return false;

    // Estimate the similarity (scale + rotation + translation, no shear) that maps
    // the detected landmarks onto the ArcFace reference. Working in complex
    // numbers gives the closed form directly, which avoids an SVD.
    const float srcMeanX = mean(face.landmarks, false);
    const float srcMeanY = mean(face.landmarks, true);
    float dstMeanX = 0.0f;
    float dstMeanY = 0.0f;
    for (int i = 0; i < 5; ++i) {
        dstMeanX += kRefLandmarks[i][0];
        dstMeanY += kRefLandmarks[i][1];
    }
    dstMeanX /= 5.0f;
    dstMeanY /= 5.0f;

    double numA = 0.0;
    double numB = 0.0;
    double den = 0.0;
    for (int i = 0; i < 5; ++i) {
        const double px = face.landmarks.at(i).x() - srcMeanX;
        const double py = face.landmarks.at(i).y() - srcMeanY;
        const double qx = kRefLandmarks[i][0] - dstMeanX;
        const double qy = kRefLandmarks[i][1] - dstMeanY;
        numA += qx * px + qy * py;
        numB += qy * px - qx * py;
        den += px * px + py * py;
    }
    if (den <= 1e-9)
        return false;

    const double a = numA / den;
    const double b = numB / den;
    const double tx = dstMeanX - (a * srcMeanX - b * srcMeanY);
    const double ty = dstMeanY - (b * srcMeanX + a * srcMeanY);
    const double det = a * a + b * b;
    if (det <= 1e-9)
        return false;

    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    QVector<float> input(1 * 3 * kEncInput * kEncInput, 0.0f);

    for (int y = 0; y < kEncInput; ++y) {
        for (int x = 0; x < kEncInput; ++x) {
            // Inverse similarity: destination (reference space) -> source pixel.
            const double dx = x - tx;
            const double dy = y - ty;
            const double sx = (a * dx + b * dy) / det;
            const double sy = (-b * dx + a * dy) / det;
            float pixel[3] = {0.0f, 0.0f, 0.0f};
            samplePixel(rgb, sx, sy, pixel);
            for (int c = 0; c < 3; ++c) {
                input[c * kEncInput * kEncInput + y * kEncInput + x] = pixel[c];
            }
        }
    }

    const int64_t shape[4] = {1, 3, kEncInput, kEncInput};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape, 4);

    static const char *const kInputNames[] = {"data"};
    static const char *const kOutputNames[] = {"fc1"};

    std::vector<Ort::Value> outputs;
    try {
        outputs = m_embedder->Run(Ort::RunOptions{nullptr}, kInputNames, &tensor, 1, kOutputNames, 1);
    } catch (const Ort::Exception &) {
        return false;
    }
    if (outputs.empty())
        return false;

    const float *data = outputs.front().GetTensorData<float>();
    face.embedding = QVector<float>(data, data + kEmbedDim);
    normalise(face.embedding);
    return true;
}

#else // !KG_ENABLE_ML

bool FaceEngine::compiledIn()
{
    return false;
}
QString FaceEngine::detectorFileName()
{
    return QStringLiteral("yunet.onnx");
}
QString FaceEngine::embedderFileName()
{
    return QStringLiteral("sface.onnx");
}
QString FaceEngine::modelMarker()
{
    return QStringLiteral("disabled");
}
FaceEngine::FaceEngine() = default;
FaceEngine::~FaceEngine() = default;
bool FaceEngine::load(const QString &, const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("This build has no ML backend");
    return false;
}
bool FaceEngine::loaded() const
{
    return false;
}
QVector<Face> FaceEngine::detect(const QImage &, float, float) const
{
    return {};
}
bool FaceEngine::embed(const QImage &, Face &) const
{
    return false;
}

#endif // KG_ENABLE_ML

} // namespace kg::ml
