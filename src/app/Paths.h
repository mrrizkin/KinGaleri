#pragma once

#include <QString>

namespace kg {

/**
 * Freedesktop-compliant runtime paths for kin-galeri.
 *
 *   config : ~/.config/kin-galeri/
 *   data   : ~/.local/share/kin-galeri/      (library.db lives here)
 *   cache  : ~/.cache/kin-galeri/            (thumbnails; safe to delete)
 *
 * XDG_* environment variables are honoured, and KG_* overrides exist so the
 * benchmark harness can keep its data out of the user's real library.
 */
class Paths
{
public:
    /// Resolve all directories, creating them if `ensureExists` is true.
    static void initialise(bool ensureExists = true);

    /// Override the database file (CLI --db). Must be called before initialise().
    static void setDatabaseOverride(const QString &filePath);
    static QString databaseOverride();

    /// Override the cache root (CLI --cache). Must be called before initialise().
    static void setCacheOverride(const QString &dirPath);

    static QString configDir();   ///< ~/.config/kin-galeri
    static QString dataDir();     ///< ~/.local/share/kin-galeri
    static QString cacheDir();    ///< ~/.cache/kin-galeri

    static QString databaseFile();      ///< <dataDir>/library.db
    static QString thumbnailCacheDir(); ///< <cacheDir>/thumbs

    /// Sharded thumbnail path, e.g. <thumbs>/a7/3f/12345.webp
    static QString thumbnailPathFor(qint64 mediaId, const QString &extension = QStringLiteral("webp"));

    /// True when the user pointed KG_CACHE_DIR at a scratch directory.
    static bool isEphemeral();

    static QString sizeForLog(qint64 bytes);

private:
    static QString s_databaseOverride;
    static QString s_cacheOverride;
    static QString s_configDir;
    static QString s_dataDir;
    static QString s_cacheDir;
    static bool s_initialised;
};

} // namespace kg
