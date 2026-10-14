import QtQuick
import QtQuick.Controls
import KinGaleri

// A single thumbnail cell.
//
// Performance contract (PRD 5.6):
//   * no animations, effects, or shaders
//   * one asynchronous Image per cell, with a bounded sourceSize
//   * extra chrome (badges) is only instantiated when actually needed
Rectangle {
    id: cell

    // Populated by Qt Quick from the MediaRowModel roles.
    required property string thumbUrl
    required property string fileName
    required property string filePath
    required property string kind
    required property string sourceAlbum
    required property string sizeText
    required property string takenAtText
    required property string dimensions
    required property bool isFavorite
    required property bool isSynthetic
    required property bool selected
    required property int mediaId
    required property int orientation
    required property int pixelWidth
    required property int pixelHeight

    signal openRequested(var payload)
    signal favoriteRequested(var payload)
    signal contextRequested(var payload, var position)
    /// Emitted in selection mode; `shift` extends the range from the last click.
    signal selectionToggled(var payload, bool shift)

    /// Set by MediaRow from the model's selection mode.
    property bool selectionMode: false
    /// Draw a subtle border while the pointer is over the cell.
    property bool hoverEffects: true

    readonly property bool isVideo: cell.kind === "video"

    radius: 4
    color: Theme.surfaceAlt
    clip: true

    // Screen readers get the file name and medium; the visual chrome is decorative.
    Accessible.role: Accessible.Button
    Accessible.name: cell.fileName
    Accessible.description: cell.isVideo ? qsTr("Video") : qsTr("Image")
    Accessible.selected: cell.selected
    Accessible.focusable: true

    // Keys deliberately match MediaGridModel::itemAt() so the viewer can swap
    // between a grid payload and a model lookup without any key translation.
    function buildPayload() {
        return {
            "mediaId": cell.mediaId,
            "fileName": cell.fileName,
            "path": cell.filePath,
            "kind": cell.kind,
            "sourceAlbum": cell.sourceAlbum,
            "sizeText": cell.sizeText,
            "takenAtText": cell.takenAtText,
            "dimensions": cell.dimensions,
            "width": cell.pixelWidth,
            "height": cell.pixelHeight,
            "isFavorite": cell.isFavorite,
            "isSynthetic": cell.isSynthetic,
            "orientation": cell.orientation,
            "thumbUrl": cell.thumbUrl
        }
    }

    Image {
        id: preview
        anchors.fill: parent
        // `sourceSize` caps the decode size so a 50 MP source never lands at full
        // resolution in the scene graph.
        sourceSize.width: Math.min(1024, Math.max(1, Math.ceil(cell.width)))
        sourceSize.height: Math.min(1024, Math.max(1, Math.ceil(cell.height)))
        source: cell.thumbUrl
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectCrop
        smooth: true
    }

    // Video marker.
    Rectangle {
        visible: cell.isVideo
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 4
        width: 18
        height: 16
        radius: 3
        color: Theme.videoBadge
        opacity: 0.82

        Label {
            anchors.centerIn: parent
            text: "▶"
            font.pixelSize: 9
            color: Theme.text
        }
    }

    // Favorite marker.
    Label {
        visible: cell.isFavorite
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 5
        text: "★"
        font.pixelSize: 13
        color: "#ffd166"
        style: Text.Outline
        styleColor: "#00000088"
    }

    // Selection mode: a tick overlay plus a tinted frame, so it is obvious why a
    // click no longer opens the item.
    Rectangle {
        anchors.fill: parent
        visible: cell.selected
        color: Theme.accent
        opacity: 0.22
    }
    Rectangle {
        anchors.fill: parent
        visible: cell.selected
        color: "transparent"
        border.color: Theme.accent
        border.width: 3
    }
    Rectangle {
        visible: cell.selectionMode
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 5
        width: 20
        height: 20
        radius: 4
        color: cell.selected ? Theme.accent : "#00000066"
        border.color: "#ffffffcc"
        border.width: 1

        Label {
            anchors.centerIn: parent
            text: cell.selected ? "\u2713" : ""
            color: "#ffffff"
            font.pixelSize: 12
            font.bold: true
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                cell.contextRequested(cell.buildPayload(), Qt.point(mouse.x, mouse.y))
                return
            }
            if (cell.selectionMode) {
                cell.selectionToggled(cell.buildPayload(),
                                      (mouse.modifiers & Qt.ShiftModifier) !== 0)
            } else {
                cell.openRequested(cell.buildPayload())
            }
        }
        onDoubleClicked: function (mouse) {
            if (!cell.selectionMode && mouse.button === Qt.LeftButton)
                cell.favoriteRequested(cell.buildPayload())
        }
    }

    // Hover highlight (optional): a thin accent frame so the pointer target is
    // obvious without altering the image itself.
    Rectangle {
        anchors.fill: parent
        z: 5
        visible: cell.hoverEffects && mouseArea.containsMouse && !cell.selectionMode
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
    }
}
