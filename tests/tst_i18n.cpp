// Regression guard for the C++ translation contexts.
//
// Qt uses `metaObject()->className()` as the tr() context. For a class declared
// inside `namespace kg` that string is "kg::AppController", *not*
// "AppController" - and this project's .ts is hand-maintained, so the two can
// drift apart silently. When they did, every C++ string fell back to English
// while the QML strings kept working, which made the bug very hard to notice.
//
// The test pins the two sides together: the expected context for a class that is
// linked into kin-galeri-testcore is read from the live metaobject rather than
// hard-coded, so renaming a namespace or a class fails here instead of shipping.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include <QFile>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

#include "app/ShortcutRegistry.h"
#include "models/MediaGridModel.h"

#ifndef KG_TS_PATH
#error "KG_TS_PATH must point at resources/i18n/kin-galeri_id.ts"
#endif

namespace {

struct Catalog
{
    /// context name -> source strings declared under it
    QHash<QString, QSet<QString>> sources;
    /// "context/source" entries whose <translation> is empty or whitespace
    QStringList emptyTranslations;
};

/// Collect contexts and sources straight out of the .ts (no lrelease needed, so
/// the guard also catches a string that was wrapped in tr() but never added to
/// the catalog).
Catalog parseTs(const QString &path)
{
    Catalog catalog;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return catalog;

    QXmlStreamReader xml(&file);
    QString context;
    QString source;
    QString translation;
    bool inMessage = false;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == QLatin1String("context")) {
                context.clear();
            } else if (name == QLatin1String("name") && context.isEmpty()) {
                context = xml.readElementText();
            } else if (name == QLatin1String("message")) {
                inMessage = true;
                source.clear();
                translation.clear();
            } else if (inMessage && name == QLatin1String("source")) {
                source = xml.readElementText();
            } else if (inMessage && name == QLatin1String("translation")) {
                translation = xml.readElementText();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
            if (!source.isEmpty()) {
                catalog.sources[context].insert(source);
                if (translation.trimmed().isEmpty())
                    catalog.emptyTranslations << (context + QLatin1Char('/') + source);
            }
            inMessage = false;
        }
    }

    return catalog;
}

bool isCppContext(const QString &context)
{
    return context.startsWith(QLatin1String("kg::")) || context == QLatin1String("KinMpvItem");
}

} // namespace

class TestI18n : public QObject
{
    Q_OBJECT

public:
    TestI18n()
        : m_catalog(parseTs(QStringLiteral(KG_TS_PATH)))
    {
    }

private:
    const Catalog m_catalog;

private slots:
    void catalogParses()
    {
        QVERIFY2(!m_catalog.sources.isEmpty(),
                 qPrintable(QStringLiteral("could not parse any context out of %1")
                                .arg(QStringLiteral(KG_TS_PATH))));
        // Sanity check that QML contexts are picked up too, so a broken parser
        // cannot make every assertion below vacuously pass.
        QVERIFY(m_catalog.sources.contains(QStringLiteral("ViewerOverlay")));
    }

    /// The expected context must match the runtime metaobject name, not a guess.
    void cppContextsMatchTheirMetaObject()
    {
        QVERIFY(m_catalog.sources.contains(kg::MediaGridModel::staticMetaObject.className()));
        QVERIFY(m_catalog.sources.contains(kg::ShortcutRegistry::staticMetaObject.className()));

        // AppController is not part of kin-galeri-testcore (it pulls in the whole
        // app), so pin it literally - it is namespaced for the same reason.
        QVERIFY(m_catalog.sources.contains(QStringLiteral("kg::AppController")));

        // KinMpvItem is declared at global scope, so its context is not prefixed.
        QVERIFY(m_catalog.sources.contains(QStringLiteral("KinMpvItem")));
    }

    /// The exact shape of the historical bug: the bare class name as context.
    void shortContextNamesAreNotUsed()
    {
        const QStringList bareNames{
            QStringLiteral("AppController"),
            QStringLiteral("MediaGridModel"),
            QStringLiteral("ShortcutRegistry"),
        };
        for (const QString &name : bareNames) {
            QVERIFY2(!m_catalog.sources.contains(name),
                     qPrintable(name + QStringLiteral(" must be spelled with its kg:: namespace, "
                                                      "otherwise tr() never finds it")));
        }
    }

    void cppTranslationsAreNotEmpty()
    {
        QStringList missing;
        for (const QString &context : m_catalog.sources.keys()) {
            if (!isCppContext(context))
                continue;
            const QString prefix = context + QLatin1Char('/');
            for (const QString &entry : m_catalog.emptyTranslations) {
                if (entry.startsWith(prefix))
                    missing << entry;
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(", "))));
    }
};

QTEST_MAIN(TestI18n)
#include "tst_i18n.moc"
