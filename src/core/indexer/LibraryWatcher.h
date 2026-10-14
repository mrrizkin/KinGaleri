#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "core/indexer/ScanPolicy.h"

namespace kg {

/**
 * Watches the library roots for filesystem changes and emits a debounced
 * `changed` signal (PRD F-1.4).
 *
 * QFileSystemWatcher is inotify-backed on Linux but is **not** recursive, so
 * every directory under a root has to be registered explicitly. The number of
 * watched directories is capped: beyond the cap the watcher degrades to watching
 * what it managed to register instead of exhausting the kernel's inotify watch
 * limit (which would break the desktop, not just this app).
 */
class LibraryWatcher : public QObject
{
    Q_OBJECT

public:
    explicit LibraryWatcher(QObject *parent = nullptr);

    /// Sets (or replaces) the watched roots. Rebuilding is skipped when the list
    /// is unchanged and watches are already in place.
    void setRoots(const QStringList &roots);

    /// Pruning policy applied while registering watches. Must mirror the
    /// indexer's options, otherwise the watcher either burns budget on
    /// directories the indexer skips or misses changes in directories it walks.
    void setScanOptions(const scan::Options &options);

    /// Re-walks the roots and registers any newly created directories. Called
    /// after each rescan so a folder created inside the tree becomes watched.
    void refresh();
    void stop();

    int watchedDirectoryCount() const { return static_cast<int>(m_watcher.directories().size()); }
    bool truncated() const { return m_truncated; }

signals:
    /// Debounced: emitted once after a burst of filesystem events settles.
    void changed();

private:
    void onDirectoryChanged(const QString &path);

    static constexpr int kMaxWatchedDirectories = 4096;

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QStringList m_roots;
    scan::Options m_scanOptions;
    bool m_truncated = false;
    bool m_refreshing = false;
};

} // namespace kg
