#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>

namespace kg {

/**
 * Directory pruning policy shared by the indexer and the filesystem watcher.
 *
 * A default library is the whole home directory, so blindly walking every
 * subdirectory is not acceptable: it would descend into `.cache`, `.git`,
 * `node_modules`, Python virtualenvs and the like, which contain tens of
 * thousands of irrelevant files (and, for the watcher, would blow through the
 * kernel's inotify watch limit).
 *
 * Keeping this in one header means the indexer never indexes a directory the
 * watcher forgets to watch, or vice versa.
 */
namespace scan {

/// The built-in junk list shipped with the app. Exposed in Settings so the user
/// can edit it; this function only provides the seed/default value. Entries are
/// glob patterns matched case-insensitively against a directory basename, so
/// versioned prefixes are expressed as `cmake-build-*` / `android-sdk-*`.
inline QStringList defaultJunkPatterns()
{
    return {
        QStringLiteral("node_modules"),
        QStringLiteral("bower_components"),
        QStringLiteral("__pycache__"),
        QStringLiteral("cmakefiles"),
        QStringLiteral("site-packages"),
        QStringLiteral("venv"),
        QStringLiteral("virtualenv"),
        QStringLiteral("vcs"),
        // Caches and scratch space that are not dot-directories.
        QStringLiteral("cache"),
        QStringLiteral("caches"),
        QStringLiteral("tmp"),
        QStringLiteral("temp"),
        QStringLiteral("trash"),
        // Language/SDK toolchain roots.
        QStringLiteral("go"),
        QStringLiteral("rustup"),
        QStringLiteral("toolchains"),
        QStringLiteral("android-sdk"),
        QStringLiteral("sdk"),
        QStringLiteral("ndk"),
        // Versioned build/toolchain directories.
        QStringLiteral("cmake-build-*"),
        QStringLiteral("android-sdk-*"),
    };
}

/// Case-insensitive glob match of one directory name against a pattern list.
/// Both sides are lowercased because QDir::match has no case-sensitivity
/// argument and would otherwise be case-sensitive on Linux.
inline bool matchesAnyGlob(const QStringList &patterns, const QString &name)
{
    if (patterns.isEmpty()) {
        return false;
    }
    QStringList lowered;
    lowered.reserve(patterns.size());
    for (const QString &pattern : patterns) {
        lowered.append(pattern.toLower());
    }
    return QDir::match(lowered, name.toLower());
}

/// Extensions that are plausibly media but not on the clear-media allowlist.
/// The motivating case is `.ts`: MPEG transport stream to a camera, TypeScript
/// source to a developer. Only these extensions (plus extensionless files) are
/// opened for a header check when hybrid detection is on; every other unknown
/// extension is ignored without any I/O. The user can edit this list, so treat
/// it as a seed/default.
inline QStringList defaultSniffExtensions()
{
    return {
        QStringLiteral("ts"),
        // JPEG container aliases seen in the wild.
        QStringLiteral("jfif"),
        QStringLiteral("jpe"),
        QStringLiteral("jif"),
        QStringLiteral("jfi"),
        QStringLiteral("pjpeg"),
        // Still frames whose extension alone does not say "clearly media".
        QStringLiteral("apng"),
        QStringLiteral("avifs"),
        QStringLiteral("heics"),
        QStringLiteral("heifs"),
    };
}

/**
 * User-tunable pruning knobs, driven by AppSettings. Defaults reproduce the
 * conservative home-scan behaviour (skip hidden + junk).
 */
struct Options
{
    bool includeHidden = false;
    bool ignoreJunk = true;
    /// Descend into symlinked directories. Off by default: a home directory can
    /// contain links back into itself, which would duplicate rows and loop.
    bool followSymlinks = false;
    /// Hybrid detection: when a file's extension is not on the allowlist, read
    /// its header to recognise images/videos anyway. Known extensions never pay
    /// this extra read. On by default so a JPEG that lost its name still lands in
    /// the library, while the common case stays I/O-free.
    bool sniffUnknownExtensions = true;
    /// The only unknown extensions worth a header check. Matched case-
    /// insensitively against the extension without the dot. Extensionless files
    /// are always treated as candidates while sniffUnknownExtensions is on.
    QStringList sniffExtensions = defaultSniffExtensions();
    /// Absolute, cleaned paths the user explicitly excluded. A directory matching
    /// one of these (or living under one) is pruned.
    QSet<QString> ignoredPaths;
    /// User glob patterns matched against the directory basename, e.g.
    /// "Screenshots" or "backup*". Case-insensitive.
    QStringList ignoredPatterns;
    /// Built-in junk patterns (node_modules, caches, toolchains...). Seeded from
    /// defaultJunkPatterns() and edited by the user in Settings; ignoreJunk gates
    /// the whole list. Case-insensitive globs.
    QStringList junkPatterns = defaultJunkPatterns();
};

/// True when a directory with this basename should be skipped. Hidden
/// directories (leading dot) are skipped wholesale: a photo library living in
/// `~/.local/share` is far rarer than the cost of crawling `.cache`/`.git`.
inline bool isIgnoredDirectoryName(const QString &name, const Options &options = Options{})
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
        return false;
    }
    if (matchesAnyGlob(options.ignoredPatterns, name)) {
        return true;
    }
    if (name.startsWith(QLatin1Char('.'))) {
        return !options.includeHidden;
    }
    if (!options.ignoreJunk) {
        return false;
    }
    return matchesAnyGlob(options.junkPatterns, name);
}

/// Convenience overload for a full directory path. Checks the user's explicit
/// ignore list (absolute paths) before falling back to the name-based policy.
inline bool isIgnoredDirectory(const QString &directoryPath, const Options &options = Options{})
{
    if (!options.ignoredPaths.isEmpty()) {
        const QString cleaned = QDir::cleanPath(directoryPath);
        if (options.ignoredPaths.contains(cleaned)) {
            return true;
        }
        // Also prune anything below an ignored folder, so adding
        // /home/u/photos/private skips its whole subtree without listing each
        // child.
        for (const QString &ignored : options.ignoredPaths) {
            if (cleaned.startsWith(ignored + QLatin1Char('/'))) {
                return true;
            }
        }
    }
    return isIgnoredDirectoryName(QFileInfo(directoryPath).fileName(), options);
}

} // namespace scan
} // namespace kg
