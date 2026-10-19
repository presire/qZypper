/**
 * @file PackageLockDialog.qml
 * @brief 指定パッケージのロック追加内容を入力するダイアログ
 *
 * LocksDialogまたはPackageDetailsPaneから開かれる
 *
 * 入力 : パッケージ名とロック候補
 * 出力 : ロック追加要求
 * 依存 : PackageController (パッケージロックAPI)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Zyppロック検出 (org.presire.qzypper.Error.ZyppLocked) 時のフレンドリーダイアログ
// 競合している外部パッケージマネージャを閉じた後に再試行できる
Dialog {
    id: lockDialog  // ダイアログ自身の参照名

    property string lockerName: ""     // 競合元プロセス名
    property int lockerPid: 0          // 競合元プロセスのPID
    property string lockerMessage: ""  // バックエンドが返す補足メッセージ

    signal retryRequested()  // 競合解消後の再試行要求

    anchors.centerIn: parent              // 親ウィンドウの中央に表示する
    width: 440                            // ダイアログの幅
    title: qsTr("Package System Locked")  // ダイアログのタイトル
    modal: true                           // 表示中は背後の画面を操作できないようにする

    // ダイアログ本体 (競合元の案内と補足メッセージ)
    contentItem: ColumnLayout {
        spacing: 12  // 項目の間隔

        // 競合元のプロセス名とPIDを示す案内メッセージ
        Label {
            Layout.fillWidth: true      // 親の幅いっぱいに広げる
            // 競合元のプロセス名 (不明な場合は[unknown]) とPIDを埋め込んだ案内文
            text: qsTr("Another package manager (%1, PID %2) is using the package system. Close it and retry.")
                  .arg(lockDialog.lockerName.length > 0 ? lockDialog.lockerName : qsTr("unknown"))
                  .arg(lockDialog.lockerPid)
            textFormat: Text.PlainText  // プロセス名をHTMLとして解釈させない
            wrapMode: Text.WordWrap     // 幅に収まらない場合は単語単位で折り返す
        }

        // バックエンドが返す補足メッセージ
        Label {
            Layout.fillWidth: true                        // 親の幅いっぱいに広げる
            // 補足メッセージがあるときだけ表示し、無いときは余白を作らない
            visible: lockDialog.lockerMessage.length > 0  // 補足メッセージがある場合のみ表示する
            opacity: 0.7                                  // 補足情報として控えめに表示する
            text: lockDialog.lockerMessage                // 補足メッセージ
            textFormat: Text.PlainText                    // HTMLとして解釈させない
            font.pixelSize: 12                            // 文字サイズ
            wrapMode: Text.WordWrap                       // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // フッタ ([Retry]ボタンと[Cancel]ボタン)
    footer: Item {
        implicitHeight: 48  // フッタの高さ

        // ボタンを右寄せで横に並べる
        RowLayout {
            anchors.right: parent.right                    // フッタの右端に配置する
            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
            spacing: 8                                     // ボタンの間隔

            // [Retry]ボタン
            Button {
                text: qsTr("Retry")  // ボタンの文字列
                highlighted: true    // 推奨操作として強調表示する

                // 再試行を要求する
                onClicked: {
                    // 先にダイアログを閉じ、閉じた状態で呼び出し側へ再試行を要求する
                    lockDialog.close()
                    lockDialog.retryRequested()
                }
            }

            // [Cancel]ボタン
            Button {
                text: qsTr("Cancel")  // ボタンの文字列

                // 再試行せずにダイアログを閉じる
                onClicked: {
                    lockDialog.close()
                }
            }
        }
    }
}
