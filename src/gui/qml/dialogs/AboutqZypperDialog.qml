// qZypperについてダイアログ
// アプリケーション情報、バージョン、ライセンス、リンク情報を表示
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: qsTr("About qZypper")
    modal: true
    width: {
        if (!ApplicationWindow.window) return 800
        var calculated = ApplicationWindow.window.width * 0.7
        return Math.min(Math.max(calculated, 800), 1000)
    }
    height: {
        if (!ApplicationWindow.window) return 600
        var calculated = ApplicationWindow.window.height * 0.7
        return Math.min(Math.max(calculated, 600), 800)
    }
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Close

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: aboutqZypperLayout.height

        ColumnLayout {
            id: aboutqZypperLayout
            width: parent.width
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 20
                Layout.bottomMargin: 20
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 20

                // qZypperロゴ画像
                Image {
                    id: qZypperLogo
                    source: "qrc:/qt/qml/org/presire/qzypper/gui/icons/qZypper.svg"
                    fillMode: Image.PreserveAspectFit
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 200
                }

                // アプリケーション情報カラム
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 20

                // アプリケーション名とバージョン
                Label {
                    text: "qZypper " + Qt.application.version
                    font.bold: true
                    font.pixelSize: 20
                    Layout.fillWidth: true
                }

                // アプリケーション説明
                Label {
                    text: qsTr("Software management tool for openSUSE / SLE")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // 著作権情報
                Label {
                    text: "Copyright (C) 2026 Presire"
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                }

                // ライセンス情報
                Label {
                    text: qsTr("This program is licensed under the GNU General Public License v3.0 or later.")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                }

                // GPLライセンスリンク
                Label {
                    text: qsTr("For more information, see <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">gnu.org/licenses/gpl-3.0</a>.")
                    textFormat: Text.RichText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true

                    onLinkActivated: function(link) {
                        Qt.openUrlExternally(link)
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                // GitHubリポジトリリンク
                Label {
                    text: qsTr("GitHub: <a href=\"https://github.com/presire/qZypper\">github.com/presire/qZypper</a>")
                    textFormat: Text.RichText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.topMargin: 10

                    onLinkActivated: function(link) {
                        Qt.openUrlExternally(link)
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                // Issuesページリンク
                Label {
                    text: qsTr("Issues: <a href=\"https://github.com/presire/qZypper/issues\">github.com/presire/qZypper/issues</a>")
                    textFormat: Text.RichText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true

                    onLinkActivated: function(link) {
                        Qt.openUrlExternally(link)
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }
                }
            }
        }
    }
}
