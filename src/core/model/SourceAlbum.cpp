#include "core/model/SourceAlbum.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace kg {

namespace {
struct CompiledRule
{
    QString album;
    QList<QRegularExpression> pathPatterns;
    QList<QRegularExpression> filenamePatterns;
};

/// Glob -> anchored, case-insensitive regex. `*` spans path separators so a
/// pattern like `*/dcim/camera/*` matches anywhere in the tree, which is what we
/// actually want for phone exports that keep their original folder structure.
QRegularExpression compileGlob(const QString &glob)
{
    QString pattern;
    pattern.reserve(glob.size() * 2);
    for (const QChar ch : glob) {
        switch (ch.unicode()) {
        case u'*':
            pattern += QStringLiteral(".*");
            break;
        case u'?':
            pattern += QLatin1Char('.');
            break;
        default:
            pattern += QRegularExpression::escape(QString(ch));
            break;
        }
    }
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}
} // namespace

const QList<SourceAlbum::Rule> &SourceAlbum::buildRules()
{
    static const QList<Rule> rules = {
        {QStringLiteral("Camera"),
         {QStringLiteral("*/dcim/camera/*"), QStringLiteral("*/dcim/*")},
         {}},
        {QStringLiteral("Screenshots"),
         {QStringLiteral("*/screenshots/*"), QStringLiteral("*/pictures/screenshots/*")},
         {QStringLiteral("screenshot*"), QStringLiteral("screen_shot*"), QStringLiteral("scr_*")}},
        {QStringLiteral("WhatsApp"),
         {QStringLiteral("*/whatsapp images/*"),
          QStringLiteral("*/whatsapp video/*"),
          QStringLiteral("*/whatsapp/*"),
          QStringLiteral("*/media/com.whatsapp/*")},
         {QStringLiteral("img-*-wa*"), QStringLiteral("vid-*-wa*")}},
        {QStringLiteral("Telegram"),
         {QStringLiteral("*/telegram/*"), QStringLiteral("*/org.telegram.messenger/*")},
         {QStringLiteral("telegram*")}},
        {QStringLiteral("Downloads"),
         {QStringLiteral("*/downloads/*"), QStringLiteral("*/download/*")},
         {}},
        {QStringLiteral("Instagram"),
         {QStringLiteral("*/instagram/*"), QStringLiteral("*/com.instagram.android/*")},
         {}},
        {QStringLiteral("Signal"),
         {QStringLiteral("*/org.thoughtcrime.securesms/*"), QStringLiteral("*/signal/*")},
         {}},
        {QStringLiteral("DCIM"),
         {QStringLiteral("*/dcim/*")},
         {}},
    };
    return rules;
}

const QList<SourceAlbum::Rule> &SourceAlbum::rules()
{
    return buildRules();
}

QString SourceAlbum::detect(const QString &absolutePath, const QString &filename)
{
    static const QHash<QString, CompiledRule> compiled = [] {
        QHash<QString, CompiledRule> out;
        for (const Rule &rule : buildRules()) {
            CompiledRule c;
            c.album = rule.album;
            for (const QString &glob : rule.pathGlobs) {
                c.pathPatterns.append(compileGlob(glob));
            }
            for (const QString &glob : rule.filenameGlobs) {
                c.filenamePatterns.append(compileGlob(glob));
            }
            out.insert(rule.album, c);
        }
        return out;
    }();

    const QString normalisedPath = QString(absolutePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString base = filename.isEmpty() ? QFileInfo(absolutePath).fileName() : filename;

    // Rules are order-sensitive, so iterate the ordered list and look up the
    // compiled patterns rather than iterating the hash.
    for (const Rule &rule : buildRules()) {
        const auto it = compiled.constFind(rule.album);
        if (it == compiled.constEnd()) {
            continue;
        }
        for (const QRegularExpression &re : it->pathPatterns) {
            if (re.match(normalisedPath).hasMatch()) {
                return rule.album;
            }
        }
        for (const QRegularExpression &re : it->filenamePatterns) {
            if (re.match(base).hasMatch()) {
                return rule.album;
            }
        }
    }

    return parentFolderFallback(absolutePath);
}

QString SourceAlbum::parentFolderFallback(const QString &absolutePath)
{
    const QString parent = QFileInfo(absolutePath).absolutePath();
    const QString name = QDir(parent).dirName();
    if (name.isEmpty()) {
        return QStringLiteral("Uncategorised");
    }
    return name;
}

} // namespace kg
