#pragma once

#include <QString>

namespace kg {

class Database;

/**
 * Persists the M3 smart-grouping output (places + events) into the library.
 *
 * Both passes are full rebuilds rather than incremental updates. Places and
 * events are cheap to recompute - a scan of a few columns plus an O(n) cluster -
 * and a full rebuild avoids an entire class of drift bugs (a moved file, an
 * edited EXIF date, a deleted root) that an incremental pass would have to
 * chase. The read path is unchanged: the grid joins the same tables either way.
 *
 * Both are idempotent and transactional: a crash leaves the previous grouping
 * intact. Deleting a place cascades `media_places` and nulls `events.place_id`,
 * so callers should rebuild places before events.
 */
namespace GroupingService {

struct Result
{
    int places = 0;
    int events = 0;
    int mediaInPlaces = 0;
    int mediaInEvents = 0;
    QString error;
};

/// Rebuilds `places` and `media_places` from `media.gps_lat/gps_lon`.
bool rebuildPlaces(Database &database, QString *error = nullptr);

/// Rebuilds `events` and `media_events` from `media.taken_at` (+ GPS jumps).
bool rebuildEvents(Database &database, QString *error = nullptr);

/// Places first, then events (so events can reference the fresh places).
Result rebuild(Database &database);

/// Same as rebuild(), but skipping the passes the user has turned off.
Result rebuild(Database &database, bool places, bool events);

} // namespace GroupingService

} // namespace kg
