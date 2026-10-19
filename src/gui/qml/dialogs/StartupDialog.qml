/**
 * @file StartupDialog.qml
 * @brief 起動中の初期化状態を表示するダイアログ
 *
 * Main.qmlの初期化中に開かれる
 *
 * 入力 : 起動進捗メッセージ
 * 出力 : なし
 * 依存 : PackageController (D-Busバックエンド初期化状態)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// -- 起動時ウェイトダイアログ --
Dialog {
    id: startupDialog  // ダイアログ自身の参照名

    anchors.centerIn: parent          // 親ウィンドウの中央に表示する
    title: qsTr("Starting")           // ダイアログのタイトル
    standardButtons: Dialog.NoButton  // 初期化完了まで閉じさせないためボタンは表示しない
    modal: true                       // 表示中は背後の画面を操作できないようにする
    closePolicy: Dialog.NoAutoClose   // [Esc]キーや外側のクリックでは閉じない

    // メッセージと処理中インジケータを縦に並べる
    ColumnLayout {
        spacing: 16  // メッセージとインジケータの間隔

        // 初期化中であることを示すメッセージ
        Label {
            Layout.fillWidth: true                      // 親の幅いっぱいに広げる
            text: qsTr("Initializing. Please wait...")  // 初期化中のメッセージ
            wrapMode: Text.WordWrap                     // 幅に収まらない場合は単語単位で折り返す
        }

        // 処理中インジケータ
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter  // 左右中央に配置する
            Layout.preferredWidth: 48          // インジケータの幅
            Layout.preferredHeight: 48         // インジケータの高さ
            // 起動完了まで回し続ける
            running: true                      // 常に回転させる
        }
    }
}
