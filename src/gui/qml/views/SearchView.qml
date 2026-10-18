import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

                    Pane {
                        padding: 12

                        ScrollView {
                            id: searchScroll
                            anchors.fill: parent
                            contentWidth: availableWidth
                            clip: true

                            ColumnLayout {
                                width: searchScroll.availableWidth
                                spacing: 8

                                // 検索入力
                                RowLayout {
                                    Layout.fillWidth: true

                                    TextField {
                                        id: searchField
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 120
                                        placeholderText: qsTr("Search packages...")
                                        Accessible.name: qsTr("Search packages")
                                        onAccepted: performSearch()
                                    }

                                    Button {
                                        text: qsTr("Search")
                                        onClicked: performSearch()
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        text: qsTr("Match:")
                                    }

                                    ComboBox {
                                        id: matchModeCombo
                                        model: [qsTr("Substring"), qsTr("Exact"), qsTr("Glob"), qsTr("Regex")]
                                        Accessible.name: qsTr("Search match mode")
                                        Accessible.description: qsTr("How the query text is matched against package data")
                                    }

                                    CheckBox {
                                        id: installedOnlyCheck
                                        text: qsTr("Installed only")
                                        Accessible.name: qsTr("Installed packages only")
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // 検索対象チェックボックス
                                GroupBox {
                                    title: qsTr("Search Criteria")
                                    Layout.fillWidth: true

                                    ColumnLayout {
                                        anchors.fill: parent

                                        component ElidedCheckBox : CheckBox {
                                            Layout.fillWidth: true
                                            contentItem: Label {
                                                text: parent.text
                                                textFormat: Text.PlainText
                                                leftPadding: parent.indicator.width + parent.spacing
                                                elide: Text.ElideRight
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }

                                        ElidedCheckBox { id: cbName;     text: qsTr("Name (&E)");                    checked: true }
                                        ElidedCheckBox { id: cbKeyword;  text: qsTr("Keywords (&K)");                 checked: true }
                                        ElidedCheckBox { id: cbSummary;  text: qsTr("Summary (&M)");                  checked: true }
                                        ElidedCheckBox { id: cbDesc;     text: qsTr("Description (&I)") }
                                        ElidedCheckBox { id: cbProvides; text: qsTr("RPM \"Provides\" (&R)") }
                                        ElidedCheckBox { id: cbRequires; text: qsTr("RPM \"Requires\" (&Q)") }
                                    }
                                }

                                // クライアント側エラー表示 (正規表現エラーなど)
                                Label {
                                    visible: PackageController.searchError.length > 0
                                    text: PackageController.searchError
                                    textFormat: Text.PlainText
                                    color: "#F44336"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                // 分類別パッケージ読み込み
                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        text: qsTr("Classification:")
                                    }

                                    ComboBox {
                                        id: classificationCombo
                                        Layout.preferredWidth: 180
                                        model: [qsTr("Recommended"), qsTr("Suggested"), qsTr("Orphaned"),
                                                qsTr("Unneeded"), qsTr("Multiversion"), qsTr("Retracted"),
                                                qsTr("Retracted (installed)"), qsTr("PTF"), qsTr("Locked")]
                                        Accessible.name: qsTr("Package classification")
                                        Accessible.description: qsTr("Load packages of the selected classification into the list")
                                    }

                                    Button {
                                        text: qsTr("Load")
                                        enabled: !PackageController.busy
                                        Accessible.name: qsTr("Load packages by classification")
                                        onClicked: {
                                            var cls = ["recommended", "suggested", "orphaned", "unneeded", "multiversion",
                                                      "retracted", "retractedInstalled", "ptf", "locked"][classificationCombo.currentIndex]
                                            PackageController.packageProxy.sortColumn = 1
                                            PackageController.packageProxy.sortAscending = true
                                            PackageController.loadClassification(cls)
                                        }
                                    }

                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

    function performSearch() {
        var flags = 0
        if (cbName.checked)     flags |= 0x01
        if (cbKeyword.checked)  flags |= 0x02
        if (cbSummary.checked)  flags |= 0x04
        if (cbDesc.checked)     flags |= 0x08
        if (cbRequires.checked) flags |= 0x10
        if (cbProvides.checked) flags |= 0x20

        // 検索結果はパッケージ名昇順で表示
        PackageController.packageProxy.sortColumn = 1
        PackageController.packageProxy.sortAscending = true

        // マッチモード / installed-only が既定と違う場合は拡張検索を使う
        var match = ["substring", "exact", "glob", "regex"][matchModeCombo.currentIndex]
        if (match !== "substring" || installedOnlyCheck.checked)
            PackageController.searchPackagesEx(searchField.text, flags, match, installedOnlyCheck.checked)
        else
            PackageController.searchPackages(searchField.text, flags)
    }
                    }
