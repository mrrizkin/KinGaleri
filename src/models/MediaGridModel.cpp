#include "models/MediaGridModel.h"

#include "app/PathUtils.h"
#include "app/Paths.h"
#include "core/db/Database.h"
#include "core/grouping/DateGrouper.h"
#include "core/model/SourceAlbum.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <algorithm>

namespace kg {

namespace {

constexpr int kDefaultColumns = 6;
constexpr int kMinColumns = 2;
constexpr int kMaxColumns = 24;

QString buildThumbUrl(qint64 mediaId, int revision)
{
    return QStringLiteral("image://thumbs/%1?v=%2").arg(mediaId).arg(revision);
}

/**
 * Turns free-typed user input into an FTS5 MATCH expression.
 *
 * Two properties matter here:
 *
 *  - **Safety.** Everything the user types is wrapped in double quotes, with
 *    embedded quotes doubled. FTS5 has its own query syntax (operators like AND,
 *    OR, NOT, NEAR, ^, -, and column filters such as `filename:`), so a raw query
 *    would let a stray `OR` or a leading `-` silently change what is being
 *    searched. Quoting makes the input literal data and nothing else.
 *
 *  - **Prefix matching.** Each term gets a trailing `*` so that typing "vac"
 *    matches "vacation.jpg" as the user types, rather than requiring the whole
 *    token. This is what a filename search is expected to do.
 *
 * Term splitting deserves care. FTS5's unicode61 tokenizer treats punctuation
 * and symbols as *separators*, so `IMG_20250808_070657.jpg` is indexed as four
 * tokens: `img`, `20250808`, `070657`, `jpg`. The user's input has to be split
 * the same way. Deleting punctuation instead of substituting a separator would
 * fuse `IMG_20250808` into the single term `img20250808`, which is not a token
 * anywhere in the index, so the query would silently return nothing.
 *
 * Terms are joined by whitespace, which FTS5 treats as implicit AND - every term
 * must appear somewhere in the row. Returns an empty string when the input holds
 * no token that FTS can index (e.g. the user typed only punctuation), which the
 * caller turns into a query that matches nothing.
 */
QString buildFtsExpression(const QString &input)
{
    QString normalized;
    normalized.reserve(input.size());
    for (const QChar &ch : input) {
        normalized.append(ch.isPunct() || ch.isSymbol() ? QLatin1Char(' ') : ch);
    }

    const QStringList terms = normalized.split(QRegularExpression(QStringLiteral("\\s+")),
                                                Qt::SkipEmptyParts);
    QStringList parts;
    parts.reserve(terms.size());
    for (const QString &term : terms) {
        QString quoted = term;
        // Nothing above can leave a quote in place, but this keeps the expression
        // safe if the separator set is ever narrowed.
        quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        parts.append(QStringLiteral("\"%1\"*").arg(quoted));
    }
    return parts.join(QLatin1Char(' '));
}

} // namespace

// ===========================================================================
// MediaRowModel
// ===========================================================================
MediaRowModel::MediaRowModel(MediaGridModel *grid, int rowIndex)
    : QAbstractListModel(grid)
    , m_grid(grid)
    , m_rowIndex(rowIndex)
{
}

int MediaRowModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_grid) {
        return 0;
    }
    const int first = m_rowIndex * m_grid->columns();
    const int total = m_grid->itemCount();
    if (first >= total) {
        return 0;
    }
    return std::min(m_grid->columns(), total - first);
}

QVariant MediaRowModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_grid) {
        return {};
    }
    const int itemIndex = m_rowIndex * m_grid->columns() + index.row();
    const MediaGridModel::Entry *entry = m_grid->entryAt(itemIndex);
    if (!entry) {
        return {};
    }

    switch (role) {
    case IdRole:
        return entry->item.id;
    case ThumbUrlRole:
        return buildThumbUrl(entry->item.id, entry->revision);
    case FileNameRole:
        return entry->item.filename;
    case PathRole:
        return entry->item.path;
    case KindRole:
        return mediaKindToString(entry->item.kind);
    case IsFavoriteRole:
        return entry->item.isFavorite;
    case IsSyntheticRole:
        return entry->item.isSynthetic;
    case SourceAlbumRole:
        return entry->item.sourceAlbum;
    case TakenAtRole:
        return entry->item.takenAt;
    case TakenAtTextRole:
        return QLocale::system().toString(entry->item.takenAtLocal(), QLocale::ShortFormat);
    case SizeTextRole:
        return Paths::sizeForLog(entry->item.sizeBytes);
    case DimensionsRole:
        return entry->item.width > 0 && entry->item.height > 0
                   ? QStringLiteral("%1 × %2").arg(entry->item.width).arg(entry->item.height)
                   : QString();
    case DayLabelRole:
        return entry->dayLabel;
    case WidthRole:
        return entry->item.width;
    case HeightRole:
        return entry->item.height;
    case ColumnIndexRole:
        return index.row();
    case OrientationRole:
        return entry->item.orientation;
    case RevisionRole:
        return entry->revision;
    case SelectedRole:
        return m_grid->isSelected(entry->item.id);
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> MediaRowModel::roleNames() const
{
    static const QHash<int, QByteArray> names = {
        {IdRole, QByteArrayLiteral("mediaId")},
        {ThumbUrlRole, QByteArrayLiteral("thumbUrl")},
        {FileNameRole, QByteArrayLiteral("fileName")},
        {PathRole, QByteArrayLiteral("filePath")},
        {KindRole, QByteArrayLiteral("kind")},
        {IsFavoriteRole, QByteArrayLiteral("isFavorite")},
        {IsSyntheticRole, QByteArrayLiteral("isSynthetic")},
        {SourceAlbumRole, QByteArrayLiteral("sourceAlbum")},
        {TakenAtRole, QByteArrayLiteral("takenAt")},
        {TakenAtTextRole, QByteArrayLiteral("takenAtText")},
        {SizeTextRole, QByteArrayLiteral("sizeText")},
        {DimensionsRole, QByteArrayLiteral("dimensions")},
        {DayLabelRole, QByteArrayLiteral("dayLabel")},
        {WidthRole, QByteArrayLiteral("pixelWidth")},
        {HeightRole, QByteArrayLiteral("pixelHeight")},
        {ColumnIndexRole, QByteArrayLiteral("columnIndex")},
        {OrientationRole, QByteArrayLiteral("orientation")},
        {RevisionRole, QByteArrayLiteral("revision")},
        {SelectedRole, QByteArrayLiteral("selected")},
    };
    return names;
}

QString MediaRowModel::dayKey() const
{
    if (!m_grid) {
        return {};
    }
    const MediaGridModel::Entry *entry = m_grid->entryAt(m_rowIndex * m_grid->columns());
    return entry ? entry->dayKey : QString();
}

QString MediaRowModel::dayLabel() const
{
    if (!m_grid) {
        return {};
    }
    const MediaGridModel::Entry *entry = m_grid->entryAt(m_rowIndex * m_grid->columns());
    return entry ? entry->dayLabel : QString();
}

void MediaRowModel::notifyEntryChanged(int column)
{
    const int count = rowCount();
    if (column < 0 || column >= count) {
        return;
    }
    const QModelIndex idx = index(column, 0);
    // A single entry can change its thumbnail, its favourite flag or its selection
    // state; emitting all three roles keeps the notify path uniform (and Qt only
    // re-evaluates the bindings that use them).
    Q_EMIT dataChanged(idx, idx, {ThumbUrlRole, RevisionRole, IsFavoriteRole, SelectedRole});
}

void MediaRowModel::notifySelectionChanged(int column)
{
    const int count = rowCount();
    if (column < 0 || column >= count) {
        return;
    }
    const QModelIndex idx = index(column, 0);
    Q_EMIT dataChanged(idx, idx, {SelectedRole});
}

void MediaRowModel::notifySelectionRangeChanged()
{
    const int count = rowCount();
    if (count <= 0) {
        return;
    }
    Q_EMIT dataChanged(index(0, 0), index(count - 1, 0), {SelectedRole});
}

void MediaRowModel::notifyRowChanged()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

// ===========================================================================
// MediaGridModel
// ===========================================================================
MediaGridModel::MediaGridModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

MediaGridModel::~MediaGridModel() = default;

void MediaGridModel::attachDatabase(Database *database)
{
    m_database = database;
}

int MediaGridModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || m_columns <= 0) {
        return 0;
    }
    return (static_cast<int>(m_entries.size()) + m_columns - 1) / m_columns;
}

QVariant MediaGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const int row = index.row();
    const int firstItem = row * m_columns;

    switch (role) {
    case ItemsModelRole:
        return QVariant::fromValue(static_cast<QObject *>(rowModel(row)));
    case RowDayKeyRole: {
        const Entry *entry = entryAt(firstItem);
        return entry ? entry->dayKey : QString();
    }
    case RowDayLabelRole: {
        const Entry *entry = entryAt(firstItem);
        return entry ? entry->dayLabel : QString();
    }
    case RowIndexRole:
        return row;
    case RowItemCountRole:
        return std::min(m_columns, static_cast<int>(m_entries.size()) - firstItem);
    case RowFirstItemIndexRole:
        return firstItem;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> MediaGridModel::roleNames() const
{
    static const QHash<int, QByteArray> names = {
        {ItemsModelRole, QByteArrayLiteral("itemsModel")},
        {RowDayKeyRole, QByteArrayLiteral("dayKey")},
        {RowDayLabelRole, QByteArrayLiteral("dayLabel")},
        {RowIndexRole, QByteArrayLiteral("rowIndex")},
        {RowItemCountRole, QByteArrayLiteral("itemCount")},
        {RowFirstItemIndexRole, QByteArrayLiteral("firstItemIndex")},
    };
    return names;
}

void MediaGridModel::setColumns(int columns)
{
    const int clamped = std::clamp(columns, kMinColumns, kMaxColumns);
    if (clamped == m_columns) {
        return;
    }
    beginResetModel();
    m_columns = clamped;
    invalidateRows();
    endResetModel();
    Q_EMIT columnsChanged();
    Q_EMIT countChanged();
}

void MediaGridModel::setFilterKind(const QString &kind)
{
    if (kind == m_filterKind) {
        return;
    }
    m_filterKind = kind;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::setFavoritesOnly(bool value)
{
    if (value == m_favoritesOnly) {
        return;
    }
    m_favoritesOnly = value;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::setSourceAlbum(const QString &album)
{
    if (album == m_sourceAlbum) {
        return;
    }
    m_sourceAlbum = album;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::setAlbumId(qint64 albumId)
{
    const qint64 clamped = std::max<qint64>(0, albumId);
    if (clamped == m_albumId) {
        return;
    }
    m_albumId = clamped;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::setLimit(int limit)
{
    const int clamped = std::max(0, limit);
    if (clamped == m_limit) {
        return;
    }
    m_limit = clamped;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::setSearchQuery(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed == m_searchQuery) {
        return;
    }
    m_searchQuery = trimmed;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::setTrashOnly(bool value)
{
    if (value == m_trashOnly) {
        return;
    }
    m_trashOnly = value;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::setDateRange(qint64 fromMs, qint64 toMs)
{
    // Accept the bounds in either order rather than silently matching nothing.
    if (fromMs > 0 && toMs > 0 && fromMs > toMs) {
        std::swap(fromMs, toMs);
    }
    if (fromMs == m_dateFrom && toMs == m_dateTo) {
        return;
    }
    m_dateFrom = std::max<qint64>(0, fromMs);
    m_dateTo = std::max<qint64>(0, toMs);
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::clearDateRange()
{
    setDateRange(0, 0);
}

QString MediaGridModel::dateRangeLabel() const
{
    if (m_dateFrom <= 0 && m_dateTo <= 0) {
        return QString();
    }
    const QLocale locale = QLocale::system();
    const QDate from = m_dateFrom > 0
        ? QDateTime::fromMSecsSinceEpoch(m_dateFrom).date()
        : QDate();
    const QDate to = m_dateTo > 0 ? QDateTime::fromMSecsSinceEpoch(m_dateTo).date() : QDate();

    if (from.isValid() && to.isValid()) {
        if (from == to) {
            return locale.toString(from, QLocale::ShortFormat);
        }
        return QStringLiteral("%1 – %2")
            .arg(locale.toString(from, QLocale::ShortFormat),
                 locale.toString(to, QLocale::ShortFormat));
    }
    if (from.isValid()) {
        return QStringLiteral("from %1").arg(locale.toString(from, QLocale::ShortFormat));
    }
    return QStringLiteral("until %1").arg(locale.toString(to, QLocale::ShortFormat));
}

void MediaGridModel::clearFilters()
{
    m_filterKind.clear();
    m_favoritesOnly = false;
    m_sourceAlbum.clear();
    m_searchQuery.clear();
    m_limit = 0;
    m_dateFrom = 0;
    m_dateTo = 0;
    m_albumId = 0;
    Q_EMIT filterChanged();
    reload();
}

void MediaGridModel::invalidateRows()
{
    // Row models are cheap but cached; drop them whenever the item list moves.
    m_rowCache.clear();
}

MediaRowModel *MediaGridModel::rowModel(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= rowCount()) {
        return nullptr;
    }
    const auto it = m_rowCache.constFind(rowIndex);
    if (it != m_rowCache.constEnd()) {
        return it->get();
    }
    auto created = std::make_shared<MediaRowModel>(const_cast<MediaGridModel *>(this), rowIndex);
    MediaRowModel *raw = created.get();
    m_rowCache.insert(rowIndex, std::move(created));
    return raw;
}

const MediaGridModel::Entry *MediaGridModel::entryAt(int itemIndex) const
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(m_entries.size())) {
        return nullptr;
    }
    return &m_entries.at(itemIndex);
}

int MediaGridModel::rowOfItem(int itemIndex) const
{
    if (itemIndex < 0 || m_columns <= 0) {
        return -1;
    }
    return itemIndex / m_columns;
}

void MediaGridModel::reload()
{
    m_error.clear();
    m_entries.clear();
    m_indexById.clear();
    m_totalBytes = 0;

    if (!m_database) {
        m_error = QStringLiteral("No database attached");
        beginResetModel();
        invalidateRows();
        endResetModel();
        rebuildSummary();
        Q_EMIT countChanged();
        Q_EMIT reloadFinished();
        return;
    }

    QString sql = QStringLiteral(
        "SELECT id, path, filename, ext, mime, size_bytes, mtime_ns, taken_at, taken_at_src, "
        "       width, height, duration_ms, kind, is_favorite, is_hidden, is_synthetic, orientation, "
        "       source_album "
        "FROM media ");

    // The library and the recycle bin are the same table read on opposite sides of
    // `deleted_at`, so the predicate is switched rather than added.
    sql += m_trashOnly ? QStringLiteral("WHERE deleted_at IS NOT NULL ")
                       : QStringLiteral("WHERE deleted_at IS NULL ");

    if (m_favoritesOnly) {
        sql += QStringLiteral("AND is_favorite = 1 ");
    }
    if (!m_filterKind.isEmpty()) {
        sql += QStringLiteral("AND kind = :kind ");
    }
    // Album filtering used to be an in-memory pass over the whole result set, which
    // also meant the LIMIT was applied *before* the filter and the byte total was
    // summed over filtered-out rows. source_album is a real column now, so the
    // filter belongs here with the others (PRD 4.2: no O(n) work in the load path).
    if (!m_sourceAlbum.isEmpty()) {
        sql += QStringLiteral("AND source_album = :album COLLATE NOCASE ");
    }
    // Capture-date range (PRD F-4.2). Bounds are epoch ms; 0 disables that side.
    // taken_at is nullable for rows with no EXIF/filename/mtime signal, and NULL
    // fails both comparisons, which is the honest behaviour: an item with no
    // known date cannot be inside a known date window.
    if (m_dateFrom > 0) {
        sql += QStringLiteral("AND taken_at >= :dateFrom ");
    }
    if (m_dateTo > 0) {
        sql += QStringLiteral("AND taken_at <= :dateTo ");
    }
    // Manual album membership (PRD F-2.8). The subquery uses the album_items
    // primary key prefix, so SQLite resolves it with an index scan.
    if (m_albumId > 0) {
        sql += QStringLiteral("AND id IN (SELECT media_id FROM album_items WHERE album_id = :albumId) ");
    }

    // Search is a full-text query against the FTS5 index when SQLite has one, and
    // falls back to a LIKE scan when it does not. LIKE '%x%' can never use an
    // index, which is why the FTS path exists: the PRD asks for < 50 ms at 100k
    // items and a scan cannot promise that.
    //
    // The expression is built before the SQL so the shape of the statement is
    // decided once. A query made only of punctuation has no indexable token at
    // all, and `AND 0` is the honest answer there: the user typed something that
    // cannot match, so matching everything would be a lie.
    QString ftsExpression;
    if (!m_searchQuery.isEmpty() && m_database->ftsAvailable()) {
        ftsExpression = buildFtsExpression(m_searchQuery);
    }
    const bool useFts = !m_searchQuery.isEmpty() && m_database->ftsAvailable();
    if (useFts) {
        if (ftsExpression.isEmpty()) {
            sql += QStringLiteral("AND 0 ");
        } else {
            sql += QStringLiteral("AND id IN (SELECT rowid FROM media_fts WHERE media_fts MATCH :fts) ");
        }
    } else if (!m_searchQuery.isEmpty()) {
        sql += QStringLiteral("AND filename LIKE :search ESCAPE '\\' ");
    }

    sql += QStringLiteral("ORDER BY taken_at DESC, id DESC ");
    if (m_limit > 0) {
        sql += QStringLiteral("LIMIT :limit");
    }

    QSqlQuery query(m_database->connection());
    query.prepare(sql);
    if (!m_filterKind.isEmpty()) {
        query.bindValue(QStringLiteral(":kind"), m_filterKind);
    }
    if (!m_sourceAlbum.isEmpty()) {
        query.bindValue(QStringLiteral(":album"), m_sourceAlbum);
    }
    if (m_dateFrom > 0) {
        query.bindValue(QStringLiteral(":dateFrom"), m_dateFrom);
    }
    if (m_dateTo > 0) {
        query.bindValue(QStringLiteral(":dateTo"), m_dateTo);
    }
    if (m_albumId > 0) {
        query.bindValue(QStringLiteral(":albumId"), m_albumId);
    }
    if (useFts && !ftsExpression.isEmpty()) {
        query.bindValue(QStringLiteral(":fts"), ftsExpression);
    } else if (!m_searchQuery.isEmpty() && !useFts) {
        QString pattern = m_searchQuery;
        // Escape the LIKE wildcards so a literal % or _ in the query still matches
        // only itself.
        pattern.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        pattern.replace(QLatin1Char('%'), QStringLiteral("\\%"));
        pattern.replace(QLatin1Char('_'), QStringLiteral("\\_"));
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%") + pattern + QStringLiteral("%"));
    }
    if (m_limit > 0) {
        query.bindValue(QStringLiteral(":limit"), m_limit);
    }

    if (!query.exec()) {
        m_error = query.lastError().text();
        qWarning("kin-galeri: grid reload failed: %s", qUtf8Printable(m_error));
    } else {
        m_entries.reserve(static_cast<int>(std::min<qint64>(query.size() > 0 ? query.size() : 4096, 200000)));

        while (query.next()) {
            MediaItem item;
            item.id = query.value(0).toLongLong();
            item.path = query.value(1).toString();
            item.filename = query.value(2).toString();
            item.extension = query.value(3).toString();
            item.mimeType = query.value(4).toString();
            item.sizeBytes = query.value(5).toLongLong();
            item.mtimeNs = query.value(6).toLongLong();
            item.takenAt = query.value(7).isNull() ? 0LL : query.value(7).toLongLong();
            item.takenAtSource = takenAtSourceFromString(query.value(8).toString());
            item.width = query.value(9).toInt();
            item.height = query.value(10).toInt();
            item.durationMs = query.value(11).toInt();
            item.kind = mediaKindFromString(query.value(12).toString());
            item.isFavorite = query.value(13).toInt() != 0;
            item.isHidden = query.value(14).toInt() != 0;
            item.isSynthetic = query.value(15).toInt() != 0;
            item.orientation = query.value(16).toInt() > 0 ? query.value(16).toInt() : 1;

            if (item.takenAt <= 0) {
                // Fall back to the newest of mtime so nothing sinks to the bottom.
                item.takenAt = item.mtimeNs / 1000000;
            }

            // The column is nullable (rows written before migration v3), so fall
            // back to deriving it rather than showing an empty album.
            item.sourceAlbum = query.value(17).toString();
            if (item.sourceAlbum.isEmpty()) {
                item.sourceAlbum = SourceAlbum::detect(item.path, item.filename);
            }

            m_totalBytes += item.sizeBytes;
            // Seed the revision from a map that outlives this reload. Qt's pixmap cache
            // is keyed by URL, so if a URL once returned a placeholder it must never be
            // requested again. Resetting the revision to 0 on every reload did exactly
            // that: the cell asked for ?v=0 again, Qt served the cached placeholder, and
            // the freshly generated thumbnail was never shown.
            m_entries.append(Entry{item, DateGrouper::dayKey(item.takenAt),
                                   DateGrouper::dayLabel(item.takenAt),
                                   m_thumbnailRevisions.value(item.id, 0)});
        }
    }

    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        m_indexById.insert(m_entries.at(i).item.id, i);
    }

    beginResetModel();
    invalidateRows();
    endResetModel();

    rebuildSummary();
    Q_EMIT countChanged();
    Q_EMIT albumsChanged();
    Q_EMIT reloadFinished();
}

void MediaGridModel::rebuildSummary()
{
    const QLocale locale = QLocale::system();
    const QString itemText = locale.toString(static_cast<qlonglong>(m_entries.size()));
    const QString sizeText = Paths::sizeForLog(m_totalBytes);

    QString filterText;
    if (m_trashOnly) {
        filterText += QStringLiteral(" · recycle bin");
    }
    if (m_favoritesOnly) {
        filterText += QStringLiteral(" · favorites");
    }
    if (!m_filterKind.isEmpty()) {
        filterText += QStringLiteral(" · ") + m_filterKind;
    }
    if (!m_sourceAlbum.isEmpty()) {
        filterText += QStringLiteral(" · ") + m_sourceAlbum;
    }
    if (!m_searchQuery.isEmpty()) {
        filterText += QStringLiteral(" · \"%1\"").arg(m_searchQuery);
    }
    const QString range = dateRangeLabel();
    if (!range.isEmpty()) {
        filterText += QStringLiteral(" · ") + range;
    }
    if (m_albumId > 0) {
        for (const QVariant &entry : albums()) {
            const QVariantMap map = entry.toMap();
            if (map.value(QStringLiteral("id")).toLongLong() == m_albumId) {
                filterText += QStringLiteral(" · album: ") + map.value(QStringLiteral("name")).toString();
                break;
            }
        }
    }

    m_summary = QStringLiteral("%1 items · %2%3").arg(itemText, sizeText, filterText);
    Q_EMIT summaryChanged();
}

QString MediaGridModel::thumbUrl(int itemIndex) const
{
    const Entry *entry = entryAt(itemIndex);
    if (!entry) {
        return {};
    }
    return buildThumbUrl(entry->item.id, entry->revision);
}

QString MediaGridModel::thumbUrlForId(qint64 mediaId) const
{
    const auto it = m_indexById.constFind(mediaId);
    if (it == m_indexById.constEnd()) {
        return {};
    }
    return buildThumbUrl(mediaId, m_entries.at(*it).revision);
}

QVariantMap MediaGridModel::itemAt(int itemIndex) const
{
    QVariantMap map;
    const Entry *entry = entryAt(itemIndex);
    if (!entry) {
        return map;
    }

    const MediaItem &item = entry->item;
    map.insert(QStringLiteral("mediaId"), item.id);
    map.insert(QStringLiteral("id"), item.id);
    map.insert(QStringLiteral("path"), item.path);
    map.insert(QStringLiteral("fileName"), item.filename);
    map.insert(QStringLiteral("mimeType"), item.mimeType);
    map.insert(QStringLiteral("sizeBytes"), item.sizeBytes);
    map.insert(QStringLiteral("sizeText"), Paths::sizeForLog(item.sizeBytes));
    map.insert(QStringLiteral("kind"), mediaKindToString(item.kind));
    map.insert(QStringLiteral("takenAt"), item.takenAt);
    map.insert(QStringLiteral("takenAtText"),
               QLocale::system().toString(item.takenAtLocal(), QLocale::ShortFormat));
    map.insert(QStringLiteral("takenAtSource"), takenAtSourceToString(item.takenAtSource));
    map.insert(QStringLiteral("dayLabel"), entry->dayLabel);
    map.insert(QStringLiteral("sourceAlbum"), item.sourceAlbum);
    map.insert(QStringLiteral("isFavorite"), item.isFavorite);
    map.insert(QStringLiteral("isSynthetic"), item.isSynthetic);
    map.insert(QStringLiteral("width"), item.width);
    map.insert(QStringLiteral("height"), item.height);
    map.insert(QStringLiteral("durationMs"), item.durationMs);
    map.insert(QStringLiteral("orientation"), item.orientation);
    map.insert(QStringLiteral("thumbUrl"), buildThumbUrl(item.id, entry->revision));
    return map;
}

QString MediaGridModel::rowDayLabel(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= rowCount()) {
        return {};
    }
    const int firstItem = rowIndex * m_columns;
    if (firstItem < 0 || firstItem >= static_cast<int>(m_entries.size())) {
        return {};
    }
    return m_entries.at(firstItem).dayLabel;
}

void MediaGridModel::toggleFavorite(int itemIndex)
{
    Entry *entry = nullptr;
    if (itemIndex >= 0 && itemIndex < static_cast<int>(m_entries.size())) {
        entry = &m_entries[itemIndex];
    }
    if (!entry || !m_database) {
        return;
    }

    entry->item.isFavorite = !entry->item.isFavorite;

    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral("UPDATE media SET is_favorite = ? WHERE id = ?"));
    query.addBindValue(entry->item.isFavorite ? 1 : 0);
    query.addBindValue(entry->item.id);
    if (!query.exec()) {
        qWarning("kin-galeri: could not persist favorite state: %s",
                 qUtf8Printable(query.lastError().text()));
    }

    const int row = rowOfItem(itemIndex);
    if (MediaRowModel *model = rowModel(row)) {
        const int column = itemIndex - row * m_columns;
        model->notifyEntryChanged(column);
    }
}

int MediaGridModel::indexOfId(qint64 mediaId) const
{
    const auto it = m_indexById.constFind(mediaId);
    return it == m_indexById.constEnd() ? -1 : *it;
}

int MediaGridModel::toggleFavoriteById(qint64 mediaId)
{
    const int index = indexOfId(mediaId);
    if (index >= 0) {
        toggleFavorite(index);
    }
    return index;
}

void MediaGridModel::toggleFavoriteFromPayload(const QVariantMap &payload)
{
    const qint64 id = payload.value(QStringLiteral("mediaId")).toLongLong();
    toggleFavoriteById(id);
}

void MediaGridModel::onThumbnailReady(qint64 mediaId)
{
    // Bump the revision for every id, not just the loaded rows: album covers live
    // outside the current filter, and their URL has to change so QML re-requests
    // the now-ready thumbnail instead of caching the placeholder.
    const int revision = ++m_thumbnailRevisions[mediaId];
    if (m_albumCoverIds.contains(mediaId)) {
        Q_EMIT albumCoverReady(mediaId);
    }

    const auto it = m_indexById.constFind(mediaId);
    if (it == m_indexById.constEnd()) {
        return;
    }

    const int itemIndex = *it;
    Entry &entry = m_entries[itemIndex];
    // Monotonic per media id, and deliberately not cleared by reload(): the revision is
    // part of the image URL, so it must only ever move forward. See the seeding comment
    // in reload().
    entry.revision = revision;

    const int row = rowOfItem(itemIndex);
    if (MediaRowModel *model = rowModel(row)) {
        model->notifyEntryChanged(itemIndex - row * m_columns);
    }
}

QVector<qint64> MediaGridModel::mediaIds() const
{
    QVector<qint64> ids;
    ids.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        ids.append(entry.item.id);
    }
    return ids;
}

int MediaGridModel::trashCount() const
{
    if (!m_database) {
        return 0;
    }
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM media WHERE deleted_at IS NOT NULL"));
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

QVariantMap MediaGridModel::mediaDetail(qint64 mediaId) const
{
    QVariantMap map;
    if (!m_database || mediaId < 0) {
        return map;
    }

    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral(R"SQL(
SELECT path, filename, ext, mime, size_bytes, mtime_ns, taken_at, taken_at_src,
       width, height, duration_ms, orientation, camera_make, camera_model, lens_model,
       iso, f_number, exposure_time, focal_length, gps_lat, gps_lon,
       kind, is_favorite, is_synthetic, indexed_at, deleted_at, source_album
FROM media WHERE id = :id)SQL"));
    query.bindValue(QStringLiteral(":id"), mediaId);
    if (!query.exec() || !query.next()) {
        return map;
    }

    const QLocale locale = QLocale::system();
    const QString takenAtText = query.value(6).isNull()
        ? QString()
        : locale.toString(QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong()).toLocalTime(),
                          QLocale::ShortFormat);

    map.insert(QStringLiteral("id"), mediaId);
    map.insert(QStringLiteral("path"), query.value(0).toString());
    map.insert(QStringLiteral("fileName"), query.value(1).toString());
    map.insert(QStringLiteral("extension"), query.value(2).toString());
    map.insert(QStringLiteral("mimeType"), query.value(3).toString());
    map.insert(QStringLiteral("sizeBytes"), query.value(4).toLongLong());
    map.insert(QStringLiteral("sizeText"), Paths::sizeForLog(query.value(4).toLongLong()));
    map.insert(QStringLiteral("modifiedAtText"),
               locale.toString(QDateTime::fromMSecsSinceEpoch(query.value(5).toLongLong() / 1000000)
                                   .toLocalTime(),
                               QLocale::ShortFormat));
    map.insert(QStringLiteral("takenAt"), query.value(6).toLongLong());
    map.insert(QStringLiteral("takenAtText"), takenAtText);
    map.insert(QStringLiteral("takenAtSource"), query.value(7).toString());
    const int width = query.value(8).toInt();
    const int height = query.value(9).toInt();
    map.insert(QStringLiteral("width"), width);
    map.insert(QStringLiteral("height"), height);
    map.insert(QStringLiteral("dimensions"),
               width > 0 ? QStringLiteral("%1 × %2").arg(width).arg(height) : QString());
    // Extra derived rows for the info panel (PRD F-3.8).
    map.insert(QStringLiteral("megapixels"),
               width > 0 && height > 0
                   ? QStringLiteral("%1 MP").arg(width * height / 1000000.0, 0, 'f', 1)
                   : QString());
    const int durationMs = query.value(10).toInt();
    map.insert(QStringLiteral("durationMs"), durationMs);
    if (durationMs > 0) {
        const int totalSeconds = durationMs / 1000;
        const int seconds = totalSeconds % 60;
        const int minutes = (totalSeconds / 60) % 60;
        const int hours = totalSeconds / 3600;
        map.insert(QStringLiteral("durationText"),
                   hours > 0 ? QStringLiteral("%1:%2:%3")
                                   .arg(hours)
                                   .arg(minutes, 2, 10, QLatin1Char('0'))
                                   .arg(seconds, 2, 10, QLatin1Char('0'))
                             : QStringLiteral("%1:%2")
                                   .arg(minutes)
                                   .arg(seconds, 2, 10, QLatin1Char('0')));
    }
    map.insert(QStringLiteral("orientation"), query.value(11).toInt());
    map.insert(QStringLiteral("cameraMake"), query.value(12).toString());
    map.insert(QStringLiteral("cameraModel"), query.value(13).toString());
    map.insert(QStringLiteral("lensModel"), query.value(14).toString());
    map.insert(QStringLiteral("iso"), query.value(15).isNull() ? QVariant() : query.value(15));
    map.insert(QStringLiteral("fNumber"), query.value(16).isNull() ? QVariant() : query.value(16));
    map.insert(QStringLiteral("exposureTime"), query.value(17).toString());
    map.insert(QStringLiteral("focalLength"), query.value(18).isNull() ? QVariant() : query.value(18));

    const bool hasGps = !query.value(19).isNull() && !query.value(20).isNull();
    map.insert(QStringLiteral("hasGps"), hasGps);
    if (hasGps) {
        map.insert(QStringLiteral("gpsLat"), query.value(19).toDouble());
        map.insert(QStringLiteral("gpsLon"), query.value(20).toDouble());
        map.insert(QStringLiteral("gpsText"),
                   QStringLiteral("%1, %2")
                       .arg(query.value(19).toDouble(), 0, 'f', 5)
                       .arg(query.value(20).toDouble(), 0, 'f', 5));
    }

    map.insert(QStringLiteral("kind"), query.value(21).toString());
    map.insert(QStringLiteral("isFavorite"), query.value(22).toInt() != 0);
    map.insert(QStringLiteral("isSynthetic"), query.value(23).toInt() != 0);
    map.insert(QStringLiteral("indexedAtText"),
               locale.toString(QDateTime::fromMSecsSinceEpoch(query.value(24).toLongLong()).toLocalTime(),
                               QLocale::ShortFormat));
    map.insert(QStringLiteral("inTrash"), !query.value(25).isNull());
    // Prefer the stored album; derive it only for rows written before migration v3.
    QString storedAlbum = query.value(26).toString();
    if (storedAlbum.isEmpty()) {
        storedAlbum = SourceAlbum::detect(map.value(QStringLiteral("path")).toString(),
                                          map.value(QStringLiteral("fileName")).toString());
    }
    map.insert(QStringLiteral("sourceAlbum"), storedAlbum);
    map.insert(QStringLiteral("thumbUrl"), thumbUrlForId(mediaId));
    return map;
}

void MediaGridModel::moveToTrash(int itemIndex)
{
    const Entry *entry = entryAt(itemIndex);
    if (!entry) {
        return;
    }
    moveToTrashById(entry->item.id);
}

void MediaGridModel::moveToTrashById(qint64 mediaId)
{
    if (!m_database || mediaId < 0) {
        return;
    }

    applyDeleted({mediaId}, true);

    UndoAction action;
    action.type = UndoAction::Trash;
    action.label = QStringLiteral("Move to recycle bin");
    action.ids = {mediaId};
    pushUndo(action);
    Q_EMIT actionPerformed(action.label);

    Q_EMIT trashChanged();
    reload();
}

void MediaGridModel::restoreFromTrash(int itemIndex)
{
    const Entry *entry = entryAt(itemIndex);
    if (!entry) {
        return;
    }
    restoreFromTrashById(entry->item.id);
}

void MediaGridModel::restoreFromTrashById(qint64 mediaId)
{
    if (!m_database || mediaId < 0) {
        return;
    }

    applyDeleted({mediaId}, false);

    UndoAction action;
    action.type = UndoAction::Restore;
    action.label = QStringLiteral("Restore from recycle bin");
    action.ids = {mediaId};
    pushUndo(action);
    Q_EMIT actionPerformed(action.label);

    Q_EMIT trashChanged();
    reload();
}

void MediaGridModel::emptyTrash()
{
    if (!m_database) {
        return;
    }

    QSqlQuery query(m_database->connection());
    // Only the database rows go away. Removing media files from disk is a
    // deliberate, separate action the user takes outside the library (PRD 4.3).
    query.prepare(QStringLiteral("DELETE FROM media WHERE deleted_at IS NOT NULL"));
    if (!query.exec()) {
        qWarning("kin-galeri: could not empty the recycle bin: %s",
                 qUtf8Printable(query.lastError().text()));
        return;
    }

    Q_EMIT trashChanged();
    reload();
}

QStringList MediaGridModel::albumNames() const
{
    QStringList names;
    if (!m_database) {
        return names;
    }

    QSqlQuery query(m_database->connection());
    // DISTINCT over the indexed column, restricted to live rows so the sidebar
    // does not offer an album that only exists inside the recycle bin.
    query.prepare(QStringLiteral(
        "SELECT DISTINCT source_album FROM media "
        "WHERE deleted_at IS NULL AND source_album IS NOT NULL AND source_album <> '' "
        "ORDER BY source_album COLLATE NOCASE"));
    if (!query.exec()) {
        qWarning("kin-galeri: could not list albums: %s", qUtf8Printable(query.lastError().text()));
        return names;
    }
    while (query.next()) {
        names.append(query.value(0).toString());
    }
    return names;
}

QStringList MediaGridModel::rootPaths() const
{
    QStringList roots;
    if (!m_database) {
        return roots;
    }
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral(
        "SELECT path FROM roots WHERE enabled = 1 AND is_synthetic = 0 ORDER BY path"));
    if (!query.exec()) {
        qWarning("kin-galeri: could not list roots: %s", qUtf8Printable(query.lastError().text()));
        return roots;
    }
    while (query.next()) {
        roots.append(query.value(0).toString());
    }
    return roots;
}

// ---------------------------------------------------------------------------
// Manual albums (PRD F-2.8 / F-5.2)
// ---------------------------------------------------------------------------
QVariantList MediaGridModel::albums() const
{
    QVariantList list;
    if (!m_database) {
        return list;
    }

    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral(
        "SELECT a.id, a.name, COUNT(ai.media_id) "
        "FROM albums a LEFT JOIN album_items ai ON ai.album_id = a.id "
        "WHERE a.kind = 'manual' "
        "GROUP BY a.id, a.name ORDER BY a.name COLLATE NOCASE"));
    if (!query.exec()) {
        qWarning("kin-galeri: could not list manual albums: %s",
                 qUtf8Printable(query.lastError().text()));
        return list;
    }

    while (query.next()) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), query.value(0).toLongLong());
        map.insert(QStringLiteral("name"), query.value(1).toString());
        map.insert(QStringLiteral("count"), query.value(2).toInt());
        list.append(map);
    }
    return list;
}

QVariantList MediaGridModel::albumCards() const
{
    QVariantList cards;
    if (!m_database) {
        return cards;
    }

    m_albumCoverIds.clear();

    const auto append = [this, &cards](const QString &kind, qint64 id, const QString &name,
                                       int count, qint64 coverId) {
        if (coverId > 0) {
            m_albumCoverIds.insert(coverId);
        }
        QVariantMap map;
        map.insert(QStringLiteral("kind"), kind);
        map.insert(QStringLiteral("id"), id);
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("count"), count);
        map.insert(QStringLiteral("coverId"), coverId);
        map.insert(QStringLiteral("coverUrl"),
                   coverId > 0 ? buildThumbUrl(coverId, m_thumbnailRevisions.value(coverId, 0))
                               : QString());
        cards.append(map);
    };

    // Manual albums lead: the ones the user made should be easy to find.
    QSqlQuery manual(m_database->connection());
    manual.prepare(QStringLiteral(
        "SELECT a.id, a.name, "
        "  (SELECT COUNT(*) FROM album_items ai JOIN media m ON m.id = ai.media_id "
        "     WHERE ai.album_id = a.id AND m.deleted_at IS NULL), "
        "  (SELECT ai2.media_id FROM album_items ai2 JOIN media m2 ON m2.id = ai2.media_id "
        "     WHERE ai2.album_id = a.id AND m2.deleted_at IS NULL "
        "     ORDER BY m2.taken_at DESC, m2.id DESC LIMIT 1) "
        "FROM albums a WHERE a.kind = 'manual' ORDER BY a.name COLLATE NOCASE"));
    if (!manual.exec()) {
        qWarning("kin-galeri: could not list album cards: %s",
                 qUtf8Printable(manual.lastError().text()));
    } else {
        while (manual.next()) {
            append(QStringLiteral("manual"), manual.value(0).toLongLong(),
                   manual.value(1).toString(), manual.value(2).toInt(),
                   manual.value(3).toLongLong());
        }
    }

    // Folder/camera albums derived from the indexed rows.
    QSqlQuery source(m_database->connection());
    source.prepare(QStringLiteral(
        "SELECT source_album, COUNT(*), "
        "  (SELECT m2.id FROM media m2 WHERE m2.source_album = media.source_album "
        "     AND m2.deleted_at IS NULL ORDER BY m2.taken_at DESC, m2.id DESC LIMIT 1) "
        "FROM media WHERE deleted_at IS NULL AND source_album IS NOT NULL "
        "AND source_album <> '' GROUP BY source_album ORDER BY source_album COLLATE NOCASE"));
    if (!source.exec()) {
        qWarning("kin-galeri: could not list source albums: %s",
                 qUtf8Printable(source.lastError().text()));
    } else {
        while (source.next()) {
            append(QStringLiteral("source"), 0, source.value(0).toString(),
                   source.value(1).toInt(), source.value(2).toLongLong());
        }
    }

    return cards;
}

QString MediaGridModel::albumCoverUrl(qint64 mediaId) const
{
    if (mediaId <= 0) {
        return {};
    }
    return buildThumbUrl(mediaId, m_thumbnailRevisions.value(mediaId, 0));
}

qint64 MediaGridModel::createAlbum(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!m_database || trimmed.isEmpty()) {
        return 0;
    }

    // UNIQUE(kind, name) would reject a duplicate insert anyway; returning the
    // existing id is friendlier and makes "add to new album" idempotent.
    QSqlQuery existing(m_database->connection());
    existing.prepare(QStringLiteral(
        "SELECT id FROM albums WHERE kind = 'manual' AND name = ? COLLATE NOCASE"));
    existing.addBindValue(trimmed);
    if (existing.exec() && existing.next()) {
        return existing.value(0).toLongLong();
    }

    QSqlQuery insert(m_database->connection());
    insert.prepare(QStringLiteral(
        "INSERT INTO albums(kind, name, created_at) VALUES('manual', ?, ?)"));
    insert.addBindValue(trimmed);
    insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!insert.exec()) {
        qWarning("kin-galeri: could not create album '%s': %s", qUtf8Printable(trimmed),
                 qUtf8Printable(insert.lastError().text()));
        return 0;
    }
    Q_EMIT albumsChanged();
    return insert.lastInsertId().toLongLong();
}

void MediaGridModel::renameAlbum(qint64 albumId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!m_database || albumId <= 0 || trimmed.isEmpty()) {
        return;
    }

    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral(
        "UPDATE albums SET name = ? WHERE id = ? AND kind = 'manual'"));
    query.addBindValue(trimmed);
    query.addBindValue(albumId);
    if (!query.exec()) {
        qWarning("kin-galeri: could not rename album: %s", qUtf8Printable(query.lastError().text()));
        return;
    }
    Q_EMIT albumsChanged();
    if (m_albumId == albumId) {
        rebuildSummary();
    }
}

void MediaGridModel::deleteAlbum(qint64 albumId)
{
    if (!m_database || albumId <= 0) {
        return;
    }

    // album_items rows go away through ON DELETE CASCADE; media rows are untouched.
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral("DELETE FROM albums WHERE id = ? AND kind = 'manual'"));
    query.addBindValue(albumId);
    if (!query.exec()) {
        qWarning("kin-galeri: could not delete album: %s", qUtf8Printable(query.lastError().text()));
        return;
    }

    if (m_albumId == albumId) {
        m_albumId = 0;
        Q_EMIT filterChanged();
        reload();
    }
    Q_EMIT albumsChanged();
}

void MediaGridModel::addToAlbum(qint64 albumId, const QVariantList &mediaIds)
{
    if (!m_database || albumId <= 0 || mediaIds.isEmpty()) {
        return;
    }

    QString error;
    m_database->begin(&error);
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO album_items(album_id, media_id, added_at) VALUES(?, ?, ?)"));
    int added = 0;
    for (const QVariant &value : mediaIds) {
        const qint64 mediaId = value.toLongLong();
        if (mediaId <= 0) {
            continue;
        }
        query.bindValue(0, albumId);
        query.bindValue(1, mediaId);
        query.bindValue(2, QDateTime::currentMSecsSinceEpoch());
        if (query.exec() && query.numRowsAffected() > 0) {
            ++added;
        }
    }
    m_database->commit(&error);

    if (added > 0) {
        // Give the album a cover if it does not have one yet.
        QSqlQuery cover(m_database->connection());
        cover.prepare(QStringLiteral(
            "UPDATE albums SET cover_media_id = COALESCE(cover_media_id, ?) WHERE id = ?"));
        cover.addBindValue(mediaIds.first().toLongLong());
        cover.addBindValue(albumId);
        cover.exec();

        Q_EMIT albumsChanged();
        if (m_albumId == albumId) {
            reload();
        }
    }
}

void MediaGridModel::removeFromAlbum(qint64 albumId, const QVariantList &mediaIds)
{
    if (!m_database || albumId <= 0 || mediaIds.isEmpty()) {
        return;
    }

    QString error;
    m_database->begin(&error);
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral("DELETE FROM album_items WHERE album_id = ? AND media_id = ?"));
    for (const QVariant &value : mediaIds) {
        const qint64 mediaId = value.toLongLong();
        if (mediaId <= 0) {
            continue;
        }
        query.bindValue(0, albumId);
        query.bindValue(1, mediaId);
        query.exec();
    }
    m_database->commit(&error);

    Q_EMIT albumsChanged();
    if (m_albumId == albumId) {
        reload();
    }
}

// ---------------------------------------------------------------------------
// Multi-selection (PRD F-3.7 / F-5.7)
// ---------------------------------------------------------------------------
void MediaGridModel::setSelectionMode(bool enabled)
{
    if (enabled == m_selectionMode) {
        return;
    }
    m_selectionMode = enabled;
    if (!enabled && !m_selected.isEmpty()) {
        m_selected.clear();
        notifySelection();
        Q_EMIT selectionChanged();
    }
    Q_EMIT selectionModeChanged();
}

bool MediaGridModel::isSelected(qint64 mediaId) const
{
    return m_selected.contains(mediaId);
}

void MediaGridModel::setSelected(qint64 mediaId, bool selected)
{
    if (mediaId <= 0) {
        return;
    }
    const bool already = m_selected.contains(mediaId);
    if (already == selected) {
        return;
    }
    if (selected) {
        m_selected.insert(mediaId);
    } else {
        m_selected.remove(mediaId);
    }

    const int itemIndex = indexOfId(mediaId);
    if (itemIndex >= 0) {
        const int row = rowOfItem(itemIndex);
        if (MediaRowModel *model = rowModel(row)) {
            model->notifySelectionChanged(itemIndex - row * m_columns);
        }
    }
    Q_EMIT selectionChanged();
}

void MediaGridModel::toggleSelected(qint64 mediaId)
{
    setSelected(mediaId, !isSelected(mediaId));
}

void MediaGridModel::selectAll()
{
    if (m_entries.isEmpty() || m_selected.size() == static_cast<int>(m_entries.size())) {
        return;
    }
    for (const Entry &entry : m_entries) {
        m_selected.insert(entry.item.id);
    }
    notifySelection();
    Q_EMIT selectionChanged();
}

void MediaGridModel::selectRange(int fromItemIndex, int toItemIndex, bool additive)
{
    const int count = itemCount();
    if (count <= 0) {
        return;
    }
    const int first = std::clamp(std::min(fromItemIndex, toItemIndex), 0, count - 1);
    const int last = std::clamp(std::max(fromItemIndex, toItemIndex), 0, count - 1);
    if (!additive) {
        m_selected.clear();
    }
    for (int i = first; i <= last; ++i) {
        m_selected.insert(m_entries.at(i).item.id);
    }
    notifySelection();
    Q_EMIT selectionChanged();
}

void MediaGridModel::clearSelection()
{
    if (m_selected.isEmpty()) {
        return;
    }
    m_selected.clear();
    notifySelection();
    Q_EMIT selectionChanged();
}

QVariantList MediaGridModel::selectedIds() const
{
    QVariantList list;
    list.reserve(m_selected.size());
    for (qint64 mediaId : m_selected) {
        list.append(mediaId);
    }
    return list;
}

void MediaGridModel::notifySelection()
{
    for (auto it = m_rowCache.begin(); it != m_rowCache.end(); ++it) {
        it->get()->notifySelectionRangeChanged();
    }
}

void MediaGridModel::addSelectionToAlbum(qint64 albumId)
{
    if (m_selected.isEmpty()) {
        return;
    }
    addToAlbum(albumId, selectedIds());
    clearSelection();
}

void MediaGridModel::removeSelectionFromAlbum(qint64 albumId)
{
    if (m_selected.isEmpty()) {
        return;
    }
    removeFromAlbum(albumId, selectedIds());
    clearSelection();
}

void MediaGridModel::setSelectionFavorite(bool favorite)
{
    if (!m_database || m_selected.isEmpty()) {
        return;
    }

    QString error;
    m_database->begin(&error);
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral("UPDATE media SET is_favorite = ? WHERE id = ?"));
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        Entry &entry = m_entries[i];
        if (!m_selected.contains(entry.item.id) || entry.item.isFavorite == favorite) {
            continue;
        }
        entry.item.isFavorite = favorite;
        query.bindValue(0, favorite ? 1 : 0);
        query.bindValue(1, entry.item.id);
        query.exec();

        const int row = rowOfItem(i);
        if (MediaRowModel *model = rowModel(row)) {
            model->notifyEntryChanged(i - row * m_columns);
        }
    }
    m_database->commit(&error);
    Q_EMIT selectionChanged();
}

void MediaGridModel::trashSelection()
{
    if (!m_database || m_selected.isEmpty()) {
        return;
    }

    const QVector<qint64> ids(m_selected.cbegin(), m_selected.cend());
    applyDeleted(ids, true);

    UndoAction action;
    action.type = UndoAction::Trash;
    action.label = QStringLiteral("Move %1 item(s) to recycle bin").arg(ids.size());
    action.ids = ids;
    pushUndo(action);
    Q_EMIT actionPerformed(action.label);

    clearSelection();
    Q_EMIT trashChanged();
    reload();
}

void MediaGridModel::restoreSelection()
{
    if (!m_database || m_selected.isEmpty()) {
        return;
    }

    const QVector<qint64> ids(m_selected.cbegin(), m_selected.cend());
    applyDeleted(ids, false);

    UndoAction action;
    action.type = UndoAction::Restore;
    action.label = QStringLiteral("Restore %1 item(s)").arg(ids.size());
    action.ids = ids;
    pushUndo(action);
    Q_EMIT actionPerformed(action.label);

    clearSelection();
    Q_EMIT trashChanged();
    reload();
}

// ---------------------------------------------------------------------------
// File operations + undo (PRD F-5.1)
// ---------------------------------------------------------------------------
void MediaGridModel::pushUndo(UndoAction action)
{
    m_undoStack.append(std::move(action));
    // A bounded history is plenty and keeps memory flat over a long session.
    constexpr int kMaxUndo = 32;
    while (m_undoStack.size() > kMaxUndo) {
        m_undoStack.removeFirst();
    }
    Q_EMIT undoChanged();
}

void MediaGridModel::clearUndo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }
    m_undoStack.clear();
    Q_EMIT undoChanged();
}

void MediaGridModel::applyDeleted(const QVector<qint64> &ids, bool deleted)
{
    if (!m_database || ids.isEmpty()) {
        return;
    }

    QString error;
    m_database->begin(&error);
    QSqlQuery query(m_database->connection());
    query.prepare(deleted ? QStringLiteral("UPDATE media SET deleted_at = ? WHERE id = ?")
                          : QStringLiteral("UPDATE media SET deleted_at = NULL WHERE id = ?"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (qint64 mediaId : ids) {
        if (deleted) {
            query.bindValue(0, now);
            query.bindValue(1, mediaId);
        } else {
            query.bindValue(0, mediaId);
        }
        if (!query.exec()) {
            qWarning("kin-galeri: could not change deleted_at for %lld: %s",
                     static_cast<long long>(mediaId), qUtf8Printable(query.lastError().text()));
        }
    }
    m_database->commit(&error);
}

bool MediaGridModel::updateMediaPath(qint64 mediaId, const QString &newPath, QString *error)
{
    const QFileInfo info(newPath);
    const QString extension = info.suffix().toLower();

    // Mime is intentionally left alone when only the name changes; a true
    // extension change would need a re-index, and silently guessing here would
    // desync kind/mime from what the indexer would have written.
    QSqlQuery query(m_database->connection());
    query.prepare(QStringLiteral(
        "UPDATE media SET path = ?, filename = ?, ext = ?, source_album = ? WHERE id = ?"));
    query.addBindValue(newPath);
    query.addBindValue(info.fileName());
    query.addBindValue(extension.isEmpty() ? QVariant() : QVariant(extension));
    query.addBindValue(SourceAlbum::detect(newPath, info.fileName()));
    query.addBindValue(mediaId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool MediaGridModel::moveOnDisk(const QString &from, const QString &to) const
{
    if (QFile::rename(from, to)) {
        return true;
    }

    // rename fails across filesystems (EXDEV). Fall back to copy+remove, which is
    // the documented behaviour of a "move" even when it is not atomic.
    if (!QFile::copy(from, to)) {
        return false;
    }
    if (!QFile::remove(from)) {
        QFile::remove(to);
        return false;
    }
    return true;
}

QString MediaGridModel::renameMedia(qint64 mediaId, const QString &newName)
{
    if (!m_database) {
        return QStringLiteral("No database attached");
    }
    const int index = indexOfId(mediaId);
    if (index < 0) {
        return QStringLiteral("The item is not loaded");
    }

    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/'))) {
        return QStringLiteral("Enter a file name without a path separator");
    }

    const QString oldPath = m_entries.at(index).item.path;
    const QFileInfo oldInfo(oldPath);
    if (trimmed == oldInfo.fileName()) {
        return QString();
    }

    const QString newPath = oldInfo.absolutePath() + QLatin1Char('/') + trimmed;
    if (QFileInfo::exists(newPath)) {
        return QStringLiteral("A file named '%1' already exists here").arg(trimmed);
    }
    if (!moveOnDisk(oldPath, newPath)) {
        return QStringLiteral("Could not rename the file on disk");
    }

    QString dbError;
    if (!updateMediaPath(mediaId, newPath, &dbError)) {
        // Keep disk and database consistent: put the file back.
        moveOnDisk(newPath, oldPath);
        return QStringLiteral("Could not update the library: %1").arg(dbError);
    }

    UndoAction action;
    action.type = UndoAction::Rename;
    action.label = QStringLiteral("Rename to %1").arg(trimmed);
    action.ids = {mediaId};
    action.oldPaths = {oldPath};
    action.newPaths = {newPath};
    pushUndo(action);
    Q_EMIT actionPerformed(action.label);
    reload();
    return QString();
}

QString MediaGridModel::moveMedia(const QVariantList &mediaIds, const QString &targetDir)
{
    if (!m_database) {
        return QStringLiteral("No database attached");
    }

    const QFileInfo dirInfo(localPath(targetDir));
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        return QStringLiteral("Target folder does not exist");
    }

    UndoAction action;
    action.type = UndoAction::Move;

    for (const QVariant &value : mediaIds) {
        const qint64 mediaId = value.toLongLong();
        const int index = indexOfId(mediaId);
        if (index < 0) {
            continue;
        }

        const QString oldPath = m_entries.at(index).item.path;
        const QString newPath = dirInfo.absoluteFilePath() + QLatin1Char('/')
                                + QFileInfo(oldPath).fileName();
        if (oldPath == newPath) {
            continue;
        }
        if (QFileInfo::exists(newPath)) {
            // Roll back what we already moved so the batch stays all-or-nothing.
            for (int i = 0; i < action.ids.size(); ++i) {
                moveOnDisk(action.newPaths.at(i), action.oldPaths.at(i));
            }
            return QStringLiteral("'%1' already exists in the target folder")
                .arg(QFileInfo(oldPath).fileName());
        }
        if (!moveOnDisk(oldPath, newPath)) {
            for (int i = 0; i < action.ids.size(); ++i) {
                moveOnDisk(action.newPaths.at(i), action.oldPaths.at(i));
            }
            return QStringLiteral("Could not move '%1'").arg(QFileInfo(oldPath).fileName());
        }

        action.ids.append(mediaId);
        action.oldPaths.append(oldPath);
        action.newPaths.append(newPath);
    }

    if (action.ids.isEmpty()) {
        return QString();
    }

    for (int i = 0; i < action.ids.size(); ++i) {
        QString dbError;
        if (!updateMediaPath(action.ids.at(i), action.newPaths.at(i), &dbError)) {
            for (int j = 0; j < action.ids.size(); ++j) {
                moveOnDisk(action.newPaths.at(j), action.oldPaths.at(j));
            }
            return QStringLiteral("Could not update the library: %1").arg(dbError);
        }
    }

    action.label = QStringLiteral("Move %1 item(s)").arg(action.ids.size());
    pushUndo(action);
    Q_EMIT actionPerformed(action.label);
    reload();
    return QString();
}

void MediaGridModel::undo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }

    const UndoAction action = m_undoStack.takeLast();
    Q_EMIT undoChanged();

    switch (action.type) {
    case UndoAction::Rename:
    case UndoAction::Move:
        for (int i = 0; i < action.ids.size(); ++i) {
            const QString from = action.newPaths.value(i);
            const QString to = action.oldPaths.value(i);
            if (!from.isEmpty() && !to.isEmpty() && QFileInfo::exists(from)
                && !QFileInfo::exists(to)) {
                moveOnDisk(from, to);
            }
            updateMediaPath(action.ids.at(i), to, nullptr);
        }
        Q_EMIT actionPerformed(QStringLiteral("Undid ") + action.label);
        reload();
        break;
    case UndoAction::Trash:
        applyDeleted(action.ids, false);
        Q_EMIT actionPerformed(QStringLiteral("Undid ") + action.label);
        Q_EMIT trashChanged();
        reload();
        break;
    case UndoAction::Restore:
        applyDeleted(action.ids, true);
        Q_EMIT actionPerformed(QStringLiteral("Undid ") + action.label);
        Q_EMIT trashChanged();
        reload();
        break;
    }
}

} // namespace kg
