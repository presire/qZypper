/**
 * @file ConflictDialog.qml
 * @brief 依存関係の競合と選択可能な解決策を表示するダイアログ
 *
 * CommitProgressDialogまたはMain.qmlから開かれる
 *
 * 入力 : problems (競合の一覧)、fromApply (適用処理から開かれたかどうか)
 * 出力 : applyChangesRequested()、applyPendingCommitCleared() シグナル
 * 依存 : PackageController (libzyppのソルバーが報告した競合情報)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// 依存関係の競合ダイアログ (問題ごとに解決策を1つ選んで再試行する)
Dialog {
    id: conflictDialog  // ダイアログ自身の参照名

    property var problems: []           // ソルバーが報告した競合の一覧
    property var selectedSolutions: []  // 各問題で選択中の解決策のインデックス
    property bool fromApply: false      // 適用処理から開かれたかどうか

    signal applyChangesRequested()      // 変更の適用を続行してほしいことを通知する
    signal applyPendingCommitCleared()  // 保留中のコミットを破棄したことを通知する

    anchors.centerIn: parent                                      // 親ウィンドウの中央に表示する
    width: Math.min((parent ? parent.width : 750) * 0.85, 750)    // 親の幅の85% (最大750px)
    height: Math.min((parent ? parent.height : 550) * 0.75, 550)  // 親の高さの75% (最大550px)
    title: qsTr("Warning")                                        // ダイアログのタイトル
    standardButtons: Dialog.NoButton                              // 標準ボタンは使わず、独自の操作ボタンを使う
    modal: true                                                   // 表示中は背後の画面を操作できないようにする

    // [Esc]キーなどで閉じられた場合
    onRejected: {
        // 保留中のコミットを解除し、Main.qmlが中断した適用フローを再開しないようにする
        conflictDialog.applyPendingCommitCleared()
        // ステータス変更を元に戻す (表示集合と表示位置は維持される)
        PackageController.restoreState()
    }

    // 競合の一覧が更新された場合
    onProblemsChanged: {
        // デフォルト : 各問題の最初の解決策を選択
        var sel = []
        for (var i = 0; i < problems.length; i++) {
            sel.push(0)
        }
        selectedSolutions = sel
    }

    // 競合の一覧と操作ボタンをまとめる領域
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 8            // 一覧とボタン行の間隔

        // 競合の一覧
        ListView {
            id: conflictListView  // 競合一覧の参照名

            Layout.fillWidth: true          // 親の幅いっぱいに広げる
            Layout.fillHeight: true         // 残りの高さをすべて使う
            spacing: 12                     // 問題の間隔
            clip: true                      // 領域外にはみ出した部分を描画しない
            model: conflictDialog.problems  // ソルバーが報告した競合の一覧

            // 問題1件分の表示 (説明、詳細、解決策の選択肢)
            delegate: ColumnLayout {
                id: conflictDelegate  // 問題1件分の参照名

                required property int index      // 競合一覧の中での問題の番号
                required property var modelData  // 問題データ

                width: conflictListView.width - 12  // 一覧の幅から右側の余白を除いた幅

                // 警告アイコンを付けた問題の説明
                Label {
                    Layout.fillWidth: true                           // 親の幅いっぱいに広げる
                    text: "\u26A0 " + (modelData.description || "")  // 警告アイコンと問題の説明
                    textFormat: Text.PlainText                       // ソルバーが返した文字列をHTMLとして解釈させない
                    font.bold: true                                  // 太字で表示する
                    wrapMode: Text.WordWrap                          // 幅に収まらない場合は単語単位で折り返す
                }

                // 問題の詳細説明
                Label {
                    Layout.fillWidth: true                     // 親の幅いっぱいに広げる
                    Layout.leftMargin: 20                      // 説明より字下げする
                    visible: (modelData.details || "") !== ""  // 詳細がある場合のみ表示する
                    text: modelData.details || ""              // 問題の詳細
                    textFormat: Text.PlainText                 // ソルバーが返した文字列をHTMLとして解釈させない
                    color: palette.placeholderText             // 補足情報として控えめな文字色
                    wrapMode: Text.WordWrap                    // 幅に収まらない場合は単語単位で折り返す
                }

                // 解決策の選択肢を示す見出し
                Label {
                    Layout.topMargin: 4                 // 上側の間隔
                    text: qsTr("Conflict resolution:")  // 見出しの文字列
                }

                // 解決策の選択肢を並べるリスト
                Repeater {
                    model: modelData.solutions || []  // この問題の解決策の一覧

                    // 解決策1件分の選択ボタンと詳細表示
                    ColumnLayout {
                        required property int index      // 解決策の番号
                        required property var modelData  // 解決策データ

                        // この解決策が属する問題の番号
                        property int problemIdx: conflictDelegate.index  // 親の問題の番号

                        Layout.fillWidth: true  // 親の幅いっぱいに広げる
                        spacing: 2              // 選択ボタンと詳細の間隔

                        // 解決策の選択ボタン
                        RadioButton {
                            id: solutionRadio  // 選択ボタンの参照名

                            Layout.fillWidth: true                                                  // 親の幅いっぱいに広げる
                            Layout.leftMargin: 12                                                   // 見出しより字下げする
                            text: (parent.index + 1) + ": " + (parent.modelData.description || "")  // 番号付きの解決策の説明
                            // 選択中の解決策に対応するボタンだけをチェック状態にする
                            checked: {
                                var sel = conflictDialog.selectedSolutions
                                var pi = parent.problemIdx
                                return sel && pi < sel.length && sel[pi] === parent.index
                            }

                            // 選択を更新する (変更検出のため新しい配列を代入する)
                            onClicked: {
                                var sel = conflictDialog.selectedSolutions.slice()
                                sel[parent.problemIdx] = parent.index
                                conflictDialog.selectedSolutions = sel
                            }

                            // 解決策の説明はソルバーが返したメタデータ由来のためPlainTextで表示する
                            contentItem: Text {
                                leftPadding: solutionRadio.indicator.width + solutionRadio.spacing  // 選択マークの右側から表示する
                                text: solutionRadio.text                                            // 選択ボタンの文字列
                                textFormat: Text.PlainText                                          // HTMLとして解釈させない
                                color: solutionRadio.palette.windowText                             // 通常の文字色
                                font: solutionRadio.font                                            // 選択ボタンと同じフォント
                                verticalAlignment: Text.AlignVCenter                                // 上下中央に揃える
                                elide: Text.ElideRight                                              // 幅に収まらない場合は末尾を省略する
                            }
                        }

                        // 解決策で実行される具体的な操作の説明
                        Label {
                            Layout.fillWidth: true                            // 親の幅いっぱいに広げる
                            Layout.leftMargin: 48                             // 選択ボタンの文字列の位置に揃えて字下げする
                            visible: (parent.modelData.details || "") !== ""  // 詳細がある場合のみ表示する
                            text: parent.modelData.details || ""              // 解決策の詳細
                            textFormat: Text.PlainText                        // HTMLとして解釈させない
                            color: palette.placeholderText                    // 補足情報として控えめな文字色
                            font.pixelSize: 13                                // 文字サイズ
                            wrapMode: Text.WordWrap                           // 幅に収まらない場合は単語単位で折り返す
                        }
                    }
                }

                // 問題間の区切り線
                Rectangle {
                    Layout.fillWidth: true  // 親の幅いっぱいに広げる
                    Layout.topMargin: 4     // 上側の間隔
                    height: 1               // 区切り線の太さ
                    opacity: 0.5            // 区切り線を控えめに表示する
                    color: palette.mid      // 区切り線の色
                }
            }
        }

        // 操作ボタンを並べる行
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 8              // ボタンの間隔

            // バグ報告用に現在のソルバー状態をテストケースとして保存する
            Button {
                enabled: !PackageController.busy                                               // 処理中は保存を無効化する
                activeFocusOnTab: true                                                         // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Save Solver Testcase")                                             // ボタンの文字列
                Accessible.name: text                                                          // スクリーンリーダー向けの名前 (ボタンの文字列と同じ)
                Accessible.description: qsTr("Save the current solver state for bug reports")  // スクリーンリーダー向けの説明

                // PackageController::saveSolverTestcase()でテストケースの保存を要求する
                onClicked: {
                    PackageController.saveSolverTestcase()
                }
            }

            // 保存ボタンと実行ボタンを左右に分ける余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [OK -- Try Again (O)]ボタン
            Button {
                text: qsTr("OK -- Try Again (O)")  // ボタンの文字列

                // 選択した解決策を適用し、呼び出し元に応じて再試行する
                onClicked: {
                    // 選択された解決策をすべて適用する
                    var sel = conflictDialog.selectedSolutions
                    for (var i = 0; i < conflictDialog.problems.length; i++) {
                        var solIdx = (sel && i < sel.length) ? sel[i] : 0
                        PackageController.applySolution(i, solIdx)
                    }
                    conflictDialog.close()

                    // 解決結果はMain.qmlのonResolveFinishedで受け取る
                    if (conflictDialog.fromApply) {
                        // [適用]から開かれた場合は依存関係を再解決してコミットへ進む
                        conflictDialog.applyChangesRequested()
                    }
                    else {
                        // チェックボックス操作から開かれた場合はソルバーを再実行する
                        conflictDialog.applyPendingCommitCleared()
                        PackageController.resolveDependencies()
                    }
                }
            }

            // [Cancel (C)]ボタン
            Button {
                text: qsTr("Cancel (C)")  // ボタンの文字列

                // 変更を取り消してダイアログを閉じる
                onClicked: {
                    // 保留中のコミットを解除し、Main.qmlが中断した適用フローを再開しないようにする
                    conflictDialog.applyPendingCommitCleared()
                    // ステータス変更を元に戻す (内部でパッケージリスト再取得も実行、表示集合と表示位置は保持)
                    PackageController.restoreState()
                    conflictDialog.close()
                }
            }
        }
    }
}
