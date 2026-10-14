import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Card grid for the smart-grouping collections (People / Places / Events / Smart).
//
// All four pages share one component because they only differ in the item list
// they are handed and in what activating a card does. The owner does the
// filtering: this view never touches the model directly, which keeps the
// "which page is open" decision in one place (Main.qml).
Item {
    id: root

    required property var app

    property string title: ""
    /// Items are {kind, id?, key?, name?, subtitle?, count, coverUrl}.
    property var items: []
    property string emptyTitle: ""
    property string emptyHint: ""
    /// Shown instead of the grid when the feature needs enabling in Settings.
    property bool needsSetup: false
    property string setupHint: ""
    /// Places/Events get a manual rebuild button.
    property bool showRebuild: false
    /// People cards get rename / merge actions.
    property bool editableTitles: false

    signal activated(var item)
    signal setupRequested()
    signal rebuildRequested()
    signal renameRequested(int id, string currentName)
    signal mergeRequested(int id)

    /// Smart collections carry only a stable key; the label is translated here so
    /// the model stays language-agnostic.
    function smartLabel(key) {
        switch (key) {
        case "screenshots": return qsTr("Screenshots")
        case "videos": return qsTr("Videos")
        case "panoramas": return qsTr("Panoramas")
        case "large": return qsTr("Large files")
        case "recent": return qsTr("Recently taken")
        default: return key
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: 22
                font.bold: true
                color: Theme.text
            }
            Label {
                visible: !root.needsSetup && root.items.length > 0
                text: qsTr("%1 item(s)").arg(root.items.length)
                color: Theme.textFaint
                font.pixelSize: 11
            }
            Button {
                visible: root.showRebuild && !root.needsSetup
                enabled: !root.app.smartRunning
                text: root.app.smartRunning ? qsTr("Grouping…") : qsTr("Rebuild")
                onClicked: root.rebuildRequested()
            }
            Button {
                visible: root.needsSetup
                text: qsTr("Open Settings")
                onClicked: root.setupRequested()
            }
        }

        // Empty / needs-setup state.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            spacing: 8
            visible: root.needsSetup || root.items.length === 0

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: root.needsSetup ? qsTr("Not set up yet") : root.emptyTitle
                color: Theme.text
                font.pixelSize: 15
                font.bold: true
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 460
                text: root.needsSetup ? root.setupHint : root.emptyHint
                color: Theme.textFaint
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        GridView {
            id: collectionGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.needsSetup && root.items.length > 0
            clip: true
            cellWidth: 190
            cellHeight: 224
            model: root.items
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index

                width: collectionGrid.cellWidth - 12
                height: collectionGrid.cellHeight - 12
                radius: 10
                color: cardMouse.containsMouse ? Theme.surfaceAlt : Theme.surface
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: Theme.surfaceAlt
                        clip: true

                        Image {
                            id: coverImage
                            anchors.fill: parent
                            source: card.modelData.coverUrl !== undefined ? card.modelData.coverUrl : ""
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop
                            visible: status === Image.Ready
                        }
                        AppIcon {
                            anchors.centerIn: parent
                            visible: !coverImage.visible
                            iconNames: card.modelData.kind === "person"
                                       ? ["avatar-default-symbolic", "avatar-default"]
                                       : (card.modelData.kind === "place"
                                          ? ["mark-location-symbolic", "mark-location"]
                                          : ["image-x-generic-symbolic", "image-x-generic"])
                            fallbackGlyph: card.modelData.kind === "smart" ? "✦" : "◉"
                            iconSize: 36
                            color: Theme.textFaint
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: card.modelData.name !== undefined && card.modelData.name !== ""
                              ? card.modelData.name
                              : (card.modelData.kind === "person" ? qsTr("Unnamed person")
                                 : (card.modelData.kind === "smart" ? root.smartLabel(card.modelData.key)
                                                                    : ""))
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: card.modelData.subtitle !== undefined && card.modelData.subtitle !== ""
                              ? qsTr("%1 · %2").arg(card.modelData.count).arg(card.modelData.subtitle)
                              : qsTr("%1 item(s)").arg(card.modelData.count)
                        color: Theme.textFaint
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (mouse) {
                        if (mouse.button === Qt.RightButton && root.editableTitles) {
                            cardMenu.currentName = card.modelData.name || ""
                            cardMenu.personId = card.modelData.id
                            cardMenu.popup()
                        } else if (mouse.button === Qt.LeftButton) {
                            root.activated(card.modelData)
                        }
                    }
                }

                Menu {
                    id: cardMenu
                    property int personId: 0
                    property string currentName: ""
                    MenuItem {
                        text: qsTr("Rename…")
                        onTriggered: root.renameRequested(cardMenu.personId, cardMenu.currentName)
                    }
                    MenuItem {
                        text: qsTr("Merge into…")
                        onTriggered: root.mergeRequested(cardMenu.personId)
                    }
                }
            }
        }
    }
}
