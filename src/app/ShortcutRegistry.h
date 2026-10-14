#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace kg {

/**
 * Rebindable keyboard shortcuts.
 *
 * The action list is fixed in code (each action is wired to one behaviour in
 * QML); only the key sequence is user-editable. Overrides live in QSettings under
 * `shortcuts/<id>`, so resetting an action simply removes the key and the built-in
 * default applies again.
 *
 * Sequences are stored as Qt *PortableText* ("Ctrl+Shift+F"), which is exactly
 * what `Shortcut.sequence` in QML and `QKeySequence::toString(PortableText)` agree
 * on. Two actions may not share a sequence while both are enabled - `setSequence`
 * refuses that and reports which action already owns it.
 */
class ShortcutRegistry : public QObject
{
    Q_OBJECT

    /** `id -> sequence` for every action, in PortableText. Bind `Shortcut.sequence`
     *  to an entry so a rebind re-evaluates immediately. */
    Q_PROPERTY(QVariantMap sequences READ sequences NOTIFY changed)
    /** Every action as {id,label,group,sequence,default,isDefault}, for the
     *  settings page. Re-read whenever a binding changes. */
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)

public:
    explicit ShortcutRegistry(QObject *parent = nullptr);

    struct Action
    {
        QString id;
        QString label; ///< translated
        QString group; ///< "Global" / "Viewer" / "Grid"
        QString defaultSequence;
    };

    /// Every action, with the current sequence and whether it is an override.
    /// Each map: {id, label, group, sequence, default, isDefault}.
    QVariantList actions() const;

    QVariantMap sequences() const;

    Q_INVOKABLE QString sequence(const QString &id) const;
    Q_INVOKABLE QString defaultSequence(const QString &id) const;
    Q_INVOKABLE bool isDefault(const QString &id) const;

    /// Rebinds `id`. Returns an empty string on success, or a human-readable
    /// error (invalid sequence, or the label of the action that already uses it).
    Q_INVOKABLE QString setSequence(const QString &id, const QString &sequence);

    /// Removes one override, restoring its built-in default.
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();

    /// Label of an action (other than `exceptId`) already bound to `sequence`,
    /// or an empty string. Used to warn about conflicts before committing.
    Q_INVOKABLE QString conflictsFor(const QString &sequence, const QString &exceptId = QString()) const;

    /// Turns a QML `Keys.onPressed` event into a PortableText sequence, so the
    /// settings page can capture a chord without reimplementing Qt's key naming.
    Q_INVOKABLE QString sequenceFromKey(int key, int modifiers) const;

signals:
    void changed();

private:
    void load();
    void save() const;
    static QVector<Action> actionTable();

    QHash<QString, QString> m_overrides;
};

} // namespace kg
