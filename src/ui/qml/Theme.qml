pragma Singleton

import QtQuick

// Central palette. Deliberately a plain singleton with no logic: the delegates in
// the grid read from it on every frame, so it must stay trivial.
//
// `dark` is driven from Main.qml, which binds it to AppSettings.dark (the user's
// theme choice, or the desktop's colour scheme). Binding the colours off a single
// boolean keeps the switch instantaneous and avoids a second palette singleton.
QtObject {
    property bool dark: true

    /// Accent preset name, driven from Main.qml off AppSettings.accent. Kept as a
    /// name rather than a colour so the light/dark variants stay in one place.
    property string accentName: "blue"

    /// When false, AppIcon skips the desktop icon theme and uses its text-glyph
    /// fallback. Driven from Main.qml off AppSettings.systemIcons.
    property bool systemIcons: true

    readonly property color background: dark ? "#14161a" : "#f6f7f9"
    readonly property color surface: dark ? "#1b1e24" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#22262e" : "#eceef2"
    readonly property color border: dark ? "#2e333d" : "#d7dbe2"
    readonly property color text: dark ? "#eef1f6" : "#1b1f27"
    readonly property color textMuted: dark ? "#a7b0bf" : "#5a6472"
    readonly property color textFaint: dark ? "#6c7686" : "#8b95a5"
    readonly property color accent: {
        switch (accentName) {
        case "teal":
            return dark ? "#2dd4bf" : "#0d9488"
        case "green":
            return dark ? "#4ade80" : "#16a34a"
        case "purple":
            return dark ? "#a78bfa" : "#7c3aed"
        case "orange":
            return dark ? "#fb923c" : "#ea580c"
        case "pink":
            return dark ? "#f472b6" : "#db2777"
        default:
            return dark ? "#4c8dff" : "#2f6fe4"
        }
    }
    readonly property color danger: dark ? "#ff6b6b" : "#d64545"
    readonly property color videoBadge: dark ? "#101216" : "#0b0c0f"

    readonly property int radius: 8
    readonly property int gap: 3
    readonly property int headerHeight: 34
}
