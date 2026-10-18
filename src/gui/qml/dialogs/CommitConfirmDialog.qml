import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

Dialog {
    id: confirmDialog
    title: qsTr("Confirm Changes")
    anchors.centerIn: parent
    modal: true
    standardButtons: Dialog.NoButton

    property var summaryModel: []
    property var diskUsageModel: []
    // バックエンドの GetTransactionSummary 結果 (件数 + 危険な変更の一覧)
    property var transactionSummary: ({})
    // 概要の取得待ち (取得完了まで Apply 不可)
    property bool summaryPending: false

    readonly property bool summaryAvailable: transactionSummary.installCount !== undefined
    readonly property int removeCount: transactionSummary.removeCount || 0
    readonly property int downgradeCount: transactionSummary.downgradeCount || 0
    readonly property int vendorChangeCount: transactionSummary.vendorChangeCount || 0
    readonly property int archChangeCount: transactionSummary.archChangeCount || 0
    // 概要が取得できない場合も危険扱いとし、明示承認を求める (fail closed)
    readonly property bool hasRiskyChanges: !summaryPending
                                            && (!summaryAvailable
                                                || removeCount + downgradeCount + vendorChangeCount + archChangeCount > 0)
    readonly property var riskyEntries: buildRiskyEntries(transactionSummary)

    width: Math.min(parent.width * 0.85, 750)
    height: Math.min(parent.height * 0.85, 680)

    // 開くたびに承認チェックをリセットする (前回の承認を持ち越さない)
    onAboutToShow: riskAckCheck.checked = false

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ===== 件数サマリ =====
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: confirmDialog.transactionSummary.installCount !== undefined
            text: qsTr("Install: %1, Update: %2, Remove: %3, Downgrade: %4, Vendor change: %5, Architecture change: %6")
                  .arg(confirmDialog.transactionSummary.installCount || 0)
                  .arg(confirmDialog.transactionSummary.updateCount || 0)
                  .arg(confirmDialog.removeCount)
                  .arg(confirmDialog.downgradeCount)
                  .arg(confirmDialog.vendorChangeCount)
                  .arg(confirmDialog.archChangeCount)
                  + "\n"
                  + qsTr("Download size: %1, Installed size change: %2")
                  .arg(formatBytes(confirmDialog.transactionSummary.downloadSize || 0))
                  .arg(formatSignedBytes(confirmDialog.transactionSummary.installedSizeChange || 0))
        }

        // ===== 危険な変更の警告 (削除 / ダウングレード / ベンダー変更 / アーキテクチャ変更) =====
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: riskColumn.implicitHeight + 16
            visible: confirmDialog.hasRiskyChanges
            color: Qt.rgba(0.96, 0.26, 0.21, Theme.darkMode ? 0.18 : 0.10)
            border.color: "#F44336"
            radius: 4

            ColumnLayout {
                id: riskColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.bold: true
                    color: "#F44336"
                    text: confirmDialog.summaryAvailable
                          ? qsTr("Warning: this transaction contains potentially dangerous changes. Review them carefully.")
                          : qsTr("Warning: the transaction summary could not be retrieved. Removals, downgrades or vendor changes cannot be ruled out.")
                }

                ListView {
                    id: riskList
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 140)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: confirmDialog.riskyEntries
                    activeFocusOnTab: true
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Dangerous changes")
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: RowLayout {
                        width: riskList.width - (riskList.ScrollBar.vertical.visible ? riskList.ScrollBar.vertical.width + 4 : 0)
                        spacing: 8

                        required property var modelData

                        Label {
                            text: modelData.kindText
                            font.bold: true
                            Layout.preferredWidth: 150
                            elide: Text.ElideRight
                        }
                        Label {
                            text: modelData.name || ""
                            textFormat: Text.PlainText
                            Layout.preferredWidth: 180
                            elide: Text.ElideRight
                        }
                        Label {
                            text: modelData.detail
                            textFormat: Text.PlainText
                            color: palette.placeholderText
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                    }
                }

                CheckBox {
                    id: riskAckCheck
                    text: qsTr("I understand the risks of these changes")
                    activeFocusOnTab: true
                    Accessible.name: text
                    Accessible.description: qsTr("Required before dangerous changes can be applied")
                }
            }
        }

        // ===== 変更予定パッケージ一覧 =====
        Label {
            text: qsTr("Changes to apply:")
            font.bold: true
        }

        ListView {
            id: changesList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            model: confirmDialog.summaryModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: RowLayout {
                width: changesList.width - (changesList.ScrollBar.vertical.visible ? changesList.ScrollBar.vertical.width + 4 : 12)

                required property var modelData

                Label {
                    text: {
                        var cat = modelData.category
                        if (cat === "install") return "+"
                        if (cat === "update")  return "\u2191"
                        if (cat === "delete")  return "-"
                        return ""
                    }
                    color: {
                        var cat = modelData.category
                        if (cat === "install") return "#4CAF50"
                        if (cat === "update")  return "#2196F3"
                        if (cat === "delete")  return "#F44336"
                        return palette.text
                    }
                    font.bold: true
                    Layout.preferredWidth: 24
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    text: modelData.name
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Label {
                    text: modelData.version || ""
                    textFormat: Text.PlainText
                    color: palette.placeholderText
                    Layout.preferredWidth: 160
                    elide: Text.ElideRight
                }
            }
        }

        // ===== 区切り線 =====
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: palette.mid
            opacity: 0.5
        }

        // ===== ディスク使用量テーブル =====
        Label {
            text: qsTr("Disk Usage:")
            font.bold: true
        }

        // ヘッダー
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: qsTr("Mount")
                font.bold: true
                Layout.preferredWidth: 150
            }
            Label {
                text: qsTr("Total")
                font.bold: true
                Layout.preferredWidth: 100
                horizontalAlignment: Text.AlignRight
            }
            Label {
                text: qsTr("Used")
                font.bold: true
                Layout.preferredWidth: 100
                horizontalAlignment: Text.AlignRight
            }
            Label {
                text: qsTr("After Commit")
                font.bold: true
                Layout.preferredWidth: 100
                horizontalAlignment: Text.AlignRight
            }
            Label {
                text: qsTr("Change")
                font.bold: true
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
            }
        }

        ListView {
            id: diskList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 150)
            Layout.minimumHeight: 40
            clip: true
            model: confirmDialog.diskUsageModel

            delegate: RowLayout {
                width: diskList.width
                spacing: 12

                required property var modelData

                // コミット後の空き容量を計算
                property real afterCommit: (modelData.usedSize || 0) + (modelData.packageUsage || 0)
                property real totalSize: modelData.totalSize || 1
                property real freeAfter: totalSize - afterCommit
                // 空き容量が10%未満で警告
                property bool isWarning: freeAfter < totalSize * 0.1

                Label {
                    text: modelData.mountPoint || ""
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 150
                    elide: Text.ElideRight
                }
                Label {
                    text: formatBytes(modelData.totalSize || 0)
                    Layout.preferredWidth: 100
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: formatBytes(modelData.usedSize || 0)
                    Layout.preferredWidth: 100
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: formatBytes(afterCommit)
                    Layout.preferredWidth: 100
                    horizontalAlignment: Text.AlignRight
                    color: isWarning ? "#F44336" : palette.text
                    font.bold: isWarning
                }
                Label {
                    text: {
                        var change = modelData.packageUsage || 0
                        var prefix = change >= 0 ? "+" : ""
                        return prefix + formatBytes(Math.abs(change))
                    }
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    color: {
                        var change = modelData.packageUsage || 0
                        if (change > 0) return "#FF9800"
                        if (change < 0) return "#4CAF50"
                        return palette.text
                    }
                }
            }
        }

        // ===== ボタン行 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                activeFocusOnTab: true
                Accessible.name: text
                onClicked: confirmDialog.reject()
            }
            Button {
                text: qsTr("Apply")
                highlighted: true
                enabled: !confirmDialog.summaryPending
                         && (!confirmDialog.hasRiskyChanges || riskAckCheck.checked)
                activeFocusOnTab: true
                Accessible.name: text
                Accessible.description: confirmDialog.hasRiskyChanges && !riskAckCheck.checked
                                        ? qsTr("Confirm the dangerous changes first") : ""
                onClicked: confirmDialog.accept()
            }
        }
    }

    function versionDetail(e) {
        return (e.oldVersion || "?") + " \u2192 " + (e.newVersion || "?")
    }

    // summary の各危険リストを表示用の一覧へ平坦化する
    function buildRiskyEntries(s) {
        var out = []
        var i
        var list = s.removals || []
        for (i = 0; i < list.length; ++i)
            out.push({ kindText: qsTr("Remove"), name: list[i].name, detail: list[i].oldVersion || "" })
        list = s.downgrades || []
        for (i = 0; i < list.length; ++i)
            out.push({ kindText: qsTr("Downgrade"), name: list[i].name, detail: versionDetail(list[i]) })
        list = s.vendorChanges || []
        for (i = 0; i < list.length; ++i)
            out.push({ kindText: qsTr("Vendor change"), name: list[i].name,
                       detail: (list[i].oldVendor || "?") + " \u2192 " + (list[i].newVendor || "?") })
        list = s.archChanges || []
        for (i = 0; i < list.length; ++i)
            out.push({ kindText: qsTr("Architecture change"), name: list[i].name,
                       detail: (list[i].oldArch || "?") + " \u2192 " + (list[i].newArch || "?") })
        return out
    }

    function formatSignedBytes(bytes) {
        return (bytes < 0 ? "-" : "+") + formatBytes(Math.abs(bytes))
    }

    function formatBytes(bytes) {
        if (bytes === 0) return "0 B"
        var units = ["B", "KiB", "MiB", "GiB", "TiB"]
        var i = 0
        var value = Math.abs(bytes)
        while (value >= 1024 && i < units.length - 1) {
            value /= 1024
            i++
        }
        return value.toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }
}
