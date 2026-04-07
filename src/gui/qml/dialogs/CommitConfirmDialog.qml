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

    width: Math.min(parent.width * 0.85, 750)
    height: Math.min(parent.height * 0.8, 600)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

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
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Label {
                    text: modelData.version || ""
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
                onClicked: confirmDialog.reject()
            }
            Button {
                text: qsTr("Apply")
                highlighted: true
                onClicked: confirmDialog.accept()
            }
        }
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
