#pragma once

#include <QString>
#include <QUrl>

namespace kg {

/**
 * Accepts either a plain filesystem path or a `file://` URL and returns the
 * local path.
 *
 * QML's FolderDialog reports its selection as a `file://` URL string, while the
 * indexer and the file operations want a path. Keeping the conversion in one
 * place avoids the two copies drifting apart - the move action originally
 * failed with "Target folder does not exist" because only the indexer had it.
 */
inline QString localPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        const QUrl url(pathOrUrl);
        if (url.isLocalFile()) {
            return url.toLocalFile();
        }
    }
    return pathOrUrl;
}

} // namespace kg
