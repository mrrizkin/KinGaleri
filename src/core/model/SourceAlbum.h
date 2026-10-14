#pragma once

#include <QString>
#include <QStringList>

namespace kg {

/**
 * Heuristics that map an absolute path onto a "source album" (PRD 6.3).
 * Rules are ordered: the first match wins. Users will be able to edit the rule
 * table in M2; for now the defaults cover the common phone/app layouts.
 */
class SourceAlbum
{
public:
    struct Rule
    {
        QString album;
        QStringList pathGlobs;      ///< matched against the full path, case-insensitive
        QStringList filenameGlobs;  ///< matched against the basename, case-insensitive
    };

    static QString detect(const QString &absolutePath, const QString &filename);

    static const QList<Rule> &rules();

    /// Fallback when nothing matches: the immediate parent directory name.
    static QString parentFolderFallback(const QString &absolutePath);

private:
    static const QList<Rule> &buildRules();
};

} // namespace kg
