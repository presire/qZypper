import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

    Dialog {
        id: conflictDialog

        signal applyChangesRequested()
        signal applyPendingCommitCleared()
        title: qsTr("Warning")
        anchors.centerIn: parent
        modal: true
        width: Math.min((parent ? parent.width : 750) * 0.85, 750)
        height: Math.min((parent ? parent.height : 550) * 0.75, 550)
        standardButtons: Dialog.NoButton

        onRejected: {
            // ステータス変更を元に戻す (表示集合と表示位置は維持される)
            PackageController.restoreState()
        }

        property var problems: []
        // 各問題で選択された解決策のインデックスを保持
        property var selectedSolutions: []
        // applyChanges()から呼ばれたかどうか
        property bool fromApply: false

        onProblemsChanged: {
            // デフォルト: 各問題の最初の解決策を選択
            var sel = []
            for (var i = 0; i < problems.length; i++)
                sel.push(0)
            selectedSolutions = sel
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            ListView {
                id: conflictListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: conflictDialog.problems
                spacing: 12

                delegate: ColumnLayout {
                    id: conflictDelegate
                    width: conflictListView.width - 12

                    required property int index
                    required property var modelData

                    // 問題の説明 (⚠アイコン付き)
                    Label {
                        text: "\u26A0 " + (modelData.description || "")
                        textFormat: Text.PlainText
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // 詳細
                    Label {
                        text: modelData.details || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        color: palette.placeholderText
                        visible: (modelData.details || "") !== ""
                    }

                    // [競合の解決方法:]ラベル
                    Label {
                        text: qsTr("Conflict resolution:")
                        Layout.topMargin: 4
                    }

                    // 解決策リスト (RadioButton + details)
                    Repeater {
                        model: modelData.solutions || []

                        ColumnLayout {
                            required property int index
                            required property var modelData

                            property int problemIdx: conflictDelegate.index

                            Layout.fillWidth: true
                            spacing: 2

                            RadioButton {
                                id: solutionRadio
                                text: (parent.index + 1) + ": " + (parent.modelData.description || "")
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                // 解決策の説明はソルバー (メタデータ) 由来のため PlainText で表示する
                                contentItem: Text {
                                    leftPadding: solutionRadio.indicator.width + solutionRadio.spacing
                                    text: solutionRadio.text
                                    textFormat: Text.PlainText
                                    font: solutionRadio.font
                                    color: solutionRadio.palette.windowText
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                checked: {
                                    var sel = conflictDialog.selectedSolutions
                                    var pi = parent.problemIdx
                                    return sel && pi < sel.length && sel[pi] === parent.index
                                }
                                onClicked: {
                                    var sel = conflictDialog.selectedSolutions.slice()
                                    sel[parent.problemIdx] = parent.index
                                    conflictDialog.selectedSolutions = sel
                                }
                            }

                            // 解決策の詳細 (具体的なアクション内容)
                            Label {
                                text: parent.modelData.details || ""
                                textFormat: Text.PlainText
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                Layout.leftMargin: 48
                                color: palette.placeholderText
                                font.pixelSize: 13
                                visible: (parent.modelData.details || "") !== ""
                            }
                        }
                    }

                    // 問題間の区切り線
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: palette.mid
                        opacity: 0.5
                        Layout.topMargin: 4
                    }
                }
            }

            // ボタン行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                // バグ報告用に現在のソルバー状態をテストケースとして保存する
                Button {
                    text: qsTr("Save Solver Testcase")
                    enabled: !PackageController.busy
                    activeFocusOnTab: true
                    Accessible.name: text
                    Accessible.description: qsTr("Save the current solver state for bug reports")
                    onClicked: PackageController.saveSolverTestcase()
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("OK -- Try Again (O)")
                    onClicked: {
                        // 選択された解決策を適用
                        var sel = conflictDialog.selectedSolutions
                        for (var i = 0; i < conflictDialog.problems.length; i++) {
                            var solIdx = (sel && i < sel.length) ? sel[i] : 0
                            PackageController.applySolution(i, solIdx)
                        }
                        conflictDialog.close()

                        // 結果は onResolveFinished で処理される
                        if (conflictDialog.fromApply) {
                            // 「適用」からの呼び出し — 再度依存関係解決してコミットへ進む
                            conflictDialog.applyChangesRequested()
                        } else {
                            // チェックボックスからの呼び出し — ソルバー再実行
                            conflictDialog.applyPendingCommitCleared()
                            PackageController.resolveDependencies()
                        }
                    }
                }

                Button {
                    text: qsTr("Cancel (C)")
                    onClicked: {
                        // ステータス変更を元に戻す (内部でパッケージリスト再取得も実行、表示集合と表示位置は保持)
                        PackageController.restoreState()
                        conflictDialog.close()
                    }
                }
            }
        }
    }
