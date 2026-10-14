#include "core/grouping/DateGrouper.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

namespace kg {

namespace {
const QString kUnknownKey = QStringLiteral("unknown");
const QString kUnknownLabel = QStringLiteral("Date unknown");
} // namespace

qint64 DateGrouper::localDayStartMs(qint64 takenAtMs)
{
    if (takenAtMs <= 0) {
        return 0;
    }
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(takenAtMs).toLocalTime();
    return QDateTime(local.date(), QTime(0, 0), QTimeZone::LocalTime).toMSecsSinceEpoch();
}

QString DateGrouper::dayKey(qint64 takenAtMs)
{
    if (takenAtMs <= 0) {
        return kUnknownKey;
    }
    return QDateTime::fromMSecsSinceEpoch(takenAtMs).toLocalTime().date().toString(Qt::ISODate);
}

QString DateGrouper::dayLabel(qint64 takenAtMs)
{
    if (takenAtMs <= 0) {
        return kUnknownLabel;
    }

    const QDate date = QDateTime::fromMSecsSinceEpoch(takenAtMs).toLocalTime().date();
    const QDate today = QDate::currentDate();

    const qint64 daysAgo = date.daysTo(today);
    if (daysAgo == 0) {
        return QStringLiteral("Today");
    }
    if (daysAgo == 1) {
        return QStringLiteral("Yesterday");
    }

    const QLocale locale = QLocale::system();
    if (date.year() == today.year()) {
        return locale.toString(date, QStringLiteral("d MMMM"));
    }
    return locale.toString(date, QStringLiteral("d MMMM yyyy"));
}

QString DateGrouper::monthKey(qint64 takenAtMs)
{
    if (takenAtMs <= 0) {
        return kUnknownKey;
    }
    return QDateTime::fromMSecsSinceEpoch(takenAtMs).toLocalTime().date().toString(QStringLiteral("yyyy-MM"));
}

QString DateGrouper::monthLabel(qint64 takenAtMs)
{
    if (takenAtMs <= 0) {
        return kUnknownLabel;
    }
    const QDate date = QDateTime::fromMSecsSinceEpoch(takenAtMs).toLocalTime().date();
    return QLocale::system().toString(date, QStringLiteral("MMMM yyyy"));
}

QString DateGrouper::yearKey(qint64 takenAtMs)
{
    if (takenAtMs <= 0) {
        return kUnknownKey;
    }
    return QDateTime::fromMSecsSinceEpoch(takenAtMs).toLocalTime().date().toString(QStringLiteral("yyyy"));
}

QString DateGrouper::sectionLabel(qint64 takenAtMs)
{
    return dayLabel(takenAtMs);
}

void DateGrouper::dayBounds(qint64 takenAtMs, qint64 *startMs, qint64 *endMs)
{
    const qint64 start = localDayStartMs(takenAtMs);
    if (startMs) {
        *startMs = start;
    }
    if (endMs) {
        *endMs = start > 0 ? start + 24 * 60 * 60 * 1000 - 1 : 0;
    }
}

} // namespace kg
