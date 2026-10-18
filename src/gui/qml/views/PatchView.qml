import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// パッチ選択ビュー (左ペイン)。GetPatches 結果を一覧表示し、
// チェックで Install/NoInst をトグル、必要パッチの一括選択も行う。
Pane {
    id: patchView
    padding: 12

    // 選択中のカテゴリ (-1 = すべて)。Types.h PatchCategory に準拠
    property int categoryFilter: -1

    function reload() {
        PackageController.loadPatches(categoryFilter)
    }

    // バックエンドは libzypp のカテゴリ文字列 (security/recommended/optional/feature/document 等) を返す
    function categoryText(c) {
        switch (String(c).toLowerCase()) {
        case "security":    return qsTr("Security")
        case "recommended": return qsTr("Recommended")
        case "optional":    return qsTr("Optional")
        case "feature":     return qsTr("Feature")
        case "document":    return qsTr("Document")
        case "yast":        return "YaST"
        default:            return String(c || "")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ---- ツール行: カテゴリフィルタ + 一括選択 ----
        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("Category:")
            }

            ComboBox {
                id: categoryCombo
                Layout.preferredWidth: 150
                model: [qsTr("All"), qsTr("Security"), qsTr("Recommended"),
                        qsTr("Optional"), qsTr("Feature")]
                activeFocusOnTab: true
                Accessible.name: qsTr("Patch category filter")
                onActivated: {
                    patchView.categoryFilter = (currentIndex === 0) ? -1 : currentIndex - 1
                    patchView.reload()
                }
            }

            Item { Layout.fillWidth: true }

            CheckBox {
                id: securityOnlyCheck
                text: qsTr("Security only")
                activeFocusOnTab: true
                Accessible.name: qsTr("Select security patches only")
            }

            CheckBox {
                id: includeOptionalCheck
                text: qsTr("Include optional")
                activeFocusOnTab: true
                Accessible.name: qsTr("Include optional patches")
            }

            Button {
                text: qsTr("Select All Needed")
                enabled: !PackageController.busy
                activeFocusOnTab: true
                Accessible.name: qsTr("Select all needed patches")
                Accessible.description: qsTr("Mark all patches needed by the solver for installation")
                onClicked: PackageController.selectAllNeededPatches(securityOnlyCheck.checked,
                                                                    includeOptionalCheck.checked)
            }
        }

        // ---- ヘッダ行 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Item { Layout.preferredWidth: 24 }   // チェックボックス列
            Label { text: qsTr("Patch");     Layout.preferredWidth: 170; font.bold: true }
            Label { text: qsTr("Category");  Layout.preferredWidth: 100; font.bold: true }
            Label { text: qsTr("Severity");  Layout.preferredWidth: 80;  font.bold: true }
            Label { text: qsTr("Summary");   Layout.fillWidth: true;     font.bold: true }
            Label { text: qsTr("Flags");     Layout.preferredWidth: 130; font.bold: true }
        }

        MenuSeparator { Layout.fillWidth: true }

        // ---- パッチ一覧 ----
        ListView {
            id: patchList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: PackageController.patches
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: RowLayout {
                width: patchList.width - (patchList.ScrollBar.vertical.visible ? patchList.ScrollBar.vertical.width + 4 : 12)
                spacing: 6

                required property var modelData

                CheckBox {
                    Layout.preferredWidth: 24
                    checked: modelData.selected === true
                    enabled: !PackageController.busy
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Select patch %1").arg(modelData.name)
                    onToggled: PackageController.setPatchStatus(modelData.id, checked ? 1 : 0)
                }

                Label {
                    text: modelData.name || ""
                    Layout.preferredWidth: 170
                    elide: Text.ElideRight
                    font.bold: modelData.isNeeded === true
                }

                Label {
                    text: patchView.categoryText(modelData.category)
                    Layout.preferredWidth: 100
                    elide: Text.ElideRight
                }

                Label {
                    text: modelData.severity || ""
                    Layout.preferredWidth: 80
                    elide: Text.ElideRight
                }

                Label {
                    text: modelData.summary || ""
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.preferredWidth: 130
                    spacing: 4

                    Label {
                        visible: modelData.interactive === true
                        text: qsTr("Interactive")
                        font.pixelSize: 10
                        color: palette.placeholderText
                    }
                    Label {
                        visible: modelData.rebootSuggested === true
                        text: qsTr("Reboot")
                        font.pixelSize: 10
                        color: "#F44336"
                    }
                    Label {
                        visible: modelData.restartSuggested === true
                        text: qsTr("Restart")
                        font.pixelSize: 10
                        color: "#FF9800"
                    }
                }
            }
        }

        // 空状態
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: patchList.count === 0 && !PackageController.busy

            Label {
                anchors.centerIn: parent
                text: qsTr("No patches to display")
                color: palette.placeholderText
            }
        }
    }
}
