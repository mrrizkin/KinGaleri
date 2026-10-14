import QtQuick
import KinGaleri

// One grid row: `columns` cells laid out horizontally. The outer ListView
// sections on `dayLabel`, so a row never mixes two days.
Item {
    id: root

    property var itemsModel: null
    property real cellSize: 120
    property real gap: Theme.gap
    property bool selectionMode: false
    property bool hoverEffects: true

    signal openRequested(var payload)
    signal favoriteRequested(var payload)
    signal contextRequested(var payload, var position)
    signal selectionToggled(var payload, bool shift)

    implicitHeight: root.cellSize
    height: root.cellSize

    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        spacing: root.gap

        Repeater {
            model: root.itemsModel

            MediaCell {
                width: root.cellSize
                height: root.cellSize
                selectionMode: root.selectionMode
                hoverEffects: root.hoverEffects

                onOpenRequested: function (payload) { root.openRequested(payload) }
                onFavoriteRequested: function (payload) { root.favoriteRequested(payload) }
                onContextRequested: function (payload, position) {
                    root.contextRequested(payload, position)
                }
                onSelectionToggled: function (payload, shift) {
                    root.selectionToggled(payload, shift)
                }
            }
        }
    }
}
