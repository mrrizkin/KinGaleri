#pragma once

#include <QQuickImageProvider>

namespace kg {

/**
 * QML image provider for the full-screen viewer: `image://full/<path>?s=<edge>`.
 *
 * Why this exists instead of `Image { source: "file://" + path }`:
 *
 *  1. EXIF orientation. `QImageReader::setAutoTransform(true)` applies the
 *     orientation tag (including the mirrored variants 2/4/5/7) exactly as the
 *     thumbnailer already does, so the viewer can never disagree with the grid.
 *     Reproducing that in QML would mean rotation *and* scale matrices plus
 *     dimension swapping.
 *  2. Memory. Decoding a 50 MP photo at full size would cost hundreds of MB for
 *     a single disposable view. `setScaledSize()` bounds the decode to what the
 *     window can actually show.
 *
 * The path travels base64url-encoded so no path character can collide with the
 * provider's URL syntax.
 */
class FullImageProvider : public QQuickImageProvider
{
public:
    static constexpr const char *kProviderId = "full";

    FullImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    /// Builds the provider URL for a local file path (used by QML through
    /// AppController::fullImageUrl()).
    static QString urlForPath(const QString &filePath, int maxEdge);

    /// Number of requests served, used by the perf overlay.
    qint64 requestCount() const { return m_requestCount.loadRelaxed(); }

private:
    QAtomicInteger<qint64> m_requestCount{0};
};

} // namespace kg
