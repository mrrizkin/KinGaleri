#include <QtTest>

#include "core/thumbs/ImageLoader.h"

#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace kg;

class TestImageLoader : public QObject
{
    Q_OBJECT

private slots:
    void isHeifPath_data();
    void isHeifPath();
    void convertDetected();
    void decodeMissingReturnsNull();
    void decodeRoundTrip();
};

void TestImageLoader::isHeifPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("heif");

    QTest::newRow("heic") << QStringLiteral("/tmp/a.HEIC") << true;
    QTest::newRow("heif") << QStringLiteral("/tmp/a.heif") << true;
    QTest::newRow("jpg") << QStringLiteral("/tmp/a.jpg") << false;
    QTest::newRow("no-ext") << QStringLiteral("/tmp/a") << false;
}

void TestImageLoader::isHeifPath()
{
    QFETCH(QString, path);
    QFETCH(bool, heif);
    QCOMPARE(ImageLoader::isHeifPath(path), heif);
}

void TestImageLoader::convertDetected()
{
    // Records the environment rather than asserting, so the suite stays green on
    // machines with only libheif (no -examples package).
    const bool available = ImageLoader::heifConvertAvailable();
    qInfo("heif-convert available: %s", available ? "yes" : "no");
}

void TestImageLoader::decodeMissingReturnsNull()
{
    QVERIFY(ImageLoader::decodeHeif(QStringLiteral("/nonexistent/photo.heic")).isNull());
    // A non-HEIF path is rejected up front, even if the file exists.
    QTemporaryDir dir;
    const QString png = dir.filePath(QStringLiteral("plain.png"));
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(png));
    QVERIFY(ImageLoader::decodeHeif(png).isNull());
}

void TestImageLoader::decodeRoundTrip()
{
    if (!ImageLoader::heifConvertAvailable())
        QSKIP("heif-convert not installed");

    const QString encoder = QStandardPaths::findExecutable(QStringLiteral("heif-enc"));
    if (encoder.isEmpty())
        QSKIP("heif-enc not installed; cannot build a HEIC fixture at runtime");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString png = dir.filePath(QStringLiteral("source.png"));
    QImage source(320, 200, QImage::Format_RGB32);
    source.fill(QColor(30, 140, 200));
    QVERIFY(source.save(png));

    const QString heic = dir.filePath(QStringLiteral("source.heic"));
    QProcess encode;
    encode.setProgram(encoder);
    encode.setArguments({png, QStringLiteral("-o"), heic, QStringLiteral("-q"), QStringLiteral("60")});
    encode.start();
    QVERIFY(encode.waitForFinished(30000));
    if (encode.exitStatus() != QProcess::NormalExit || encode.exitCode() != 0
        || !QFileInfo::exists(heic)) {
        QSKIP("heif-enc could not encode the fixture (missing encoder backend)");
    }

    const QImage decoded = ImageLoader::decodeHeif(heic);
    QVERIFY2(!decoded.isNull(), "decodeHeif returned a null image for a valid HEIC file");
    QCOMPARE(decoded.size(), QSize(320, 200));

    const QImage scaled = ImageLoader::decodeHeif(heic, 100);
    QVERIFY(!scaled.isNull());
    QCOMPARE(qMax(scaled.width(), scaled.height()), 100);
}

QTEST_MAIN(TestImageLoader)
#include "tst_imageloader.moc"
