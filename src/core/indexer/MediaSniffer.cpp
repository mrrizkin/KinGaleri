#include "core/indexer/MediaSniffer.h"

#include <QByteArray>
#include <QFile>

#include <cstring>

namespace kg {

namespace {

constexpr int kHeaderBytes = 512;

bool startsWith(const QByteArray &data, const char *magic, int length)
{
    return data.size() >= length && std::memcmp(data.constData(), magic, length) == 0;
}

bool asciiAt(const QByteArray &data, int offset, const char *text, int length)
{
    return data.size() >= offset + length
        && std::memcmp(data.constData() + offset, text, length) == 0;
}

/// True when the first non-whitespace/BOM character starts an XML/SVG document.
/// Used to keep SVG sniffing from matching an inline <svg> deep inside arbitrary
/// text.
bool looksLikeSvg(const QByteArray &data)
{
    const QByteArray trimmed = data.trimmed();
    int index = 0;
    if (trimmed.startsWith("\xEF\xBB\xBF"))
        index = 3;
    while (index < trimmed.size()
           && (trimmed[index] == ' ' || trimmed[index] == '\t' || trimmed[index] == '\r'
               || trimmed[index] == '\n')) {
        ++index;
    }
    if (index >= trimmed.size() || trimmed[index] != '<')
        return false;
    if (trimmed.mid(index).startsWith("<?xml")) {
        const int end = trimmed.indexOf('>', index);
        if (end < 0)
            return false;
        index = end + 1;
        while (index < trimmed.size()
               && (trimmed[index] == ' ' || trimmed[index] == '\t' || trimmed[index] == '\r'
                   || trimmed[index] == '\n')) {
            ++index;
        }
    }
    return trimmed.mid(index, 4).toLower() == "<svg";
}

/// Maps an ISO-BMFF `ftyp` major/compatible brand to a media format.
SniffResult fromIsoBrand(const QByteArray &brand)
{
    const QByteArray b = brand.toLower();
    const auto image = [](const char *ext, const char *mime, const char *format) {
        SniffResult result;
        result.media = true;
        result.video = false;
        result.extension = QString::fromLatin1(ext);
        result.mime = QString::fromLatin1(mime);
        result.format = QString::fromLatin1(format);
        return result;
    };
    const auto video = [](const char *ext, const char *mime, const char *format) {
        SniffResult result;
        result.media = true;
        result.video = true;
        result.extension = QString::fromLatin1(ext);
        result.mime = QString::fromLatin1(mime);
        result.format = QString::fromLatin1(format);
        return result;
    };

    if (b == "avif" || b == "avis")
        return image("avif", "image/avif", "AVIF");
    if (b == "heic" || b == "heix" || b == "hevc" || b == "hevx" || b == "heim"
        || b == "heis" || b == "hevm" || b == "hevs")
        return image("heic", "image/heic", "HEIF");
    if (b == "mif1" || b == "msf1" || b == "mif2")
        return image("heif", "image/heif", "HEIF");
    if (b == "qt  ")
        return video("mov", "video/quicktime", "QuickTime");
    if (b.startsWith("3gp") || b.startsWith("3g2"))
        return video("3gp", "video/3gpp", "3GPP");
    if (b == "m4v " || b == "m4v" || b == "msnv")
        return video("m4v", "video/mp4", "MP4");
    if (b == "isom" || b == "iso2" || b == "iso4" || b == "iso5" || b == "iso6"
        || b == "mp41" || b == "mp42" || b == "avc1" || b == "dash" || b == "hvc1"
        || b == "hev1")
        return video("mp4", "video/mp4", "MP4");
    return {};
}

/// ISO-BMFF files are common; when the major brand is generic the real signal
/// can sit in the compatible-brands list a few bytes in. Scan the head of the
/// box for any brand we know.
SniffResult sniffIsoBmff(const QByteArray &data)
{
    // Layout: [0..3] box size, [4..7] "ftyp", [8..] major brand then compatible
    // brands. Only the first 64 bytes are inspected, which covers the ftyp box
    // of every file we have seen.
    for (int offset = 8; offset + 4 <= qMin(data.size(), 96); offset += 4) {
        const SniffResult result = fromIsoBrand(data.mid(offset, 4));
        if (result.media)
            return result;
    }
    return {};
}

} // namespace

SniffResult MediaSniffer::sniff(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.read(kHeaderBytes);
    if (data.size() < 2)
        return {};

    const auto image = [](const char *ext, const char *mime, const char *format) {
        SniffResult result;
        result.media = true;
        result.extension = QString::fromLatin1(ext);
        result.mime = QString::fromLatin1(mime);
        result.format = QString::fromLatin1(format);
        return result;
    };
    const auto video = [](const char *ext, const char *mime, const char *format) {
        SniffResult result;
        result.media = true;
        result.video = true;
        result.extension = QString::fromLatin1(ext);
        result.mime = QString::fromLatin1(mime);
        result.format = QString::fromLatin1(format);
        return result;
    };

    // --- Stills -----------------------------------------------------------
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xFF
        && static_cast<unsigned char>(data[1]) == 0xD8
        && static_cast<unsigned char>(data[2]) == 0xFF) {
        return image("jpg", "image/jpeg", "JPEG");
    }
    if (startsWith(data, "\x89PNG\r\n\x1a\n", 8))
        return image("png", "image/png", "PNG");
    if (startsWith(data, "GIF87a", 6) || startsWith(data, "GIF89a", 6))
        return image("gif", "image/gif", "GIF");
    if (startsWith(data, "BM", 2))
        return image("bmp", "image/bmp", "BMP");
    if (asciiAt(data, 0, "RIFF", 4) && asciiAt(data, 8, "WEBP", 4))
        return image("webp", "image/webp", "WebP");
    // TIFF and the RAW formats built on it (CR2, NEF, ARW, DNG, RW2...).
    if (data.size() >= 4
        && ((data[0] == 'I' && data[1] == 'I' && data[2] == 0x2A && data[3] == 0)
            || (data[0] == 'M' && data[1] == 'M' && data[2] == 0 && data[3] == 0x2A))) {
        return image("tiff", "image/tiff", "TIFF");
    }
    // Olympus ORF uses its own magic that is TIFF-adjacent but not `II*\0`.
    if (startsWith(data, "IIRO", 4) || startsWith(data, "MMOR", 4))
        return image("orf", "image/x-olympus-orf", "ORF");
    if (startsWith(data, "IIU\0", 4) || startsWith(data, "MM\0", 4))
        return image("rw2", "image/x-panasonic-rw2", "RW2");
    if (startsWith(data, "FUJIFILMCCD-RAW", 15))
        return image("raf", "image/x-fuji-raf", "RAF");
    // JPEG XL: bare codestream starts with FF 0A; container carries "JXL ".
    if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0xFF
        && static_cast<unsigned char>(data[1]) == 0x0A) {
        return image("jxl", "image/jxl", "JXL");
    }
    if (asciiAt(data, 4, "JXL ", 4))
        return image("jxl", "image/jxl", "JXL");
    if (looksLikeSvg(data))
        return image("svg", "image/svg+xml", "SVG");

    // --- ISO-BMFF: HEIC/HEIF/AVIF plus MP4/MOV/3GP -----------------------
    if (asciiAt(data, 4, "ftyp", 4)) {
        const SniffResult result = sniffIsoBmff(data);
        if (result.media)
            return result;
    }

    // --- Videos -----------------------------------------------------------
    // Matroska / WebM share an EBML header.
    if (data.size() >= 4 && static_cast<unsigned char>(data[0]) == 0x1A
        && static_cast<unsigned char>(data[1]) == 0x45
        && static_cast<unsigned char>(data[2]) == 0xDF
        && static_cast<unsigned char>(data[3]) == 0xA3) {
        return video("mkv", "video/x-matroska", "Matroska");
    }
    if (asciiAt(data, 0, "RIFF", 4) && asciiAt(data, 8, "AVI ", 4))
        return video("avi", "video/x-msvideo", "AVI");
    if (startsWith(data, "FLV", 3))
        return video("flv", "video/x-flv", "FLV");
    // ASF/WMV GUID header.
    if (data.size() >= 4 && static_cast<unsigned char>(data[0]) == 0x30
        && static_cast<unsigned char>(data[1]) == 0x26
        && static_cast<unsigned char>(data[2]) == 0xB2
        && static_cast<unsigned char>(data[3]) == 0x75) {
        return video("wmv", "video/x-ms-wmv", "WMV");
    }
    // MPEG transport stream: the 0x47 sync byte repeats every 188 bytes.
    if (data.size() >= 189 && static_cast<unsigned char>(data[0]) == 0x47
        && static_cast<unsigned char>(data[188]) == 0x47) {
        return video("mts", "video/mp2t", "MPEG-TS");
    }

    return {};
}

} // namespace kg
