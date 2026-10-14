#include "app/PathUtils.h"

#include <QtTest>

using namespace kg;

/**
 * localPath() is the single conversion point for QML's `file://` FolderDialog
 * selection. It replaced two divergent copies, one of which was the direct cause
 * of the "Target folder does not exist" bug in the move action.
 */
class TestPathUtils : public QObject
{
    Q_OBJECT

private slots:
    void plainPathIsUnchanged();
    void fileUrlIsDecoded();
    void encodedFileUrlIsDecoded();
    void schemeIsCaseInsensitive();
    void nonLocalUrlIsUnchanged();
    void emptyStaysEmpty();
};

void TestPathUtils::plainPathIsUnchanged()
{
    QCOMPARE(localPath(QStringLiteral("/home/user/Pictures")),
             QStringLiteral("/home/user/Pictures"));
    QCOMPARE(localPath(QStringLiteral("relative/dir")), QStringLiteral("relative/dir"));
}

void TestPathUtils::fileUrlIsDecoded()
{
    QCOMPARE(localPath(QStringLiteral("file:///tmp/a b")), QStringLiteral("/tmp/a b"));
}

void TestPathUtils::encodedFileUrlIsDecoded()
{
    // QUrl decodes %20 as part of toLocalFile().
    QCOMPARE(localPath(QStringLiteral("file:///tmp/a%20b")), QStringLiteral("/tmp/a b"));
}

void TestPathUtils::schemeIsCaseInsensitive()
{
    QCOMPARE(localPath(QStringLiteral("FILE:///tmp/x")), QStringLiteral("/tmp/x"));
}

void TestPathUtils::nonLocalUrlIsUnchanged()
{
    const QString remote = QStringLiteral("https://example.com/x");
    QCOMPARE(localPath(remote), remote);
    // A file URL with a host is decoded to its UNC-style local form; what matters
    // is that a non-file scheme is never touched.
    QCOMPARE(localPath(QStringLiteral("file://server/share/x")), QStringLiteral("//server/share/x"));
}

void TestPathUtils::emptyStaysEmpty()
{
    QCOMPARE(localPath(QString()), QString());
}

QTEST_MAIN(TestPathUtils)
#include "tst_pathutils.moc"
