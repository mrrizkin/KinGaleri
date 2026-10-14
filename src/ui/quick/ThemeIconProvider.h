#pragma once

#include <QQuickImageProvider>

namespace kg {

/**
 * QML image provider for `image://themeicon/<name>[,<fallback>...]?c=<color>&s=<px>`.
 *
 * Serves icons from the desktop's icon theme (QIcon::fromTheme) so the UI uses
 * the same icon set as the rest of the session instead of hard-coded glyphs.
 * Several comma-separated names may be given; the first one the theme actually
 * provides wins, which keeps the UI working across icon themes that use
 * different naming conventions (Mint-Y vs Breeze vs Adwaita).
 *
 * The optional `c` parameter tints monochrome icons to an app palette colour;
 * `s` is the raster size in device pixels (the caller multiplies by the screen
 * devicePixelRatio so icons stay crisp on HiDPI).
 *
 * When none of the theme names resolve the provider returns a null image, which
 * QML surfaces as `Image.Error` - AppIcon.qml uses that to fall back to a text
 * glyph on systems without an icon theme.
 */
class ThemeIconProvider : public QQuickImageProvider
{
public:
    static constexpr const char *kProviderId = "themeicon";

    ThemeIconProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace kg
