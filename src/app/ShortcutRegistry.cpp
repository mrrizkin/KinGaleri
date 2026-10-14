#include "app/ShortcutRegistry.h"

#include <QHash>
#include <QKeyCombination>
#include <QKeySequence>
#include <QSettings>

namespace kg {

namespace {

constexpr auto kGroupKey = "shortcuts";

} // namespace

ShortcutRegistry::ShortcutRegistry(QObject *parent)
    : QObject(parent)
{
    load();
}

QVector<ShortcutRegistry::Action> ShortcutRegistry::actionTable()
{
    return {
        {QStringLiteral("search"), tr("Focus search"), tr("Global"), QStringLiteral("Ctrl+F")},
        {QStringLiteral("reload"), tr("Reload library"), tr("Global"), QStringLiteral("Ctrl+R")},
        {QStringLiteral("undo"), tr("Undo last action"), tr("Global"), QStringLiteral("Ctrl+Z")},
        {QStringLiteral("cancel"), tr("Close / clear focus"), tr("Global"), QStringLiteral("Escape")},
        {QStringLiteral("trash"), tr("Move to recycle bin"), tr("Grid"), QStringLiteral("Delete")},

        {QStringLiteral("viewer_previous"), tr("Previous item / seek back"), tr("Viewer"), QStringLiteral("Left")},
        {QStringLiteral("viewer_next"), tr("Next item / seek forward"), tr("Viewer"), QStringLiteral("Right")},
        {QStringLiteral("viewer_play_pause"), tr("Play-pause / reset view"), tr("Viewer"), QStringLiteral("Space")},
        {QStringLiteral("viewer_favorite"), tr("Toggle favourite"), tr("Viewer"), QStringLiteral("F")},
        {QStringLiteral("viewer_mute"), tr("Mute video"), tr("Viewer"), QStringLiteral("M")},
        {QStringLiteral("viewer_zoom_in"), tr("Zoom in"), tr("Viewer"), QStringLiteral("Ctrl+=")},
        {QStringLiteral("viewer_zoom_out"), tr("Zoom out"), tr("Viewer"), QStringLiteral("Ctrl+-")},
    };
}

void ShortcutRegistry::load()
{
    m_overrides.clear();
    const QSettings settings;
    for (const Action &action : actionTable()) {
        const QString stored = settings
                                   .value(QString::fromLatin1(kGroupKey) + QLatin1Char('/') + action.id)
                                   .toString()
                                   .trimmed();
        if (stored.isEmpty())
            continue;
        // A hand-edited config can hold garbage; ignore anything Qt cannot parse
        // rather than binding a dead key.
        const QString normalised =
            QKeySequence(stored, QKeySequence::PortableText).toString(QKeySequence::PortableText);
        if (!normalised.isEmpty() && normalised != action.defaultSequence)
            m_overrides.insert(action.id, normalised);
    }
}

void ShortcutRegistry::save() const
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kGroupKey));
    settings.remove(QString()); // clear the group; overrides are the only state
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.endGroup();
}

QVariantMap ShortcutRegistry::sequences() const
{
    QVariantMap map;
    for (const Action &action : actionTable())
        map.insert(action.id, m_overrides.value(action.id, action.defaultSequence));
    return map;
}

QString ShortcutRegistry::sequence(const QString &id) const
{
    for (const Action &action : actionTable()) {
        if (action.id == id)
            return m_overrides.value(action.id, action.defaultSequence);
    }
    return {};
}

QString ShortcutRegistry::defaultSequence(const QString &id) const
{
    for (const Action &action : actionTable()) {
        if (action.id == id)
            return action.defaultSequence;
    }
    return {};
}

bool ShortcutRegistry::isDefault(const QString &id) const
{
    return !m_overrides.contains(id);
}

QVariantList ShortcutRegistry::actions() const
{
    QVariantList list;
    for (const Action &action : actionTable()) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), action.id);
        map.insert(QStringLiteral("label"), action.label);
        map.insert(QStringLiteral("group"), action.group);
        map.insert(QStringLiteral("sequence"), m_overrides.value(action.id, action.defaultSequence));
        map.insert(QStringLiteral("default"), action.defaultSequence);
        map.insert(QStringLiteral("isDefault"), !m_overrides.contains(action.id));
        list.append(map);
    }
    return list;
}

QString ShortcutRegistry::conflictsFor(const QString &sequence, const QString &exceptId) const
{
    if (sequence.isEmpty())
        return {};
    const QString normalised =
        QKeySequence(sequence, QKeySequence::PortableText).toString(QKeySequence::PortableText);
    if (normalised.isEmpty())
        return {};
    for (const Action &action : actionTable()) {
        if (action.id == exceptId)
            continue;
        if (m_overrides.value(action.id, action.defaultSequence) == normalised)
            return action.label;
    }
    return {};
}

QString ShortcutRegistry::setSequence(const QString &id, const QString &keySequence)
{
    const QString normalised =
        QKeySequence(keySequence, QKeySequence::PortableText).toString(QKeySequence::PortableText);
    if (normalised.isEmpty())
        return tr("That key combination cannot be used.");

    const QString conflict = conflictsFor(normalised, id);
    if (!conflict.isEmpty())
        return tr("Already used by “%1”.").arg(conflict);

    const QString fallback = defaultSequence(id);
    if (fallback.isEmpty() && this->sequence(id).isEmpty())
        return tr("Unknown action.");

    if (normalised == fallback)
        m_overrides.remove(id);
    else
        m_overrides.insert(id, normalised);
    save();
    Q_EMIT changed();
    return {};
}

void ShortcutRegistry::reset(const QString &id)
{
    if (m_overrides.remove(id) <= 0)
        return;
    save();
    Q_EMIT changed();
}

void ShortcutRegistry::resetAll()
{
    if (m_overrides.isEmpty())
        return;
    m_overrides.clear();
    save();
    Q_EMIT changed();
}

QString ShortcutRegistry::sequenceFromKey(int key, int modifiers) const
{
    const auto typedKey = static_cast<Qt::Key>(key);
    switch (typedKey) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_unknown:
        return {}; // a bare modifier is not a binding
    default:
        break;
    }
    const QKeySequence seq(QKeyCombination(static_cast<Qt::KeyboardModifiers>(modifiers), typedKey));
    return seq.toString(QKeySequence::PortableText);
}

} // namespace kg
