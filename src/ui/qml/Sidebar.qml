import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var app

    signal addFolderRequested()
    signal clearThumbnailCacheRequested()
    signal albumsRequested()
    signal photosRequested()
    signal settingsRequested()
    signal collectionsRequested(string kind)

    /// "photos", "albums" or "settings"; drives the selected state of the entries.
    property string currentPage: "photos"
    /// When off, hides placeholders and storage internals meant for development.
    property bool developerMode: false

    color: Theme.surface
    implicitWidth: 212

    function applyFilter(kind, favoritesOnly, album) {
        root.photosRequested()
        root.app.model.trashOnly = false
        root.app.model.albumId = 0
        root.app.model.placeId = 0
        root.app.model.personId = 0
        root.app.model.eventId = 0
        root.app.model.smartCategory = ""
        root.app.model.favoritesOnly = favoritesOnly
        root.app.model.filterKind = kind
        root.app.model.sourceAlbum = album
    }

    /// The recycle bin is the same table read on the other side of `deleted_at`,
    /// so it replaces every other filter instead of stacking with them.
    function showTrash() {
        root.photosRequested()
        root.app.model.favoritesOnly = false
        root.app.model.filterKind = ""
        root.app.model.sourceAlbum = ""
        root.app.model.albumId = 0
        root.app.model.placeId = 0
        root.app.model.personId = 0
        root.app.model.eventId = 0
        root.app.model.smartCategory = ""
        root.app.model.trashOnly = true
    }

    // Albums can easily outgrow the window height, so the whole sidebar scrolls.
    // The action buttons live at the end of the content instead of being pinned,
    // so they can never be hidden behind a long album list.
    Flickable {
        id: sidebarScroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: sidebarColumn.implicitHeight + 20
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: ScrollBar {
            id: sidebarBar
            policy: ScrollBar.AsNeeded
        }

    ColumnLayout {
        id: sidebarColumn
        x: 10
        y: 10
        width: sidebarScroll.width - 20
        spacing: 4

        Label {
            Layout.fillWidth: true
            text: qsTr("LIBRARY")
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 1.2
            color: Theme.textFaint
            topPadding: 4
            bottomPadding: 4
        }

        NavEntry {
            text: qsTr("All photos")
            glyph: "▦"
            iconNames: ["view-grid-symbolic", "view-grid", "view-list-icons", "applications-graphics-symbolic", "image-x-generic-symbolic"]
            selected: root.currentPage === "photos"
                        && root.app.model.filterKind === ""
                        && !root.app.model.favoritesOnly
                        && !root.app.model.trashOnly
                        && root.app.model.sourceAlbum === ""
                        && root.app.model.albumId === 0
                        && root.app.model.placeId === 0
                        && root.app.model.personId === 0
                        && root.app.model.eventId === 0
                        && root.app.model.smartCategory === ""
            onTapped: root.applyFilter("", false, "")
        }
        NavEntry {
            text: qsTr("Albums")
            glyph: "▤"
            iconNames: ["folder-images-symbolic", "folder-images", "folder-symbolic", "folder"]
            selected: root.currentPage === "albums"
            onTapped: root.albumsRequested()
        }
        NavEntry {
            text: qsTr("Images")
            glyph: "▣"
            iconNames: ["image-x-generic-symbolic", "image-x-generic"]
            selected: root.currentPage === "photos" && root.app.model.filterKind === "image"
            onTapped: root.applyFilter("image", false, "")
        }
        NavEntry {
            text: qsTr("Videos")
            glyph: "▶"
            iconNames: ["video-x-generic-symbolic", "video-x-generic"]
            selected: root.currentPage === "photos" && root.app.model.filterKind === "video"
            onTapped: root.applyFilter("video", false, "")
        }
        NavEntry {
            text: qsTr("Favorites")
            glyph: "★"
            iconNames: ["starred-symbolic", "starred", "emblem-favorite-symbolic", "emblem-favorite"]
            selected: root.currentPage === "photos" && root.app.model.favoritesOnly
            onTapped: root.applyFilter("", true, "")
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("COLLECTIONS")
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 1.2
            color: Theme.textFaint
            topPadding: 14
            bottomPadding: 4
        }

        NavEntry {
            Layout.fillWidth: true
            text: qsTr("People")
            glyph: "☺"
            iconNames: ["system-users-symbolic", "system-users", "avatar-default-symbolic", "avatar-default"]
            selected: root.currentPage === "people"
            onTapped: root.collectionsRequested("people")
        }
        NavEntry {
            Layout.fillWidth: true
            text: qsTr("Places")
            glyph: "◉"
            iconNames: ["mark-location-symbolic", "mark-location", "globe"]
            selected: root.currentPage === "places"
            onTapped: root.collectionsRequested("places")
        }
        NavEntry {
            Layout.fillWidth: true
            text: qsTr("Events")
            glyph: "▤"
            iconNames: ["view-calendar-symbolic", "view-calendar", "x-office-calendar-symbolic"]
            selected: root.currentPage === "events"
            onTapped: root.collectionsRequested("events")
        }
        NavEntry {
            Layout.fillWidth: true
            text: qsTr("Smart")
            glyph: "✦"
            iconNames: ["system-run-symbolic", "system-run", "tools-wizard"]
            selected: root.currentPage === "smart"
            onTapped: root.collectionsRequested("smart")
        }

        NavEntry {
            id: trashEntry
            Layout.fillWidth: true
            text: qsTr("Recycle bin")
            glyph: "⌫"
            iconNames: ["user-trash-symbolic", "user-trash"]
            badge: root.app.model.trashCount > 0 ? String(root.app.model.trashCount) : ""
            selected: root.currentPage === "photos" && root.app.model.trashOnly
            onTapped: root.showTrash()
        }

        NavEntry {
            Layout.fillWidth: true
            text: qsTr("Settings")
            glyph: "⚙"
            iconNames: ["settings-configure", "preferences-system-symbolic", "preferences-system", "systemsettings"]
            selected: root.currentPage === "settings"
            onTapped: root.settingsRequested()
        }

        Button {
            Layout.fillWidth: true
            palette.button: Theme.surfaceAlt
            palette.buttonText: Theme.text
            palette.text: Theme.text
            text: qsTr("Empty recycle bin")
            enabled: root.app.model.trashCount > 0
            onClicked: emptyConfirm.open()
        }
        Button {
            Layout.fillWidth: true
            palette.button: Theme.surfaceAlt
            palette.buttonText: Theme.text
            palette.text: Theme.text
            text: qsTr("Add folder…")
            onClicked: root.addFolderRequested()
        }
        Button {
            Layout.fillWidth: true
            palette.button: Theme.surfaceAlt
            palette.buttonText: Theme.text
            palette.text: Theme.text
            text: qsTr("Clear thumbnail cache")
            onClicked: root.clearThumbnailCacheRequested()
        }

        Label {
            Layout.fillWidth: true
            visible: root.developerMode
            text: qsTr("DB · ") + root.app.storageInfo()
            font.pixelSize: 10
            color: Theme.textFaint
            wrapMode: Text.WordWrap
            topPadding: 6
        }
    }
    }

    // Emptying the bin only removes library rows; the files stay on disk, and the
    // dialog says so explicitly so nobody assumes a destructive delete (PRD 4.3).
    Dialog {
        id: emptyConfirm
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 360
        title: qsTr("Empty recycle bin?")
        standardButtons: Dialog.Ok | Dialog.Cancel

        Label {
            width: 320
            wrapMode: Text.WordWrap
            text: qsTr("Removes %1 item(s) from the library. Files on disk are not deleted.")
                    .arg(root.app.model.trashCount)
        }

        onAccepted: root.app.model.emptyTrash()
    }

    component NavEntry: Rectangle {
        id: entry

        required property string text
        property string glyph: ""
        property var iconNames: []
        property string badge: ""
        property bool selected: false

        signal tapped()
        signal contextRequested(real x, real y)

        Layout.fillWidth: true
        implicitHeight: 30
        radius: 6
        color: entry.selected ? Theme.surfaceAlt
                              : (entryMouse.containsMouse ? Theme.surfaceAlt : "transparent")
        opacity: entry.enabled ? 1.0 : 0.4

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8
            AppIcon {
                iconNames: entry.iconNames
                fallbackGlyph: entry.glyph
                color: entry.selected ? Theme.accent : Theme.textFaint
                iconSize: 16
            }
            Label {
                Layout.fillWidth: true
                text: entry.text
                color: entry.selected ? Theme.text : Theme.textMuted
                elide: Text.ElideRight
            }
            Label {
                visible: entry.badge !== ""
                text: entry.badge
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }

        MouseArea {
            id: entryMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            enabled: entry.enabled
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function (mouse) {
                if (mouse.button === Qt.RightButton) {
                    entry.contextRequested(mouse.x, mouse.y)
                } else {
                    entry.tapped()
                }
            }
        }
    }
}
