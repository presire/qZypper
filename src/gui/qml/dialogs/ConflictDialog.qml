/**
 * @file ConflictDialog.qml
 * @brief 依存関係の競合と選択可能な解決策を表示するダイアログ
 *
 * CommitProgressDialogまたはMain.qmlから開かれる
 *
 * 入力 : conflict (競合情報), 解決候補
 * 出力 : 選択した解決策
 * 依存 : PackageController (libzyppソルバの競合情報)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// 依存関係の競合ダイアログ (問題ごとに解決策を1つ選んで再試行する)
Dialog {
    id: conflictDialog  // ダイアログ自身の参照名

    // ソルバが報告した競合の一覧
    property var problems: []           // 競合の一覧
    // 各問題で選択された解決策のインデックスを保持
    property var selectedSolutions: []  // 選択中の解決策
    // applyChanges()から呼ばれたかどうか
    property bool fromApply: false      // 適用処理からの起動か

    signal applyChangesRequested()      // 変更適用の続行を要求する
    signal applyPendingCommitCleared()  // 保留コミットを消去したことを通知する

    anchors.centerIn: parent                                      // 親ウィンドウの中央に表示する
    width: Math.min((parent ? parent.width : 750) * 0.85, 750)    // 親幅の85% (最大750px)
    height: Math.min((parent ? parent.height : 550) * 0.75, 550)  // 親の高さの75% (最大550px)
    title: qsTr("Warning")                                        // ダイアログのタイトル
    standardButtons: Dialog.NoButton                              // 標準ボタンは使わず、ボタン行の独自ボタンを使う
    modal: true                                                   // 表示中は背後の画面を操作できないようにする

    // [Esc]キーなどで閉じられた場合
    onRejected: {
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

    // 競合の一覧とボタン行を縦に並べる
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
            model: conflictDialog.problems  // ソルバが報告した競合の一覧

            // 問題1件分の表示 (説明、詳細、解決策の選択肢)
            delegate: ColumnLayout {
                id: conflictDelegate  // 問題1件分の参照名

                required property int index      // 問題の行番号
                required property var modelData  // 問題データ

                width: conflictListView.width - 12  // 一覧の幅から右側の余白を除いた幅

                // 問題の説明 (⚠アイコン付き)
                Label {
                    Layout.fillWidth: true                           // 親の幅いっぱいに広げる
                    text: "\u26A0 " + (modelData.description || "")  // 警告アイコンと問題の説明
                    textFormat: Text.PlainText                       // ソルバ由来の文字列をHTMLとして解釈させない
                    font.bold: true                                  // 太字で表示する
                    wrapMode: Text.WordWrap                          // 幅に収まらない場合は単語単位で折り返す
                }

                // 詳細
                Label {
                    Layout.fillWidth: true                     // 親の幅いっぱいに広げる
                    Layout.leftMargin: 20                      // 説明より字下げする
                    // 詳細がある場合のみ表示する
                    visible: (modelData.details || "") !== ""  // 詳細がある場合のみ表示する
                    text: modelData.details || ""              // 問題の詳細
                    textFormat: Text.PlainText                 // ソルバ由来の文字列をHTMLとして解釈させない
                    color: palette.placeholderText             // 補足情報として控えめな文字色
                    wrapMode: Text.WordWrap                    // 幅に収まらない場合は単語単位で折り返す
                }

                // [競合の解決方法:]ラベル
                Label {
                    Layout.topMargin: 4                 // 上側の間隔
                    text: qsTr("Conflict resolution:")  // 見出しの文字列
                }

                // 解決策リスト (RadioButton + details)
                Repeater {
                    model: modelData.solutions || []  // この問題の解決策の一覧

                    // 解決策1件分の表示 (選択ボタンと詳細)
                    ColumnLayout {
                        required property int index      // 解決策の行番号
                        required property var modelData  // 解決策データ

                        // 所属する問題のインデックス
                        property int problemIdx: conflictDelegate.index  // 親の問題の番号

                        Layout.fillWidth: true  // 親の幅いっぱいに広げる
                        spacing: 2              // 選択ボタンと詳細の間隔

                        // 解決策の選択ボタン
                        RadioButton {
                            id: solutionRadio  // 選択ボタンの参照名

                            Layout.fillWidth: true                                                  // 親の幅いっぱいに広げる
                            Layout.leftMargin: 12                                                   // 見出しより字下げする
                            text: (parent.index + 1) + ": " + (parent.modelData.description || "")  // 番号付きの解決策の説明
                            // 選択中の解決策をチェック表示する
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

                            // 解決策の説明はソルバー (メタデータ) 由来のためPlainTextで表示する
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

                        // 解決策の詳細 (具体的なアクション内容)
                        Label {
                            Layout.fillWidth: true                            // 親の幅いっぱいに広げる
                            Layout.leftMargin: 48                             // 選択ボタンの文字列の位置に揃えて字下げする
                            // 詳細がある場合のみ表示する
                            visible: (parent.modelData.details || "") !== ""  // 詳細がある場合のみ表示する
                            text: parent.modelData.details || ""              // 解決策の詳細
                            textFormat: Text.PlainText                        // ソルバ由来の文字列をHTMLとして解釈させない
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
                    opacity: 0.5            // 控えめに表示する
                    color: palette.mid      // 区切り線の色
                }
            }
        }

        // ボタン行
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

            // ボタンを左右に分けるための余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [OK -- Try Again (O)]ボタン
            Button {
                text: qsTr("OK -- Try Again (O)")  // ボタンの文字列

                // 選択した解決策を適用し、呼び出し元に応じて再試行する
                onClicked: {
                    // 選択された解決策を適用
                    var sel = conflictDialog.selectedSolutions
                    for (var i = 0; i < conflictDialog.problems.length; i++) {
                        var solIdx = (sel && i < sel.length) ? sel[i] : 0
                        PackageController.applySolution(i, solIdx)
                    }
                    conflictDialog.close()

                    // 結果はonResolveFinishedで処理される
                    if (conflictDialog.fromApply) {
                        // 「適用」からの呼び出しの場合は再度依存関係解決してコミットへ進む
                        conflictDialog.applyChangesRequested()
                    }
                    else {
                        // チェックボックスからの呼び出しの場合はソルバー再実行
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
                    // ステータス変更を元に戻す (内部でパッケージリスト再取得も実行、表示集合と表示位置は保持)
                    PackageController.restoreState()
                    conflictDialog.close()
                }
            }
        }
    }
}
