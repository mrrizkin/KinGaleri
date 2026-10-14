#include "ui/quick/FullImageProvider.h"

#include "core/thumbs/ImageLoader.h"

#include <QByteArray>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QUrl>
#include <QUrlQuery>

namespace kg {
namespace {

/// Upper bound on what the viewer will decode. Beyond this a photo is already
/// larger than any display it will be shown on, and the extra pixels only cost
/// memory (PRD 4.1.1).
constexpr int kMaxViewerEdge = 4096;

QByteArray encodeBase64Url(const QByteArray &raw)
{
    QByteArray encoded = raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return encoded;
}

QByteArray decodeBase64Url(const QByteArray &text)
{
    return QByteArray::fromBase64(text, QByteArray::Base64UrlEncoding);
}

} // namespace

FullImageProvider::FullImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QString FullImageProvider::urlForPath(const QString &filePath, int maxEdge)
{
    if (filePath.isEmpty()) {
        return {};
    }

    const QByteArray encoded = encodeBase64Url(filePath.toUtf8());
    const int edge = maxEdge > 0 ? maxEdge : kMaxViewerEdge;
    return QStringLiteral("image://%1/%2?s=%3")
        .arg(QLatin1String(kProviderId), QString::fromLatin1(encoded))
        .arg(edge);
}

QImage FullImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    m_requestCount.fetchAndAddRelaxed(1);

    if (size) {
        *size = QSize();
    }

    // Strip any query string the URL carried; only the path token matters.
    const QString token = id.section(QLatin1Char('?'), 0, 0);
    const QString filePath = QString::fromUtf8(decodeBase64Url(token.toLatin1()));
    if (filePath.isEmpty()) {
        return {};
    }

    // The viewer only ever shows real files; synthetic benchmark rows have no
    // source and fall back to their thumbnail in QML.
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return {};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true); // apply EXIF orientation, mirrored variants included
    reader.setDecideFormatFromContent(true);
    reader.setAllocationLimit(1024);

    // Bound the decode to the requested size, capped so a huge source cannot
    // allocate an unreasonable buffer.
    int targetEdge = kMaxViewerEdge;
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0) {
        targetEdge = qMin(kMaxViewerEdge, qMax(requestedSize.width(), requestedSize.height()));
    }

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && !sourceSize.isEmpty()) {
        // Preserve aspect ratio: scale so the long edge matches `targetEdge`.
        QSize scaled = sourceSize;
        if (qMax(sourceSize.width(), sourceSize.height()) > targetEdge) {
            scaled.scale(targetEdge, targetEdge, Qt::KeepAspectRatio);
        }
        if (scaled != sourceSize) {
            reader.setScaledSize(scaled);
        }
    }

    QImage image = reader.read();
    if (image.isNull() && ImageLoader::isHeifPath(filePath)) {
        // No Qt HEIC plugin: convert with heif-convert when available.
        image = ImageLoader::decodeHeif(filePath, targetEdge, 1024);
    }
    if (!image.isNull() && size) {
        *size = image.size();
    }
    return image;
}

} // namespace kg
