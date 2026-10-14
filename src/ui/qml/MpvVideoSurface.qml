import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KinGaleri
import KinGaleri.Mpv

// Video playback surface, backed by libmpv through mpvqt.
//
// This file MUST stay in its own component: QML imports are resolved statically,
// so `import KinGaleri.Mpv` inside ViewerOverlay.qml would make the whole viewer
// fail to load in a build without mpvqt. ViewerOverlay loads this file through a
// Loader that is only active when app.videoBackend === "mpv".
//
// No Shortcuts are registered here on purpose. ViewerOverlay already owns
// Left/Right/Space/F, and two enabled Shortcuts sharing one sequence make Qt
// report an *ambiguous* activation and fire neither. The viewer dispatches its
// keys into the functions below instead.
Item {
    id: surface

    /// Absolute path of the video to play.
    property string filePath: ""
    /// Thumbnail to show until the first video frame arrives.
    property string posterUrl: ""

    /// Playback preferences, seeded by ViewerOverlay from AppSettings.
    property bool autoplay: true
    property bool loop: false
    property bool muted: false
    property real volume: 100

    readonly property alias hasVideo: mpv.hasVideo
    readonly property alias loaded: mpv.loaded
    readonly property alias ended: mpv.ended
    readonly property alias paused: mpv.paused
    readonly property alias position: mpv.position
    readonly property alias duration: mpv.duration

    function togglePause() {
        mpv.togglePause()
    }
    function replay() {
        mpv.play()
    }
    function seekTo(ms) {
        mpv.seek(ms)
    }
    function seekRelative(ms) {
        mpv.seekRelative(ms)
    }
    function stop() {
        mpv.stop()
    }
    function adjustVolume(delta) {
        mpv.volume = Math.max(0, Math.min(100, mpv.volume + delta))
    }
    function toggleMute() {
        mpv.muted = !mpv.muted
    }

    function formatTime(ms) {
        if (!ms || ms < 0)
            return "0:00"
        const total = Math.floor(ms / 1000)
        const s = total % 60
        const m = Math.floor(total / 60) % 60
        const h = Math.floor(total / 3600)
        const pad = function (n) {
            return n < 10 ? "0" + n : "" + n
        }
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    onFilePathChanged: {
        if (surface.filePath.length > 0)
            mpv.source = "file://" + surface.filePath
    }

    MpvItem {
        id: mpv
        anchors.fill: parent
        // Hidden while there is no decoded video track so the poster (or the
        // "audio only" notice) is what the user sees rather than a black FBO.
        visible: mpv.hasVideo

        // User preferences are live-bound so changes in the settings page apply
        // to the currently playing clip too.
        loop: surface.loop
        muted: surface.muted
        volume: surface.volume

        // Autoplay once the file is actually loaded, matching how every other
        // gallery viewer behaves when you click a video. When autoplay is off the
        // clip stays on its first frame until the user presses play.
        onLoadedChanged: {
            if (!loaded)
                return
            mpv.muted = surface.muted
            mpv.volume = surface.volume
            if (surface.autoplay)
                mpv.play()
        }
    }

    // Poster shown until libmpv produces a frame. Reusing the already-cached
    // thumbnail means there is no visible gap while the file is demuxed.
    Image {
        anchors.fill: parent
        // `ended` keeps hasVideo true (keep-open), but guard anyway so the stale
        // poster can never reappear over a finished clip.
        visible: !mpv.hasVideo && !mpv.ended
        source: surface.posterUrl
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
    }

    BusyIndicator {
        anchors.centerIn: parent
        // Never spin for a finished clip: `loaded` stays true at EOF, but this
        // makes the intent explicit and covers backends that unload on EOF.
        running: !mpv.loaded && !mpv.ended && surface.filePath.length > 0
        visible: running
    }

    // Replay affordance. At EOF the last frame is still on screen, so the user
    // needs an obvious way back to the start. Clicking it (or Space) replays.
    ToolButton {
        anchors.centerIn: parent
        visible: mpv.ended
        enabled: mpv.ended
        text: "\u21BB"
        font.pixelSize: 40
        onClicked: surface.replay()
        ToolTip.text: qsTr("Replay (Space)")
        ToolTip.visible: hovered
    }

    Label {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 52
        visible: mpv.loaded && !mpv.hasVideo
        text: qsTr("Audio only — this file has no video track")
        color: Theme.textMuted
        font.pixelSize: 12
    }

    Label {
        anchors.centerIn: parent
        visible: mpv.errorString.length > 0
        text: mpv.errorString
        color: Theme.danger
        font.pixelSize: 12
    }

    // ---- transport controls -------------------------------------------------
    Rectangle {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        height: 46
        radius: Theme.radius
        color: "#1b1e24ee"
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10

            ToolButton {
                text: mpv.paused ? "▶" : "❚❚"
                font.pixelSize: 16
                onClicked: mpv.togglePause()
                ToolTip.text: qsTr("Play / pause (Space)")
                ToolTip.visible: hovered
            }

            Label {
                text: surface.formatTime(mpv.position)
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignRight
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, mpv.duration)

                // Binding rather than a plain `value:` assignment: a direct
                // assignment would fight `onMoved` and produce a binding loop.
                Binding on value {
                    when: !seekSlider.pressed
                    value: mpv.position
                }
                onMoved: mpv.seek(value)
            }

            Label {
                text: surface.formatTime(mpv.duration)
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.preferredWidth: 48
            }

            ToolButton {
                text: mpv.muted || mpv.volume === 0 ? "🔇" : "🔊"
                onClicked: mpv.muted = !mpv.muted
                ToolTip.text: qsTr("Mute (M)")
                ToolTip.visible: hovered
            }

            Slider {
                Layout.preferredWidth: 90
                from: 0
                to: 100
                value: mpv.volume
                onMoved: mpv.volume = value
            }

            ToolButton {
                text: mpv.speed.toFixed(2).replace(/\.?0+$/, "") + "×"
                onClicked: {
                    const steps = [0.5, 1, 1.5, 2]
                    const i = steps.indexOf(mpv.speed)
                    mpv.speed = steps[(i + 1) % steps.length]
                }
                ToolTip.text: qsTr("Playback speed")
                ToolTip.visible: hovered
            }
        }
    }
}
