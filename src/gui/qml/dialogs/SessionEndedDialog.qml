/**
 * @file SessionEndedDialog.qml
 * @brief バックエンドD-Busセッションが終了したことを知らせるダイアログ
 *
 * Main.qmlでPackageControllerの接続状態変化を受けて開かれる
 *
 * 入力 : セッション終了理由
 * 出力 : 再接続または終了操作
 * 依存 : PackageController (D-Bus接続状態)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 編集セッション解放 (ReleaseSession) 後に再開始を促すダイアログ
Dialog {
    id: sessionEndedDialog  // ダイアログ自身の参照名

    signal startSessionRequested()  // 新規セッション開始の要求
    signal quitRequested()          // アプリ終了の要求

    anchors.centerIn: parent          // 親ウィンドウの中央に表示する
    width: 420                        // ダイアログの幅
    title: qsTr("Session Ended")      // ダイアログのタイトル
    standardButtons: Dialog.NoButton  // 標準ボタンは使わず、フッタの独自ボタンを使う
    modal: true                       // 表示中は背後の画面を操作できないようにする
    closePolicy: Dialog.NoAutoClose   // [Esc]キーや外側のクリックでは閉じない (必ずボタンで選択させる)

    // ダイアログ本体 (セッション終了の案内)
    contentItem: ColumnLayout {
        spacing: 12  // 項目の間隔

        // セッション終了の案内メッセージ
        Label {
            Layout.fillWidth: true                                                                 // 親の幅いっぱいに広げる
            text: qsTr("The package editing session has ended. Start a new session to continue.")  // 案内メッセージ
            wrapMode: Text.WordWrap                                                                // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // フッタ ([Start Session]ボタンと[Quit]ボタン)
    footer: Item {
        implicitHeight: 48  // フッタの高さ

        // ボタンを右寄せで横に並べる
        RowLayout {
            anchors.right: parent.right                    // フッタの右端に配置する
            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
            spacing: 8                                     // ボタンの間隔

            // [Start Session]ボタン
            Button {
                text: qsTr("Start Session")  // ボタンの文字列
                highlighted: true            // 推奨操作として強調表示する

                // 新規セッションの開始を要求する
                onClicked: {
                    // 開始に失敗した場合に再試行できるよう、このダイアログは閉じない
                    // 接続に成功した時点でMain.qmlが閉じる
                    sessionEndedDialog.startSessionRequested()
                }
            }

            // [Quit]ボタン
            Button {
                text: qsTr("Quit")  // ボタンの文字列

                // ダイアログを閉じて、アプリケーションの終了を要求する
                onClicked: {
                    sessionEndedDialog.close()
                    sessionEndedDialog.quitRequested()
                }
            }
        }
    }
}
