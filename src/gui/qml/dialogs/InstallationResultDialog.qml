import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

    Dialog {
        id: resultDialog

        signal refreshRequested()
        title: qsTr("Installation Report")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        width: 600
        // 縦サイズは親ウィンドウの85%を上限 (大量パッケージ時もコンパクトに)
        height: Math.min((parent ? parent.height : 760) * 0.85, 760)

        background: Rectangle {
            color: palette.window
            border.color: palette.highlight
            border.width: 2
        }
        header: Label {
            text: resultDialog.title
            font.bold: true
            padding: 10
        }

        property bool resultSuccess: true
        property var resultData: ({})

        // バックエンドの outcome (completed / partial / cancelled / failed)。
        // 古いバックエンドでは success から推定する。
        readonly property string outcome: resultData.outcome
                                          || (resultSuccess ? "completed" : "failed")
        readonly property var scriptLines: (resultData.scriptOutput || []).concat(resultData.rpmMessages || [])
        readonly property var servicesToRestart: resultData.servicesToRestart || []
        readonly property var snapshots: resultData.snapshots || []

        // 再起動・サービス再起動の案内やエラーがある場合は、サマリーページ設定に関わらず表示すべき
        readonly property bool needsAttention: outcome !== "completed"
                                               || resultData.rebootNeeded === true
                                               || resultData.restartSuggested === true
                                               || servicesToRestart.length > 0

        function outcomeText() {
            switch (outcome) {
            case "completed": return qsTr("All changes were applied successfully.")
            case "partial":   return qsTr("Some changes were applied, but others failed or were skipped.")
            case "cancelled": return qsTr("The operation was cancelled. Some changes may already have been applied.")
            default:          return qsTr("The changes could not be applied.")
            }
        }

        function outcomeColor() {
            switch (outcome) {
            case "completed": return "#4CAF50"
            case "partial":   return "#FF9800"
            case "cancelled": return "#FF9800"
            default:          return "#F44336"
            }
        }

        function formatSnapshot(s) {
            var text = "#" + (s.num || "?")
            if (s.type)
                text += " [" + s.type + "]"
            if (s.date)
                text += "  " + s.date
            if (s.description)
                text += "  " + s.description
            return text
        }

        component PackageSection: ColumnLayout {
            property string heading: ""
            property var items: []
            property int count: items.length
            property color textColor: palette.text

            Layout.fillWidth: true
            spacing: 4
            visible: count > 0

            Label {
                text: "\u2022 " + parent.heading
                color: parent.textColor
                Layout.fillWidth: true
            }
            TextEdit {
                text: parent.items.join("\n")
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                selectByKeyboard: true
                wrapMode: TextEdit.NoWrap
                Layout.fillWidth: true
                Layout.leftMargin: 16
                color: parent.textColor
                font.pixelSize: 13
                font.family: "monospace"
                visible: parent.items.length > 0
                Accessible.role: Accessible.EditableText
                Accessible.name: parent.heading
            }
        }

        component Separator: Rectangle {
            Layout.fillWidth: true
            height: 1
            color: palette.mid
        }

        Flickable {
            id: resultFlickable
            anchors.fill: parent
            contentHeight: resultColumn.implicitHeight
            clip: true
            boundsMovement: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: resultColumn
                width: resultFlickable.width
                spacing: 12

            // ヘッダ (結果区分)
            Label {
                text: resultDialog.outcomeText()
                font.bold: true
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                color: resultDialog.outcomeColor()
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Label {
                visible: (resultDialog.resultData.doneCount !== undefined)
                text: qsTr("Completed steps: %1, errors: %2, skipped: %3")
                      .arg(resultDialog.resultData.doneCount || 0)
                      .arg(resultDialog.resultData.errorCount || 0)
                      .arg(resultDialog.resultData.skippedCount || 0)
                font.pixelSize: 12
                Layout.fillWidth: true
            }

            // エラーメッセージ
            Label {
                visible: resultDialog.outcome !== "completed" && (resultDialog.resultData.errorMessage || "") !== ""
                text: resultDialog.resultData.errorMessage || ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: "#FF9800"
                font.pixelSize: 12
            }

            // 再起動 / サービス再起動の案内
            Frame {
                Layout.fillWidth: true
                visible: resultDialog.resultData.rebootNeeded === true
                         || resultDialog.resultData.restartSuggested === true
                         || resultDialog.servicesToRestart.length > 0
                background: Rectangle {
                    color: Qt.rgba(1.0, 0.6, 0.0, 0.12)
                    border.color: "#FF9800"
                    radius: 4
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Label {
                        visible: resultDialog.resultData.rebootNeeded === true
                        text: qsTr("A system reboot is required to complete the update.")
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: resultDialog.resultData.rebootNeeded !== true
                                 && resultDialog.resultData.restartSuggested === true
                        text: qsTr("Restarting the package manager or affected applications is recommended.")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: resultDialog.servicesToRestart.length > 0
                        text: qsTr("The following services use updated files and should be restarted:")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    TextEdit {
                        visible: resultDialog.servicesToRestart.length > 0
                        text: resultDialog.servicesToRestart.join("\n")
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        color: palette.text
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Accessible.role: Accessible.EditableText
                        Accessible.name: qsTr("Services to restart")
                    }
                }
            }

            Separator {}

            // パッケージ セクション
            Label {
                text: qsTr("Packages")
                font.bold: true
                font.pixelSize: 13
                color: palette.highlight
            }

            PackageSection {
                heading: qsTr("Installed packages: %1").arg(count)
                items: resultDialog.resultData.installedPackages || []
                count: resultDialog.resultData.installed || items.length
            }
            PackageSection {
                heading: qsTr("Updated packages: %1").arg(count)
                items: resultDialog.resultData.updatedPackages || []
                count: resultDialog.resultData.updated || items.length
            }
            PackageSection {
                heading: qsTr("Removed packages: %1").arg(count)
                items: resultDialog.resultData.removedPackages || []
                count: resultDialog.resultData.removed || items.length
            }
            PackageSection {
                heading: qsTr("Failed packages: %1").arg(count)
                items: resultDialog.resultData.failedPackages || []
                textColor: "#F44336"
            }
            PackageSection {
                heading: qsTr("Skipped packages: %1").arg(count)
                items: resultDialog.resultData.skippedPackages || []
                textColor: "#FF9800"
            }

            // スナップショット (snapper)
            Separator { visible: resultDialog.snapshots.length > 0 }
            Label {
                visible: resultDialog.snapshots.length > 0
                text: qsTr("Snapshots")
                font.bold: true
                font.pixelSize: 13
                color: palette.highlight
            }
            Label {
                visible: resultDialog.snapshots.length > 0
                text: qsTr("Snapper created the following snapshots. You can roll back with \"snapper rollback\" if needed.")
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            TextEdit {
                visible: resultDialog.snapshots.length > 0
                text: resultDialog.snapshots.map(resultDialog.formatSnapshot).join("\n")
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                color: palette.text
                font.family: "monospace"
                font.pixelSize: 12
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Snapshots")
            }

            // RPM スクリプト出力
            Separator { visible: resultDialog.scriptLines.length > 0 }
            Label {
                visible: resultDialog.scriptLines.length > 0
                text: qsTr("Package script output")
                font.bold: true
                font.pixelSize: 13
                color: palette.highlight
            }
            Label {
                visible: resultDialog.resultData.scriptOutputTruncated === true
                text: qsTr("The output was truncated. See /var/log/zypp/history for the full log.")
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#FF9800"
                Layout.fillWidth: true
            }
            ScrollView {
                visible: resultDialog.scriptLines.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(200, scriptText.implicitHeight + 12)

                TextArea {
                    id: scriptText
                    text: resultDialog.scriptLines.join("\n")
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    font.family: "monospace"
                    font.pixelSize: 12
                    Accessible.name: qsTr("Package script output")
                }
            }

            Separator {}

            // 統計 セクション
            Label {
                text: qsTr("Statistics")
                font.bold: true
                font.pixelSize: 13
                color: palette.highlight
            }

            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 12
                rowSpacing: 4

                Label { text: "\u2022 " + qsTr("Elapsed time:") }
                Label {
                    text: {
                        var secs = resultDialog.resultData.elapsedSeconds || 0
                        var min = Math.floor(secs / 60)
                        var sec = Math.floor(secs % 60)
                        return String(min).padStart(2, '0') + ":" + String(sec).padStart(2, '0')
                    }
                }

                Label { text: "\u2022 " + qsTr("Total installed size:") }
                Label {
                    text: Util.formatBytes(resultDialog.resultData.totalInstalledSize || 0)
                }

                Label { text: "\u2022 " + qsTr("Total download size:") }
                Label {
                    text: Util.formatBytes(resultDialog.resultData.totalDownloadSize || 0)
                }
            }
            }   // ColumnLayout
        }   // Flickable

        onAccepted: {
            // パッケージリストを再検索して更新
            resultDialog.refreshRequested()
        }
    }
