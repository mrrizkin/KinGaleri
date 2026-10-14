import QtQuick
import QtQuick.Controls
import KinGaleri

// Section header inserted by ListView at every day boundary
// (ListView.section.property == "dayLabel").
Item {
    id: root

    property string label: ""
    property int itemCount: 0

    implicitHeight: Theme.headerHeight
    height: Theme.headerHeight

    Label {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 2
        text: root.label
        font.pixelSize: 14
        font.bold: true
        color: Theme.text
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
        opacity: 0.6
    }
}
