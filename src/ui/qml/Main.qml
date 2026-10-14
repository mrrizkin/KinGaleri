import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window

    // Injected via QQmlApplicationEngine::setInitialProperties().
    required property var app

    visible: true
    width: 1440
    height: 920
    minimumWidth: 720
    minimumHeight: 480
    title: qsTr("Kin Galeri")

    color: Theme.background

    // Keep native Qt Quick Controls on the same palette as custom QML surfaces.
    palette {
        window: Theme.surface
        windowText: Theme.text
        base: Theme.background
        alternateBase: Theme.surfaceAlt
        text: Theme.text
        button: Theme.surfaceAlt
        buttonText: Theme.text
        brightText: Theme.text
        highlight: Theme.accent
        highlightedText: "#ffffff"
        mid: Theme.border
        dark: Theme.border
        placeholderText: Theme.textMuted
    }

    property bool showPerf: false
    property bool detailsVisible: false
    /// Developer-only surfaces (benchmark seeding, perf overlay, storage internals)
    /// are hidden unless the user opts in from Settings → General.
    readonly property bool developerMode: window.app.settings.developerMode
    // Full metadata for one item, fetched on demand (see the details popup).
    property var details: null
    // Top-level page: "photos" (timeline grid), "albums" (overview) or "settings".
    property string libraryPage: "photos"

    Component.onCompleted: {
        // AppSettings is injected after the QML object is constructed.  Seed the
        // singleton explicitly so the first rendered frame cannot use Theme's
        // dark default while the binding is being established.
        Theme.dark = window.app.settings.dark
        Theme.accentName = window.app.settings.accent
        Theme.systemIcons = window.app.settings.systemIcons
        window.showPerf = window.app.initialPerfOverlay || window.app.settings.showPerfOverlay
        window.app.perf.attachWindow(window)

        // Startup page: an explicit page, or whatever was open when the app was
        // last quit.
        const startup = window.app.settings.startupPage
        window.libraryPage = startup === "last" ? window.app.settings.lastPage : startup
    }

    // Remember the last page so "resume where I left off" works.
    onLibraryPageChanged: window.app.settings.lastPage = window.libraryPage

    Connections {
        target: window.app.settings
        function onDarkChanged() { Theme.dark = window.app.settings.dark }
        function onAccentChanged() { Theme.accentName = window.app.settings.accent }
        function onSystemIconsChanged() { Theme.systemIcons = window.app.settings.systemIcons }
    }

    // Keep the binding as the steady-state fallback for settings changes made
    // from QML and for bindings restored after component completion.
    Binding {
        target: Theme
        property: "dark"
        value: window.app.settings.dark
    }
    Binding {
        target: Theme
        property: "accentName"
        value: window.app.settings.accent
    }
    Binding {
        target: Theme
        property: "systemIcons"
        value: window.app.settings.systemIcons
    }

    Shortcut {
        sequence: "F12"
        enabled: window.developerMode
        onActivated: window.showPerf = !window.showPerf
    }
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: window.app.reload()
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: window.app.model.canUndo && !viewer.opened
        onActivated: window.app.model.undo()
    }
    Shortcut {
        // Left disabled while the viewer is open: the viewer registers its own
        // Escape handling, and two enabled Shortcuts with the same sequence make
        // Qt report an ambiguous activation, which fires neither one.
        sequence: "Escape"
        enabled: !viewer.opened
        onActivated: {
            window.detailsVisible = false
            searchField.focus = false
        }
    }

    function showDetails(payload) {
        if (!payload || payload.mediaId === undefined)
            return
        window.details = window.app.model.mediaDetail(payload.mediaId)
        window.detailsVisible = true
    }

    /// Opens one album card from the album overview in the photo grid. Every
    /// other filter is cleared so the album shows exactly its own contents.
    function openAlbum(kind, id, name) {
        const model = window.app.model
        model.trashOnly = false
        model.favoritesOnly = false
        model.filterKind = ""
        model.searchQuery = ""
        model.clearDateRange()
        model.clearSelection()
        searchField.text = ""
        if (kind === "manual") {
            model.sourceAlbum = ""
            model.albumId = id
        } else {
            model.albumId = 0
            model.sourceAlbum = name
        }
        window.libraryPage = "photos"
    }

    /// Turns the detail map into label/value rows, skipping anything absent so the
    /// panel stays compact for files without EXIF.
    function detailRows(d) {
        if (!d)
            return []
        const rows = []
        function add(label, value, suffix) {
            if (value === undefined || value === null || value === "" || value === 0)
                return
            rows.push({ label: label, value: suffix ? (String(value) + suffix) : String(value) })
        }
        add(qsTr("Path"), d.path)
        add(qsTr("Album"), d.sourceAlbum)
        add(qsTr("Type"), d.mimeType)
        add(qsTr("Size"), d.sizeText)
        add(qsTr("Dimensions"), d.dimensions)
        add(qsTr("Resolution"), d.megapixels)
        add(qsTr("Duration"), d.durationText)
        add(qsTr("Captured"), d.takenAtText,
            d.takenAtSource ? "  (" + d.takenAtSource + ")" : "")
        add(qsTr("Modified"), d.modifiedAtText)
        add(qsTr("Camera"), [d.cameraMake, d.cameraModel].filter(function (x) { return !!x }).join(" "))
        add(qsTr("Lens"), d.lensModel)
        add(qsTr("ISO"), d.iso)
        add(qsTr("Aperture"), d.fNumber ? ("f/" + d.fNumber) : "")
        add(qsTr("Exposure"), d.exposureTime, d.exposureTime ? " s" : "")
        add(qsTr("Focal length"), d.focalLength, d.focalLength ? " mm" : "")
        add(qsTr("Location"), d.gpsText)
        add(qsTr("Indexed"), d.indexedAtText)
        return rows
    }

    header: ToolBar {
        background: Rectangle { color: Theme.surface }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            Label {
                text: qsTr("Kin Galeri")
                font.bold: true
                font.pixelSize: 16
                color: Theme.text
            }

            ToolButton {
                visible: window.libraryPage === "photos"
                         && (window.app.model.albumId > 0 || window.app.model.sourceAlbum !== "")
                text: "\u2190 " + qsTr("Albums")
                onClicked: window.libraryPage = "albums"
                ToolTip.text: qsTr("Back to albums")
                ToolTip.visible: hovered
            }

            // ---- search ------------------------------------------------------
            TextField {
                id: searchField
                visible: window.libraryPage !== "settings"
                Layout.preferredWidth: 260
                placeholderText: qsTr("Search file name…  (Ctrl+F)")
                selectByMouse: true
                Accessible.name: qsTr("Search file name")

                // textEdited (not textChanged) so clearing the field in code does
                // not re-trigger a query.
                onTextEdited: searchDebounce.restart()
                Keys.onEscapePressed: {
                    text = ""
                    searchDebounce.restart()
                    focus = false
                }
            }

            // Debounced so typing does not fire a query per keystroke; the model
            // reload is a synchronous database read on the GUI thread.
            Timer {
                id: searchDebounce
                interval: 220
                repeat: false
                onTriggered: window.app.model.searchQuery = searchField.text
            }

            Label {
                Layout.fillWidth: true
                text: window.libraryPage === "albums"
                      ? qsTr("%1 album(s)").arg(window.app.model.albumCards.length)
                      : (window.libraryPage === "settings" ? qsTr("Settings")
                                                            : window.app.summary)
                color: Theme.textMuted
                elide: Text.ElideRight
                leftPadding: 8
            }

            ProgressBar {
                visible: window.app.busy
                indeterminate: window.app.progressTotal <= 0
                from: 0
                to: Math.max(1, window.app.progressTotal)
                value: window.app.progressCurrent
                Layout.preferredWidth: 160
            }

            Button {
                text: qsTr("Cancel")
                visible: window.app.busy
                onClicked: window.app.cancelCurrentJob()
            }

            ToolButton {
                id: dateFilterButton
                visible: window.libraryPage === "photos"
                text: window.app.model.dateRangeLabel.length > 0
                      ? ("\u25BE " + window.app.model.dateRangeLabel)
                      : qsTr("Date \u25BE")
                highlighted: window.app.model.dateRangeLabel.length > 0
                onClicked: dateMenu.popup(dateFilterButton)
                ToolTip.text: qsTr("Filter by capture date (F-4.2)")
                ToolTip.visible: hovered
            }

            ToolButton {
                visible: window.libraryPage === "photos"
                         && window.app.model.dateRangeLabel.length > 0
                text: "\u2715"
                onClicked: window.app.model.clearDateRange()
                ToolTip.text: qsTr("Clear date filter")
                ToolTip.visible: hovered
                Accessible.name: qsTr("Clear date filter")
            }

            ToolButton {
                visible: window.libraryPage === "photos"
                text: window.app.model.selectionMode ? qsTr("Selecting") : qsTr("Select")
                highlighted: window.app.model.selectionMode
                onClicked: window.app.model.selectionMode = !window.app.model.selectionMode
                ToolTip.text: qsTr("Selection mode (batch actions)")
                ToolTip.visible: hovered
            }

            ToolButton {
                text: qsTr("Undo")
                enabled: window.app.model.canUndo
                onClicked: window.app.model.undo()
                ToolTip.text: window.app.model.canUndo
                             ? qsTr("Undo: %1 (Ctrl+Z)").arg(window.app.model.undoLabel)
                             : qsTr("Nothing to undo")
                ToolTip.visible: hovered
            }

            ToolSeparator {}

            ToolButton {
                visible: window.libraryPage === "photos"
                text: "−"
                font.pixelSize: 18
                onClicked: window.app.settings.thumbnailSize =
                           Math.max(80, window.app.settings.thumbnailSize - 24)
                ToolTip.text: qsTr("Smaller thumbnails")
                ToolTip.visible: hovered
                Accessible.name: qsTr("Smaller thumbnails")
            }
            ToolButton {
                visible: window.libraryPage === "photos"
                text: "+"
                font.pixelSize: 18
                onClicked: window.app.settings.thumbnailSize =
                           Math.min(384, window.app.settings.thumbnailSize + 24)
                ToolTip.text: qsTr("Larger thumbnails")
                ToolTip.visible: hovered
                Accessible.name: qsTr("Larger thumbnails")
            }

            ToolSeparator {}

            ToolButton {
                visible: window.developerMode || window.app.initialPerfOverlay
                text: window.showPerf ? qsTr("Perf ●") : qsTr("Perf ○")
                onClicked: {
                    window.showPerf = !window.showPerf
                    window.app.settings.showPerfOverlay = window.showPerf
                }
                ToolTip.text: qsTr("Frame time overlay (F12)")
                ToolTip.visible: hovered
            }

            ToolButton {
                id: settingsButton
                highlighted: window.libraryPage === "settings"
                onClicked: window.libraryPage = "settings"
                ToolTip.text: qsTr("Settings")
                ToolTip.visible: hovered
                Accessible.name: qsTr("Settings")
                contentItem: AppIcon {
                    iconNames: ["settings-configure", "preferences-system-symbolic", "preferences-system"]
                    fallbackGlyph: "⚙"
                    iconSize: 18
                    color: settingsButton.highlighted ? Theme.accent : Theme.text
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            visible: window.app.settings.showSidebar
            Layout.fillHeight: true
            Layout.preferredWidth: window.app.settings.sidebarWidth
            app: window.app
            developerMode: window.developerMode
            currentPage: window.libraryPage
            onAddFolderRequested: folderDialog.open()
            onClearThumbnailCacheRequested: window.app.clearThumbnailCache()
            onAlbumsRequested: window.libraryPage = "albums"
            onPhotosRequested: window.libraryPage = "photos"
            onSettingsRequested: window.libraryPage = "settings"
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            MediaGrid {
                id: grid
                anchors.fill: parent
                visible: window.libraryPage === "photos"
                app: window.app
                gridModel: window.app.model
                targetCellSize: window.app.settings.thumbnailSize

                onItemFavoriteToggled: function (payload) {
                    window.app.model.toggleFavoriteById(payload.mediaId)
                }
                onOpenRequested: function (payload) {
                    viewer.payload = payload
                    viewer.open()
                }
                onDetailsRequested: function (payload) { window.showDetails(payload) }
                onTrashRequested: function (payload) {
                    window.confirmThen(qsTr("Move '%1' to the recycle bin?").arg(payload.fileName),
                                       function () {
                                           window.app.model.moveToTrashById(payload.mediaId)
                                       })
                }
                onRestoreRequested: function (payload) {
                    window.app.model.restoreFromTrashById(payload.mediaId)
                }
                onAddToAlbumRequested: function (ids) {
                    window.openAlbumPicker(ids)
                }
                onRenameRequested: function (payload) {
                    window.renameTarget = payload
                    renameField.text = payload ? payload.fileName : ""
                    renameField.selectAll()
                    renameDialog.open()
                }
                onMoveRequested: function (ids) {
                    window.moveTargetIds = ids && ids.length > 0
                                           ? ids : window.app.model.selectedIds()
                    if (window.moveTargetIds.length > 0)
                        moveFolderDialog.open()
                }
            }

            AlbumsView {
                id: albumsView
                anchors.fill: parent
                visible: window.libraryPage === "albums"
                app: window.app
                onAlbumActivated: function (kind, id, name) {
                    window.openAlbum(kind, id, name)
                }
            }

            SettingsView {
                id: settingsView
                anchors.fill: parent
                visible: window.libraryPage === "settings"
                app: window.app
                onAddFolderRequested: folderDialog.open()
            }

            // ---- batch action bar (PRD F-5.7) -----------------------------
            // Overlays the bottom of the grid while selection mode is on. It lives
            // here rather than in the window footer so it only covers the grid.
            Rectangle {
                id: selectionBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 56
                z: 20
                visible: window.app.model.selectionMode && window.libraryPage === "photos"
                color: Theme.surface
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 10

                    Label {
                        text: qsTr("%1 selected").arg(window.app.model.selectionCount)
                        color: Theme.text
                        font.bold: true
                    }
                    Button {
                        text: qsTr("Select all")
                        onClicked: window.app.model.selectAll()
                    }
                    Button {
                        text: qsTr("Clear")
                        onClicked: window.app.model.clearSelection()
                    }

                    ToolSeparator {}

                    Button {
                        text: qsTr("Add to album…")
                        enabled: window.app.model.selectionCount > 0
                        onClicked: window.openAlbumPicker([])
                    }
                    Button {
                        text: qsTr("Remove from album")
                        visible: window.app.model.albumId > 0
                        enabled: window.app.model.selectionCount > 0
                        onClicked: window.app.model.removeSelectionFromAlbum(window.app.model.albumId)
                    }
                    Button {
                        text: qsTr("Favorite")
                        enabled: window.app.model.selectionCount > 0
                        onClicked: window.app.model.setSelectionFavorite(true)
                    }
                    Button {
                        text: qsTr("Unfavorite")
                        enabled: window.app.model.selectionCount > 0
                        onClicked: window.app.model.setSelectionFavorite(false)
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: window.app.model.trashOnly ? qsTr("Restore") : qsTr("Move to recycle bin")
                        enabled: window.app.model.selectionCount > 0
                        onClicked: {
                            if (window.app.model.trashOnly) {
                                window.app.model.restoreSelection()
                            } else {
                                window.confirmThen(qsTr("Move %1 item(s) to the recycle bin?")
                                                   .arg(window.app.model.selectionCount), function () {
                                                       window.app.model.trashSelection()
                                                   })
                            }
                        }
                    }
                    Button {
                        text: qsTr("Done")
                        onClicked: window.app.model.selectionMode = false
                    }
                }
            }

            // Empty state: shown when the library has nothing to display.
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 48)
                spacing: 14
                visible: window.libraryPage === "photos"
                         && window.app.model.itemCount === 0 && !window.app.busy

                Label {
                    Layout.fillWidth: true
                    text: window.app.model.trashOnly ? qsTr("Recycle bin is empty")
                                                     : (window.app.model.searchQuery !== ""
                                                        ? qsTr("No matches")
                                                        : qsTr("No media yet"))
                    font.pixelSize: 22
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    text: window.app.model.trashOnly
                          ? qsTr("Items you remove from the library land here. Files on disk are never touched.")
                          : (window.developerMode
                             ? qsTr("Add a folder with your photos, or generate a synthetic library to measure scroll performance.")
                             : qsTr("Add a folder with your photos to get started."))
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textMuted
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10
                    visible: !window.app.model.trashOnly
                    Button {
                        text: qsTr("Add folder…")
                        onClicked: folderDialog.open()
                    }
                    Button {
                        visible: window.developerMode
                        text: qsTr("Generate 50,000 rows")
                        onClicked: window.app.seedBenchmark(50000)
                    }
                }
            }
        }
    }

    footer: Rectangle {
        height: 34
        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: window.app.statusText
                color: Theme.textMuted
                elide: Text.ElideMiddle
            }
            Label {
                visible: window.developerMode
                text: window.app.databasePath()
                color: Theme.textFaint
                elide: Text.ElideLeft
                Layout.maximumWidth: 320
            }
            Label {
                visible: window.developerMode
                text: window.app.thumbnailInfo()
                color: Theme.textFaint
                elide: Text.ElideLeft
                Layout.maximumWidth: 420
            }
            Label {
                visible: window.developerMode && window.app.watching
                text: qsTr("watching %1 folders").arg(window.app.watchedDirectories)
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Add a folder to the library")
        onAccepted: window.app.indexFolder(selectedFolder.toString())
    }

    // ---- date range filter (PRD F-4.2) ------------------------------------
    // Presets rather than a calendar popup: the common question is "what did I
    // shoot recently", and presets answer it in one click. The model keeps the
    // underlying range so a future custom picker can drive the same API.
    function startOfDay(d) {
        const c = new Date(d)
        c.setHours(0, 0, 0, 0)
        return c.getTime()
    }
    function endOfDay(d) {
        const c = new Date(d)
        c.setHours(23, 59, 59, 999)
        return c.getTime()
    }
    function applyDateDays(days) {
        const now = Date.now()
        const from = window.startOfDay(now - (days - 1) * 86400000)
        window.app.model.setDateRange(from, window.endOfDay(now))
    }

    Menu {
        id: dateMenu

        MenuItem {
            text: qsTr("Today")
            onTriggered: window.app.model.setDateRange(window.startOfDay(Date.now()),
                                                       window.endOfDay(Date.now()))
        }
        MenuItem {
            text: qsTr("Last 7 days")
            onTriggered: window.applyDateDays(7)
        }
        MenuItem {
            text: qsTr("Last 30 days")
            onTriggered: window.applyDateDays(30)
        }
        MenuItem {
            text: qsTr("This year")
            onTriggered: {
                const now = new Date()
                window.app.model.setDateRange(new Date(now.getFullYear(), 0, 1).getTime(),
                                               window.endOfDay(now.getTime()))
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Any time")
            enabled: window.app.model.dateRangeLabel.length > 0
            onTriggered: window.app.model.clearDateRange()
        }
    }

    ViewerOverlay {
        id: viewer
        app: window.app
        onDetailsRequested: function (payload) { window.showDetails(payload) }
    }

    // ---- "add to album" picker (PRD F-5.2) --------------------------------
    property var albumTargetIds: []
    property var renameTarget: null
    property var moveTargetIds: []
    property string lastError: ""
    property var pendingConfirmAction: null
    property string confirmMessage: ""

    /// Runs `action` immediately when the user turned off confirmation, else
    /// asks first. Used by every destructive action (recycle bin).
    function confirmThen(message, action) {
        if (!window.app.settings.confirmDestructive) {
            action()
            return
        }
        window.confirmMessage = message
        window.pendingConfirmAction = action
        confirmDialog.open()
    }

    function reportError(message) {
        if (!message || message.length === 0)
            return
        window.lastError = message
        errorDialog.open()
    }

    function openAlbumPicker(ids) {
        // Context-menu calls pass explicit ids; the batch bar passes nothing and
        // falls back to the current selection.
        window.albumTargetIds = (ids && ids.length > 0) ? ids : window.app.model.selectedIds()
        if (window.albumTargetIds.length === 0)
            return
        albumNameField.text = ""
        albumPicker.open()
    }

    Dialog {
        id: renameDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Rename file")
        width: 400
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextField {
            id: renameField
            width: 320
            selectByMouse: true
        }

        onAccepted: {
            if (window.renameTarget)
                window.reportError(window.app.model.renameMedia(window.renameTarget.mediaId,
                                                                renameField.text))
        }
    }

    FolderDialog {
        id: moveFolderDialog
        title: qsTr("Move to folder")
        onAccepted: window.reportError(window.app.model.moveMedia(window.moveTargetIds,
                                                                   selectedFolder.toString()))
    }

    Dialog {
        id: errorDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Could not complete the action")
        width: 400
        standardButtons: Dialog.Ok

        Label {
            width: 320
            wrapMode: Text.WordWrap
            text: window.lastError
        }
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Confirm")
        width: 400
        standardButtons: Dialog.Ok | Dialog.Cancel

        Label {
            width: 340
            wrapMode: Text.WordWrap
            text: window.confirmMessage
        }

        onAccepted: {
            const action = window.pendingConfirmAction
            window.pendingConfirmAction = null
            if (action)
                action()
        }
        onRejected: window.pendingConfirmAction = null
    }

    Dialog {
        id: albumPicker
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Add to album")
        standardButtons: Dialog.Close
        width: 400

        ColumnLayout {
            width: 360
            spacing: 8

            Label {
                text: qsTr("%1 item(s)").arg(window.albumTargetIds.length)
                color: Theme.textMuted
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: albumNameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("New album name…")
                }
                Button {
                    text: qsTr("Create + add")
                    enabled: albumNameField.text.trim().length > 0
                    onClicked: {
                        const id = window.app.model.createAlbum(albumNameField.text)
                        if (id > 0) {
                            window.app.model.addToAlbum(id, window.albumTargetIds)
                            window.app.model.clearSelection()
                            albumPicker.close()
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            Label {
                visible: window.app.model.albums.length === 0
                text: qsTr("No albums yet — create one above")
                color: Theme.textFaint
                font.pixelSize: 11
            }

            Repeater {
                model: window.app.model.albums
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.name + "  (" + modelData.count + ")"
                    onClicked: {
                        window.app.model.addToAlbum(modelData.id, window.albumTargetIds)
                        window.app.model.clearSelection()
                        albumPicker.close()
                    }
                }
            }
        }
    }

    // ---- details / EXIF panel ----------------------------------------------
    Popup {
        id: detailsSheet

        visible: window.detailsVisible
        width: Math.min(560, window.width - 60)
        height: Math.min(window.height - 100, content.implicitHeight + 30)
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.color: Theme.border
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: window.details ? window.details.fileName : ""
                font.bold: true
                font.pixelSize: 15
                wrapMode: Text.WrapAnywhere
                color: Theme.text
            }

            // A Flickable has no intrinsic height, so inside a ColumnLayout that is
            // sized from `content.implicitHeight` it would collapse to zero and clip
            // every row away. Its height has to come from the rows it holds.
            Flickable {
                id: rowsFlick
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(1, Math.min(rowsColumn.implicitHeight, 360))
                contentWidth: width
                contentHeight: rowsColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: rowsColumn
                    width: rowsFlick.width
                    spacing: 4

                    Repeater {
                        model: window.detailsVisible ? window.detailRows(window.details) : []

                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                text: modelData.label
                                color: Theme.textFaint
                                font.pixelSize: 12
                                Layout.preferredWidth: 108
                                Layout.alignment: Qt.AlignTop
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.value
                                color: Theme.textMuted
                                font.pixelSize: 12
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Copy path")
                    enabled: window.details !== null
                    onClicked: {
                        window.app.copyToClipboard(window.details.path)
                        window.detailsVisible = false
                    }
                }
                Button {
                    text: qsTr("Reveal")
                    enabled: window.details !== null && !window.details.isSynthetic
                    onClicked: {
                        window.app.revealInFileManager(window.details.path)
                        window.detailsVisible = false
                    }
                }
                Button {
                    text: qsTr("Close")
                    onClicked: window.detailsVisible = false
                }
            }
        }
    }

    PerfOverlay {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 14
        visible: window.showPerf
        monitor: window.app.perf
    }
}
