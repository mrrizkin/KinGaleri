import QtQuick
import QtQuick.Controls

// One tile in the album overview grid: a square cover with the album name and
// item count underneath. Clicking it opens the album in the photo grid.
Item {
    id: root

    required property var album
    required property var app

    signal activated()
    signal contextRequested(real x, real y)

    readonly property string initialCoverUrl: album && album.coverUrl ? album.coverUrl : ""

    // Bumped when the model reports that this cover finished generating, so the
    // URL changes and QML fetches the real thumbnail (the pixmap cache is keyed
    // by URL and would otherwise keep the placeholder).
    property int coverRevision: 0

    onAlbumChanged: coverRevision = 0

    Connections {
        target: root.app ? root.app.model : null

        function onAlbumCoverReady(mediaId) {
            if (root.album && mediaId === root.album.coverId) {
                root.coverRevision++
            }
        }
    }

    Rectangle {
        id: cover
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: width
        radius: Theme.radius
        color: Theme.surfaceAlt
        clip: true
        border.width: 1
        border.color: mouse.containsMouse ? Theme.accent : Theme.border

        Image {
            id: coverImage
            anchors.fill: parent
            source: {
                // The `void` read makes the binding depend on coverRevision.
                void root.coverRevision
                if (root.coverRevision > 0 && root.album && root.album.coverId > 0) {
                    return root.app.model.albumCoverUrl(root.album.coverId)
                }
                return root.initialCoverUrl
            }
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize.width: Math.max(1, Math.round(cover.width))
            sourceSize.height: Math.max(1, Math.round(cover.width))
        }

        // Placeholder glyph shown until the cover lands, and for coverless albums.
        Label {
            anchors.centerIn: parent
            visible: coverImage.status !== Image.Ready
            text: root.album && root.album.kind === "manual" ? "\u25C6" : "\u25A6"
            color: Theme.textFaint
            font.pixelSize: 30
        }
    }

    Label {
        id: nameLabel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: cover.bottom
        anchors.topMargin: 6
        text: root.album ? root.album.name : ""
        color: Theme.text
        elide: Text.ElideRight
        font.pixelSize: 13
    }

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: nameLabel.bottom
        anchors.topMargin: 1
        text: root.album ? qsTr("%1 item(s)").arg(root.album.count) : ""
        color: Theme.textFaint
        elide: Text.ElideRight
        font.pixelSize: 11
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function (event) {
            if (event.button === Qt.RightButton)
                root.contextRequested(event.x, event.y)
            else
                root.activated()
        }
    }
}
