#include "app/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace kg {

QString Paths::s_databaseOverride;
QString Paths::s_cacheOverride;
QString Paths::s_configDir;
QString Paths::s_dataDir;
QString Paths::s_cacheDir;
bool Paths::s_initialised = false;

namespace {

/// XDG_*_HOME with the fallback the spec prescribes.
QString xdgDir(const char *envVar, const QString &fallbackRelative)
{
    const QByteArray raw = qgetenv(envVar);
    if (!raw.isEmpty()) {
        return QString::fromLocal8Bit(raw);
    }
    return QDir::homePath() + QLatin1Char('/') + fallbackRelative;
}

} // namespace

void Paths::setDatabaseOverride(const QString &filePath)
{
    s_databaseOverride = filePath;
}

QString Paths::databaseOverride()
{
    return s_databaseOverride;
}

void Paths::setCacheOverride(const QString &dirPath)
{
    s_cacheOverride = dirPath;
}

void Paths::initialise(bool ensureExists)
{
    s_configDir = xdgDir("XDG_CONFIG_HOME", QStringLiteral(".config")) + QStringLiteral("/kin-galeri");
    s_dataDir = xdgDir("XDG_DATA_HOME", QStringLiteral(".local/share")) + QStringLiteral("/kin-galeri");
    s_cacheDir = xdgDir("XDG_CACHE_HOME", QStringLiteral(".cache")) + QStringLiteral("/kin-galeri");

    if (!s_cacheOverride.isEmpty()) {
        s_cacheDir = QDir::cleanPath(s_cacheOverride);
    }

    if (ensureExists) {
        QDir().mkpath(s_configDir);
        QDir().mkpath(s_dataDir);
        QDir().mkpath(thumbnailCacheDir());
    }

    s_initialised = true;
}

QString Paths::configDir()
{
    return s_configDir;
}

QString Paths::dataDir()
{
    return s_dataDir;
}

QString Paths::cacheDir()
{
    return s_cacheDir;
}

QString Paths::databaseFile()
{
    if (!s_databaseOverride.isEmpty()) {
        return s_databaseOverride;
    }
    return s_dataDir + QStringLiteral("/library.db");
}

QString Paths::thumbnailCacheDir()
{
    return s_cacheDir + QStringLiteral("/thumbs");
}

QString Paths::thumbnailPathFor(qint64 mediaId, const QString &extension)
{
    // Two-level sharding keeps directory entry counts low. With 500k items the
    // deepest directory holds ~60 files on average (PRD 5.4).
    const QString hex = QString::number(mediaId, 16).rightJustified(8, QLatin1Char('0'));
    const QString l1 = hex.mid(0, 2);
    const QString l2 = hex.mid(2, 2);
    return thumbnailCacheDir() + QLatin1Char('/') + l1 + QLatin1Char('/') + l2
           + QLatin1Char('/') + QString::number(mediaId) + QLatin1Char('.') + extension;
}

bool Paths::isEphemeral()
{
    return !s_cacheOverride.isEmpty();
}

QString Paths::sizeForLog(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(QLatin1String(units[unit]));
}

} // namespace kg
