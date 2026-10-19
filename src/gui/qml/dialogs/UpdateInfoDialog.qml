/**
 * @file UpdateInfoDialog.qml
 * @brief 利用可能なアプリ更新情報を表示するダイアログ
 *
 * Main.qmlの更新確認結果から開かれる
 *
 * 入力 : 更新バージョンと案内URL
 * 出力 : 外部URLを開く要求または閉じる操作
 * 依存 : PackageController (更新情報と外部URL起動)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// 全パッケージ更新結果通知ダイアログ
Dialog {
    id: updateInfoDialog  // ダイアログ自身の参照名

    property string message: ""  // 更新結果の通知メッセージ

    anchors.centerIn: parent            // 親ウィンドウの中央に表示する
    width: 400                          // ダイアログの幅
    title: qsTr("Update All Packages")  // ダイアログのタイトル
    standardButtons: Dialog.Ok          // [OK]ボタンのみ表示する
    modal: true                         // 表示中は背後の画面を操作できないようにする

    // 更新結果の通知メッセージ
    Label {
        anchors.left: parent.left       // ダイアログ内側の左端を基準にする
        anchors.right: parent.right     // ダイアログ内側の右端を基準にする
        text: updateInfoDialog.message  // 通知メッセージ
        textFormat: Text.PlainText      // HTMLとして解釈させない
        wrapMode: Text.WordWrap         // 幅に収まらない場合は単語単位で折り返す
    }
}
