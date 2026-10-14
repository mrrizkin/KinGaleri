import QtQuick

// A desktop-theme icon with a text-glyph fallback.
//
// `iconNames` is an ordered list of freedesktop icon names: the provider tries
// each until the running theme provides one (themes disagree on canonical names,
// so e.g. "view-grid" on Breeze and "applications-graphics" on Mint-Y). If none
// resolve, the provider returns a null image and QML reports `Image.Error`; the
// glyph is shown instead, so a system without an icon theme still renders a
// readable UI.
Item {
    id: root

    property var iconNames: []
    property string fallbackGlyph: ""
    property color color: Theme.textMuted
    property int iconSize: 16

    readonly property bool wantsIcon: Theme.systemIcons && root.iconNames.length > 0
    // Rasterise at 2x so the icon stays sharp when scaled down on HiDPI; the
    // provider caps the size at 512px.
    readonly property int pixelSize: Math.max(16, Math.round(root.iconSize * 2))
    readonly property string iconPath: {
        var parts = [];
        for (var i = 0; i < root.iconNames.length; ++i) {
            parts.push(encodeURIComponent(root.iconNames[i]));
        }
        return parts.join(",");
    }

    implicitWidth: root.iconSize
    implicitHeight: root.iconSize

    Image {
        id: image
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: true
        visible: root.wantsIcon && status === Image.Ready
        sourceSize.width: root.pixelSize
        sourceSize.height: root.pixelSize
        source: root.wantsIcon
                ? "image://themeicon/" + root.iconPath
                  + "?c=" + encodeURIComponent(root.color.toString())
                  + "&s=" + root.pixelSize
                : ""
    }

    Text {
        anchors.centerIn: parent
        visible: !image.visible
        text: root.fallbackGlyph
        color: root.color
        font.pixelSize: Math.round(root.iconSize * 0.85)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
