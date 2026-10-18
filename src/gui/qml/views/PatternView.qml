import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

                    Pane {
                        padding: 0

                        // スクロール位置保存用
                        property real savedContentY: 0
                        property int  savedIndex: -1
                        property bool restorePending: false

                        ListView {
                            id: patternListView
                            anchors.fill: parent
                            model: PackageController.patterns
                            clip: true

                            onModelChanged: {
                                if (parent.restorePending) {
                                    parent.restorePending = false
                                    patternListView.currentIndex = parent.savedIndex
                                    patternListView.contentY = parent.savedContentY
                                }
                            }

                            // パターンのチェック状態をトグル (スクロール位置を保持)
                            function togglePattern(name) {
                                parent.savedContentY = patternListView.contentY
                                parent.savedIndex    = patternListView.currentIndex
                                parent.restorePending = true
                                PackageController.togglePatternStatus(name)
                            }

                            section.property: "category"
                            section.delegate: Rectangle {
                                width: patternListView.width
                                height: 28
                                color: palette.alternateBase

                                Label {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: section
                                    textFormat: Text.PlainText
                                    font.bold: true
                                    font.pixelSize: 13
                                }
                            }

                            delegate: ItemDelegate {
                                id: patternDelegate
                                width: patternListView.width
                                highlighted: ListView.isCurrentItem

                                readonly property int pStatus: modelData.status || 0
                                readonly property bool pChecked:
                                    pStatus === 1 || pStatus === 2 || pStatus === 3 ||
                                    pStatus === 4 || pStatus === 5 || pStatus === 9
                                readonly property color statusColor: {
                                    if (pStatus === 3 || pStatus === 9) return "#4CAF50"  // KeepInstalled/Protected
                                    if (pStatus === 1 || pStatus === 2) return "#2196F3"  // Install/AutoInstall
                                    if (pStatus === 4 || pStatus === 5) return "#FF9800"  // Update/AutoUpdate
                                    if (pStatus === 6 || pStatus === 7) return "#F44336"  // Del/AutoDel
                                    return palette.mid
                                }

                                contentItem: RowLayout {
                                    spacing: 6

                                    // カスタムチェックボックス
                                    Rectangle {
                                        width: 18; height: 18
                                        radius: 3
                                        color: patternDelegate.pChecked ? patternDelegate.statusColor : "transparent"
                                        border.color: patternDelegate.pChecked ? patternDelegate.statusColor : palette.mid
                                        border.width: patternDelegate.pChecked ? 0 : 1.5

                                        // チェックマーク
                                        Label {
                                            anchors.centerIn: parent
                                            text: "\u2713"
                                            font.pixelSize: 13
                                            font.bold: true
                                            color: "white"
                                            visible: patternDelegate.pChecked
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                patternListView.togglePattern(modelData.name)
                                            }
                                        }
                                    }

                                    Label {
                                        text: modelData.summary || modelData.name || ""
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: patternDelegate.highlighted ? palette.highlightedText : palette.text
                                    }
                                }

                                onClicked: {
                                    patternListView.currentIndex = index
                                    PackageController.loadPackagesByPattern(modelData.name)
                                }
                            }

                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            Label {
                                anchors.centerIn: parent
                                text: qsTr("Loading patterns...")
                                color: palette.placeholderText
                                visible: patternListView.count === 0
                            }
                        }
                    }
