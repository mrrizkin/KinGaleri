import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KinGaleri

// Full-screen media viewer with zoom, pan and keyboard navigation.
//
// Loads through the `image://full` provider rather than `file://` so that EXIF
// orientation is applied by QImageReader (identical to the grid thumbnails) and
// the decode is bounded to a sensible edge length instead of a full 50 MP frame.
Popup {
    id: viewer

    required property var app

    // Payload produced by MediaCell::buildPayload().
    property var payload: null

    // Navigation is delegated to the grid model so the viewer always walks the
    // same order the grid shows (PRD F-3).
    readonly property var model: viewer.app.model
    readonly property int itemIndex: viewer.payload
                                      ? viewer.model.indexOfId(viewer.payload.mediaId)
                                      : -1

    parent: Overlay.overlay
    // Popup is not an Item, so it has no `anchors`; geometry is set explicitly to
    // cover the whole window, which is the point of a full-screen viewer.
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    x: 0
    y: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0

    background: Rectangle { color: "#0c0e12" }

    // Zoom state. 1 = fit to window.
    property real zoom: 1
    readonly property real minZoom: 1
    readonly property real maxZoom: 8

    readonly property bool isVideo: viewer.payload && viewer.payload.kind === "video"
    readonly property bool isSynthetic: viewer.payload && viewer.payload.isSynthetic

    readonly property string sourceUrl: {
        if (!viewer.payload)
            return ""
        // Synthetic benchmark rows have no file behind them, so the thumbnail is
        // the only thing that can be shown.
        if (viewer.payload.isSynthetic)
            return viewer.payload.thumbUrl
        // Video stills are not decoded yet (frame extraction lands with playback
        // in M2), so asking the full-image provider for an .mp4 would just log a
        // provider failure. Show the thumbnail and say why.
        if (viewer.payload.kind === "video")
            return viewer.payload.thumbUrl
        return viewer.app.fullImageUrl(viewer.payload.path, viewer.zoom > 2 ? 4096 : 2048)
    }

    function resetView() {
        viewer.zoom = 1
        flick.contentX = 0
        flick.contentY = 0
    }

    function step(delta) {
        const next = viewer.itemIndex + delta
        if (next < 0 || next >= viewer.model.itemCount)
            return
        const map = viewer.model.itemAt(next)
        if (map && map.mediaId !== undefined) {
            viewer.payload = map
            viewer.resetView()
        }
    }

    onOpened: viewer.resetView()
    onPayloadChanged: viewer.resetView()

    // ---- top bar ------------------------------------------------------------
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: "#14161aee"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: viewer.payload ? viewer.payload.fileName : ""
                color: Theme.text
                elide: Text.ElideMiddle
            }

            Label {
                text: viewer.payload ? (viewer.payload.takenAtText || viewer.payload.takenAtText || "") : ""
                color: Theme.textMuted
                font.pixelSize: 12
            }

            Label {
                visible: viewer.payload && viewer.payload.orientation > 1
                text: qsTr("rotated")
                color: Theme.textFaint
                font.pixelSize: 11
            }

            ToolButton {
                text: viewer.payload && viewer.payload.isFavorite ? "★" : "☆"
                font.pixelSize: 17
                onClicked: {
                    if (viewer.payload)
                        viewer.model.toggleFavoriteById(viewer.payload.mediaId)
                }
                ToolTip.text: qsTr("Toggle favorite")
                ToolTip.visible: hovered
            }

            ToolButton {
                text: qsTr("Details")
                onClicked: viewer.detailsRequested(viewer.payload)
            }

            ToolButton {
                text: "✕"
                onClicked: viewer.close()
                ToolTip.text: qsTr("Close (Esc)")
                ToolTip.visible: hovered
            }
        }
    }

    signal detailsRequested(var payload)

    // ---- image surface ------------------------------------------------------
    // A Flickable gives panning and kinetic scrolling for free once zoomed in;
    // at zoom 1 it is not interactive so the image stays centred.
    Flickable {
        id: flick
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        contentWidth: Math.max(width, image.width)
        contentHeight: Math.max(height, image.height)
        boundsBehavior: Flickable.StopAtBounds
        interactive: viewer.zoom > 1.001
        clip: true

        Image {
            id: image

            // Size is derived from the *decoded* image rather than from the payload's
            // pixel width/height: the provider applies EXIF orientation, so a photo
            // rotated 90° has swapped dimensions relative to what the database says.
            // implicitWidth/Height are read-only and reflect what actually loaded.
            readonly property real naturalW: implicitWidth > 0 ? implicitWidth : 1024
            readonly property real naturalH: implicitHeight > 0 ? implicitHeight : 768
            readonly property real fit: Math.min(flick.width / naturalW, flick.height / naturalH)

            // Centre the (possibly zoomed) image inside the viewport.
            x: Math.max(0, (flick.width - width) / 2)
            y: Math.max(0, (flick.height - height) / 2)
            width: Math.max(1, naturalW * fit * viewer.zoom)
            height: Math.max(1, naturalH * fit * viewer.zoom)

            source: viewer.sourceUrl
            asynchronous: true
            cache: false
            // The provider has already bounded the decode, so no sourceSize here:
            // setting one would fight the requested edge length.
            fillMode: Image.PreserveAspectFit
            smooth: true
            // The provider already applied EXIF orientation, so no QML rotation.
        }

        // Click zones for prev/next, mirroring the arrow keys.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                if (viewer.zoom > 1.001)
                    return // while zoomed, clicks should not navigate
                const p = point.position
                if (p.x < flick.width * 0.22)
                    viewer.step(-1)
                else if (p.x > flick.width * 0.78)
                    viewer.step(1)
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function (event) {
                const factor = event.angleDelta.y > 0 ? 1.18 : 1 / 1.18
                viewer.zoom = Math.max(viewer.minZoom, Math.min(viewer.maxZoom, viewer.zoom * factor))
            }
        }

        PinchHandler {
            onActiveChanged: {
                if (!active)
                    return
                viewer.zoom = Math.max(viewer.minZoom, Math.min(viewer.maxZoom, viewer.zoom * scale))
                scale = 1
            }
        }
    }

    // ---- video playback -----------------------------------------------------
    // Only loaded for the mpv backend. QML imports are static, so the mpv item
    // lives in its own component: importing KinGaleri.Mpv here would break this
    // whole file in a build without mpvqt.
    Loader {
        id: videoLoader
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        active: viewer.opened && viewer.isVideo && !viewer.isSynthetic && viewer.app.videoBackend === "mpv"
        source: "MpvVideoSurface.qml"
        // Bound rather than assigned once, so navigating to the next item with
        // the arrow keys swaps the playing file.
        onLoaded: {
            item.filePath = Qt.binding(() => viewer.payload ? viewer.payload.path : "")
            item.posterUrl = Qt.binding(() => viewer.payload ? viewer.payload.thumbUrl : "")
            item.autoplay = Qt.binding(() => viewer.app.settings.videoAutoplay)
            item.loop = Qt.binding(() => viewer.app.settings.videoLoop)
            item.muted = Qt.binding(() => viewer.app.settings.videoMute)
            item.volume = Qt.binding(() => viewer.app.settings.videoVolume)
        }
    }

    Rectangle {
        id: bottomBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: "#14161aee"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            ToolButton {
                text: "‹"
                font.pixelSize: 19
                enabled: viewer.itemIndex > 0
                onClicked: viewer.step(-1)
                ToolTip.text: qsTr("Previous (←)")
                ToolTip.visible: hovered
            }
            ToolButton {
                text: "›"
                font.pixelSize: 19
                enabled: viewer.itemIndex >= 0 && viewer.itemIndex < viewer.model.itemCount - 1
                onClicked: viewer.step(1)
                ToolTip.text: qsTr("Next (→)")
                ToolTip.visible: hovered
            }

            ToolSeparator {}

            ToolButton {
                text: "−"
                onClicked: viewer.zoom = Math.max(viewer.minZoom, viewer.zoom / 1.25)
            }
            Label {
                text: Math.round(viewer.zoom * 100) + "%"
                color: Theme.textMuted
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 52
            }
            ToolButton {
                text: "+"
                onClicked: viewer.zoom = Math.min(viewer.maxZoom, viewer.zoom * 1.25)
            }
            ToolButton {
                text: qsTr("Fit")
                onClicked: viewer.resetView()
            }

            Item { Layout.fillWidth: true }

            Label {
                text: viewer.itemIndex >= 0
                      ? qsTr("%1 of %2").arg(viewer.itemIndex + 1).arg(viewer.model.itemCount)
                      : ""
                color: Theme.textFaint
                font.pixelSize: 12
            }
        }
    }

    // ---- keyboard -----------------------------------------------------------
    Shortcut {
        sequence: viewer.app.shortcuts.sequences["viewer_previous"]
        enabled: viewer.opened
        onActivated: {
            // For a video, Left seeks; for an image it steps to the previous item.
            // Same key, context-dependent meaning - which is why this is
            // dispatched here instead of registered inside MpvVideoSurface
            // (two Shortcuts on one sequence would become ambiguous and fire neither).
            if (videoLoader.active && videoLoader.item)
                videoLoader.item.seekRelative(-5000)
            else
                viewer.step(-1)
        }
    }
    Shortcut {
        sequence: viewer.app.shortcuts.sequences["viewer_next"]
        enabled: viewer.opened
        onActivated: {
            if (videoLoader.active && videoLoader.item)
                videoLoader.item.seekRelative(5000)
            else
                viewer.step(1)
        }
    }
    // Escape is deliberately NOT registered here: Main.qml owns a single
    // always-enabled Escape shortcut that closes the topmost layer. Two enabled
    // Shortcuts sharing one sequence are treated as an *ambiguous* activation by
    // Qt and neither fires. Escape also still works via closePolicy below.
    Shortcut {
        sequence: viewer.app.shortcuts.sequences["viewer_play_pause"]
        enabled: viewer.opened
        onActivated: {
            if (videoLoader.active && videoLoader.item)
                videoLoader.item.togglePause()
            else
                viewer.resetView()
        }
    }
    Shortcut {
        sequence: viewer.app.shortcuts.sequences["viewer_mute"]
        enabled: viewer.opened && videoLoader.active
        onActivated: if (videoLoader.item)
            videoLoader.item.toggleMute()
    }
    Shortcut {
        sequence: viewer.app.shortcuts.sequences["viewer_favorite"]
        enabled: viewer.opened
        onActivated: {
            if (viewer.payload)
                viewer.model.toggleFavoriteById(viewer.payload.mediaId)
        }
    }
    Shortcut {
        sequence: viewer.app.shortcuts.sequences["viewer_zoom_in"]
        enabled: viewer.opened
        onActivated: viewer.zoom = Math.min(viewer.maxZoom, viewer.zoom * 1.25)
    }
    Shortcut {
        sequence: viewer.app.shortcuts.sequences["viewer_zoom_out"]
        enabled: viewer.opened
        onActivated: viewer.zoom = Math.max(viewer.minZoom, viewer.zoom / 1.25)
    }
}
