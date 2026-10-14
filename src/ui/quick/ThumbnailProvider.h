#pragma once

#include <QQuickImageProvider>

namespace kg {

class ThumbnailService;

/**
 * QML image provider for `image://thumbs/<mediaId>?v=<revision>`.
 *
 * URL form (PRD 5.6): the `v` query parameter is the per-item revision from
 * MediaGridModel. QML caches by URL, so bumping the revision after the
 * asynchronous generator finishes is what makes the real thumbnail replace the
 * placeholder without any model reset.
 *
 * requestImage() runs on Qt Quick's image-loading thread, so this class must not
 * touch anything owned by the GUI thread other than the thread-safe
 * ThumbnailService.
 */
class ThumbnailProvider : public QQuickImageProvider
{
public:
    static constexpr const char *kProviderId = "thumbs";

    explicit ThumbnailProvider(ThumbnailService *service);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    /// Number of requests served, used by the perf overlay.
    qint64 requestCount() const { return m_requestCount.loadRelaxed(); }

private:
    struct ParsedId
    {
        qint64 mediaId = -1;
        int revision = 0;
        int requestedEdge = 0;
    };

    static ParsedId parseId(const QString &id);

    ThumbnailService *m_service = nullptr;
    QAtomicInteger<qint64> m_requestCount{0};
};

} // namespace kg
