#pragma once

#include <QString>

namespace kg {

class Database;

/**
 * One-off data migrations that cannot be expressed as pure schema DDL because
 * they call C++ logic (`SourceAlbum::detect`) or need a pass over existing rows.
 *
 * Everything here is idempotent and guarded by a value in the `meta` table, so
 * it is safe to call on every start: the cheap case is a single SELECT.
 *
 * PRD 4.3 requires that indexing be crash-safe. These routines follow the same
 * rule as the schema migrations - each one writes its result and its version
 * marker in one transaction, so an interruption leaves the work either entirely
 * done or entirely pending.
 */
namespace LibraryMaintenance {

/// Bump when `SourceAlbum::rules()` changes shape. Stored in `meta` and compared
/// on start; a mismatch re-derives the album for every row, which is what makes
/// editing the rule table a supported operation rather than a manual reindex.
inline constexpr int kSourceAlbumRulesVersion = 1;

/// Bump when the FTS row layout or the trigger definitions change, so existing
/// libraries are rebuilt instead of silently keeping a stale index.
/// v2 indexes camera_make together with camera_model (migration v5).
inline constexpr int kFtsIndexVersion = 2;

/**
 * Derives `media.source_album` for every row whose value is missing, and for all
 * rows when the stored rules version differs from kSourceAlbumRulesVersion.
 *
 * On a library created before migration v3 every row is NULL, so the first run
 * touches every row once. The UPDATE is what fires the v4 FTS triggers, so this
 * one pass also fills the full-text index for pre-existing libraries.
 *
 * @return false only on a database failure; a no-op returns true.
 */
bool ensureSourceAlbum(Database &database, QString *error = nullptr);

/**
 * Rebuilds `media_fts` from `media` when the stored index version differs from
 * kFtsIndexVersion, then records the new version. A matching version is a no-op,
 * so this costs one SELECT on every start after the first.
 *
 * A rebuild is required rather than a gap-fill because the indexed *content* can
 * change between versions (v2 folds camera_make into the camera identity), and
 * rows indexed under the old definition would otherwise keep matching the old
 * text forever.
 *
 * Steady-state consistency does not depend on this function: the migration v4/v5
 * triggers keep the index in step with every INSERT, UPDATE and DELETE. This is
 * the one-time catch-up for rows that predate the triggers.
 *
 * Does nothing when SQLite was built without FTS5.
 *
 * @return false only on a database failure; a no-op returns true.
 */
bool ensureFtsPopulated(Database &database, QString *error = nullptr);

/**
 * Unconditional full rebuild of the full-text index, including recreating the
 * virtual table and its triggers when they are missing.
 *
 * This is the recovery path for a library whose search index is out of step with
 * `media` while the version marker still claims it is current - the case that
 * `ensureFtsPopulated` deliberately does not cover, because it only reacts to a
 * version bump. Exposed through `--rebuild-search-index` and
 * `AppController::rebuildSearchIndex()`.
 *
 * No-op-with-failure when SQLite has no FTS5: there is nothing to rebuild and the
 * caller should be told rather than silently pretending success.
 *
 * @return false on a database failure or when FTS5 is unavailable.
 */
bool rebuildFts(Database &database, QString *error = nullptr);

} // namespace LibraryMaintenance

} // namespace kg
