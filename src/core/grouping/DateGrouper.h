#pragma once

#include <QString>

namespace kg {

/**
 * Date bucketing for the timeline (PRD 6.3).
 *
 * Keys are stable, sortable, locale-independent strings; labels are the
 * human-readable form rendered into sticky section headers.
 */
class DateGrouper
{
public:
    /// "2025-07-12" (or "unknown" when takenAt is 0).
    static QString dayKey(qint64 takenAtMs);

    /// "Today" / "Yesterday" / "12 July 2025" / "12 July 2024" for older years.
    static QString dayLabel(qint64 takenAtMs);

    /// "2025-07"
    static QString monthKey(qint64 takenAtMs);

    /// "July 2025"
    static QString monthLabel(qint64 takenAtMs);

    /// "2025"
    static QString yearKey(qint64 takenAtMs);

    /// The header text used by the grid's section delegate.
    static QString sectionLabel(qint64 takenAtMs);

    /// Inclusive epoch-ms bounds of the day containing `takenAtMs`.
    static void dayBounds(qint64 takenAtMs, qint64 *startMs, qint64 *endMs);

private:
    /// Start of the *local* day, in epoch milliseconds.
    static qint64 localDayStartMs(qint64 takenAtMs);
};

} // namespace kg
