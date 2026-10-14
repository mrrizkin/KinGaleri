#include "core/thumbs/ImageLoader.h"

#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace kg {
namespace ImageLoader {

namespace {

const QStringList &heifExtensions()
{
    static const QStringList extensions = {QStringLiteral("heic"), QStringLiteral("heif")};
    return extensions;
}

} // namespace

bool isHeifPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return heifExtensions().contains(suffix);
}

bool heifConvertAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("heif-convert")).isEmpty();
}

QImage decodeHeif(const QString &path, int targetEdge, int allocationLimit)
{
    if (!isHeifPath(path))
        return {};

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("heif-convert"));
    if (executable.isEmpty())
        return {};

    QTemporaryDir scratch;
    if (!scratch.isValid())
        return {};
    const QString converted = scratch.filePath(QStringLiteral("converted.jpg"));

    QProcess process;
    process.setProgram(executable);
    process.setArguments({path, converted});
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start();

    // A full-resolution HEIC on a slow disk is the worst case; 60 s is generous
    // for a user-initiated decode but still bounds a hung tool.
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0 || !QFileInfo::exists(converted)) {
        process.kill();
        return {};
    }

    QImageReader reader(converted);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(allocationLimit);
    if (targetEdge > 0) {
        const QSize size = reader.size();
        if (size.isValid() && !size.isEmpty()
            && qMax(size.width(), size.height()) > targetEdge) {
            reader.setScaledSize(size.scaled(targetEdge, targetEdge, Qt::KeepAspectRatio));
        }
    }
    QImage image = reader.read();
    if (!image.isNull() && targetEdge > 0
        && (image.width() > targetEdge || image.height() > targetEdge)) {
        image = image.scaled(targetEdge, targetEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

} // namespace ImageLoader
} // namespace kg
