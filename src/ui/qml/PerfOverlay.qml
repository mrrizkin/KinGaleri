import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KinGaleri

// Frame pacing readout for the M0 scroll benchmark (PRD 4.1).
// `dropped` is the number that matters: it counts frames whose interval blew the
// 60 Hz budget, which is what "lag" actually looks like.
Rectangle {
    id: root

    property var monitor: null

    radius: 8
    color: "#cc0d0f13"
    border.color: Theme.border
    implicitWidth: column.implicitWidth + 20
    implicitHeight: column.implicitHeight + 16

    readonly property bool healthy: root.monitor && root.monitor.frameMs <= 18.0

    ColumnLayout {
        id: column
        anchors.centerIn: parent
        spacing: 1

        Label {
            text: root.monitor
                  ? root.monitor.fps.toFixed(1) + " fps"
                  : "— fps"
            font.pixelSize: 17
            font.bold: true
            color: root.healthy ? "#7ce38b" : "#ff9f68"
        }
        Label {
            text: root.monitor
                  ? "avg " + root.monitor.frameMs.toFixed(2) + " ms"
                  : "avg — ms"
            font.pixelSize: 11
            color: Theme.textMuted
        }
        Label {
            text: root.monitor
                  ? "worst " + root.monitor.worstFrameMs.toFixed(1) + " ms"
                  : "worst — ms"
            font.pixelSize: 11
            color: Theme.textMuted
        }
        Label {
            text: root.monitor
                  ? root.monitor.frames + " frames · " + root.monitor.droppedFrames + " dropped"
                  : "— frames"
            font.pixelSize: 11
            color: Theme.textFaint
        }

        RowLayout {
            Layout.topMargin: 6
            spacing: 6
            Button {
                text: qsTr("Reset")
                font.pixelSize: 10
                implicitHeight: 22
                onClicked: if (root.monitor) root.monitor.reset()
            }
        }
    }
}
