pragma Singleton
/**
 * @file Theme.qml
 * @brief Light/Darkの配色をアプリ全体へ供給するシングルトン
 *
 * Main.qmlがdarkModeを設定し、各コンポーネントはpalette経由でこの色を受け取る
 * Qtのパレットロール名と同じ名前で定義しているため、Fusionスタイルの配色をそのまま置き換えられる
 *
 * 入力 : darkMode (Main.qmlのバインディングから設定される)
 * 出力 : なし (色プロパティの参照のみ)
 * 依存 : なし
 */
import QtQuick

QtObject {
    // darkModeはMain.qmlからバインディングで設定される
    property bool darkMode: false                                    // Light配色ならfalse

    // -- Active / Inactive (共通カラー) --
    readonly property color window:          darkMode ? "#353535" : "#efefef"    // ウィンドウ背景
    readonly property color windowText:      darkMode ? "#ffffff" : "#000000"    // 標準の文字色
    readonly property color base:            darkMode ? "#252525" : "#ffffff"    // 入力欄・一覧の背景
    readonly property color alternateBase:   darkMode ? "#353535" : "#f7f7f7"    // 一覧の縞模様の背景
    readonly property color text:            darkMode ? "#ffffff" : "#000000"    // 本文の文字色
    readonly property color button:          darkMode ? "#454545" : "#e0e0e0"    // ボタン背景
    readonly property color buttonText:      darkMode ? "#ffffff" : "#000000"    // ボタンの文字色
    readonly property color highlight:       darkMode ? "#2196F3" : "#308cc6"    // 選択項目の背景
    readonly property color highlightedText: darkMode ? "#ffffff" : "#ffffff"    // 選択項目の文字色
    readonly property color placeholderText: darkMode ? "#888888" : "#595959"    // 入力欄のプレースホルダ
    readonly property color mid:             darkMode ? "#555555" : "#b0b0b0"    // 枠線の標準色
    readonly property color light:           darkMode ? "#555555" : "#ffffff"    // 明るい枠線色
    readonly property color midlight:        darkMode ? "#404040" : "#e3e3e3"    // 明るさの中間色
    readonly property color dark:            darkMode ? "#232323" : "#9f9f9f"    // 暗い枠線色
    readonly property color shadow:          darkMode ? "#141414" : "#767676"    // 影の色
    readonly property color link:            darkMode ? "#64b5f6" : "#0000ff"    // リンクの文字色
    readonly property color linkVisited:     darkMode ? "#ce93d8" : "#ff00ff"    // 訪問済みリンクの文字色
    readonly property color toolTipBase:     darkMode ? "#454545" : "#ffffdc"    // ツールチップの背景
    readonly property color toolTipText:     darkMode ? "#ffffff" : "#000000"    // ツールチップの文字色

    // -- Disabledカラー --
    readonly property color disabledWindowText:      darkMode ? "#808080" : "#a0a0a0"    // 無効時のウィンドウ文字色
    readonly property color disabledBase:            darkMode ? "#2a2a2a" : "#efefef"    // 無効時の背景
    readonly property color disabledText:            darkMode ? "#808080" : "#a0a0a0"    // 無効時の本文文字色
    readonly property color disabledButtonText:      darkMode ? "#808080" : "#a0a0a0"    // 無効時のボタン文字色
    readonly property color disabledHighlight:       darkMode ? "#444444" : "#c0c0c0"    // 無効時の選択背景
    readonly property color disabledHighlightedText: darkMode ? "#808080" : "#a0a0a0"    // 無効時の選択文字色
    readonly property color disabledPlaceholderText: darkMode ? "#606060" : "#c0c0c0"    // 無効時のプレースホルダ
    readonly property color disabledLink:            darkMode ? "#506888" : "#8080c0"    // 無効時のリンク色
    readonly property color disabledLinkVisited:     darkMode ? "#7a6888" : "#c080c0"    // 無効時の訪問済みリンク色
    readonly property color disabledToolTipText:     darkMode ? "#808080" : "#a0a0a0"    // 無効時のツールチップ文字色
}
