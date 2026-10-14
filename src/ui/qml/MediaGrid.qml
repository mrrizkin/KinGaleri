import QtQuick
import QtQuick.Controls
import KinGaleri

Item {
    id: root

    required property var app
    required property var gridModel
    property real targetCellSize: 168

    readonly property real sideMargin: 16
    readonly property real gap: root.app.settings ? root.app.settings.gridSpacing : 3
    readonly property bool dateHeaders: root.app.settings ? root.app.settings.showDateHeaders : true
    readonly property bool animations: root.app.settings ? root.app.settings.animateTransitions : true
    readonly property int columns: Math.max(2, gridModel.columns)
    // Space reserved on the right for the native scroll bar so cells never sit
    // underneath it.
    readonly property real scrollBarWidth: 14
    readonly property real availableWidth: Math.max(1, width - sideMargin * 2 - scrollBarWidth)

    // Cell edge that exactly fills `availableWidth` with `columns` cells, so rows
    // never wrap and ListView can treat every row as uniform height.
    readonly property real cellSize: Math.max(32, Math.floor((availableWidth - gap * (columns - 1)) / columns))

    readonly property bool trashView: root.gridModel.trashOnly

    signal openRequested(var payload)
    signal detailsRequested(var payload)
    signal itemFavoriteToggled(var payload)
    signal trashRequested(var payload)
    signal restoreRequested(var payload)
    signal addToAlbumRequested(var payloads)
    signal renameRequested(var payload)
    signal moveRequested(var ids)

    /// Index of the last plain selection click, so Shift can extend the range.
    property int lastSelectionIndex: -1

    function handleSelectionToggle(payload, shift) {
        if (!payload)
            return
        const index = root.gridModel.indexOfId(payload.mediaId)
        if (shift && root.lastSelectionIndex >= 0 && index >= 0) {
            root.gridModel.selectRange(root.lastSelectionIndex, index, true)
        } else {
            root.gridModel.toggleSelected(payload.mediaId)
            root.lastSelectionIndex = index
        }
    }

    function syncColumns() {
        const target = Math.max(80, root.targetCellSize)
        const n = Math.max(2, Math.min(12, Math.floor(root.availableWidth / target)))
        if (root.gridModel.columns !== n) {
            root.gridModel.columns = n
        }
    }

    // PRD 4.1.1 point 2: the decoded-thumbnail LRU is sized from the number of
    // cells actually visible, not a flat 128 MB.
    function syncThumbBudget() {
        const rows = Math.max(1, Math.ceil(list.height / Math.max(1, root.cellSize + root.gap)))
        root.app.updateThumbnailBudget(root.columns, rows)
    }

    onAvailableWidthChanged: syncColumns()
    onTargetCellSizeChanged: syncColumns()
    onColumnsChanged: syncThumbBudget()
    onCellSizeChanged: syncThumbBudget()
    onHeightChanged: syncThumbBudget()
    Component.onCompleted: {
        syncColumns()
        syncThumbBudget()
    }

    ListView {
        id: list
        anchors.top: stickyHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sideMargin
        anchors.topMargin: 0
        anchors.bottomMargin: 12

        model: root.gridModel
        clip: true
        reuseItems: true
        spacing: root.gap
        // Only a few screens worth of rows stay alive. This is the knob that keeps
        // memory flat on a 500k item library (PRD 5.5).
        cacheBuffer: Math.max(600, root.cellSize * 4)
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 4200
        maximumFlickVelocity: 14000

        // The inline day header that opens each section.
        section.property: "dayLabel"
        section.criteria: ViewSection.FullString
        section.delegate: TimelineHeader {
            width: list.width
            label: section
            visible: root.dateHeaders
            height: visible ? Theme.headerHeight : 0
            implicitHeight: height
        }

        delegate: MediaRow {
            width: list.width
            cellSize: root.cellSize
            gap: root.gap
            itemsModel: model.itemsModel
            selectionMode: root.gridModel.selectionMode
            hoverEffects: root.app.settings ? root.app.settings.hoverEffects : true

            onOpenRequested: function (payload) { root.openRequested(payload) }
            onFavoriteRequested: function (payload) { root.itemFavoriteToggled(payload) }
            onContextRequested: function (payload, position) {
                contextMenu.payload = payload
                contextMenu.popup()
            }
            onSelectionToggled: function (payload, shift) {
                root.handleSelectionToggle(payload, shift)
            }
        }

        ScrollBar.vertical: ScrollBar {
            id: gridScrollBar
            // Always on so it behaves like a normal desktop scroll bar, and a
            // minimum handle length so a 50k-item library does not leave a
            // 2-pixel thumb that cannot be grabbed.
            policy: ScrollBar.AlwaysOn
            minimumSize: Math.min(0.5, 48 / Math.max(1, list.height))
        }
    }

    // ---- Sticky section header ---------------------------------------------
    // Pinned to the top of the grid and fed by whichever row is currently under
    // the viewport edge. `list.contentY` is read inside the binding on purpose:
    // that is what makes the label re-evaluate while scrolling.
    readonly property int stickyRow: {
        void list.contentY
        return list.count > 0 ? list.indexAt(1, 1) : -1
    }

    TimelineHeader {
        id: stickyHeader
        width: list.width
        visible: root.dateHeaders && root.stickyRow >= 0
        label: root.stickyRow >= 0 ? root.gridModel.rowDayLabel(root.stickyRow) : ""
        // Fades in so it does not look like a second header at the very top.
        opacity: list.contentY > 4 ? 1 : 0
        Behavior on opacity {
            enabled: root.animations
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }
    }

    // ---- Timeline date bubble ----------------------------------------------
    // The native ScrollBar above is the only scroll control (always on, with a
    // minimum handle length so huge libraries stay grabbable). This bubble keeps
    // the timeline feedback: while the bar is hovered or dragged it shows the day
    // currently under the viewport edge.
    Rectangle {
        id: dateBubble
        readonly property bool active: gridScrollBar.pressed || gridScrollBar.hovered
        readonly property real handleCenterY: list.y + list.height
            * (gridScrollBar.visualPosition + gridScrollBar.visualSize / 2)

        visible: active && root.stickyRow >= 0
        anchors.right: root.right
        anchors.rightMargin: 18
        y: Math.max(0, Math.min(root.height - height, handleCenterY - height / 2))
        width: bubbleLabel.implicitWidth + 16
        height: bubbleLabel.implicitHeight + 10
        radius: 6
        color: Theme.surface
        border.color: Theme.border

        Label {
            id: bubbleLabel
            anchors.centerIn: parent
            text: root.stickyRow >= 0 ? root.gridModel.rowDayLabel(root.stickyRow) : ""
            color: Theme.text
            font.pixelSize: 11
        }
    }

    // ---- M0 scroll benchmark (--bench-scroll) -------------------------------
    // Warm up first so the initial burst of thumbnail generation (expected to be
    // slow and one-off) does not pollute the measurement.
    readonly property real benchDistance: 30000
    readonly property int benchItemCount: Math.max(0, Math.round(
        (Math.min(root.benchDistance, Math.max(0, list.contentHeight - list.height))
         / Math.max(1, root.cellSize + root.gap)) * root.columns))

    Timer {
        id: benchWarmup
        interval: 2000
        repeat: false
        running: root.app.scrollBenchmarkEnabled
        onTriggered: {
            root.app.perf.reset()
            root.app.reportScrollBenchmarkStart()
            benchAnim.start()
        }
    }

    NumberAnimation {
        id: benchAnim
        target: list
        property: "contentY"
        from: 0
        to: Math.min(root.benchDistance, Math.max(0, list.contentHeight - list.height))
        duration: Math.max(1, root.app.scrollBenchmarkSeconds * 1000)
        easing.type: Easing.Linear
        onFinished: root.app.finishScrollBenchmark(root.app.scrollBenchmarkSeconds,
                                                   benchAnim.to - benchAnim.from,
                                                   root.benchItemCount)
    }

    Menu {
        id: contextMenu
        property var payload: null

        MenuItem {
            text: (contextMenu.payload && contextMenu.payload.isFavorite)
                  ? qsTr("Remove from favorites")
                  : qsTr("Add to favorites")
            enabled: !root.trashView
            onTriggered: root.itemFavoriteToggled(contextMenu.payload)
        }
        MenuItem {
            text: qsTr("Details…")
            onTriggered: root.detailsRequested(contextMenu.payload)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Select")
            onTriggered: {
                root.gridModel.selectionMode = true
                if (contextMenu.payload)
                    root.gridModel.setSelected(contextMenu.payload.mediaId, true)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Add to album…")
            enabled: !root.trashView
            onTriggered: {
                if (contextMenu.payload)
                    root.addToAlbumRequested([contextMenu.payload.mediaId])
            }
        }
        MenuItem {
            text: qsTr("Remove from this album")
            visible: root.gridModel.albumId > 0
            onTriggered: {
                if (contextMenu.payload)
                    root.gridModel.removeFromAlbum(root.gridModel.albumId,
                                                   [contextMenu.payload.mediaId])
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Rename\u2026")
            enabled: !root.trashView && contextMenu.payload && !contextMenu.payload.isSynthetic
            onTriggered: root.renameRequested(contextMenu.payload)
        }
        MenuItem {
            text: qsTr("Move to\u2026")
            enabled: !root.trashView && contextMenu.payload && !contextMenu.payload.isSynthetic
            onTriggered: root.moveRequested([contextMenu.payload.mediaId])
        }
        MenuSeparator {}
        MenuItem {
            text: root.trashView ? qsTr("Restore to library") : qsTr("Move to recycle bin")
            onTriggered: {
                if (root.trashView) {
                    root.restoreRequested(contextMenu.payload)
                } else {
                    root.trashRequested(contextMenu.payload)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Copy path")
            onTriggered: {
                if (contextMenu.payload) {
                    // Was copying fileName; the path is what this action promises.
                    root.app.copyToClipboard(contextMenu.payload.path)
                }
            }
        }
        MenuItem {
            text: qsTr("Show in file manager")
            enabled: contextMenu.payload && !contextMenu.payload.isSynthetic
            onTriggered: {
                if (contextMenu.payload) {
                    root.app.revealInFileManager(contextMenu.payload.path)
                }
            }
        }
    }
}
