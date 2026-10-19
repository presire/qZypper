/**
 * @file SolverTestcaseDialog.qml
 * @brief 依存関係ソルバのテストケース保存結果を表示するダイアログ
 *
 * Main.qmlまたはConflictDialogから開かれる
 *
 * 入力 : 保存先と成功・失敗の結果
 * 出力 : 閉じる操作
 * 依存 : PackageController (ソルバテストケースAPI)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ソルバーテストケースの保存先を表示する情報ダイアログ
// パスは選択してコピーできる
Dialog {
    id: testcaseDialog  // ダイアログ自身の参照名

    property string testcasePath: ""  // 保存されたテストケースのファイルパス

    anchors.centerIn: parent                                   // 親ウィンドウの中央に表示する
    width: Math.min((parent ? parent.width : 600) * 0.7, 560)  // 親幅の7割 (最大560) に収めて狭い画面でもはみ出さない
    title: qsTr("Solver Testcase")                             // ダイアログのタイトル
    standardButtons: Dialog.Ok                                 // [OK]ボタンのみ表示する
    modal: true                                                // 表示中は背後の画面を操作できないようにする

    // 案内メッセージと保存先パスを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 8            // 項目の間隔

        // 保存先の案内メッセージ
        Label {
            Layout.fillWidth: true                                // 親の幅いっぱいに広げる
            text: qsTr("The solver testcase has been saved to:")  // 案内メッセージ
            wrapMode: Text.WordWrap                               // 幅に収まらない場合は単語単位で折り返す
        }

        // 保存先パスと[Copy]ボタンを横に並べる
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 6              // パス欄とボタンの間隔

            // 保存先パス (選択してコピーできる読み取り専用の欄)
            TextField {
                id: pathField  // パス欄の参照名

                Layout.fillWidth: true                                                  // 残りの幅をすべて使う
                activeFocusOnTab: true                                                  // [Tab]キーでフォーカスを移動できるようにする
                text: testcaseDialog.testcasePath                                       // 保存先のファイルパス
                readOnly: true                                                          // 表示専用で編集させない
                selectByMouse: true                                                     // マウスで文字列を選択できるようにする
                Accessible.name: qsTr("Testcase file path")                             // スクリーンリーダー向けの名前
                Accessible.description: qsTr("Path of the saved solver testcase file")  // スクリーンリーダー向けの説明
            }

            // [Copy]ボタン
            Button {
                activeFocusOnTab: true                           // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Copy")                               // ボタンの文字列
                Accessible.name: qsTr("Copy path to clipboard")  // スクリーンリーダー向けの名前

                // パス全体を選択してクリップボードへコピーする
                onClicked: {
                    pathField.selectAll()
                    pathField.copy()
                }
            }
        }
    }
}
