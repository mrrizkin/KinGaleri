import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

// Video playback surface backed by Qt Multimedia, which is the PRD 10.1 default.
//
// This is the counterpart of MpvVideoSurface.qml and must expose the same
// interface, because ViewerOverlay picks between the two by `app.videoBackend`
// and drives both through the same property/function names. Keep the two files in
// step: if you add a function here, add it there.
//
// It lives in its own file for the same reason as the mpv one: QML imports are
// resolved statically, so importing QtMultimedia inside ViewerOverlay.qml would
// make the whole viewer fail to load in a build without the Multimedia module.
// The file is only added to the QML module when KG_ENABLE_MULTIMEDIA is on.
//
// No Shortcuts are registered here on purpose - ViewerOverlay owns Left/Right/
// Space/F/M and dispatches its keys into the functions below.
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
    /// 0..100, matching MpvVideoSurface and AppSettings. QMediaPlayer itself
    /// wants 0..1, so the two conversions live in one place below.
    property real volume: 100

    readonly property alias hasVideo: player.hasVideo
    readonly property alias ended: player.ended
    readonly property alias paused: player.paused
    readonly property alias position: player.position
    readonly property alias duration: player.duration
    /// "Enough of the file has been read to know what it is", which is what the
    /// mpv surface reports as `loaded` and what the BusyIndicator keys off.
    readonly property alias loaded: player.loaded

    function togglePause() {
        player.togglePause()
    }
    function replay() {
        player.setPosition(0)
        player.play()
    }
    function seekTo(ms) {
        player.setPosition(Math.max(0, ms))
    }
    function seekRelative(ms) {
        player.setPosition(Math.max(0, Math.min(player.duration, player.position + ms)))
    }
    function stop() {
        player.stop()
    }
    function adjustVolume(delta) {
        surface.volume = Math.max(0, Math.min(100, surface.volume + delta))
    }
    function toggleMute() {
        surface.muted = !surface.muted
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

    // The QML type wraps QMediaPlayer and adds the few derived properties the
    // transport bar needs, so the surface itself stays a thin composition.
    // Qt Multimedia routes muting and gain through a separate AudioOutput object;
    // QMediaPlayer itself has no `muted`/`volume` (unlike the mpv item, whose
    // MpvItem does). Both are live-bound so a change in the settings page applies
    // to the clip playing right now.
    AudioOutput {
        id: audio
        muted: surface.muted
        volume: surface.volume / 100
    }

    MediaPlayer {
        id: player
        source: surface.filePath.length > 0 ? "file://" + surface.filePath : ""
        audioOutput: audio
        videoOutput: output

        // Autoplay once the file is loaded, matching every other gallery viewer.
        // With autoplay off the clip sits on its first frame until Space/play.
        autoPlay: surface.autoplay
        // Infinite is -1; anything else means "play once and stop".
        loops: surface.loop ? MediaPlayer.Infinite : 1

        /// Read-only conveniences over QMediaPlayer, kept in the C++-free part
        /// of the API surface so the loader can call them by the same names as
        /// the mpv item.
        readonly property bool paused: playbackState !== MediaPlayer.PlayingState
        // LoadedMedia is reached for both video and audio-only files. A file that
        // failed to load stays at NoMedia/Loading and never claims to be loaded,
        // which is what stops the spinner after an error. EndOfMedia counts as
        // loaded too, mirroring the mpv surface, which keeps `loaded` true at EOF.
        readonly property bool loaded: mediaStatus === MediaPlayer.LoadedMedia
                                       || mediaStatus === MediaPlayer.BufferingMedia
                                       || mediaStatus === MediaPlayer.BufferedMedia
                                       || mediaStatus === MediaPlayer.EndOfMedia
        // EndOfMedia keeps the last frame on screen and is the state the replay
        // button and the "do not spin" rule key off.
        readonly property bool ended: mediaStatus === MediaPlayer.EndOfMedia

        function togglePause() {
            if (playbackState === MediaPlayer.PlayingState)
                pause()
            else
                play()
        }

        onErrorOccurred: function (error, errorString) {
            if (error !== MediaPlayer.NoError)
                console.warn("kin-galeri: Qt Multimedia error:", errorString)
        }
    }

    // The connection is MediaPlayer.videoOutput -> VideoOutput. In Qt 6 the old
    // Qt 5 direction (VideoOutput.source = player) no longer exists.
    VideoOutput {
        id: output
        anchors.fill: parent
        // Hidden while there is no decoded video track so the poster (or the
        // "audio only" notice) is what the user sees rather than a black frame.
        visible: player.hasVideo
        fillMode: VideoOutput.PreserveAspectFit
    }

    // Poster shown until the decoder produces a frame. Reusing the already-cached
    // thumbnail means there is no visible gap while the file is demuxed.
    Image {
        anchors.fill: parent
        // `ended` keeps the last frame up; guard anyway so the stale poster can
        // never reappear over a finished clip.
        visible: !player.hasVideo && !player.ended
        source: surface.posterUrl
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
    }

    BusyIndicator {
        anchors.centerIn: parent
        // Never spin for a finished clip: `loaded` stays true at EOF, but this
        // makes the intent explicit and covers backends that unload on EOF.
        running: !player.loaded && !player.ended && surface.filePath.length > 0
        visible: running
    }

    // Replay affordance. At EOF the last frame is still on screen, so the user
    // needs an obvious way back to the start. Clicking it (or Space) replays.
    ToolButton {
        anchors.centerIn: parent
        visible: player.ended
        enabled: player.ended
        text: "\u21BB"
        font.pixelSize: 40
        onClicked: surface.replay()
        ToolTip.text: qsTr("Replay (Space)")
        ToolTip.visible: hovered
    }

    Label {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 52
        visible: player.loaded && !player.hasVideo && player.hasAudio
        text: qsTr("Audio only — this file has no video track")
        color: Theme.textMuted
        font.pixelSize: 12
    }

    Label {
        anchors.centerIn: parent
        visible: player.errorString.length > 0
        text: player.errorString
        color: Theme.danger
        font.pixelSize: 12
    }

    // ---- transport controls -------------------------------------------------
    Rectangle {
        id: controls
        z: 1
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
                text: player.paused ? "▶" : "❚❚"
                font.pixelSize: 16
                onClicked: player.togglePause()
                ToolTip.text: qsTr("Play / pause (Space)")
                ToolTip.visible: hovered
            }

            Label {
                text: surface.formatTime(player.position)
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignRight
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, player.duration)

                // Binding rather than a plain `value:` assignment: a direct
                // assignment would fight `onMoved` and produce a binding loop.
                Binding on value {
                    when: !seekSlider.pressed
                    value: player.position
                }
                onMoved: player.setPosition(value)
            }

            Label {
                text: surface.formatTime(player.duration)
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.preferredWidth: 48
            }

            ToolButton {
                text: surface.muted || surface.volume === 0 ? "🔇" : "🔊"
                onClicked: surface.toggleMute()
                ToolTip.text: qsTr("Mute (M)")
                ToolTip.visible: hovered
            }

            Slider {
                Layout.preferredWidth: 90
                from: 0
                to: 100
                value: surface.volume
                onMoved: surface.volume = value
            }

            ToolButton {
                text: player.playbackRate.toFixed(2).replace(/\.?0+$/, "") + "×"
                onClicked: {
                    const steps = [0.5, 1, 1.5, 2]
                    const i = steps.indexOf(player.playbackRate)
                    player.playbackRate = steps[(i + 1) % steps.length]
                }
                ToolTip.text: qsTr("Playback speed")
                ToolTip.visible: hovered
            }
        }
    }

}
