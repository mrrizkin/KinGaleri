#pragma once

#include <QString>

namespace kg {

/**
 * Second-tier media detector for the folder indexer.
 *
 * The primary filter is the extension allowlist (fast, zero I/O). This sniffer
 * only runs for files whose extension is unknown, so a home library full of
 * source code is not turned into thousands of header reads. It looks at the
 * first bytes of a file and recognises the common image/video containers; it
 * never decodes pixels and never reads more than a small header.
 */
struct SniffResult
{
    bool media = false;
    bool video = false;
    /// Canonical lower-case extension without the dot, e.g. "jpg". Chosen so the
    /// row's `ext`/`mime` line up with what an allowlisted file would have.
    QString extension;
    QString mime;
    /// Short format label, useful in debug logs ("JPEG", "PNG", "Matroska"...).
    QString format;
};

class MediaSniffer
{
public:
    /// Reads at most 512 bytes from the head of `path`. Returns `media == false`
    /// for anything that is not a recognised image/video container.
    static SniffResult sniff(const QString &path);
};

} // namespace kg
