import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Dedicated settings page.
//
// A category rail on the left, a scrollable pane of grouped cards on the right.
// Every control is wired straight to AppSettings (persisted with QSettings), so
// there is no "apply" step - the change is live and survives a restart. Storage
// actions (cache, folders) call into AppController because they touch the
// library rather than the preferences.
Item {
    id: root

    required property var app

    signal addFolderRequested()

    readonly property var settings: root.app.settings

    /// Whether the editable built-in junk list is expanded in the Library tab.
    property bool junkExpanded: false

    // Category rail model. Order matches componentFor() below.
    ListModel {
        id: categories
        ListElement { name: qsTr("General"); glyph: "\u2699"; icons: "preferences-system-symbolic,preferences-system,settings-configure" }
        ListElement { name: qsTr("Interface"); glyph: "\u25A6"; icons: "preferences-desktop-theme-symbolic,preferences-desktop-theme,preferences-desktop-symbolic,applications-graphics-symbolic" }
        ListElement { name: qsTr("Library"); glyph: "\u25A4"; icons: "folder-symbolic,folder,folder-images-symbolic" }
        ListElement { name: qsTr("Thumbnails"); glyph: "\u25A3"; icons: "view-preview,image-x-generic-symbolic,image-x-generic" }
        ListElement { name: qsTr("Playback"); glyph: "\u25B6"; icons: "media-playback-start-symbolic,media-playback-start" }
        ListElement { name: qsTr("Storage"); glyph: "\u2263"; icons: "drive-harddisk-symbolic,drive-harddisk" }
        ListElement { name: qsTr("About"); glyph: "\u2139"; icons: "help-about-symbolic,help-about,dialog-information-symbolic" }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- category rail --------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 196
            Layout.fillHeight: true
            color: Theme.surface

            ListView {
                id: categoryList
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                spacing: 2
                model: categories

                delegate: Rectangle {
                    id: categoryRow
                    required property int index
                    required property string name
                    required property string glyph
                    required property string icons

                    width: categoryList.width
                    height: 40
                    radius: 8
                    color: categoryList.currentIndex === categoryRow.index ? Theme.surfaceAlt
                                                                          : "transparent"
                    border.color: categoryList.currentIndex === categoryRow.index ? Theme.accent
                                                                                   : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        AppIcon {
                            iconNames: categoryRow.icons.split(",")
                            fallbackGlyph: categoryRow.glyph
                            color: categoryList.currentIndex === categoryRow.index ? Theme.accent
                                                                                   : Theme.textMuted
                            iconSize: 18
                        }
                        Label {
                            Layout.fillWidth: true
                            text: categoryRow.name
                            color: categoryList.currentIndex === categoryRow.index ? Theme.text
                                                                                   : Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: categoryList.currentIndex = categoryRow.index
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            Flickable {
                id: pane
                anchors.fill: parent
                contentWidth: width
                contentHeight: loader.item ? loader.item.implicitHeight + 64 : 0
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Loader {
                    id: loader
                    x: Math.max(24, (pane.width - width) / 2)
                    y: 28
                    width: Math.min(780, Math.max(320, pane.width - 48))
                    sourceComponent: root.componentFor(categoryList.currentIndex)
                }
            }
        }
    }

    function componentFor(index) {
        switch (index) {
        case 0: return generalPage
        case 1: return interfacePage
        case 2: return libraryPage
        case 3: return thumbnailsPage
        case 4: return playbackPage
        case 5: return storagePage
        default: return aboutPage
        }
    }

    // ---- General ------------------------------------------------------------
    Component {
        id: generalPage

        ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("General")
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }

            SettingCard {
                title: qsTr("Startup")

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Page shown when the app opens")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }

                ComboBox {
                    Layout.fillWidth: true
                    model: [
                        { value: "photos", label: qsTr("All photos") },
                        { value: "albums", label: qsTr("Albums") },
                        { value: "last", label: qsTr("Resume where I left off") }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: Math.max(0, ["photos", "albums", "last"].indexOf(root.settings.startupPage))
                    onActivated: root.settings.startupPage = currentValue
                }
            }

            SettingCard {
                title: qsTr("Safety")

                ToggleRow {
                    label: qsTr("Confirm before removing to the recycle bin")
                    description: qsTr("Ask for confirmation before items leave the library. Files on disk are never deleted.")
                    checked: root.settings.confirmDestructive
                    onToggled: function (value) { root.settings.confirmDestructive = value }
                }
            }

            SettingCard {
                title: qsTr("Diagnostics")

                ToggleRow {
                    label: qsTr("Show the frame-time overlay on start")
                    description: qsTr("F12 toggles it while the app runs.")
                    checked: root.settings.showPerfOverlay
                    onToggled: function (value) { root.settings.showPerfOverlay = value }
                }
            }

            SettingCard {
                title: qsTr("Developer")

                ToggleRow {
                    label: qsTr("Developer mode")
                    description: qsTr("Reveal benchmark tools, the frame-time overlay button and storage internals in the status bar.")
                    checked: root.settings.developerMode
                    onToggled: function (value) { root.settings.developerMode = value }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Leave this off for day-to-day use. The command line flags (--bench-scroll, --seed-benchmark) keep working either way.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // ---- Interface ----------------------------------------------------------
    Component {
        id: interfacePage

        ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("Interface")
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }

            SettingCard {
                title: qsTr("Theme")

                ButtonGroup { id: themeGroup }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 22

                    RadioButton {
                        text: qsTr("System")
                        ButtonGroup.group: themeGroup
                        checked: root.settings.theme === "system"
                        onClicked: root.settings.theme = "system"
                    }
                    RadioButton {
                        text: qsTr("Light")
                        ButtonGroup.group: themeGroup
                        checked: root.settings.theme === "light"
                        onClicked: root.settings.theme = "light"
                    }
                    RadioButton {
                        text: qsTr("Dark")
                        ButtonGroup.group: themeGroup
                        checked: root.settings.theme === "dark"
                        onClicked: root.settings.theme = "dark"
                    }
                    Item { Layout.fillWidth: true }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("System follows your desktop's light/dark preference and switches live.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            SettingCard {
                title: qsTr("Accent colour")

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: ["blue", "teal", "green", "purple", "orange", "pink"]

                        delegate: Rectangle {
                            id: swatch
                            required property string modelData

                            readonly property color swatchColor: {
                                const light = !Theme.dark
                                switch (swatch.modelData) {
                                case "teal": return light ? "#0d9488" : "#2dd4bf"
                                case "green": return light ? "#16a34a" : "#4ade80"
                                case "purple": return light ? "#7c3aed" : "#a78bfa"
                                case "orange": return light ? "#ea580c" : "#fb923c"
                                case "pink": return light ? "#db2777" : "#f472b6"
                                default: return light ? "#2f6fe4" : "#4c8dff"
                                }
                            }

                            width: 32
                            height: 32
                            radius: 16
                            color: swatchColor
                            border.width: root.settings.accent === swatch.modelData ? 3 : 0
                            border.color: Theme.text

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.settings.accent = swatch.modelData
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            SettingCard {
                title: qsTr("Icons")

                ToggleRow {
                    label: qsTr("Use system icons")
                    description: qsTr("Use the desktop's icon theme for the sidebar, settings and toolbar. Turn this off to use the built-in text glyphs instead.")
                    checked: root.settings.systemIcons
                    onToggled: function (value) { root.settings.systemIcons = value }
                }
            }

            SettingCard {
                title: qsTr("Language")

                ButtonGroup { id: languageGroup }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 22

                    RadioButton {
                        text: qsTr("System")
                        ButtonGroup.group: languageGroup
                        checked: root.settings.language === "system"
                        onClicked: root.settings.language = "system"
                    }
                    RadioButton {
                        text: qsTr("English")
                        ButtonGroup.group: languageGroup
                        checked: root.settings.language === "en"
                        onClicked: root.settings.language = "en"
                    }
                    RadioButton {
                        text: qsTr("Bahasa Indonesia")
                        ButtonGroup.group: languageGroup
                        checked: root.settings.language === "id"
                        onClicked: root.settings.language = "id"
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            SettingCard {
                title: qsTr("Layout")

                ToggleRow {
                    label: qsTr("Show the sidebar")
                    description: qsTr("Navigate photos, albums and settings from the left rail.")
                    checked: root.settings.showSidebar
                    onToggled: function (value) { root.settings.showSidebar = value }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("Sidebar width")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Slider {
                        Layout.fillWidth: true
                        enabled: root.settings.showSidebar
                        from: 160
                        to: 360
                        stepSize: 4
                        snapMode: Slider.SnapAlways
                        value: root.settings.sidebarWidth
                        onMoved: root.settings.sidebarWidth = value
                    }
                    Label {
                        Layout.preferredWidth: 54
                        text: qsTr("%1 px").arg(root.settings.sidebarWidth)
                        color: Theme.textMuted
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            SettingCard {
                title: qsTr("Photo grid")

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Thumbnail size")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Slider {
                        Layout.fillWidth: true
                        from: 80
                        to: 384
                        stepSize: 8
                        snapMode: Slider.SnapAlways
                        value: root.settings.thumbnailSize
                        onMoved: root.settings.thumbnailSize = value
                    }
                    Label {
                        Layout.preferredWidth: 60
                        text: qsTr("%1 px").arg(root.settings.thumbnailSize)
                        color: Theme.textMuted
                        horizontalAlignment: Text.AlignRight
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("The toolbar +/− buttons change the same value.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: qsTr("Spacing between tiles")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 24
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: root.settings.gridSpacing
                        onMoved: root.settings.gridSpacing = value
                    }
                    Label {
                        Layout.preferredWidth: 60
                        text: qsTr("%1 px").arg(root.settings.gridSpacing)
                        color: Theme.textMuted
                        horizontalAlignment: Text.AlignRight
                    }
                }

                ToggleRow {
                    label: qsTr("Show date headers")
                    description: qsTr("Group the timeline into sticky day sections.")
                    checked: root.settings.showDateHeaders
                    onToggled: function (value) { root.settings.showDateHeaders = value }
                }
            }

            SettingCard {
                title: qsTr("Effects")

                ToggleRow {
                    label: qsTr("Animate transitions")
                    description: qsTr("Fade and slide effects. Turning this off can help on slow GPUs.")
                    checked: root.settings.animateTransitions
                    onToggled: function (value) { root.settings.animateTransitions = value }
                }
                ToggleRow {
                    label: qsTr("Hover highlight")
                    description: qsTr("Draw an accent frame around the tile under the pointer.")
                    checked: root.settings.hoverEffects
                    onToggled: function (value) { root.settings.hoverEffects = value }
                }
            }
        }
    }

    // ---- Library ------------------------------------------------------------
    Component {
        id: libraryPage

        ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("Library")
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }

            SettingCard {
                title: qsTr("Folders")

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Every folder below is scanned recursively. Removing one forgets its items but never touches the files on disk.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: root.app.libraryRoots

                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: modelData.path
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            Label {
                                text: qsTr("%1 item(s)").arg(modelData.count)
                                color: Theme.textFaint
                                font.pixelSize: 10
                            }
                        }
                        Button {
                            text: qsTr("Remove")
                            onClicked: root.app.removeRoot(modelData.path)
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.app.libraryRoots.length === 0
                    text: qsTr("No folders yet — add one to start building your library.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 10

                    Button {
                        text: qsTr("Add folder…")
                        onClicked: root.addFolderRequested()
                    }
                    Button {
                        text: qsTr("Rescan now")
                        enabled: root.app.libraryRoots.length > 0 && !root.app.busy
                        onClicked: root.app.rescanLibrary()
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            SettingCard {
                title: qsTr("Watching")

                ToggleRow {
                    label: qsTr("Watch folders for changes")
                    description: qsTr("Rescan automatically when files are added, moved or deleted.")
                    checked: root.settings.autoWatch
                    onToggled: function (value) { root.settings.autoWatch = value }
                }
                ToggleRow {
                    label: qsTr("Rescan the library on startup")
                    description: qsTr("Re-index every folder each time the app launches. Slower to start, but catches drifted files.")
                    checked: root.settings.rescanOnStartup
                    onToggled: function (value) { root.settings.rescanOnStartup = value }
                }
            }

            SettingCard {
                title: qsTr("Scanning")

                ToggleRow {
                    label: qsTr("Follow symlinked folders")
                    description: qsTr("Descend into directory symbolic links. Off by default to avoid loops and duplicate items.")
                    checked: root.settings.followSymlinks
                    onToggled: function (value) { root.settings.followSymlinks = value }
                }
                ToggleRow {
                    label: qsTr("Include hidden folders")
                    description: qsTr("Walk into dotted directories such as ~/.local/share.")
                    checked: root.settings.includeHidden
                    onToggled: function (value) { root.settings.includeHidden = value }
                }
                ToggleRow {
                    label: qsTr("Skip junk folders")
                    description: qsTr("Ignore node_modules, caches, toolchains and similar non-media trees. The exact list is editable below.")
                    checked: root.settings.ignoreJunk
                    onToggled: function (value) { root.settings.ignoreJunk = value }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%1 built-in junk pattern(s)")
                                .arg(root.settings.junkPatterns.length)
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }
                    Button {
                        text: root.junkExpanded ? qsTr("Hide") : qsTr("Customize…")
                        onClicked: root.junkExpanded = !root.junkExpanded
                    }
                }

                Repeater {
                    model: root.junkExpanded ? root.settings.junkPatterns : []

                    delegate: RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Button {
                            text: qsTr("Remove")
                            onClicked: root.settings.removeJunkPattern(modelData)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.junkExpanded
                    spacing: 8

                    TextField {
                        id: junkPatternField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Add junk pattern (e.g. node_modules, build-*)")
                        onAccepted: {
                            root.settings.addJunkPattern(text)
                            text = ""
                        }
                    }
                    Button {
                        text: qsTr("Add")
                        onClicked: {
                            root.settings.addJunkPattern(junkPatternField.text)
                            junkPatternField.text = ""
                        }
                    }
                    Button {
                        text: qsTr("Reset")
                        onClicked: root.settings.resetJunkPatterns()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.junkExpanded
                    text: qsTr("Applied only while “Skip junk folders” is on. Patterns are case-insensitive globs, so build-* matches build-debug too.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("Scanning options apply to the next scan. Use “Rescan now” to apply them immediately.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            SettingCard {
                title: qsTr("Ignored folders")

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Folders here are skipped during scanning, including everything inside them. Adding one never deletes anything from disk.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: root.settings.ignoredFolders

                    delegate: RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                        Button {
                            text: qsTr("Remove")
                            onClicked: root.settings.removeIgnoredFolder(modelData)
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.settings.ignoredFolders.length === 0
                    text: qsTr("No ignored folders yet.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                }

                Button {
                    Layout.topMargin: 2
                    text: qsTr("Add ignored folder…")
                    onClicked: ignoredFolderDialog.open()
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: qsTr("Ignored name patterns")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. Screenshots, backup*, *.tmp")
                    text: root.settings.ignoredPatterns.join(", ")
                    onEditingFinished: root.settings.setIgnoredPatternsFromText(text)
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Comma-separated globs matched against folder names anywhere in the library.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // ---- Thumbnails ---------------------------------------------------------
    Component {
        id: thumbnailsPage

        ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("Thumbnails")
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }

            SettingCard {
                title: qsTr("Memory cache")

                ToggleRow {
                    label: qsTr("Adaptive budget")
                    description: qsTr("Size the decoded-thumbnail cache from what is visible on screen.")
                    checked: root.settings.thumbnailMemoryMb === 0
                    onToggled: function (value) {
                        root.settings.thumbnailMemoryMb = value ? 0 : 128
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Slider {
                        Layout.fillWidth: true
                        enabled: root.settings.thumbnailMemoryMb > 0
                        from: 32
                        to: 2048
                        stepSize: 32
                        snapMode: Slider.SnapAlways
                        value: root.settings.thumbnailMemoryMb > 0 ? root.settings.thumbnailMemoryMb : 128
                        onMoved: root.settings.thumbnailMemoryMb = value
                    }
                    Label {
                        Layout.preferredWidth: 70
                        text: root.settings.thumbnailMemoryMb === 0
                              ? qsTr("Auto")
                              : qsTr("%1 MB").arg(root.settings.thumbnailMemoryMb)
                        color: Theme.textMuted
                        horizontalAlignment: Text.AlignRight
                    }
                }

                InfoRow {
                    label: qsTr("In use")
                    value: qsTr("%1 MB").arg(root.app.thumbnailMemoryBudgetMb)
                }
            }

            SettingCard {
                title: qsTr("Disk cache")

                InfoRow {
                    label: qsTr("Cache")
                    value: root.app.thumbnailInfo()
                }
                InfoRow {
                    label: qsTr("Folder")
                    value: root.app.cachePath()
                }

                Button {
                    Layout.topMargin: 4
                    text: qsTr("Clear thumbnail cache")
                    onClicked: root.app.clearThumbnailCache()
                }
            }
        }
    }

    // ---- Playback -----------------------------------------------------------
    Component {
        id: playbackPage

        ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("Playback")
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }

            SettingCard {
                title: qsTr("Video")

                ToggleRow {
                    label: qsTr("Play videos automatically")
                    description: qsTr("Start playback as soon as the viewer opens a clip.")
                    checked: root.settings.videoAutoplay
                    onToggled: function (value) { root.settings.videoAutoplay = value }
                }
                ToggleRow {
                    label: qsTr("Loop videos")
                    description: qsTr("Restart from the beginning at the end of a clip.")
                    checked: root.settings.videoLoop
                    onToggled: function (value) { root.settings.videoLoop = value }
                }
                ToggleRow {
                    label: qsTr("Start muted")
                    description: qsTr("Open every clip with the sound off.")
                    checked: root.settings.videoMute
                    onToggled: function (value) { root.settings.videoMute = value }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: qsTr("Default volume")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 5
                        snapMode: Slider.SnapAlways
                        value: root.settings.videoVolume
                        onMoved: root.settings.videoVolume = value
                    }
                    Label {
                        Layout.preferredWidth: 54
                        text: qsTr("%1 %").arg(root.settings.videoVolume)
                        color: Theme.textMuted
                        horizontalAlignment: Text.AlignRight
                    }
                }

                InfoRow {
                    label: qsTr("Backend")
                    value: root.app.videoBackend
                }
            }
        }
    }

    // ---- Storage ------------------------------------------------------------
    Component {
        id: storagePage

        ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("Storage")
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }

            SettingCard {
                title: qsTr("Files")

                InfoRow {
                    label: qsTr("Thumbnail cache")
                    value: root.app.thumbnailInfo()
                }
                InfoRow {
                    label: qsTr("Cache folder")
                    value: root.app.cachePath()
                }
                InfoRow {
                    label: qsTr("Database")
                    value: root.app.databasePath()
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 10

                    Button {
                        text: qsTr("Clear thumbnail cache")
                        onClicked: root.app.clearThumbnailCache()
                    }
                    Button {
                        text: qsTr("Add folder…")
                        onClicked: root.addFolderRequested()
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    // ---- About --------------------------------------------------------------
    Component {
        id: aboutPage

        ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("About")
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }

            SettingCard {
                title: qsTr("Kin Galeri")

                InfoRow {
                    label: qsTr("Version")
                    value: Qt.application.version
                }
                InfoRow {
                    label: qsTr("Video backend")
                    value: root.app.videoBackend
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("A local photo and video gallery for Linux. Files are never modified without an explicit action.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    FolderDialog {
        id: ignoredFolderDialog
        title: qsTr("Choose a folder to ignore")
        onAccepted: root.settings.addIgnoredFolder(selectedFolder.toString())
    }

    // A titled card that hosts arbitrary content in a vertical stack.
    component SettingCard: Rectangle {
        id: card

        property string title
        default property alias contentData: body.data

        Layout.fillWidth: true
        implicitHeight: cardLayout.implicitHeight + 32
        color: Theme.surface
        radius: 10
        border.color: Theme.border

        ColumnLayout {
            id: cardLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: card.title
                font.bold: true
                font.pixelSize: 15
                color: Theme.text
            }

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                spacing: 8
            }
        }
    }

    // A muted label/value pair for read-only facts.
    component InfoRow: RowLayout {
        id: infoRow

        property string label
        property string value

        Layout.fillWidth: true
        spacing: 12

        Label {
            text: infoRow.label
            color: Theme.textFaint
            font.pixelSize: 12
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignTop
        }
        Label {
            Layout.fillWidth: true
            text: infoRow.value
            color: Theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
        }
    }

    // A labelled switch. The Binding re-asserts the value after the control
    // handles a click, which is what keeps two-way binding working even though
    // Switch.checked is broken by user interaction.
    component ToggleRow: RowLayout {
        id: row

        property string label
        property string description
        property bool checked: false

        signal toggled(bool value)

        Layout.fillWidth: true
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: row.label
                color: Theme.text
                font.pixelSize: 13
            }
            Label {
                Layout.fillWidth: true
                visible: row.description.length > 0
                text: row.description
                color: Theme.textFaint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Switch {
            id: control
            onToggled: row.toggled(control.checked)
        }

        Binding {
            target: control
            property: "checked"
            value: row.checked
            restoreMode: Binding.RestoreBinding
        }
    }
}
