#include "core/indexer/MediaSniffer.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace kg;

namespace {

/// Writes `bytes` to a temp file whose extension carries no hint, so the sniffer
/// has to rely on the content only.
QString writeBlob(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(bytes);
    file.close();
    return path;
}

} // namespace

/**
 * The sniffer is the tier-2 half of hybrid detection: it only ever runs for
 * files whose extension is not on the allowlist. These tests pin the containers
 * a camera or download folder is most likely to drop in without a useful name.
 */
class TestMediaSniffer : public QObject
{
    Q_OBJECT

private slots:
    void recognisesJpeg();
    void recognisesPng();
    void recognisesWebp();
    void recognisesTiffAndRaw();
    void recognisesHeifAndAvif();
    void recognisesMp4AndMov();
    void recognisesMatroskaAndAvi();
    void recognisesSvg();
    void recognisesMpegTransportStream();
    void rejectsTextAndRandomData();
};

void TestMediaSniffer::recognisesJpeg()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path =
        writeBlob(dir, QStringLiteral("photo.dat"),
                  QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(32, '\0'));
    const SniffResult result = MediaSniffer::sniff(path);
    QVERIFY(result.media);
    QVERIFY(!result.video);
    QCOMPARE(result.extension, QStringLiteral("jpg"));
    QCOMPARE(result.mime, QStringLiteral("image/jpeg"));
}

void TestMediaSniffer::recognisesPng()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeBlob(dir, QStringLiteral("blob"),
                                   QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(32, '\0'));
    const SniffResult result = MediaSniffer::sniff(path);
    QVERIFY(result.media);
    QCOMPARE(result.extension, QStringLiteral("png"));
}

void TestMediaSniffer::recognisesWebp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray bytes = QByteArray("RIFF") + QByteArray("\x24\x00\x00\x00", 4)
        + QByteArray("WEBPVP8 ");
    const QString path = writeBlob(dir, QStringLiteral("blob"), bytes);
    const SniffResult result = MediaSniffer::sniff(path);
    QVERIFY(result.media);
    QCOMPARE(result.extension, QStringLiteral("webp"));
}

void TestMediaSniffer::recognisesTiffAndRaw()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const SniffResult little = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("a"), QByteArray("II*\0", 4) + QByteArray(32, '\0')));
    QVERIFY(little.media);
    QCOMPARE(little.extension, QStringLiteral("tiff"));

    const SniffResult big = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("b"), QByteArray("MM\0*", 4) + QByteArray(32, '\0')));
    QVERIFY(big.media);
    QCOMPARE(big.extension, QStringLiteral("tiff"));

    const SniffResult raf = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("c"), QByteArray("FUJIFILMCCD-RAW ") + QByteArray(8, '\0')));
    QVERIFY(raf.media);
    QCOMPARE(raf.extension, QStringLiteral("raf"));
}

void TestMediaSniffer::recognisesHeifAndAvif()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto isoBox = [](const QByteArray &brand) {
        return QByteArray("\x00\x00\x00\x18", 4) + QByteArray("ftyp") + brand
            + QByteArray("\x00\x00\x02\x00", 4) + brand + QByteArray("mif1", 4);
    };

    const SniffResult heic = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("a"), isoBox(QByteArray("heic"))));
    QVERIFY(heic.media);
    QVERIFY(!heic.video);
    QCOMPARE(heic.extension, QStringLiteral("heic"));

    const SniffResult avif = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("b"), isoBox(QByteArray("avif"))));
    QVERIFY(avif.media);
    QVERIFY(!avif.video);
    QCOMPARE(avif.extension, QStringLiteral("avif"));
}

void TestMediaSniffer::recognisesMp4AndMov()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto isoBox = [](const QByteArray &brand) {
        return QByteArray("\x00\x00\x00\x18", 4) + QByteArray("ftyp") + brand
            + QByteArray("\x00\x00\x02\x00", 4) + brand + QByteArray("\x00\x00\x00\x00", 4);
    };

    const SniffResult mp4 = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("a"), isoBox(QByteArray("isom"))));
    QVERIFY(mp4.media);
    QVERIFY(mp4.video);
    QCOMPARE(mp4.extension, QStringLiteral("mp4"));

    const SniffResult mov = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("b"), isoBox(QByteArray("qt  "))));
    QVERIFY(mov.media);
    QVERIFY(mov.video);
    QCOMPARE(mov.extension, QStringLiteral("mov"));
}

void TestMediaSniffer::recognisesMatroskaAndAvi()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const SniffResult mkv = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("a"),
                  QByteArray("\x1A\x45\xDF\xA3", 4) + QByteArray(32, '\0')));
    QVERIFY(mkv.media);
    QVERIFY(mkv.video);
    QCOMPARE(mkv.extension, QStringLiteral("mkv"));

    const SniffResult avi = MediaSniffer::sniff(
        writeBlob(dir, QStringLiteral("b"),
                  QByteArray("RIFF") + QByteArray("\x10\x00\x00\x00", 4) + QByteArray("AVI ")));
    QVERIFY(avi.media);
    QVERIFY(avi.video);
    QCOMPARE(avi.extension, QStringLiteral("avi"));
}

void TestMediaSniffer::recognisesSvg()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeBlob(
        dir, QStringLiteral("icon"), QByteArray("<?xml version=\"1.0\"?>\n<svg xmlns=\"...\">"));
    const SniffResult result = MediaSniffer::sniff(path);
    QVERIFY(result.media);
    QVERIFY(!result.video);
    QCOMPARE(result.extension, QStringLiteral("svg"));
}

void TestMediaSniffer::recognisesMpegTransportStream()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray bytes(256, '\0');
    bytes[0] = 0x47;
    bytes[188] = 0x47;
    const SniffResult result = MediaSniffer::sniff(writeBlob(dir, QStringLiteral("stream"), bytes));
    QVERIFY(result.media);
    QVERIFY(result.video);
    QCOMPARE(result.extension, QStringLiteral("mts"));
}

void TestMediaSniffer::rejectsTextAndRandomData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!MediaSniffer::sniff(
                 writeBlob(dir, QStringLiteral("notes"), QByteArray("hello world, not media")))
                 .media);
    QVERIFY(!MediaSniffer::sniff(writeBlob(dir, QStringLiteral("empty"), QByteArray()))
                 .media);
    QVERIFY(!MediaSniffer::sniff(
                 writeBlob(dir, QStringLiteral("xml"),
                           QByteArray("<?xml version=\"1.0\"?>\n<note>hi</note>")))
                 .media);
}

QTEST_MAIN(TestMediaSniffer)
#include "tst_mediasniffer.moc"
