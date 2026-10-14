#include "core/db/Migrations.h"

namespace kg {

namespace {

QList<Migration> buildMigrations()
{
    QList<Migration> list;

    // ---------------------------------------------------------------------
    // v1 - initial schema (PRD 5.3)
    // ---------------------------------------------------------------------
    Migration v1;
    v1.version = 1;
    v1.name = QStringLiteral("initial_schema");
    v1.statements = {
        QStringLiteral(R"SQL(
CREATE TABLE roots (
    id         INTEGER PRIMARY KEY,
    path       TEXT    NOT NULL UNIQUE,
    enabled    INTEGER NOT NULL DEFAULT 1,
    is_synthetic INTEGER NOT NULL DEFAULT 0,
    added_at   INTEGER NOT NULL
))SQL"),

        QStringLiteral(R"SQL(
CREATE TABLE media (
    id            INTEGER PRIMARY KEY,
    path          TEXT    NOT NULL UNIQUE,
    path_hash     BLOB,
    root_id       INTEGER NOT NULL REFERENCES roots(id) ON DELETE CASCADE,
    filename      TEXT    NOT NULL,
    ext           TEXT    NOT NULL,
    mime          TEXT    NOT NULL,
    size_bytes    INTEGER NOT NULL DEFAULT 0,
    mtime_ns      INTEGER NOT NULL DEFAULT 0,
    taken_at      INTEGER,
    taken_at_src  TEXT    NOT NULL DEFAULT 'unknown',
    width         INTEGER NOT NULL DEFAULT 0,
    height        INTEGER NOT NULL DEFAULT 0,
    duration_ms   INTEGER NOT NULL DEFAULT 0,
    orientation   INTEGER NOT NULL DEFAULT 1,
    gps_lat       REAL,
    gps_lon       REAL,
    camera_make   TEXT,
    camera_model  TEXT,
    lens_model    TEXT,
    iso           INTEGER,
    f_number      REAL,
    exposure_time TEXT,
    focal_length  REAL,
    hash_partial  BLOB,
    kind          TEXT    NOT NULL DEFAULT 'unknown',
    is_favorite   INTEGER NOT NULL DEFAULT 0,
    is_hidden     INTEGER NOT NULL DEFAULT 0,
    is_synthetic  INTEGER NOT NULL DEFAULT 0,
    deleted_at    INTEGER,
    indexed_at    INTEGER NOT NULL DEFAULT 0
))SQL"),

        QStringLiteral("CREATE INDEX idx_media_taken ON media(taken_at DESC) WHERE deleted_at IS NULL"),
        QStringLiteral("CREATE INDEX idx_media_root ON media(root_id)"),
        QStringLiteral("CREATE INDEX idx_media_kind ON media(kind) WHERE deleted_at IS NULL"),
        QStringLiteral("CREATE INDEX idx_media_fav ON media(is_favorite) WHERE is_favorite = 1"),
        QStringLiteral("CREATE INDEX idx_media_path_hash ON media(path_hash)"),
        QStringLiteral("CREATE INDEX idx_media_hash_partial ON media(hash_partial)"),

        QStringLiteral(R"SQL(
CREATE TABLE albums (
    id             INTEGER PRIMARY KEY,
    kind           TEXT    NOT NULL,
    name           TEXT    NOT NULL,
    cover_media_id INTEGER REFERENCES media(id) ON DELETE SET NULL,
    created_at     INTEGER NOT NULL DEFAULT 0,
    UNIQUE(kind, name)
))SQL"),

        QStringLiteral(R"SQL(
CREATE TABLE album_items (
    album_id INTEGER NOT NULL REFERENCES albums(id) ON DELETE CASCADE,
    media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
    added_at INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (album_id, media_id)
))SQL"),

        QStringLiteral("CREATE INDEX idx_album_items_media ON album_items(media_id)"),

        QStringLiteral(R"SQL(
CREATE TABLE people (
    id               INTEGER PRIMARY KEY,
    name             TEXT,
    cover_face_id    INTEGER,
    auto_cluster_key TEXT
))SQL"),

        QStringLiteral(R"SQL(
CREATE TABLE faces (
    id         INTEGER PRIMARY KEY,
    media_id   INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
    person_id  INTEGER REFERENCES people(id) ON DELETE SET NULL,
    bbox_x     REAL NOT NULL,
    bbox_y     REAL NOT NULL,
    bbox_w     REAL NOT NULL,
    bbox_h     REAL NOT NULL,
    embedding  BLOB,
    confidence REAL
))SQL"),

        QStringLiteral("CREATE INDEX idx_faces_media ON faces(media_id)"),
        QStringLiteral("CREATE INDEX idx_faces_person ON faces(person_id)"),

        QStringLiteral(R"SQL(
CREATE TABLE places (
    id       INTEGER PRIMARY KEY,
    name     TEXT,
    lat      REAL NOT NULL,
    lon      REAL NOT NULL,
    radius_m REAL NOT NULL DEFAULT 0
))SQL"),

        QStringLiteral(R"SQL(
CREATE TABLE media_places (
    media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
    place_id INTEGER NOT NULL REFERENCES places(id) ON DELETE CASCADE,
    PRIMARY KEY (media_id, place_id)
))SQL"),

        QStringLiteral(R"SQL(
CREATE TABLE tags (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    kind TEXT NOT NULL DEFAULT 'smart'
))SQL"),

        QStringLiteral(R"SQL(
CREATE TABLE media_tags (
    media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
    tag_id   INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    score    REAL,
    PRIMARY KEY (media_id, tag_id)
))SQL"),

        QStringLiteral(R"SQL(
CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT
))SQL"),
    };
    list.append(v1);

    // ---------------------------------------------------------------------
    // v2 - full text search (PRD 4.4 / F-4.1)
    //
    // Created in a separate migration because FTS5 is a compile-time option of
    // SQLite. Database::migrate() treats the failure of this step as a warning
    // rather than a hard error and records the outcome in `meta`.
    // ---------------------------------------------------------------------
    Migration v2;
    v2.version = 2;
    v2.name = QStringLiteral("full_text_search");
    v2.requiresFts5 = true;
    v2.statements = {
        QStringLiteral(R"SQL(
CREATE VIRTUAL TABLE media_fts USING fts5(
    filename,
    camera_model,
    album,
    tags,
    tokenize = "unicode61 remove_diacritics 2"
))SQL"),
    };
    list.append(v2);

    // ---------------------------------------------------------------------
    // v3 - source album becomes a real column
    //
    // Until now the source album (Camera / Screenshots / WhatsApp / ...) was
    // recomputed in C++ for every row on every reload, so filtering by album was
    // an in-memory pass over the whole result set - an O(n) step in the load path
    // that would not survive a 500k library. Storing it lets SQLite filter (and
    // index) it instead.
    //
    // The column is intentionally nullable: rows indexed before this migration
    // have no value, and LibraryMaintenance backfills them once the schema is in
    // place. Running the backfill as an UPDATE also fires the v4 triggers, which
    // is what populates the FTS index for pre-existing rows.
    // ---------------------------------------------------------------------
    Migration v3;
    v3.version = 3;
    v3.name = QStringLiteral("media_source_album");
    v3.statements = {
        QStringLiteral("ALTER TABLE media ADD COLUMN source_album TEXT"),
        QStringLiteral("CREATE INDEX idx_media_source_album ON media(source_album) "
                       "WHERE deleted_at IS NULL"),
    };
    list.append(v3);

    // ---------------------------------------------------------------------
    // v4 - keep media_fts in step with media via triggers
    //
    // The FTS index is maintained by SQLite itself rather than by application
    // code, so there is no path (indexer, recycle bin, empty trash, future
    // wrapper) that can forget to update it. The triggers only reference columns
    // that live on `media`, which is why source_album had to become a column
    // first.
    //
    // `tags` is carried in the FTS row but is always '' for now: the tags
    // feature does not exist yet (M2), and media_tags has no rows. When tagging
    // lands it needs its own triggers on media_tags rather than widening these.
    //
    // The DELETE+INSERT pair on update is belt-and-braces against FTS5 refusing
    // an update that leaves the indexed text byte-identical; it also keeps the
    // three triggers uniform.
    // ---------------------------------------------------------------------
    Migration v4;
    v4.version = 4;
    v4.name = QStringLiteral("fts_sync_triggers");
    v4.requiresFts5 = true;
    v4.statements = {
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_ai AFTER INSERT ON media BEGIN
    INSERT INTO media_fts(rowid, filename, camera_model, album, tags)
    VALUES (new.id, new.filename, COALESCE(new.camera_model, ''),
            COALESCE(new.source_album, ''), '');
END)SQL"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_ad AFTER DELETE ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
END)SQL"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_au AFTER UPDATE OF filename, camera_model, source_album ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
    INSERT INTO media_fts(rowid, filename, camera_model, album, tags)
    VALUES (new.id, new.filename, COALESCE(new.camera_model, ''),
            COALESCE(new.source_album, ''), '');
END)SQL"),
    };
    list.append(v4);

    // ---------------------------------------------------------------------
    // v5 - index the camera make alongside the model
    //
    // v4 indexed only camera_model, so searching "Xiaomi" or "Canon" matched
    // nothing even though the make is sitting in the media table. Searching by
    // brand is an ordinary thing to want.
    //
    // A virtual table cannot be altered in place, but it does not need to be: the
    // two values are concatenated into the existing camera_model column, which is
    // now best understood as "camera identity". Only the triggers are recreated.
    //
    // The rows already indexed under the old definition are refreshed by
    // LibraryMaintenance::ensureFtsPopulated, whose version marker (kFtsIndexVersion)
    // is bumped alongside this migration so a rebuild is triggered once.
    // ---------------------------------------------------------------------
    Migration v5;
    v5.version = 5;
    v5.name = QStringLiteral("fts_camera_identity");
    v5.requiresFts5 = true;
    v5.statements = {
        QStringLiteral("DROP TRIGGER IF EXISTS media_fts_ai"),
        QStringLiteral("DROP TRIGGER IF EXISTS media_fts_ad"),
        QStringLiteral("DROP TRIGGER IF EXISTS media_fts_au"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_ai AFTER INSERT ON media BEGIN
    INSERT INTO media_fts(rowid, filename, camera_model, album, tags)
    VALUES (new.id, new.filename,
            trim(COALESCE(new.camera_make, '') || ' ' || COALESCE(new.camera_model, '')),
            COALESCE(new.source_album, ''), '');
END)SQL"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_ad AFTER DELETE ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
END)SQL"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_au AFTER UPDATE OF filename, camera_make, camera_model, source_album ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
    INSERT INTO media_fts(rowid, filename, camera_model, album, tags)
    VALUES (new.id, new.filename,
            trim(COALESCE(new.camera_make, '') || ' ' || COALESCE(new.camera_model, '')),
            COALESCE(new.source_album, ''), '');
END)SQL"),
    };
    list.append(v5);

    // ---------------------------------------------------------------------
    // v6 - composite index for the album + recency read path
    //
    // v3 indexed source_album on its own, which let SQLite find the rows for an
    // album but not deliver them in timeline order. At 100k rows the resulting
    // sort measured ~51 ms, over the PRD's 50 ms search budget, and it was memory
    // proportional to the album size on every reload.
    //
    // Leading with source_album and carrying taken_at means the index satisfies
    // both the filter and the ORDER BY, so the sort disappears. It also serves
    // plain `source_album = ?` lookups, which makes the v3 index redundant -
    // hence the DROP rather than keeping both.
    //
    // Partial on `deleted_at IS NULL` to match the read path, which never mixes
    // live rows and recycle-bin rows in one query.
    //
    // The index column carries COLLATE NOCASE to match the query, which compares
    // with `source_album = ? COLLATE NOCASE`. Without it SQLite cannot use the
    // index for the comparison at all and falls back to scanning idx_media_taken
    // and sorting - the exact cost this migration exists to remove.
    //
    // `id DESC` is carried as well so the index satisfies the whole
    // `ORDER BY taken_at DESC, id DESC`. Leaving it out still forces SQLite to
    // build a temp b-tree for the tiebreaker, because taken_at alone is not a
    // total order.
    // ---------------------------------------------------------------------
    Migration v6;
    v6.version = 6;
    v6.name = QStringLiteral("media_album_recency_index");
    v6.statements = {
        QStringLiteral("DROP INDEX IF EXISTS idx_media_source_album"),
        QStringLiteral("CREATE INDEX idx_media_album_taken "
                       "ON media(source_album COLLATE NOCASE, taken_at DESC, id DESC) "
                       "WHERE deleted_at IS NULL"),
    };
    list.append(v6);

    return list;
}

} // namespace

QStringList ftsSchemaStatements()
{
    return {
        QStringLiteral(R"SQL(
CREATE VIRTUAL TABLE IF NOT EXISTS media_fts USING fts5(
    filename,
    camera_model,
    album,
    tags,
    tokenize = "unicode61 remove_diacritics 2"
))SQL"),
        QStringLiteral("DROP TRIGGER IF EXISTS media_fts_ai"),
        QStringLiteral("DROP TRIGGER IF EXISTS media_fts_ad"),
        QStringLiteral("DROP TRIGGER IF EXISTS media_fts_au"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_ai AFTER INSERT ON media BEGIN
    INSERT INTO media_fts(rowid, filename, camera_model, album, tags)
    VALUES (new.id, new.filename,
            trim(COALESCE(new.camera_make, '') || ' ' || COALESCE(new.camera_model, '')),
            COALESCE(new.source_album, ''), '');
END)SQL"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_ad AFTER DELETE ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
END)SQL"),
        QStringLiteral(R"SQL(
CREATE TRIGGER media_fts_au AFTER UPDATE OF filename, camera_make, camera_model, source_album ON media BEGIN
    DELETE FROM media_fts WHERE rowid = old.id;
    INSERT INTO media_fts(rowid, filename, camera_model, album, tags)
    VALUES (new.id, new.filename,
            trim(COALESCE(new.camera_make, '') || ' ' || COALESCE(new.camera_model, '')),
            COALESCE(new.source_album, ''), '');
END)SQL"),
    };
}

const QList<Migration> &migrations()
{
    static const QList<Migration> list = buildMigrations();
    return list;
}

int latestSchemaVersion()
{
    int version = 0;
    for (const Migration &m : migrations()) {
        version = qMax(version, m.version);
    }
    return version;
}

} // namespace kg
