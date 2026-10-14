#include "app/PerfMonitor.h"

#include <QQuickWindow>
#include <QTimer>

#include <algorithm>

namespace kg {

namespace {
/// Rolling window length for the reported average.
constexpr qint64 kWindowNs = 1000LL * 1000 * 1000;
/// How often QML is notified; 4 Hz keeps the label readable.
constexpr int kEmitIntervalMs = 250;
} // namespace

PerfMonitor::PerfMonitor(QObject *parent)
    : QObject(parent)
{
}

void PerfMonitor::attachWindow(QQuickWindow *window)
{
    if (!window || m_attached) {
        return;
    }
    connect(window, &QQuickWindow::frameSwapped, this, &PerfMonitor::onFrameSwapped,
            Qt::DirectConnection);
    m_clock.start();
    m_lastFrameNs = m_clock.nsecsElapsed();
    m_attached = true;
    Q_EMIT attachedChanged();
}

void PerfMonitor::reset()
{
    m_intervalsMs.clear();
    m_frames = 0;
    m_droppedFrames = 0;
    m_fps = 0.0;
    m_frameMs = 0.0;
    m_worstFrameMs = 0.0;
    m_snapshot = Stats{};
    m_snapshot.targetFps = m_targetFps;
    if (m_clock.isValid()) {
        m_lastFrameNs = m_clock.nsecsElapsed();
    }
    Q_EMIT statsChanged();
}

void PerfMonitor::setTargetFps(int fps)
{
    const int clamped = std::clamp(fps, 1, 480);
    if (clamped == m_targetFps) {
        return;
    }
    m_targetFps = clamped;
    m_snapshot.targetFps = clamped;
    Q_EMIT targetFpsChanged();
}

void PerfMonitor::onFrameSwapped()
{
    if (!m_firstFrameSeen) {
        m_firstFrameSeen = true;
        Q_EMIT firstFramePresented();
    }

    if (!m_clock.isValid()) {
        m_clock.start();
        m_lastFrameNs = m_clock.nsecsElapsed();
        return;
    }

    const qint64 now = m_clock.nsecsElapsed();
    const double intervalMs = static_cast<double>(now - m_lastFrameNs) / 1.0e6;
    m_lastFrameNs = now;

    ++m_frames;
    if (intervalMs > 1000.0 / m_targetFps * 1.5) {
        ++m_droppedFrames;
    }

    // Skip the first sample after a stall: it is not representative.
    if (intervalMs < 1000.0) {
        m_intervalsMs.append(intervalMs);
    }

    // Single source of truth for the window: prune by wall clock, so we do not
    // need a timer just to keep the vector bounded.
    constexpr int kMaxSamples = 240;
    if (m_intervalsMs.size() > kMaxSamples) {
        m_intervalsMs.remove(0, m_intervalsMs.size() - kMaxSamples);
    }

    // Emit at most kEmitIntervalMs apart without needing an extra timer.
    static QElapsedTimer emitClock;
    if (!emitClock.isValid()) {
        emitClock.start();
    }
    if (emitClock.elapsed() < kEmitIntervalMs && m_intervalsMs.size() < kMaxSamples) {
        return;
    }
    emitClock.restart();

    double sum = 0.0;
    double worst = 0.0;
    for (const double value : std::as_const(m_intervalsMs)) {
        sum += value;
        worst = std::max(worst, value);
    }
    const double average = m_intervalsMs.isEmpty() ? 0.0 : sum / m_intervalsMs.size();

    m_frameMs = average;
    m_worstFrameMs = worst;
    m_fps = average > 0.0 ? 1000.0 / average : 0.0;

    m_snapshot.fps = m_fps;
    m_snapshot.frameMs = m_frameMs;
    m_snapshot.worstFrameMs = m_worstFrameMs;
    m_snapshot.frames = m_frames;
    m_snapshot.droppedFrames = m_droppedFrames;
    m_snapshot.targetFps = m_targetFps;

    Q_EMIT statsChanged();
}

} // namespace kg
