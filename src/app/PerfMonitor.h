#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class QQuickWindow;

namespace kg {

/**
 * Frame pacing monitor for the M0 scroll benchmark (PRD 4.1 and milestone M0
 * "Proof: QML GridView render 50k dummy item @60fps").
 *
 * Hooks QQuickWindow::frameSwapped, which is exactly the signal that tells us a
 * frame reached the screen, and keeps a one second rolling window. `droppedFrames`
 * counts frames whose interval exceeded the target budget (16.7 ms at 60 Hz), which
 * is the number that actually distinguishes "smooth" from "smooth enough on
 * average".
 */
class PerfMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(double frameMs READ frameMs NOTIFY statsChanged)
    Q_PROPERTY(double worstFrameMs READ worstFrameMs NOTIFY statsChanged)
    Q_PROPERTY(qint64 frames READ frames NOTIFY statsChanged)
    Q_PROPERTY(qint64 droppedFrames READ droppedFrames NOTIFY statsChanged)
    Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps NOTIFY targetFpsChanged)
    Q_PROPERTY(bool attached READ attached NOTIFY attachedChanged)

public:
    /// Immutable copy of the current counters, for reporting outside the GUI thread.
    struct Stats
    {
        double fps = 0.0;
        double frameMs = 0.0;
        double worstFrameMs = 0.0;
        qint64 frames = 0;
        qint64 droppedFrames = 0;
        int targetFps = 60;
    };

    explicit PerfMonitor(QObject *parent = nullptr);

    Stats snapshot() const { return m_snapshot; }

    double fps() const { return m_fps; }
    double frameMs() const { return m_frameMs; }
    double worstFrameMs() const { return m_worstFrameMs; }
    qint64 frames() const { return m_frames; }
    qint64 droppedFrames() const { return m_droppedFrames; }
    int targetFps() const { return m_targetFps; }
    void setTargetFps(int fps);
    bool attached() const { return m_attached; }

    /// Idempotent; safe to call from Component.onCompleted.
    Q_INVOKABLE void attachWindow(QQuickWindow *window);
    Q_INVOKABLE void reset();

signals:
    void statsChanged();
    void targetFpsChanged();
    void attachedChanged();

    /// Emitted once, when the very first frame reaches the screen. main() uses
    /// this to report cold-start latency (PRD 4.1).
    void firstFramePresented();

private:
    void onFrameSwapped();

    QVector<double> m_intervalsMs;
    QElapsedTimer m_clock;
    qint64 m_lastFrameNs = 0;
    qint64 m_frames = 0;
    qint64 m_droppedFrames = 0;
    double m_fps = 0.0;
    double m_frameMs = 0.0;
    double m_worstFrameMs = 0.0;
    int m_targetFps = 60;
    bool m_attached = false;
    bool m_pendingEmit = false;
    bool m_firstFrameSeen = false;
    Stats m_snapshot;
};

} // namespace kg
