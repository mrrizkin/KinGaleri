#include "ui/quick/KinMpvItem.h"

#ifdef KG_HAS_MPV

// mpvqt 1.2.0 pulls mpvcontroller.h in from mpvabstractitem.h; 1.1.0 (the version
// Debian 13 and the AppImage build ship) only forward-declares MpvController, so
// including it here is required to call setProperty()/observeProperty() and to
// connect to propertyChanged(). This has to stay inside the guard: the file is in
// the unconditional source list and degrades to an empty translation unit when
// mpvqt is absent, which is what lets one source tree build with and without it.
#include <MpvQt/mpvcontroller.h>

#include <QDebug>
#include <QFileInfo>

#include <mpv/client.h>

KinMpvItem::KinMpvItem(QQuickItem *parent)
    : MpvAbstractItem(parent)
{
    // mpvqt instantiates and initialises the mpv handle internally and emits
    // ready() once it is usable. Observing properties before that is a no-op at
    // best and an error at worst, so gate everything on ready().
    connect(this, &MpvAbstractItem::ready, this, [this] {
        // Keep the file loaded at EOF. Without this mpv unloads the file when it
        // ends, which is indistinguishable from "nothing loaded" and was the
        // root cause of the forever-spinner / 0:00 / stale-poster bug.
        mpvController()->setProperty(QStringLiteral("keep-open"), QStringLiteral("yes"));
        mpvController()->setProperty(QStringLiteral("loop-file"),
                                     m_loop ? QStringLiteral("inf") : QStringLiteral("no"));
        attachObservers();
        applySource();
    });

    connect(mpvController(), &MpvController::propertyChanged,
            this, &KinMpvItem::onPropertyChanged);

    connect(mpvController(), &MpvController::fileLoaded, this, [this] {
        // A fresh file supersedes any previous EOF state.
        if (m_ended) {
            m_ended = false;
            Q_EMIT endedChanged();
        }
        if (m_loaded)
            return;
        m_loaded = true;
        Q_EMIT loadedChanged();
    });

    connect(mpvController(), &MpvController::videoReconfig, this, [this] {
        if (m_hasVideo)
            return;
        m_hasVideo = true;
        Q_EMIT hasVideoChanged();
    });

    connect(mpvController(), &MpvController::endFile, this, [this](const QString &reason) {
        // mpv reports `eof` when the file simply ran out. With keep-open=yes this
        // normally never arrives (eof-reached is used instead), but keep the
        // handling as a fallback in case keep-open is overridden.
        // stop/quit/error/redirect really do unload the file.
        if (reason == QLatin1String("eof")) {
            if (!m_ended) {
                m_ended = true;
                Q_EMIT endedChanged();
            }
            // Pin the timeline to the end; keep duration and the video track.
            if (m_duration > 0 && m_position != m_duration) {
                m_position = m_duration;
                Q_EMIT positionChanged();
            }
            if (!m_paused) {
                m_paused = true;
                Q_EMIT pausedChanged();
                // keep-open pauses playback by itself, but confirming the flag
                // stops a late `pause` observation from flipping the UI back to
                // "playing" while the clip sits at EOF.
                mpvController()->setProperty(QStringLiteral("pause"), true);
            }
            return;
        }

        // Genuinely unloaded.
        if (m_ended) {
            m_ended = false;
            Q_EMIT endedChanged();
        }
        if (m_position != 0) {
            m_position = 0;
            Q_EMIT positionChanged();
        }
        if (m_loaded) {
            m_loaded = false;
            Q_EMIT loadedChanged();
        }
        if (!m_paused) {
            m_paused = true;
            Q_EMIT pausedChanged();
        }
        if (m_hasVideo) {
            m_hasVideo = false;
            Q_EMIT hasVideoChanged();
        }
    });
}

KinMpvItem::~KinMpvItem() = default;

void KinMpvItem::attachObservers()
{
    if (m_observersAttached)
        return;
    m_observersAttached = true;

    MpvController *c = mpvController();
    c->observeProperty(QStringLiteral("time-pos"), MPV_FORMAT_DOUBLE);
    c->observeProperty(QStringLiteral("duration"), MPV_FORMAT_DOUBLE);
    c->observeProperty(QStringLiteral("pause"), MPV_FORMAT_FLAG);
    c->observeProperty(QStringLiteral("volume"), MPV_FORMAT_DOUBLE);
    c->observeProperty(QStringLiteral("mute"), MPV_FORMAT_FLAG);
    c->observeProperty(QStringLiteral("speed"), MPV_FORMAT_DOUBLE);
    // `width` becomes > 0 only once a video track is actually being decoded,
    // which is exactly the condition for showing the video surface.
    c->observeProperty(QStringLiteral("width"), MPV_FORMAT_INT64);
    // With keep-open=yes mpv does NOT emit end-file at EOF; instead it sets
    // `eof-reached` and pauses. This is the real "playback finished" signal.
    c->observeProperty(QStringLiteral("eof-reached"), MPV_FORMAT_FLAG);
}

void KinMpvItem::onPropertyChanged(const QString &property, const QVariant &value)
{
    // mpv emits an *unavailable* property as an invalid QVariant. Transient
    // unavailability happens during seeks, track changes and at EOF, so an
    // unguarded toDouble()/toBool() silently zeroes position, duration and
    // width. Ignore those updates and keep the last known good values.
    if (!value.isValid())
        return;

    if (property == QLatin1String("time-pos")) {
        // Once EOF is reached, keep the pinned end position: mpv can still report
        // a slightly-earlier last frame time after `eof-reached`, which would
        // otherwise drag the timeline back from duration to ~0:46.
        if (m_ended)
            return;
        const double seconds = value.toDouble();
        if (!(seconds >= 0.0))
            return;
        const qint64 ms = static_cast<qint64>(seconds * 1000.0);
        if (ms == m_position)
            return;
        m_position = ms;
        Q_EMIT positionChanged();
    } else if (property == QLatin1String("duration")) {
        const double seconds = value.toDouble();
        // A real clip never has a zero/negative duration; such a value only ever
        // means "unavailable", so never let it clobber a known duration.
        if (!(seconds > 0.0))
            return;
        const qint64 ms = static_cast<qint64>(seconds * 1000.0);
        if (ms == m_duration)
            return;
        m_duration = ms;
        Q_EMIT durationChanged();
    } else if (property == QLatin1String("pause")) {
        const bool paused = value.toBool();
        if (paused == m_paused)
            return;
        m_paused = paused;
        Q_EMIT pausedChanged();
    } else if (property == QLatin1String("volume")) {
        const double volume = value.toDouble();
        if (qFuzzyCompare(volume + 1.0, m_volume + 1.0))
            return;
        m_volume = volume;
        Q_EMIT volumeChanged();
    } else if (property == QLatin1String("mute")) {
        const bool muted = value.toBool();
        if (muted == m_muted)
            return;
        m_muted = muted;
        Q_EMIT mutedChanged();
    } else if (property == QLatin1String("speed")) {
        const double speed = value.toDouble();
        if (qFuzzyCompare(speed + 1.0, m_speed + 1.0))
            return;
        m_speed = speed;
        Q_EMIT speedChanged();
    } else if (property == QLatin1String("width")) {
        const bool hasVideo = value.toLongLong() > 0;
        if (hasVideo == m_hasVideo)
            return;
        m_hasVideo = hasVideo;
        Q_EMIT hasVideoChanged();
    } else if (property == QLatin1String("eof-reached")) {
        // The canonical end-of-playback signal when keep-open=yes: the file stays
        // loaded (duration/width/time-pos remain valid, last frame is retained),
        // so this only flips the `ended` flag rather than unloading anything.
        const bool ended = value.toBool();
        if (ended == m_ended)
            return;
        m_ended = ended;
        if (ended) {
            if (m_duration > 0 && m_position != m_duration) {
                m_position = m_duration;
                Q_EMIT positionChanged();
            }
            if (!m_paused) {
                m_paused = true;
                Q_EMIT pausedChanged();
            }
        }
        Q_EMIT endedChanged();
    }
}

void KinMpvItem::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    m_source = source;
    Q_EMIT sourceChanged();

    // Reset per-file state so a stale duration from the previous clip is not
    // shown while the new one is still loading.
    m_position = 0;
    m_duration = 0;
    m_loaded = false;
    m_hasVideo = false;
    m_ended = false;
    setErrorString(QString());
    Q_EMIT positionChanged();
    Q_EMIT durationChanged();
    Q_EMIT loadedChanged();
    Q_EMIT hasVideoChanged();
    Q_EMIT endedChanged();

    if (!m_source.isEmpty())
        applySource();
}

void KinMpvItem::applySource()
{
    if (!m_observersAttached || m_source.isEmpty())
        return;

    const QString path = m_source.isLocalFile() ? m_source.toLocalFile() : m_source.toString();
    if (m_source.isLocalFile() && !QFileInfo::exists(path)) {
        setErrorString(tr("File not found: %1").arg(path));
        return;
    }

    // `replace` rather than `append-play`: switching clips in the viewer must not
    // queue them up.
    commandAsync(QStringList{QStringLiteral("loadfile"), path, QStringLiteral("replace")});
}

void KinMpvItem::setErrorString(const QString &message)
{
    if (message == m_errorString)
        return;
    m_errorString = message;
    Q_EMIT errorStringChanged();
}

void KinMpvItem::play()
{
    if (m_ended) {
        replay();
        return;
    }
    setPaused(false);
}

void KinMpvItem::pause()
{
    setPaused(true);
}

void KinMpvItem::togglePause()
{
    if (m_ended) {
        replay();
        return;
    }
    setPaused(!m_paused);
}

void KinMpvItem::replay()
{
    if (m_ended) {
        m_ended = false;
        Q_EMIT endedChanged();
    }
    // Jump back to the start before unpausing: keep-open leaves the file loaded
    // and sitting at EOF, so without this seek resuming would immediately finish
    // again. Seeking also clears mpv's own `eof-reached` flag, which is what the
    // property observer above listens to.
    if (m_observersAttached)
        commandAsync(QStringList{QStringLiteral("seek"), QStringLiteral("0"), QStringLiteral("absolute")});
    if (m_position != 0) {
        m_position = 0;
        Q_EMIT positionChanged();
    }
    setPaused(false);
}

void KinMpvItem::setPaused(bool paused)
{
    if (paused == m_paused)
        return;
    // Optimistic local update: mpv confirms via the `pause` observation, which
    // then finds the value already current and stays silent. Without this the
    // UI would lag a round-trip behind the user's click.
    m_paused = paused;
    Q_EMIT pausedChanged();
    if (m_observersAttached)
        mpvController()->setProperty(QStringLiteral("pause"), paused);
}

void KinMpvItem::seek(qint64 positionMs)
{
    const qint64 target = qMax<qint64>(0, positionMs);
    if (m_position != target) {
        m_position = target;
        Q_EMIT positionChanged();
    }
    if (!m_observersAttached)
        return;
    commandAsync(QStringList{QStringLiteral("seek"),
                                     QString::number(target / 1000.0, 'f', 3),
                                     QStringLiteral("absolute")});
}

void KinMpvItem::seekRelative(qint64 deltaMs)
{
    if (!m_observersAttached)
        return;
    commandAsync(QStringList{QStringLiteral("seek"),
                                     QString::number(deltaMs / 1000.0, 'f', 3),
                                     QStringLiteral("relative")});
}

void KinMpvItem::stop()
{
    if (!m_observersAttached)
        return;
    commandAsync(QStringList{QStringLiteral("stop")});
    m_paused = true;
    m_loaded = false;
    m_hasVideo = false;
    m_position = 0;
    m_ended = false;
    Q_EMIT pausedChanged();
    Q_EMIT loadedChanged();
    Q_EMIT hasVideoChanged();
    Q_EMIT positionChanged();
    Q_EMIT endedChanged();
}

void KinMpvItem::setVolume(double volume)
{
    const double clamped = qBound(0.0, volume, 100.0);
    if (qFuzzyCompare(clamped + 1.0, m_volume + 1.0))
        return;
    m_volume = clamped;
    Q_EMIT volumeChanged();
    if (m_observersAttached)
        mpvController()->setProperty(QStringLiteral("volume"), clamped);
}

void KinMpvItem::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    Q_EMIT mutedChanged();
    if (m_observersAttached)
        mpvController()->setProperty(QStringLiteral("mute"), muted);
}

void KinMpvItem::setSpeed(double speed)
{
    const double clamped = qBound(0.25, speed, 4.0);
    if (qFuzzyCompare(clamped + 1.0, m_speed + 1.0))
        return;
    m_speed = clamped;
    Q_EMIT speedChanged();
    if (m_observersAttached)
        mpvController()->setProperty(QStringLiteral("speed"), clamped);
}

void KinMpvItem::setLoop(bool loop)
{
    if (loop == m_loop)
        return;
    m_loop = loop;
    Q_EMIT loopChanged();
    if (m_observersAttached)
        mpvController()->setProperty(QStringLiteral("loop-file"),
                                     loop ? QStringLiteral("inf") : QStringLiteral("no"));
}

#endif // KG_HAS_MPV
