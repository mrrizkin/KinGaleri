#include "core/db/LibraryMaintenance.h"

#include "core/db/Database.h"
#include "core/db/Migrations.h"
#include "core/model/SourceAlbum.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace kg::LibraryMaintenance {

namespace {

constexpr auto kSourceAlbumKey = "source_album_rules_version";
constexpr auto kFtsIndexKey = "fts_index_version";

QString describe(const QSqlError &error)
{
    return error.text().isEmpty() ? QStringLiteral("unknown SQL error") : error.text();
}

} // namespace

bool ensureSourceAlbum(Database &database, QString *error)
{
    const QString applied = database.metaValue(QString::fromLatin1(kSourceAlbumKey));
    if (applied == QString::number(kSourceAlbumRulesVersion)) {
        return true;
    }

    QSqlDatabase db = database.connection();

    // Read first, write second. The read is deliberately not inside the write
    // transaction: holding a transaction open while walking a cursor over the
    // same connection that issues the UPDATEs is a self-deadlock on some SQLite
    // builds, and re-deriving an album is idempotent anyway.
    QSqlQuery select(db);
    if (!select.exec(QStringLiteral("SELECT id, path, filename FROM media"))) {
        if (error) {
            *error = QStringLiteral("source album backfill could not read media: %1")
                         .arg(describe(select.lastError()));
        }
        return false;
    }

    QVector<QPair<qint64, QString>> pending;
    while (select.next()) {
        const qint64 id = select.value(0).toLongLong();
        const QString path = select.value(1).toString();
        const QString filename = select.value(2).toString();
        pending.append({id, SourceAlbum::detect(path, filename)});
    }
    select.finish();

    if (!database.begin(error)) {
        return false;
    }

    QSqlQuery update(db);
    if (!update.prepare(QStringLiteral("UPDATE media SET source_album = :album WHERE id = :id"))) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("source album backfill could not prepare: %1")
                         .arg(describe(update.lastError()));
        }
        return false;
    }

    for (const auto &[id, album] : pending) {
        update.bindValue(QStringLiteral(":album"), album);
        update.bindValue(QStringLiteral(":id"), id);
        if (!update.exec()) {
            database.rollback();
            if (error) {
                *error = QStringLiteral("source album backfill failed on id %1: %2")
                             .arg(id)
                             .arg(describe(update.lastError()));
            }
            return false;
        }
    }

    if (!database.setMetaValue(QString::fromLatin1(kSourceAlbumKey),
                               QString::number(kSourceAlbumRulesVersion))) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("source album backfill could not record its version");
        }
        return false;
    }

    return database.commit(error);
}

bool ensureFtsPopulated(Database &database, QString *error)
{
    if (!database.ftsAvailable()) {
        return true;
    }

    const QString applied = database.metaValue(QString::fromLatin1(kFtsIndexKey));
    if (applied == QString::number(kFtsIndexVersion)) {
        return true;
    }

    return rebuildFts(database, error);
}

bool rebuildFts(Database &database, QString *error)
{
    if (!database.ftsAvailable()) {
        if (error) {
            *error = QStringLiteral("This build of SQLite has no FTS5 support, so the "
                                    "search index cannot be rebuilt.");
        }
        return false;
    }

    if (!database.begin(error)) {
        return false;
    }

    // Recreate the table and triggers first: a library created on a build without
    // FTS5 never got them, and `IF NOT EXISTS` keeps this harmless when they are
    // already in place.
    for (const QString &statement : ftsSchemaStatements()) {
        if (!database.exec(statement, error)) {
            database.rollback();
            return false;
        }
    }

    // A full rebuild, not a gap-fill: the indexed content itself can change
    // between versions, so every row has to be re-derived. Both statements are
    // whole-table operations rather than a row-at-a-time loop, which keeps this
    // to a single pass over media.
    if (!database.exec(QStringLiteral("DELETE FROM media_fts"), error)) {
        database.rollback();
        return false;
    }

    if (!database.exec(
            QStringLiteral(
                "INSERT INTO media_fts(rowid, filename, camera_model, album, tags) "
                "SELECT m.id, m.filename, "
                "       trim(COALESCE(m.camera_make, '') || ' ' || COALESCE(m.camera_model, '')), "
                "       COALESCE(m.source_album, ''), '' "
                "FROM media m"),
            error)) {
        database.rollback();
        return false;
    }

    if (!database.setMetaValue(QString::fromLatin1(kFtsIndexKey), QString::number(kFtsIndexVersion))) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("FTS rebuild could not record its version");
        }
        return false;
    }

    return database.commit(error);
}

} // namespace kg::LibraryMaintenance
