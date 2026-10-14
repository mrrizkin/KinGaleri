#include "app/ShortcutRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace kg;

/**
 * The registry is the whole of the "custom keyboard shortcuts" feature. The
 * tests pin the three things that would silently corrupt a user's setup:
 * persistence, conflict rejection, and reset-to-default.
 *
 * XDG_CONFIG_HOME is redirected so the developer's real bindings are untouched.
 */
class TestShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void hasDefaults();
    void rebindPersists();
    void conflictRejected();
    void resetRestoresDefault();
    void resetAllRestoresDefaults();
    void captureFromKeyEvent();
    void storedGarbageIsIgnored();

private:
    QTemporaryDir m_configDir;
};

void TestShortcuts::initTestCase()
{
    QVERIFY(m_configDir.isValid());
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toLocal8Bit());
    QCoreApplication::setOrganizationName(QStringLiteral("kin-test"));
    QCoreApplication::setApplicationName(QStringLiteral("Kin Galeri Shortcut Test"));
}

void TestShortcuts::init()
{
    QSettings().clear();
}

void TestShortcuts::hasDefaults()
{
    ShortcutRegistry registry;
    QCOMPARE(registry.sequence(QStringLiteral("search")), QStringLiteral("Ctrl+F"));
    QCOMPARE(registry.sequence(QStringLiteral("viewer_next")), QStringLiteral("Right"));
    QVERIFY(registry.isDefault(QStringLiteral("search")));
    QVERIFY(registry.actions().size() >= 10);
    QVERIFY(registry.sequences().contains(QStringLiteral("trash")));
}

void TestShortcuts::rebindPersists()
{
    {
        ShortcutRegistry registry;
        QCOMPARE(registry.setSequence(QStringLiteral("search"), QStringLiteral("Ctrl+Shift+F")),
                 QString());
        QVERIFY(!registry.isDefault(QStringLiteral("search")));
    }
    ShortcutRegistry reloaded;
    QCOMPARE(reloaded.sequence(QStringLiteral("search")), QStringLiteral("Ctrl+Shift+F"));
}

void TestShortcuts::conflictRejected()
{
    ShortcutRegistry registry;
    // Ctrl+F is already the search binding.
    const QString error = registry.setSequence(QStringLiteral("reload"), QStringLiteral("Ctrl+F"));
    QVERIFY(!error.isEmpty());
    QCOMPARE(registry.sequence(QStringLiteral("reload")), QStringLiteral("Ctrl+R"));
}

void TestShortcuts::resetRestoresDefault()
{
    ShortcutRegistry registry;
    QCOMPARE(registry.setSequence(QStringLiteral("search"), QStringLiteral("Ctrl+Shift+F")),
             QString());
    registry.reset(QStringLiteral("search"));
    QCOMPARE(registry.sequence(QStringLiteral("search")), QStringLiteral("Ctrl+F"));
    QVERIFY(registry.isDefault(QStringLiteral("search")));
}

void TestShortcuts::resetAllRestoresDefaults()
{
    ShortcutRegistry registry;
    QCOMPARE(registry.setSequence(QStringLiteral("search"), QStringLiteral("Ctrl+Shift+F")),
             QString());
    QCOMPARE(registry.setSequence(QStringLiteral("reload"), QStringLiteral("Ctrl+Shift+R")), QString());
    registry.resetAll();
    QCOMPARE(registry.sequence(QStringLiteral("search")), QStringLiteral("Ctrl+F"));
    QCOMPARE(registry.sequence(QStringLiteral("reload")), QStringLiteral("Ctrl+R"));
}

void TestShortcuts::captureFromKeyEvent()
{
    ShortcutRegistry registry;
    QCOMPARE(registry.sequenceFromKey(Qt::Key_K, Qt::ControlModifier), QStringLiteral("Ctrl+K"));
    QCOMPARE(registry.sequenceFromKey(Qt::Key_F5, Qt::NoModifier), QStringLiteral("F5"));
    // A bare modifier must not become a binding.
    QCOMPARE(registry.sequenceFromKey(Qt::Key_Shift, Qt::ShiftModifier), QString());
    QCOMPARE(registry.sequenceFromKey(Qt::Key_unknown, Qt::NoModifier), QString());
}

void TestShortcuts::storedGarbageIsIgnored()
{
    // A hand-edited config with an unparseable chord must fall back to default.
    QSettings().setValue(QStringLiteral("shortcuts/search"), QStringLiteral("NotAKey!!!"));
    ShortcutRegistry registry;
    QCOMPARE(registry.sequence(QStringLiteral("search")), QStringLiteral("Ctrl+F"));
    QVERIFY(registry.isDefault(QStringLiteral("search")));
}

QTEST_MAIN(TestShortcuts)
#include "tst_shortcuts.moc"
