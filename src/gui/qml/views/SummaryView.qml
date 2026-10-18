import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

                    Pane {
                        padding: 0

                        ListView {
                            id: summaryListView
                            anchors.fill: parent
                            clip: true
                            model: Util.buildSummaryModel()

                            section.property: "category"
                            section.delegate: Rectangle {
                                width: summaryListView.width
                                height: 28
                                color: section === "install" ? "#4caf50"
                                     : section === "update"  ? "#2196f3"
                                     : section === "delete"  ? "#f44336" : "#9e9e9e"

                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    font.bold: true
                                    color: "white"
                                    text: section === "install" ? qsTr("Install")
                                        : section === "update"  ? qsTr("Update")
                                        : section === "delete"  ? qsTr("Delete") : section
                                }
                            }

                            delegate: ItemDelegate {
                                width: summaryListView.width
                                height: 24

                                required property var modelData

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16
                                    spacing: 8

                                    Label {
                                        text: modelData.name || ""
                                        textFormat: Text.PlainText
                                        Layout.preferredWidth: 200
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        text: modelData.version || ""
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        color: palette.placeholderText
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            Label {
                                anchors.centerIn: parent
                                text: qsTr("No pending package changes")
                                color: palette.placeholderText
                                visible: summaryListView.count === 0
                            }
                        }
                    }
