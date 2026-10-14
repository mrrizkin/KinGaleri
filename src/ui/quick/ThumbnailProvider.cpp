#include "ui/quick/ThumbnailProvider.h"

#include "core/thumbs/ThumbnailService.h"

#include <QImage>
#include <QUrlQuery>

namespace kg {

ThumbnailProvider::ThumbnailProvider(ThumbnailService *service)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_service(service)
{
}

ThumbnailProvider::ParsedId ThumbnailProvider::parseId(const QString &id)
{
    ParsedId parsed;

    // Qt hands us everything after "image://thumbs/", e.g. "1234?v=7&s=256".
    const int queryStart = id.indexOf(QLatin1Char('?'));
    const QString idPart = queryStart < 0 ? id : id.left(queryStart);

    bool ok = false;
    parsed.mediaId = idPart.toLongLong(&ok);
    if (!ok) {
        parsed.mediaId = -1;
        return parsed;
    }

    if (queryStart >= 0) {
        const QUrlQuery query(id.mid(queryStart + 1));
        parsed.revision = query.queryItemValue(QStringLiteral("v")).toInt();
        parsed.requestedEdge = query.queryItemValue(QStringLiteral("s")).toInt();
    }

    return parsed;
}

QImage ThumbnailProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    m_requestCount.fetchAndAddRelaxed(1);

    const ParsedId parsed = parseId(id);

    // Prefer an explicit ?s= hint; otherwise trust the size Qt Quick asked for,
    // which comes from the delegate's sourceSize.
    int edge = parsed.requestedEdge;
    if (edge <= 0 && requestedSize.isValid()) {
        edge = qMax(requestedSize.width(), requestedSize.height());
    }

    QImage image;
    if (m_service && parsed.mediaId >= 0) {
        image = m_service->fetch(parsed.mediaId, edge);
    }

    if (image.isNull()) {
        // A 1x1 transparent image keeps QML happy and avoids a broken-image icon.
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }

    if (size) {
        *size = image.size();
    }

    return image;
}

} // namespace kg
