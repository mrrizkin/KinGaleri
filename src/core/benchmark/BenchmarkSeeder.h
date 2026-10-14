#pragma once

#include <QObject>
#include <QString>

namespace kg {

class Database;

/**
 * Generates synthetic `media` rows so the O(50k) scroll benchmark from PRD M0 can
 * run without a real photo collection.
 *
 * The rows are honest rows in the real schema, flagged `is_synthetic`, and they
 * live under a dedicated `<benchmark>` library root so they can be deleted with a
 * single statement without touching a user's library.
 */
class BenchmarkSeeder : public QObject
{
    Q_OBJECT

public:
    struct Result
    {
        int requested = 0;
        int inserted = 0;
        qint64 elapsedMs = 0;
        QString error;

        bool ok() const { return error.isEmpty(); }
        double rowsPerSecond() const
        {
            return elapsedMs > 0 ? (inserted * 1000.0 / static_cast<double>(elapsedMs)) : 0.0;
        }
    };

    explicit BenchmarkSeeder(Database *database, QObject *parent = nullptr);

    /// Inserts `count` synthetic rows. Existing synthetic rows are removed first,
    /// so repeated invocations are idempotent.
    Result seed(int count);

    /// Deletes every synthetic row and the `<benchmark>` root.
    bool clear(QString *error = nullptr);

    static int countSyntheticRows(Database *database);

signals:
    void progress(int inserted, int total);

private:
    Database *m_database = nullptr;
};

} // namespace kg
