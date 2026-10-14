#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>

#ifdef KG_HAS_MPV

#include <MpvQt/mpvabstractitem.h>

/**
 * QML-facing wrapper around mpvqt's MpvAbstractItem.
 *
 * mpvqt ships libMpvQt.so and headers but NO qmldir/plugin/QML type, so the type
 * has to be registered by hand (see main.cpp). MpvAbstractItem already implements
 * createRenderer(), i.e. the hard part -- mpv's render API driving a
 * QQuickFramebufferObject. This subclass only adds a sane, typed QML surface.
 *
 * Two mpvqt details drive the design:
 *   - The invokables on MpvAbstractItem take the C++ enum `mpv_format`, which QML
 *     cannot produce, so every property access goes through a C++ wrapper here.
 *   - Property updates arrive as MpvController::propertyChanged. We observe the
 *     handful of properties we expose and remap them onto NOTIFY signals.
 */
class KinMpvItem : public MpvAbstractItem
{
    Q_OBJECT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    /** Playback position in milliseconds. */
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    /** Total duration in milliseconds; 0 until mpv reports one. */
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    /** 0-100, matching mpv's own volume scale. */
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    /** Repeat the current file forever instead of stopping at EOF. */
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY loopChanged)
    /** True once mpv has a video track; false for audio-only or before load. */
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    /** True once the file has been demuxed and playback can actually start. */
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    /**
     * True once playback reached the end of the file. The file stays loaded
     * (mpv `keep-open=yes`) so the last frame and the duration remain visible;
     * playing again replays from the start. Distinct from `loaded == false`,
     * which means the file was actually unloaded.
     */
    Q_PROPERTY(bool ended READ ended NOTIFY endedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit KinMpvItem(QQuickItem *parent = nullptr);
    ~KinMpvItem() override;

    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);

    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }

    bool paused() const { return m_paused; }
    void setPaused(bool paused);

    double volume() const { return m_volume; }
    void setVolume(double volume);

    bool muted() const { return m_muted; }
    void setMuted(bool muted);

    double speed() const { return m_speed; }
    void setSpeed(double speed);

    bool loop() const { return m_loop; }
    void setLoop(bool loop);

    bool hasVideo() const { return m_hasVideo; }
    bool loaded() const { return m_loaded; }
    bool ended() const { return m_ended; }
    QString errorString() const { return m_errorString; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePause();
    /** Absolute seek, in milliseconds. */
    Q_INVOKABLE void seek(qint64 positionMs);
    /** Relative seek, in milliseconds (negative rewinds). */
    Q_INVOKABLE void seekRelative(qint64 deltaMs);
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void sourceChanged();
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void loopChanged();
    void hasVideoChanged();
    void loadedChanged();
    void endedChanged();
    void errorStringChanged();

private:
    void attachObservers();
    void onPropertyChanged(const QString &property, const QVariant &value);
    void setErrorString(const QString &message);
    void applySource();
    /** Seek back to the start, clear `ended` and resume a finished file. */
    void replay();

    QUrl m_source;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_paused = true;
    double m_volume = 100.0;
    bool m_muted = false;
    double m_speed = 1.0;
    bool m_loop = false;
    bool m_hasVideo = false;
    bool m_loaded = false;
    bool m_ended = false;
    bool m_observersAttached = false;
    QString m_errorString;
};

#endif // KG_HAS_MPV
