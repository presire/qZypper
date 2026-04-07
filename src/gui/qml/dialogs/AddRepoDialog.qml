import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui
import "../components" as Components

Dialog {
    id: root
    title: qsTr("Add Repository")
    modal: true
    width: 600
    height: 450
    anchors.centerIn: parent
    closePolicy: Dialog.CloseOnEscape
    standardButtons: Dialog.NoButton

    signal repoAdded()

    property int currentStep: 0
    property string detectedType: ""
    property bool probing: false

    onOpened: {
        currentStep = 0
        detectedType = ""
        probing = false
        urlSelector.clear()
        nameField.text = ""
        aliasField.text = ""
        prioritySpin.value = 99
        autoRefreshCheck.checked = true
        keepPkgCheck.checked = false
    }

    contentItem: StackLayout {
        currentIndex: root.currentStep

        // ─── ステップ1: URL入力 ──────────────────────
        ColumnLayout {
            spacing: 16

            Label {
                text: qsTr("Enter the repository URL")
                font.pixelSize: 14
                font.bold: true
            }

            Label {
                text: qsTr("Select the URL type and enter the repository URL.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: palette.placeholderText
            }

            Components.UrlTypeSelector {
                id: urlSelector
                Layout.fillWidth: true
            }

            Item { Layout.fillHeight: true }

            // プロービング中インジケーター
            RowLayout {
                visible: root.probing
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                BusyIndicator { running: root.probing; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                Label { text: qsTr("Detecting repository type...") }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Cancel")
                    onClicked: root.close()
                }
                Button {
                    text: qsTr("Next")
                    enabled: urlSelector.isValid() && !root.probing
                    highlighted: true
                    onClicked: {
                        root.probing = true
                        root.detectedType = PackageController.probeRepoType(urlSelector.url)
                        root.probing = false

                        // 名前のデフォルト生成
                        if (nameField.text === "") {
                            try {
                                var url = urlSelector.url
                                var parts = url.replace(/^[a-z]+:\/\//, "").split("/")
                                nameField.text = parts[0] || "repository"
                            } catch (e) {
                                nameField.text = "repository"
                            }
                        }
                        if (aliasField.text === "")
                            aliasField.text = nameField.text.replace(/[^a-zA-Z0-9_-]/g, "_")

                        root.currentStep = 1
                    }
                }
            }
        }

        // ─── ステップ2: プロパティ設定 ───────────────
        ColumnLayout {
            spacing: 12

            Label {
                text: qsTr("Configure repository properties")
                font.pixelSize: 14
                font.bold: true
            }

            // 検出結果
            RowLayout {
                spacing: 8
                Label { text: qsTr("Detected type:"); font.bold: true }
                Label {
                    text: root.detectedType || qsTr("Unknown (detection failed) ")
                    color: root.detectedType ? palette.text : "#e74c3c"
                }
            }

            GridLayout {
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Layout.fillWidth: true

                Label { text: qsTr("URL:") }
                Label {
                    text: urlSelector.url
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    color: palette.link
                }

                Label { text: qsTr("Name:") }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Repository name")
                }

                Label { text: qsTr("Alias:") }
                TextField {
                    id: aliasField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Unique identifier")
                }

                Label { text: qsTr("Priority:") }
                SpinBox {
                    id: prioritySpin
                    from: 0; to: 200; value: 99
                }

                Item { width: 1 }
                Label {
                    text: qsTr("Lower values have higher priority (0=highest, 99=default, 200=lowest)")
                    font.pixelSize: 11
                    color: palette.placeholderText
                }
            }

            RowLayout {
                spacing: 16
                CheckBox {
                    id: autoRefreshCheck
                    text: qsTr("Auto-refresh")
                    checked: true
                }
                CheckBox {
                    id: keepPkgCheck
                    text: qsTr("Keep Packages")
                    checked: false
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Back")
                    onClicked: root.currentStep = 0
                }
                Button {
                    text: qsTr("Add")
                    highlighted: true
                    enabled: nameField.text.length > 0 && aliasField.text.length > 0
                    onClicked: {
                        var props = {
                            "url":          urlSelector.url,
                            "name":         nameField.text,
                            "alias":        aliasField.text,
                            "enabled":      true,
                            "autoRefresh":  autoRefreshCheck.checked,
                            "keepPackages": keepPkgCheck.checked,
                            "priority":     prioritySpin.value,
                            "type":         root.detectedType
                        }

                        var ok = PackageController.addRepoFull(props)
                        if (ok) {
                            root.repoAdded()
                            root.close()
                        }
                    }
                }
                Button {
                    text: qsTr("Cancel")
                    onClicked: root.close()
                }
            }
        }
    }
}
