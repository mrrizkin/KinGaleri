#include "core/indexer/LibraryWatcher.h"

#include "core/indexer/ScanPolicy.h"

#include <QDir>

namespace kg {

LibraryWatcher::LibraryWatcher(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(900);
    connect(&m_debounce, &QTimer::timeout, this, &LibraryWatcher::changed);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &LibraryWatcher::onDirectoryChanged);
}

void LibraryWatcher::setRoots(const QStringList &roots)
{
    if (roots == m_roots && !m_watcher.directories().isEmpty()) {
        return;
    }
    m_roots = roots;
    refresh();
}

void LibraryWatcher::setScanOptions(const scan::Options &options)
{
    if (options.includeHidden == m_scanOptions.includeHidden
        && options.ignoreJunk == m_scanOptions.ignoreJunk
        && options.followSymlinks == m_scanOptions.followSymlinks
        && options.ignoredPaths == m_scanOptions.ignoredPaths
        && options.ignoredPatterns == m_scanOptions.ignoredPatterns
        && options.junkPatterns == m_scanOptions.junkPatterns) {
        return;
    }
    m_scanOptions = options;
    if (!m_roots.isEmpty()) {
        refresh();
    }
}

void LibraryWatcher::refresh()
{
    // refresh() is called from a signal handler that itself may be triggered by
    // removePaths/addPaths; the guard keeps that from recursing.
    if (m_refreshing) {
        return;
    }
    m_refreshing = true;

    const QStringList current = m_watcher.directories();
    if (!current.isEmpty()) {
        m_watcher.removePaths(current);
    }

    m_truncated = false;
    QStringList dirs;
    int budget = kMaxWatchedDirectories;

    // Only maintained when following symlinks; keeps a link that points back into
    // an already-watched tree from looping.
    QSet<QString> visited;

    for (const QString &root : m_roots) {
        QDir rootDir(root);
        if (!rootDir.exists()) {
            continue;
        }
        if (budget <= 0) {
            m_truncated = true;
            break;
        }
        dirs.append(QDir::cleanPath(root));
        if (m_scanOptions.followSymlinks) {
            visited.insert(rootDir.canonicalPath());
        }
        --budget;

        // Explicit stack rather than QDirIterator: the watcher must prune the
        // same subtrees the indexer skips, otherwise it burns its watch budget on
        // directories that can never contribute a media row.
        QStringList stack;
        stack.append(QDir::cleanPath(root));
        while (!stack.isEmpty()) {
            if (budget <= 0) {
                m_truncated = true;
                break;
            }
            const QFileInfoList children =
                QDir(stack.takeLast()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &child : children) {
                if (budget <= 0) {
                    m_truncated = true;
                    break;
                }
                if (child.isSymLink()) {
                    if (!child.exists()) {
                        continue;
                    }
                    if (child.isDir()) {
                        if (!m_scanOptions.followSymlinks) {
                            continue;
                        }
                    } else {
                        continue;
                    }
                }
                if (scan::isIgnoredDirectory(child.absoluteFilePath(), m_scanOptions)) {
                    continue;
                }
                if (m_scanOptions.followSymlinks) {
                    const QString canonical = child.canonicalFilePath();
                    if (canonical.isEmpty() || visited.contains(canonical)) {
                        continue;
                    }
                    visited.insert(canonical);
                }
                dirs.append(child.absoluteFilePath());
                stack.append(child.absoluteFilePath());
                --budget;
            }
        }
    }

    dirs.removeDuplicates();
    if (!dirs.isEmpty()) {
        m_watcher.addPaths(dirs);
    }

    m_refreshing = false;
}

void LibraryWatcher::stop()
{
    const QStringList current = m_watcher.directories();
    if (!current.isEmpty()) {
        m_watcher.removePaths(current);
    }
    m_debounce.stop();
}

void LibraryWatcher::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path)
    if (m_refreshing) {
        return;
    }
    // Restart on every event so a burst (copying a folder, extracting an archive)
    // produces one rescan instead of dozens.
    m_debounce.start();
}

} // namespace kg
