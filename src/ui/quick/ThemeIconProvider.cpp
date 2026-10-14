#include "ui/quick/ThemeIconProvider.h"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace kg {

ThemeIconProvider::ThemeIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage ThemeIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QString path = id;
    QColor tint;
    int edge = 0;

    const int queryStart = id.indexOf(QLatin1Char('?'));
    if (queryStart >= 0) {
        path = id.left(queryStart);
        const QUrlQuery query(id.mid(queryStart + 1));
        const QString colorText = query.queryItemValue(QStringLiteral("c"));
        if (!colorText.isEmpty()) {
            tint = QColor(colorText);
        }
        if (query.hasQueryItem(QStringLiteral("s"))) {
            edge = query.queryItemValue(QStringLiteral("s")).toInt();
        }
    }

    // Try each candidate name in order; themes disagree on canonical names, so
    // the first one that resolves is used. All candidates failing is the signal
    // that QML should show its text-glyph fallback.
    QIcon icon;
    const QStringList rawNames = path.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &raw : rawNames) {
        const QString name = QUrl::fromPercentEncoding(raw.toUtf8());
        if (name.isEmpty()) {
            continue;
        }
        icon = QIcon::fromTheme(name);
        if (!icon.isNull()) {
            break;
        }
    }
    if (icon.isNull()) {
        // Unknown icon: leave the URL unresolved so QML can show a fallback.
        return {};
    }

    if (edge <= 0) {
        edge = requestedSize.isValid() && requestedSize.width() > 0 ? requestedSize.width() : 16;
    }
    edge = std::clamp(edge, 8, 512);

    QImage image = icon.pixmap(edge, edge).toImage();
    if (image.isNull()) {
        return {};
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (tint.isValid()) {
        // SourceIn keeps the icon's anti-aliased alpha and replaces its colour,
        // which is what makes a monochrome symbolic icon follow the app theme.
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), tint);
    }

    if (size) {
        *size = image.size();
    }
    return image;
}

} // namespace kg
