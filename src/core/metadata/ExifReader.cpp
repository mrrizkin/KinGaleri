#include "core/metadata/ExifReader.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QList>
#include <cmath>

namespace kg {
namespace {

/// Refuse absurd payloads: a real EXIF block is a few hundred KB at most, and an
/// oversized one is either corrupt or hostile.
constexpr qsizetype kMaxExifBlock = 4 * 1024 * 1024;
/// How far into a JPEG we are willing to walk looking for the APP1 segment.
constexpr qsizetype kJpegScanLimit = 2 * 1024 * 1024;

enum class ByteOrder { Little, Big };

/// Standard TIFF field types. Index 0 is unused so the table can be indexed by
/// the on-disk type id directly.
constexpr int kTypeSize[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};

/// One IFD entry, resolved to an absolute offset inside the TIFF block.
struct Entry
{
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    qsizetype valueOffset = 0; ///< absolute offset of the value bytes
    bool valid = false;
};

/**
 * Bounds-checked TIFF block reader.
 *
 * Every accessor returns a safe default when the offset is out of range, so a
 * truncated or malicious file degrades to "no metadata" instead of crashing.
 */
class TiffBlock
{
public:
    explicit TiffBlock(const QByteArray &data)
        : m_data(data)
    {
    }

    bool parseHeader()
    {
        if (m_data.size() < 8)
            return false;

        // startsWith is used instead of indexing constData(): an empty QByteArray
        // points at Qt's shared 1-byte null terminator, which trips -Warray-bounds.
        if (m_data.startsWith(QByteArrayLiteral("II")))
            m_order = ByteOrder::Little;
        else if (m_data.startsWith(QByteArrayLiteral("MM")))
            m_order = ByteOrder::Big;
        else
            return false;

        if (u16(2) != 42)
            return false;

        m_ok = true;
        return true;
    }

    bool ok() const { return m_ok; }
    qsizetype size() const { return m_data.size(); }

    bool contains(qsizetype offset, qsizetype length) const
    {
        return offset >= 0 && length >= 0 && offset + length <= m_data.size();
    }

    quint8 u8(qsizetype offset) const
    {
        if (!contains(offset, 1))
            return 0;
        return static_cast<quint8>(m_data.at(offset));
    }

    quint16 u16(qsizetype offset) const
    {
        if (!contains(offset, 2))
            return 0;
        const auto *p = reinterpret_cast<const quint8 *>(m_data.constData() + offset);
        if (m_order == ByteOrder::Little)
            return static_cast<quint16>(quint16(p[0]) | (quint16(p[1]) << 8));
        return static_cast<quint16>((quint16(p[0]) << 8) | quint16(p[1]));
    }

    quint32 u32(qsizetype offset) const
    {
        if (!contains(offset, 4))
            return 0;
        const auto *p = reinterpret_cast<const quint8 *>(m_data.constData() + offset);
        if (m_order == ByteOrder::Little) {
            return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
        }
        return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
    }

    /// Reads the IFD at `ifdOffset`, appending resolved entries to `out`.
    /// Returns false when the IFD header is out of range.
    bool readIfd(quint32 ifdOffset, QList<Entry> *out) const
    {
        if (!m_ok)
            return false;

        const qsizetype base = static_cast<qsizetype>(ifdOffset);
        if (!contains(base, 2))
            return false;

        const quint32 entryCount = u16(base);
        // A plausible IFD has far fewer than 4096 entries; more means corruption.
        if (entryCount > 4096)
            return false;

        if (!contains(base + 2, static_cast<qsizetype>(entryCount) * 12))
            return false;

        for (quint32 i = 0; i < entryCount; ++i) {
            const qsizetype entryOffset = base + 2 + static_cast<qsizetype>(i) * 12;
            Entry entry;
            entry.tag = u16(entryOffset);
            entry.type = u16(entryOffset + 2);
            entry.count = u32(entryOffset + 4);

            if (entry.type == 0 || entry.type > 12)
                continue;

            const qint64 byteCount =
                static_cast<qint64>(kTypeSize[entry.type]) * static_cast<qint64>(entry.count);
            if (byteCount <= 0 || byteCount > kMaxExifBlock)
                continue;

            // Values of 4 bytes or fewer live inline in the entry itself.
            if (byteCount <= 4) {
                entry.valueOffset = entryOffset + 8;
            } else {
                entry.valueOffset = static_cast<qsizetype>(u32(entryOffset + 8));
            }

            if (!contains(entry.valueOffset, static_cast<qsizetype>(byteCount)))
                continue;

            entry.valid = true;
            out->append(entry);
        }

        return true;
    }

    /// Offset of the next IFD, or 0.
    quint32 nextIfdOffset(quint32 ifdOffset) const
    {
        const qsizetype base = static_cast<qsizetype>(ifdOffset);
        if (!contains(base, 2))
            return 0;
        const quint32 entryCount = u16(base);
        const qsizetype next = base + 2 + static_cast<qsizetype>(entryCount) * 12;
        if (!contains(next, 4))
            return 0;
        return u32(next);
    }

    // --- typed value accessors ------------------------------------------------

    QString asString(const Entry &entry) const
    {
        if (!entry.valid)
            return {};
        qsizetype length = static_cast<qsizetype>(entry.count);
        if (!contains(entry.valueOffset, length))
            return {};

        QByteArray raw = m_data.mid(entry.valueOffset, length);
        // ASCII per spec, but cameras frequently emit UTF-8; strip the terminator.
        while (!raw.isEmpty() && (raw.endsWith('\0') || raw.endsWith(' ')))
            raw.chop(1);
        return QString::fromUtf8(raw).trimmed();
    }

    /// Reads the first element of an integer-typed entry (SHORT or LONG).
    quint32 asInteger(const Entry &entry) const
    {
        if (!entry.valid || entry.count < 1)
            return 0;
        if (entry.type == 3) // SHORT
            return u16(entry.valueOffset);
        if (entry.type == 4) // LONG
            return u32(entry.valueOffset);
        if (entry.type == 1) // BYTE
            return u8(entry.valueOffset);
        return 0;
    }

    /// Reads element `index` as a rational (type 5) or signed rational (type 10).
    double asRational(const Entry &entry, quint32 index = 0) const
    {
        if (!entry.valid || index >= entry.count)
            return 0.0;
        if (entry.type != 5 && entry.type != 10)
            return 0.0;

        const qsizetype offset = entry.valueOffset + static_cast<qsizetype>(index) * 8;
        if (!contains(offset, 8))
            return 0.0;

        double numerator = 0.0;
        double denominator = 0.0;
        if (entry.type == 5) {
            numerator = static_cast<double>(u32(offset));
            denominator = static_cast<double>(u32(offset + 4));
        } else {
            numerator = static_cast<double>(static_cast<qint32>(u32(offset)));
            denominator = static_cast<double>(static_cast<qint32>(u32(offset + 4)));
        }

        if (denominator == 0.0)
            return 0.0;
        return numerator / denominator;
    }

private:
    QByteArray m_data;
    ByteOrder m_order = ByteOrder::Little;
    bool m_ok = false;
};

/// EXIF stores dates as "YYYY:MM:DD HH:MM:SS" with no timezone, so the value is
/// interpreted in local time. Returns 0 when absent or implausible.
qint64 parseExifDateTime(const QString &text)
{
    if (text.size() < 19)
        return 0;

    // Some writers use '-' instead of ':' for the date part.
    QString normalised = text;
    normalised.replace(QLatin1Char('-'), QLatin1Char(':'));

    const QDateTime parsed =
        QDateTime::fromString(normalised.left(19), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    if (!parsed.isValid())
        return 0;

    const QDate date = parsed.date();
    // "0000:00:00 00:00:00" is the documented "unknown" encoding.
    if (date.year() < 1900 || date.year() > 2200)
        return 0;

    return parsed.toMSecsSinceEpoch();
}

/// GPS coordinates are stored as 3 rationals (degrees, minutes, seconds) plus a
/// hemisphere reference character.
double gpsCoordinate(const TiffBlock &tiff, const Entry &entry, const QString &ref)
{
    if (!entry.valid)
        return 0.0;

    // Some cameras store a single rational; treat it as decimal degrees.
    if (entry.count < 3)
        return tiff.asRational(entry, 0);

    const double degrees = tiff.asRational(entry, 0);
    const double minutes = tiff.asRational(entry, 1);
    const double seconds = tiff.asRational(entry, 2);
    double value = degrees + minutes / 60.0 + seconds / 3600.0;

    if (ref.compare(QLatin1String("S"), Qt::CaseInsensitive) == 0
        || ref.compare(QLatin1String("W"), Qt::CaseInsensitive) == 0) {
        value = -value;
    }
    return value;
}

void extractFromTiff(const TiffBlock &tiff, ExifReader::Metadata *out)
{
    QList<Entry> ifd0;
    if (!tiff.readIfd(tiff.u32(4), &ifd0))
        return;

    quint32 exifIfdOffset = 0;
    quint32 gpsIfdOffset = 0;
    qint64 dateTimeFallback = 0;

    for (const Entry &entry : ifd0) {
        switch (entry.tag) {
        case 0x0100: // ImageWidth
        case 0xA002: // PixelXDimension
            if (out->width == 0)
                out->width = static_cast<int>(tiff.asInteger(entry));
            break;
        case 0x0101: // ImageLength
        case 0xA003: // PixelYDimension
            if (out->height == 0)
                out->height = static_cast<int>(tiff.asInteger(entry));
            break;
        case 0x010F: // Make
            out->cameraMake = tiff.asString(entry);
            break;
        case 0x0110: // Model
            out->cameraModel = tiff.asString(entry);
            break;
        case 0x0112: // Orientation
            out->orientation = static_cast<int>(tiff.asInteger(entry));
            break;
        case 0x0132: // DateTime (modification time: weakest date)
            if (dateTimeFallback == 0)
                dateTimeFallback = parseExifDateTime(tiff.asString(entry));
            break;
        case 0x8769: // ExifIFD pointer
            exifIfdOffset = tiff.asInteger(entry);
            break;
        case 0x8825: // GPS IFD pointer
            gpsIfdOffset = tiff.asInteger(entry);
            break;
        default:
            break;
        }
    }

    // The Exif sub-IFD holds the authoritative capture time and exposure data.
    if (exifIfdOffset != 0) {
        QList<Entry> exifIfd;
        if (tiff.readIfd(exifIfdOffset, &exifIfd)) {
            qint64 original = 0;
            qint64 digitised = 0;

            for (const Entry &entry : exifIfd) {
                switch (entry.tag) {
                case 0x829A: // ExposureTime
                    out->exposureTimeSeconds = tiff.asRational(entry);
                    break;
                case 0x829D: // FNumber
                    out->fNumber = tiff.asRational(entry);
                    break;
                case 0x8827: // ISOSpeedRatings
                    out->iso = static_cast<int>(tiff.asInteger(entry));
                    break;
                case 0x9003: // DateTimeOriginal  <- the one that matters
                    original = parseExifDateTime(tiff.asString(entry));
                    break;
                case 0x9004: // DateTimeDigitized
                    digitised = parseExifDateTime(tiff.asString(entry));
                    break;
                case 0x920A: // FocalLength
                    out->focalLengthMm = tiff.asRational(entry);
                    break;
                case 0xA002:
                    if (out->width == 0)
                        out->width = static_cast<int>(tiff.asInteger(entry));
                    break;
                case 0xA003:
                    if (out->height == 0)
                        out->height = static_cast<int>(tiff.asInteger(entry));
                    break;
                case 0xA434: // LensModel
                    out->lensModel = tiff.asString(entry);
                    break;
                default:
                    break;
                }
            }

            out->takenAtMs = original != 0 ? original : digitised;
        }
    }

    // Fallback chain for the capture time: original -> digitised -> file DateTime.
    if (out->takenAtMs == 0)
        out->takenAtMs = dateTimeFallback;

    if (gpsIfdOffset != 0) {
        QList<Entry> gps;
        if (tiff.readIfd(gpsIfdOffset, &gps)) {
            QString latitudeRef;
            QString longitudeRef;
            const Entry *latitudeEntry = nullptr;
            const Entry *longitudeEntry = nullptr;

            for (const Entry &entry : gps) {
                switch (entry.tag) {
                case 0x0001:
                    latitudeRef = tiff.asString(entry);
                    break;
                case 0x0002:
                    latitudeEntry = &entry;
                    break;
                case 0x0003:
                    longitudeRef = tiff.asString(entry);
                    break;
                case 0x0004:
                    longitudeEntry = &entry;
                    break;
                default:
                    break;
                }
            }

            if (latitudeEntry && longitudeEntry) {
                const double latitude = gpsCoordinate(tiff, *latitudeEntry, latitudeRef);
                const double longitude = gpsCoordinate(tiff, *longitudeEntry, longitudeRef);
                // (0,0) is in the Atlantic and is what cameras write when the fix
                // failed, so it is not treated as a real location.
                if (std::abs(latitude) > 1e-6 || std::abs(longitude) > 1e-6) {
                    out->hasGps = true;
                    out->latitude = latitude;
                    out->longitude = longitude;
                }
            }
        }
    }

    out->valid = out->takenAtMs != 0 || out->width > 0 || out->height > 0
                 || !out->cameraModel.isEmpty();
}

/// Walks JPEG segments looking for the EXIF APP1 payload.
ExifReader::Metadata readJpeg(QFile *file)
{
    ExifReader::Metadata metadata;

    file->seek(2); // skip SOI
    qsizetype scanned = 0;

    while (scanned < kJpegScanLimit) {
        // Markers are 0xFF followed by an id; runs of 0xFF are padding.
        char byte = 0;
        do {
            if (!file->getChar(&byte))
                return metadata;
            ++scanned;
        } while (static_cast<quint8>(byte) != 0xFF);

        char markerByte = 0;
        do {
            if (!file->getChar(&markerByte))
                return metadata;
            ++scanned;
        } while (static_cast<quint8>(markerByte) == 0xFF);

        const auto markerId = static_cast<quint8>(markerByte);

        // Standalone markers carry no length field.
        if (markerId == 0x01 || markerId == 0xD8
            || (markerId >= 0xD0 && markerId <= 0xD7)) {
            continue;
        }
        // Start of scan / end of image: pixel data, nothing more to find.
        if (markerId == 0xDA || markerId == 0xD9)
            return metadata;

        char lengthBytes[2];
        if (file->read(lengthBytes, 2) != 2)
            return metadata;
        const auto segmentLength = static_cast<int>(
            (static_cast<quint8>(lengthBytes[0]) << 8) | static_cast<quint8>(lengthBytes[1]));
        if (segmentLength < 2)
            return metadata;

        const int payloadLength = segmentLength - 2;
        if (payloadLength <= 0)
            continue;

        if (markerId == 0xE1) {
            const QByteArray payload = file->read(payloadLength);
            scanned += payload.size();

            // APP1 also carries XMP; only the segment starting with "Exif\0\0"
            // is the one we want.
            if (payload.startsWith(QByteArrayLiteral("Exif\0\0"))) {
                const QByteArray block = payload.mid(6);
                if (block.size() > kMaxExifBlock)
                    return metadata;

                TiffBlock tiff(block);
                if (tiff.parseHeader())
                    extractFromTiff(tiff, &metadata);
                return metadata;
            }
        } else {
            if (!file->seek(file->pos() + payloadLength))
                return metadata;
            scanned += payloadLength;
        }
    }

    return metadata;
}

} // namespace

ExifReader::Metadata ExifReader::read(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    char magic[8] = {0};
    const qint64 read = file.read(magic, sizeof(magic));
    if (read < 4)
        return {};

    const auto *bytes = reinterpret_cast<const quint8 *>(magic);

    // JPEG: SOI marker.
    if (bytes[0] == 0xFF && bytes[1] == 0xD8)
        return readJpeg(&file);

    // TIFF: the file itself is the IFD structure.
    const bool littleTiff = magic[0] == 'I' && magic[1] == 'I' && bytes[2] == 42;
    const bool bigTiff = magic[0] == 'M' && magic[1] == 'M' && bytes[2] == 0 && bytes[3] == 42;
    if (littleTiff || bigTiff) {
        file.seek(0);
        const QByteArray block = file.read(kMaxExifBlock);
        TiffBlock tiff(block);
        Metadata metadata;
        if (tiff.parseHeader())
            extractFromTiff(tiff, &metadata);
        return metadata;
    }

    return {};
}

bool ExifReader::isSupportedContainer(const QString &filePath)
{
    const QString suffix = filePath.section(QLatin1Char('.'), -1).toLower();
    return suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
           || suffix == QLatin1String("jpe") || suffix == QLatin1String("tif")
           || suffix == QLatin1String("tiff");
}

void ExifReader::orientationToTransform(int orientation, int *rotationDegrees, bool *mirrored)
{
    int rotation = 0;
    bool flip = false;

    switch (orientation) {
    case 1:
        break;
    case 2:
        flip = true;
        break;
    case 3:
        rotation = 180;
        break;
    case 4:
        rotation = 180;
        flip = true;
        break;
    case 5:
        rotation = 90;
        flip = true;
        break;
    case 6:
        rotation = 90;
        break;
    case 7:
        rotation = 270;
        flip = true;
        break;
    case 8:
        rotation = 270;
        break;
    default:
        break;
    }

    if (rotationDegrees)
        *rotationDegrees = rotation;
    if (mirrored)
        *mirrored = flip;
}

} // namespace kg
