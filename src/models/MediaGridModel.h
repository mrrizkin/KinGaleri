#pragma once

#include "core/model/MediaItem.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include <memory>

namespace kg {

class MediaGridModel;

/**
 * One row of the grid. Owned by MediaGridModel and handed to QML through the
 * `itemsModel` role of the row.
 *
 * The split exists so that a thumbnail becoming available only invalidates the
 * single cell that changed, instead of forcing the whole row (and its QML
 * delegates) to be rebuilt. With a flat per-row QVariantList that rebuild cost
 * shows up as jank during the first cold scroll.
 */
class MediaRowModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int rowIndex READ rowIndex CONSTANT)
    Q_PROPERTY(QString dayKey READ dayKey NOTIFY revisionChanged)
    Q_PROPERTY(QString dayLabel READ dayLabel NOTIFY revisionChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY revisionChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum ItemRole {
        IdRole = Qt::UserRole + 1,
        ThumbUrlRole,
        FileNameRole,
        PathRole,
        KindRole,
        IsFavoriteRole,
        IsSyntheticRole,
        SourceAlbumRole,
        TakenAtRole,
        TakenAtTextRole,
        SizeTextRole,
        DimensionsRole,
        DayLabelRole,
        WidthRole,
        HeightRole,
        ColumnIndexRole,
        OrientationRole,
        RevisionRole,
        SelectedRole,
    };
    Q_ENUM(ItemRole)

    MediaRowModel(MediaGridModel *grid, int rowIndex);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int rowIndex() const { return m_rowIndex; }
    QString dayKey() const;
    QString dayLabel() const;
    int revision() const { return m_revision; }

    /// Called by MediaGridModel when one of this row's entries changed.
    void notifyEntryChanged(int column);
    /// Called when only the selection state of one cell changed.
    void notifySelectionChanged(int column);
    /// Called when the selection of the whole row may have changed.
    void notifySelectionRangeChanged();
    /// Called when the day label of the row's first entry changed.
    void notifyRowChanged();

signals:
    void revisionChanged();

private:
    MediaGridModel *m_grid = nullptr;
    int m_rowIndex = 0;
    int m_revision = 0;
};

/**
 * Grid model: the outer list is *rows*, each holding `columns` media items
 * (PRD 5.5). `dayLabel` on a row drives the sticky section header.
 *
 * Rows are materialised lazily and cached, so memory stays proportional to the
 * number of visible rows rather than to the library size.
 */
class MediaGridModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY countChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(QString filterKind READ filterKind WRITE setFilterKind NOTIFY filterChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filterChanged)
    Q_PROPERTY(QString sourceAlbum READ sourceAlbum WRITE setSourceAlbum NOTIFY filterChanged)
    /** Exact-file-folder filter used by "Open with": only items whose immediate
     *  parent directory is this path. Empty = no folder filter. */
    Q_PROPERTY(QString folderPath READ folderPath WRITE setFolderPath NOTIFY filterChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY filterChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY filterChanged)
    Q_PROPERTY(bool trashOnly READ trashOnly WRITE setTrashOnly NOTIFY filterChanged)
    /** Lower bound of the taken-at filter, epoch ms. 0 = unbounded. */
    Q_PROPERTY(qint64 dateFrom READ dateFrom NOTIFY filterChanged)
    /** Upper bound of the taken-at filter, epoch ms. 0 = unbounded. */
    Q_PROPERTY(qint64 dateTo READ dateTo NOTIFY filterChanged)
    /** Human readable summary of the active date range, for the filter chip. */
    Q_PROPERTY(QString dateRangeLabel READ dateRangeLabel NOTIFY filterChanged)
    Q_PROPERTY(int trashCount READ trashCount NOTIFY trashChanged)
    Q_PROPERTY(QStringList albumNames READ albumNames NOTIFY albumsChanged)
    /** Manual albums as a list of {id, name, count} maps (PRD F-2.8). */
    Q_PROPERTY(QVariantList albums READ albums NOTIFY albumsChanged)
    /**
     * Albums for the album overview page: manual albums followed by the
     * folder/camera albums detected while indexing. Each entry is
     * {kind, id, name, count, coverId, coverUrl}. Manual albums carry a real id;
     * source albums use kind "source" and filter by name.
     */
    Q_PROPERTY(QVariantList albumCards READ albumCards NOTIFY albumsChanged)
    /** Active manual-album filter id, or 0 when none. */
    Q_PROPERTY(qint64 albumId READ albumId WRITE setAlbumId NOTIFY filterChanged)
    /** Smart-grouping filters (M3). id 0 = inactive. */
    Q_PROPERTY(qint64 placeId READ placeId WRITE setPlaceId NOTIFY filterChanged)
    Q_PROPERTY(qint64 personId READ personId WRITE setPersonId NOTIFY filterChanged)
    Q_PROPERTY(qint64 eventId READ eventId WRITE setEventId NOTIFY filterChanged)
    Q_PROPERTY(QString smartCategory READ smartCategory WRITE setSmartCategory NOTIFY filterChanged)
    /** Single-tag filter, matched case-insensitively. Empty = no tag filter. */
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY filterChanged)
    /** Occurrence lists for the People / Places / Events / Smart pages. Each entry
     *  is {kind, id, name, subtitle, count, coverId, coverUrl}. */
    Q_PROPERTY(QVariantList places READ places NOTIFY collectionsChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY collectionsChanged)
    Q_PROPERTY(QVariantList people READ people NOTIFY collectionsChanged)
    Q_PROPERTY(QVariantList smartCategories READ smartCategories NOTIFY collectionsChanged)
    /** Tags that have at least one live media item: {kind:"tag", id, name, count, coverUrl}. */
    Q_PROPERTY(QVariantList tags READ tags NOTIFY collectionsChanged)
    /** Multi-selection state (PRD F-3.7 / F-5.7). */
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(bool selectionMode READ selectionMode WRITE setSelectionMode NOTIFY selectionModeChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY undoChanged)

public:
    enum RowRole {
        ItemsModelRole = Qt::UserRole + 1,
        RowDayKeyRole,
        RowDayLabelRole,
        RowIndexRole,
        RowItemCountRole,
        RowFirstItemIndexRole,
    };
    Q_ENUM(RowRole)

    struct Entry
    {
        MediaItem item;
        QString dayKey;
        QString dayLabel;
        int revision = 0;
    };

    /// Monotonic thumbnail revision per media id. Outlives reload() on purpose: the
    /// revision is embedded in the image URL, and Qt's pixmap cache remembers a
    /// placeholder forever if the same URL is requested twice. See reload().
    QHash<qint64, int> m_thumbnailRevisions;

    explicit MediaGridModel(QObject *parent = nullptr);
    ~MediaGridModel() override;

    /// Owns the Database pointer, so the model itself is not copyable/movable.
    void attachDatabase(class Database *database);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int itemCount() const { return static_cast<int>(m_entries.size()); }
    int columns() const { return m_columns; }
    void setColumns(int columns);

    QString summary() const { return m_summary; }

    QString filterKind() const { return m_filterKind; }
    void setFilterKind(const QString &kind);

    bool favoritesOnly() const { return m_favoritesOnly; }
    void setFavoritesOnly(bool value);

    QString sourceAlbum() const { return m_sourceAlbum; }
    void setSourceAlbum(const QString &album);

    QString folderPath() const { return m_folderPath; }
    void setFolderPath(const QString &path);

    int limit() const { return m_limit; }
    void setLimit(int limit);

    /// Substring match against the file name (PRD F-4.1). Empty = no filter.
    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &query);

    /// Switches the grid between the library and the recycle bin. The two are
    /// mutually exclusive because they query the same table on opposite sides of
    /// `deleted_at`.
    bool trashOnly() const { return m_trashOnly; }
    void setTrashOnly(bool value);

    /// Filter by capture date (PRD F-4.2). `from`/`to` are epoch ms; 0 means
    /// unbounded on that side. `to` is inclusive to the end of the day when the
    /// caller passes a start-of-day timestamp.
    qint64 dateFrom() const { return m_dateFrom; }
    qint64 dateTo() const { return m_dateTo; }
    QString dateRangeLabel() const;
    Q_INVOKABLE void setDateRange(qint64 fromMs, qint64 toMs);
    Q_INVOKABLE void clearDateRange();

    /// Rows currently sitting in the recycle bin, regardless of the active view.
    int trashCount() const;

    /// Re-reads the library according to the current filter.
    Q_INVOKABLE void reload();

    Q_INVOKABLE void clearFilters();

    /// Toggles the favourite flag and persists it.
    Q_INVOKABLE void toggleFavorite(int itemIndex);

    /// Toggles the favourite flag of the media row with the given id. Returns the
    /// item index, or -1 when the id is not loaded.
    Q_INVOKABLE int toggleFavoriteById(qint64 mediaId);

    /// Index of a media id within the currently loaded entries, or -1.
    Q_INVOKABLE int indexOfId(qint64 mediaId) const;

    /// Applies a favourite toggle coming from QML; `payload` is the map produced
    /// by MediaCell::buildPayload().
    Q_INVOKABLE void toggleFavoriteFromPayload(const QVariantMap &payload);

    /// Flattened item access for the row models and for QML tooltips.
    Q_INVOKABLE QVariantMap itemAt(int itemIndex) const;

    /// Payload for a file path, used when a file is opened from the command line
    /// (a file manager's "Open with"). Looks the path up in the library first so
    /// the viewer can favourite/navigate it; when the file is not indexed it
    /// still returns a payload with mediaId -1 built from the file itself, so an
    /// unindexed photo opens instead of silently landing on the library page.
    Q_INVOKABLE QVariantMap itemForPath(const QString &path) const;

    /// Day label of a grid row. The sticky section header asks for this as the
    /// user scrolls, so it has to be cheap: it only touches the row's first entry.
    Q_INVOKABLE QString rowDayLabel(int rowIndex) const;

    /// Full metadata for one item, read on demand from the database.
    ///
    /// The grid deliberately does not keep camera/GPS/exposure fields in memory:
    /// M0 measured ~0.9 KB per loaded row already, and those columns are only ever
    /// needed for a single item in the info panel or the viewer (PRD 4.1.1).
    Q_INVOKABLE QVariantMap mediaDetail(qint64 mediaId) const;

    /// Soft delete: hides the item from the library and moves it to the recycle
    /// bin by stamping `deleted_at`. No file on disk is touched (PRD 4.3).
    Q_INVOKABLE void moveToTrash(int itemIndex);
    Q_INVOKABLE void moveToTrashById(qint64 mediaId);

    /// Undoes a soft delete.
    Q_INVOKABLE void restoreFromTrash(int itemIndex);
    Q_INVOKABLE void restoreFromTrashById(qint64 mediaId);

    /// Drops every recycle-bin row from the database. Still never touches files.
    Q_INVOKABLE void emptyTrash();

    /// Distinct source-album names currently in the library, ordered by name.
    ///
    /// The sidebar used to hardcode this list, which meant a folder that matched
    /// no built-in rule (and so fell back to its parent directory) could never be
    /// selected. Reading it from the library also keeps the sidebar honest about
    /// what is actually there.
    ///
    /// Exposed as a property rather than an invokable so the sidebar re-evaluates
    /// it when the library changes; the query only runs when it is actually read.
    QStringList albumNames() const;
    /// Enabled, non-synthetic root folders. Used by the filesystem watcher.
    QStringList rootPaths() const;

    // ---- manual albums (PRD F-2.8 / F-5.2) --------------------------------
    QVariantList albums() const;
    /// Merged manual + source albums for the album overview grid.
    QVariantList albumCards() const;
    /// Cache-busted cover URL for an album cover thumbnail.
    Q_INVOKABLE QString albumCoverUrl(qint64 mediaId) const;
    qint64 albumId() const { return m_albumId; }
    void setAlbumId(qint64 albumId);

    // ---- smart grouping (M3) ----------------------------------------------
    qint64 placeId() const { return m_placeId; }
    void setPlaceId(qint64 placeId);
    qint64 personId() const { return m_personId; }
    void setPersonId(qint64 personId);
    qint64 eventId() const { return m_eventId; }
    void setEventId(qint64 eventId);
    QString smartCategory() const { return m_smartCategory; }
    void setSmartCategory(const QString &category);
    QString tag() const { return m_tag; }
    void setTag(const QString &tag);

    /// Cached-busting cover URL for a smart-grouping cover id, reusing the
    /// thumbnail revision map so a regenerated thumbnail refreshes the card.
    Q_INVOKABLE QString coverUrl(qint64 mediaId) const;

    QVariantList places() const;
    QVariantList events() const;
    QVariantList people() const;
    /// The fixed heuristic smart albums, with live counts.
    QVariantList smartCategories() const;
    /// Tags ordered by item count, each {kind:"tag", id, name, count, coverId, coverUrl}.
    QVariantList tags() const;
    /// Tag names attached to one media item, alphabetically.
    Q_INVOKABLE QStringList mediaTags(qint64 mediaId) const;
    /// Adds `name` to every media id in the list, creating the tag when needed.
    Q_INVOKABLE void addTagToMedia(const QVariantList &mediaIds, const QString &name);
    /// Removes `name` from every media id in the list, dropping the tag when it
    /// no longer labels anything.
    Q_INVOKABLE void removeTagFromMedia(const QVariantList &mediaIds, const QString &name);
    Q_INVOKABLE void renameTag(qint64 tagId, const QString &name);
    Q_INVOKABLE void deleteTag(qint64 tagId);
    /// Renames a person (PRD F-2.4 manual tidy-up). Empty names are ignored.
    Q_INVOKABLE void renamePerson(qint64 personId, const QString &name);
    /// Merges `sourceId` into `targetId`, moving faces and removing the source.
    Q_INVOKABLE void mergePeople(qint64 targetId, qint64 sourceId);
    /// Rebuilds places + events from the current library. Runs synchronously on
    /// the GUI thread; callers should only do this for small libraries or after
    /// an explicit user action.
    Q_INVOKABLE bool rebuildSmartGrouping();

    /// Creates (or returns the id of an existing) manual album with `name`.
    Q_INVOKABLE qint64 createAlbum(const QString &name);
    Q_INVOKABLE void renameAlbum(qint64 albumId, const QString &name);
    Q_INVOKABLE void deleteAlbum(qint64 albumId);
    Q_INVOKABLE void addToAlbum(qint64 albumId, const QVariantList &mediaIds);
    Q_INVOKABLE void removeFromAlbum(qint64 albumId, const QVariantList &mediaIds);

    // ---- selection (PRD F-3.7 / F-5.7) ------------------------------------
    int selectionCount() const { return static_cast<int>(m_selected.size()); }
    bool selectionMode() const { return m_selectionMode; }
    void setSelectionMode(bool enabled);

    Q_INVOKABLE bool isSelected(qint64 mediaId) const;
    Q_INVOKABLE void toggleSelected(qint64 mediaId);
    Q_INVOKABLE void setSelected(qint64 mediaId, bool selected);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectRange(int fromItemIndex, int toItemIndex, bool additive = true);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QVariantList selectedIds() const;

    /// Batch actions operate on the current selection (PRD F-5.7).
    Q_INVOKABLE void addSelectionToAlbum(qint64 albumId);
    Q_INVOKABLE void setSelectionFavorite(bool favorite);
    Q_INVOKABLE void trashSelection();
    Q_INVOKABLE void restoreSelection();
    Q_INVOKABLE void removeSelectionFromAlbum(qint64 albumId);

    // ---- file operations + undo (PRD F-5.1) -------------------------------
    /// Renames a file on disk and updates the library row. Returns an empty
    /// string on success, or a user-facing error message.
    Q_INVOKABLE QString renameMedia(qint64 mediaId, const QString &newName);
    /// Moves files into `targetDir` (same filesystem or copy+remove) and updates
    /// the library rows. Returns an error message or an empty string.
    Q_INVOKABLE QString moveMedia(const QVariantList &mediaIds, const QString &targetDir);

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    QString undoLabel() const { return m_undoStack.isEmpty() ? QString() : m_undoStack.last().label; }
    /// Reverts the most recent rename / move / recycle-bin action.
    Q_INVOKABLE void undo();
    void clearUndo();
    Q_INVOKABLE QString thumbUrl(int itemIndex) const;
    Q_INVOKABLE QString thumbUrlForId(qint64 mediaId) const;

    /// Called when ThumbnailService finished generating a thumbnail.
    void onThumbnailReady(qint64 mediaId);

    /// Read-only access used by MediaRowModel.
    const Entry *entryAt(int itemIndex) const;
    int rowOfItem(int itemIndex) const;
    MediaRowModel *rowModel(int rowIndex) const;

    /// Media ids currently loaded, used to prime ThumbnailService.
    QVector<qint64> mediaIds() const;

    /// Emitted after reload() so AppController can register thumb sources.
    QString currentError() const { return m_error; }

signals:
    void countChanged();
    void summaryChanged();
    void columnsChanged();
    void filterChanged();
    void reloadFinished();
    /// Emitted whenever the recycle-bin row count may have changed.
    void trashChanged();
    /// Emitted after reload(), so the sidebar picks up newly seen albums.
    void albumsChanged();
    /// Emitted when the People / Places / Events / Smart lists may have changed.
    void collectionsChanged();
    /// A cover thumbnail finished generating, so the album card can refresh.
    void albumCoverReady(qint64 mediaId);
    void selectionChanged();
    void selectionModeChanged();
    void undoChanged();
    void actionPerformed(const QString &description);
    /// User-facing feedback for actions the model performs itself.
    void infoMessage(const QString &message);

private:
    /// One reversible file/recycle operation. Paths are absolute and stored for
    /// every affected id so undo never has to consult the (possibly reloaded)
    /// entry list.
    struct UndoAction
    {
        enum Type { Rename, Move, Trash, Restore };
        Type type = Rename;
        QString label;
        QVector<qint64> ids;
        QStringList oldPaths;
        QStringList newPaths;
    };

    void pushUndo(UndoAction action);
    /// Applies a deleted_at change to many ids in one transaction; no undo.
    void applyDeleted(const QVector<qint64> &ids, bool deleted);
    /// Updates path/filename/ext/source_album for one row after a disk move.
    bool updateMediaPath(qint64 mediaId, const QString &newPath, QString *error);
    bool moveOnDisk(const QString &from, const QString &to) const;

    void rebuildSummary();
    void invalidateRows();
    void notifySelection();

    Database *m_database = nullptr;

    QVector<Entry> m_entries;
    QHash<qint64, int> m_indexById; ///< media id -> index in m_entries
    mutable QHash<int, std::shared_ptr<MediaRowModel>> m_rowCache;

    int m_columns = 6;
    int m_limit = 0;
    QString m_filterKind;
    bool m_favoritesOnly = false;
    QString m_sourceAlbum;
    QString m_folderPath;
    QString m_searchQuery;
    bool m_trashOnly = false;
    qint64 m_albumId = 0;
    qint64 m_placeId = 0;
    qint64 m_personId = 0;
    qint64 m_eventId = 0;
    QString m_smartCategory;
    QString m_tag;
    bool m_selectionMode = false;
    QSet<qint64> m_selected;
    /// Cover media ids produced by albumCards(), so onThumbnailReady knows which
    /// album cards to refresh.
    mutable QSet<qint64> m_albumCoverIds;
    /// Cache for the sidebar/collection aggregates (albumCards(), smartCategories()).
    /// They are pure functions of the database and QML re-reads them several times
    /// per reload - the albumsChanged/collectionsChanged bindings plus the thumbnail
    /// cover pass - which on a 100k library meant the same table scans ran three
    /// times per start. Cleared in reload(), which every mutation already goes
    /// through.
    mutable QHash<QString, QVariantList> m_aggregateCache;
    QVector<UndoAction> m_undoStack;
    qint64 m_dateFrom = 0;
    qint64 m_dateTo = 0;
    QString m_summary;
    QString m_error;
    qint64 m_totalBytes = 0;

    friend class MediaRowModel;
};

} // namespace kg
