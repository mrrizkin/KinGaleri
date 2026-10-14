#pragma once

#include <QDateTime>
#include <QString>

namespace kg {

/**
 * Dependency-free EXIF reader (PRD M1: "Exiv2 metadata extraction").
 *
 * The PRD named Exiv2, but Exiv2 is an optional external dependency that is not
 * present on every target machine, and the tags this application actually needs
 * are a small, stable subset of the spec. Reading them directly keeps the
 * default build free of third-party requirements while covering all P0 formats.
 *
 * Supported containers:
 *   - JPEG (EXIF in an APP1 segment)
 *   - TIFF (the file *is* the IFD structure)
 *   - HEIC/HEIF/AVIF: falls back to nothing here; the indexer still gets pixel
 *     dimensions from QImageReader, and `taken_at` degrades to filename/mtime.
 *
 * Everything is bounds-checked: a malformed or hostile file yields an invalid
 * Metadata rather than reading out of bounds. Callers must treat `valid == false`
 * as "no EXIF" and keep their own fallback chain (PRD 8: EXIF -> filename -> mtime).
 */
class ExifReader
{
public:
    struct Metadata
    {
        bool valid = false;

        /// DateTimeOriginal / DateTimeDigitized / DateTime, as epoch ms in local
        /// time (EXIF has no timezone concept unless OffsetTime is present).
        qint64 takenAtMs = 0;

        int width = 0;
        int height = 0;

        /// EXIF orientation, 1..8. 0 means "not present".
        int orientation = 0;

        QString cameraMake;
        QString cameraModel;
        QString lensModel;

        int iso = 0;
        double fNumber = 0.0;
        double exposureTimeSeconds = 0.0;
        double focalLengthMm = 0.0;

        bool hasGps = false;
        double latitude = 0.0;
        double longitude = 0.0;
    };

    /// Parses EXIF from `filePath`. Never throws; returns an invalid Metadata on
    /// any error (unreadable file, unsupported container, no EXIF block).
    static Metadata read(const QString &filePath);

    /// True when the format can plausibly carry EXIF we parse.
    static bool isSupportedContainer(const QString &filePath);

    /// EXIF orientation -> clockwise rotation in degrees (0/90/180/270) and
    /// whether the image must be mirrored. Used by the thumbnailer and viewer so
    /// a rotated phone photo is not displayed sideways.
    static void orientationToTransform(int orientation, int *rotationDegrees, bool *mirrored);
};

} // namespace kg
