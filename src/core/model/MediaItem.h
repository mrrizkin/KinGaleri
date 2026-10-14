#pragma once

#include <QDateTime>
#include <QString>

namespace kg {

/// Where a media row came from. Kept as a bit flag so the source-album scanner
/// can mark a file that matches several heuristics.
enum class MediaKind : quint8 {
    Unknown = 0,
    Image = 1,
    Video = 2,
    LivePhotoStill = 3,
};

/// How `takenAt` was derived. Surfaced in the info panel (PRD 8, risk row).
enum class TakenAtSource : quint8 {
    Unknown = 0,
    Exif,
    Filename,
    FileMtime,
    Manual,
};

/// Persistent shape of a `media` row. Deliberately a plain value type: the grid
/// model stores these in a contiguous vector and must not allocate per item.
struct MediaItem
{
    qint64 id = -1;
    QString path;
    QString filename;
    QString extension;
    QString mimeType;

    qint64 sizeBytes = 0;
    qint64 mtimeNs = 0;
    qint64 takenAt = 0; ///< epoch milliseconds
    TakenAtSource takenAtSource = TakenAtSource::Unknown;

    int width = 0;
    int height = 0;
    int durationMs = 0;
    /// EXIF orientation (1..8). Needed by the full-screen viewer, which cannot
    /// ask Qt to auto-transform the way QImageReader does. Thumbnails already
    /// apply this during decode via QImageReader::setAutoTransform().
    int orientation = 1;
    MediaKind kind = MediaKind::Unknown;

    bool isFavorite = false;
    bool isHidden = false;
    bool isSynthetic = false; ///< benchmark row: no real file on disk

    QString sourceAlbum; ///< Camera / Screenshots / WhatsApp / ...

    bool isValid() const { return id >= 0 && !path.isEmpty(); }

    QDateTime takenAtLocal() const
    {
        return QDateTime::fromMSecsSinceEpoch(takenAt).toLocalTime();
    }
};

inline QString mediaKindToString(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::LivePhotoStill:
        return QStringLiteral("live");
    case MediaKind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

inline MediaKind mediaKindFromString(const QString &value)
{
    if (value == QLatin1String("image"))
        return MediaKind::Image;
    if (value == QLatin1String("video"))
        return MediaKind::Video;
    if (value == QLatin1String("live"))
        return MediaKind::LivePhotoStill;
    return MediaKind::Unknown;
}

inline QString takenAtSourceToString(TakenAtSource source)
{
    switch (source) {
    case TakenAtSource::Exif:
        return QStringLiteral("exif");
    case TakenAtSource::Filename:
        return QStringLiteral("filename");
    case TakenAtSource::FileMtime:
        return QStringLiteral("mtime");
    case TakenAtSource::Manual:
        return QStringLiteral("manual");
    case TakenAtSource::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

inline TakenAtSource takenAtSourceFromString(const QString &value)
{
    if (value == QLatin1String("exif"))
        return TakenAtSource::Exif;
    if (value == QLatin1String("filename"))
        return TakenAtSource::Filename;
    if (value == QLatin1String("mtime"))
        return TakenAtSource::FileMtime;
    if (value == QLatin1String("manual"))
        return TakenAtSource::Manual;
    return TakenAtSource::Unknown;
}

} // namespace kg
