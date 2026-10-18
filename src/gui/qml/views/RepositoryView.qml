import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

                    Pane {
                        padding: 0

                        ListView {
                            id: repoListView
                            anchors.fill: parent
                            clip: true
                            model: PackageController.repos
                            delegate: ItemDelegate {
                                id: repoDelegate
                                width: repoListView.width
                                text: modelData.name || modelData.alias || ""
                                highlighted: ListView.isCurrentItem
                                // リポジトリ名はメタデータ由来のため PlainText で表示する
                                // (既定の IconLabel は AutoText で HTML を解釈する)
                                contentItem: Text {
                                    text: repoDelegate.text
                                    textFormat: Text.PlainText
                                    font: repoDelegate.font
                                    color: repoDelegate.highlighted ? repoDelegate.palette.highlightedText
                                                                    : repoDelegate.palette.text
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: {
                                    repoListView.currentIndex = index
                                    PackageController.loadPackagesByRepo(modelData.alias)
                                }
                            }
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        }
                    }
