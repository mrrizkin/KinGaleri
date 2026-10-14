#pragma once

#include <QString>
#include <QStringList>

namespace kg {

struct Migration
{
    int version = 0;
    QString name;
    QStringList statements;
    /// True when the migration depends on SQLite being built with FTS5. Such a
    /// migration is skipped (with a warning) rather than failing, because FTS5
    /// is a compile-time option of SQLite. The schema version still advances so
    /// the migration is not retried on every start.
    bool requiresFts5 = false;
};

/**
 * Ordered, append-only schema migrations. The applied version is tracked in
 * SQLite's own `PRAGMA user_version`, which is updated inside the same
 * transaction as the migration body, so a crash can never leave a half-applied
 * schema behind (PRD 4.3).
 */
const QList<Migration> &migrations();

int latestSchemaVersion();

/**
 * DDL that (re)creates `media_fts` and its sync triggers at the *current*
 * definition, using `IF NOT EXISTS` / `DROP ... IF EXISTS` so it is safe to run
 * on a database whose table is already present and populated.
 *
 * This exists so a corrupt or missing full-text index can be repaired without
 * duplicating the schema: migrations v2/v4/v5 remain the historical record of
 * how the table got here, and this is the one definition the recovery path
 * (`LibraryMaintenance::rebuildFts`) and fresh installs agree on.
 */
QStringList ftsSchemaStatements();

} // namespace kg
