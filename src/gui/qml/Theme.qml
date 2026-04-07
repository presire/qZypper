pragma Singleton
import QtQuick

QtObject {
    // darkMode は Main.qml からバインディングで設定される
    property bool darkMode: false

    // -- Active / Inactive (共通カラー) --
    readonly property color window:          darkMode ? "#353535" : "#efefef"
    readonly property color windowText:      darkMode ? "#ffffff" : "#000000"
    readonly property color base:            darkMode ? "#252525" : "#ffffff"
    readonly property color alternateBase:   darkMode ? "#353535" : "#f7f7f7"
    readonly property color text:            darkMode ? "#ffffff" : "#000000"
    readonly property color button:          darkMode ? "#454545" : "#e0e0e0"
    readonly property color buttonText:      darkMode ? "#ffffff" : "#000000"
    readonly property color highlight:       darkMode ? "#2196F3" : "#308cc6"
    readonly property color highlightedText: darkMode ? "#ffffff" : "#ffffff"
    readonly property color placeholderText: darkMode ? "#888888" : "#595959"
    readonly property color mid:             darkMode ? "#555555" : "#b0b0b0"
    readonly property color light:           darkMode ? "#555555" : "#ffffff"
    readonly property color midlight:        darkMode ? "#404040" : "#e3e3e3"
    readonly property color dark:            darkMode ? "#232323" : "#9f9f9f"
    readonly property color shadow:          darkMode ? "#141414" : "#767676"
    readonly property color link:            darkMode ? "#64b5f6" : "#0000ff"
    readonly property color linkVisited:     darkMode ? "#ce93d8" : "#ff00ff"
    readonly property color toolTipBase:     darkMode ? "#454545" : "#ffffdc"
    readonly property color toolTipText:     darkMode ? "#ffffff" : "#000000"

    // -- Disabled カラー --
    readonly property color disabledWindowText:      darkMode ? "#808080" : "#a0a0a0"
    readonly property color disabledBase:            darkMode ? "#2a2a2a" : "#efefef"
    readonly property color disabledText:            darkMode ? "#808080" : "#a0a0a0"
    readonly property color disabledButtonText:      darkMode ? "#808080" : "#a0a0a0"
    readonly property color disabledHighlight:       darkMode ? "#444444" : "#c0c0c0"
    readonly property color disabledHighlightedText: darkMode ? "#808080" : "#a0a0a0"
    readonly property color disabledPlaceholderText: darkMode ? "#606060" : "#c0c0c0"
    readonly property color disabledLink:            darkMode ? "#506888" : "#8080c0"
    readonly property color disabledLinkVisited:     darkMode ? "#7a6888" : "#c080c0"
    readonly property color disabledToolTipText:     darkMode ? "#808080" : "#a0a0a0"
}
