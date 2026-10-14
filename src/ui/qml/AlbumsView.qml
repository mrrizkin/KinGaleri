import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Album overview page: every manual album and every indexed folder/camera album
// as a grid of cover tiles, plus the album create/rename/delete actions. Tapping
// a tile asks the window to open that album in the photo grid.
Item {
    id: root

    required property var app

    signal albumActivated(string kind, var id, string name)

    readonly property real sideMargin: 16
    readonly property real gap: 14
    readonly property real minCell: 150

    // The grid is allowed to run `gap` into the right margin so the last column can
    // finish flush with the content edge. That spare slot is exactly where the
    // vertical scroll bar sits, so the trailing gap never reads as dead space.
    readonly property real gridWidth: Math.max(1, width - sideMargin * 2 + gap)
    readonly property int columns: Math.max(2, Math.min(8,
        Math.floor(gridWidth / (minCell + gap))))
    // Truncated (not rounded) so `GridView`'s own floor(width / cellWidth) cannot drop
    // a column to a 6.999… floating-point result.
    readonly property real cellPitch: Math.floor(gridWidth / columns * 1000) / 1000
    readonly property real cardWidth: Math.max(80, cellPitch - gap)
    readonly property real cardHeight: cardWidth + 44

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.sideMargin
        anchors.rightMargin: Math.max(0, root.sideMargin - root.gap)
        anchors.topMargin: 10
        anchors.bottomMargin: root.sideMargin
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: root.gap

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("+ New album…")
                onClicked: {
                    newAlbumField.text = ""
                    newAlbumDialog.open()
                }
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: root.cellPitch
            cellHeight: root.cardHeight + root.gap
            clip: true
            model: root.app.model.albumCards
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                minimumSize: Math.min(0.5, 48 / Math.max(1, grid.height))
            }

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight

                AlbumCard {
                    width: root.cardWidth
                    height: root.cardHeight
                    album: modelData
                    app: root.app
                    onActivated: root.albumActivated(modelData.kind, modelData.id, modelData.name)
                    onContextRequested: function (x, y) {
                        if (!modelData || modelData.kind !== "manual")
                            return
                        albumMenu.album = modelData
                        albumMenu.popup()
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: grid.count === 0
        width: Math.min(parent.width - 80, 360)
        text: qsTr("No albums yet — index a folder or create an album to populate this page")
        color: Theme.textFaint
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    // ---- manual album management (PRD F-2.8) ------------------------------
    Menu {
        id: albumMenu
        property var album: null

        MenuItem {
            text: qsTr("Rename…")
            enabled: albumMenu.album !== null
            onTriggered: {
                renameAlbumField.text = albumMenu.album ? albumMenu.album.name : ""
                renameAlbumDialog.open()
            }
        }
        MenuItem {
            text: qsTr("Delete album")
            enabled: albumMenu.album !== null
            onTriggered: {
                if (albumMenu.album)
                    root.app.model.deleteAlbum(albumMenu.album.id)
            }
        }
    }

    Dialog {
        id: newAlbumDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("New album")
        width: 400
        padding: 18
        topPadding: 48
        bottomPadding: 18
        leftPadding: 18
        rightPadding: 18
        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.color: Theme.border
        }
        header: Label {
            text: newAlbumDialog.title
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
            leftPadding: 18
            rightPadding: 18
            topPadding: 14
            bottomPadding: 8
        }
        footer: DialogButtonBox {
            standardButtons: Dialog.Ok | Dialog.Cancel
            alignment: Qt.AlignRight
            spacing: 8
            padding: 14
            palette.button: Theme.surfaceAlt
            palette.buttonText: Theme.text
        }

        TextField {
            id: newAlbumField
            width: parent.width
            placeholderText: qsTr("Album name")
            palette.text: Theme.text
            background: Rectangle {
                color: Theme.background
                radius: 6
                border.color: newAlbumField.activeFocus ? Theme.accent : Theme.border
            }
            onAccepted: newAlbumDialog.accept()
        }

        onOpened: newAlbumField.forceActiveFocus()
        onAccepted: {
            if (newAlbumField.text.trim() !== "")
                root.app.model.createAlbum(newAlbumField.text)
        }
    }

    Dialog {
        id: renameAlbumDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Rename album")
        width: 400
        padding: 18
        topPadding: 48
        bottomPadding: 18
        leftPadding: 18
        rightPadding: 18
        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.color: Theme.border
        }
        header: Label {
            text: renameAlbumDialog.title
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
            leftPadding: 18
            rightPadding: 18
            topPadding: 14
            bottomPadding: 8
        }
        footer: DialogButtonBox {
            standardButtons: Dialog.Ok | Dialog.Cancel
            alignment: Qt.AlignRight
            spacing: 8
            padding: 14
            palette.button: Theme.surfaceAlt
            palette.buttonText: Theme.text
        }

        TextField {
            id: renameAlbumField
            width: parent.width
            palette.text: Theme.text
            background: Rectangle {
                color: Theme.background
                radius: 6
                border.color: renameAlbumField.activeFocus ? Theme.accent : Theme.border
            }
            onAccepted: renameAlbumDialog.accept()
        }

        onOpened: {
            renameAlbumField.text = albumMenu.album ? albumMenu.album.name : ""
            renameAlbumField.forceActiveFocus()
            renameAlbumField.selectAll()
        }
        onAccepted: {
            if (albumMenu.album && renameAlbumField.text.trim() !== "")
                root.app.model.renameAlbum(albumMenu.album.id, renameAlbumField.text)
        }
    }
}
