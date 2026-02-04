pragma Singleton
import QtQuick 6.0
import Firebird.Emu 1.0

QtObject {
    // VS Code inspired palettes for dark/light.
    property bool darkMode: (typeof Emu !== "undefined") ? Emu.darkTheme : true
    property color background: darkMode ? "#1e1e1e" : "#f5f5f5"
    property color surface: darkMode ? "#252526" : "#ffffff"
    property color surfaceAlt: darkMode ? "#2d2d2d" : "#f0f0f0"
    property color sunken: darkMode ? "#202020" : "#e6e6e6"
    property color border: darkMode ? "#3c3c3c" : "#d0d0d0"
    property color borderStrong: darkMode ? "#4a4a4a" : "#c0c0c0"

    property color accent: darkMode ? "#3794ff" : "#0066b8"
    property color accentMuted: darkMode ? "#4fa1ff" : "#2e8ae6"
    property color text: darkMode ? "#d4d4d4" : "#1f1f1f"
    property color textMuted: darkMode ? "#9f9f9f" : "#5e5e5e"
    property color textOnAccent: "#fefefe"
    property color selection: darkMode ? "#264f78" : "#cce6ff"
    property color textOnSelection: darkMode ? "#ffffff" : "#1a1a1a"
}
