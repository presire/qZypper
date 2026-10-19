/**
 * @file RefreshOverlay.qml
 * @brief リポジトリリフレッシュ中に全画面を覆うモーダルオーバーレイ
 *
 * Main.qmlから開かれ、リフレッシュ完了まで他の操作をブロックする
 *
 * 入力 : cancellable (キャンセル操作を提供するか), cancelling (キャンセル要求済みか)
 * 出力 : なし
 * 依存 : PackageController (statusMessage, cancelRefresh(), refreshFinishedシグナル)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// リポジトリリフレッシュ中のモーダルオーバーレイ
Popup {
    id: refreshOverlay  // オーバーレイ自身の参照名

    property bool cancellable: true  // キャンセル操作を提供するか (サービス更新はlibzyppが途中中断に対応しないためfalseにする)
    property bool cancelling: false  // キャンセル要求を送信済みか

    /**
     * @brief キャンセル要求をC++側へ送る
     * @note PackageController::cancelRefreshを呼び出す
     *       実際の停止は現在のリポジトリ処理が終わってから行われる
     */
    function doCancel() {
        cancelling = true
        PackageController.cancelRefresh()
    }

    anchors.centerIn: parent              // 親ウィンドウの中央に配置する
    width: parent ? parent.width : 800    // 親の幅いっぱい (親が未確定のときのフォールバック)
    height: parent ? parent.height : 600  // 親の高さいっぱい (親が未確定のときのフォールバック)
    modal: true                           // 表示中は背後の画面を操作できないようにする
    closePolicy: Popup.NoAutoClose        // 完了シグナルを受け取るまで閉じさせない

    // 再度開いた時にキャンセル表示が残らないよう状態を戻す
    onClosed: {
        cancellable = true
        cancelling = false
    }

    // 半透明にして背後の一覧が操作対象でないことを示す
    background: Rectangle {
        color: palette.window  // ウィンドウと同じ背景色
        opacity: 0.85          // 背後がうっすら見える不透明度
    }

    // Popup内のShortcutはモーダル中でもウィンドウレベルで動作する
    Shortcut {
        sequence: "Escape"                                             // Escキーでキャンセルする
        enabled: refreshOverlay.visible && refreshOverlay.cancellable && !refreshOverlay.cancelling  // 表示中・キャンセル可能・未要求の場合のみ有効 (2重送信を防ぐ)

        // Escキーが押されたのでキャンセルを要求する
        onActivated: {
            refreshOverlay.doCancel()
        }
    }

    // オーバーレイの内容 (進捗表示とキャンセル操作)
    contentItem: Item {
        id: refreshOverlayRoot  // 内容領域の参照名

        // 進捗表示とキャンセル操作を中央にまとめる
        Column {
            anchors.centerIn: parent  // 内容領域の中央に配置する
            spacing: 16               // 各要素の縦方向の間隔

            // 処理中であることを示すインジケータ
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter  // 左右中央に配置する
                running: refreshOverlay.visible                    // オーバーレイの表示中だけ回転させる
            }

            // C++側が設定する現在の処理内容
            Label {
                anchors.horizontalCenter: parent.horizontalCenter  // 左右中央に配置する
                text: PackageController.statusMessage              // PackageControllerが通知する現在の処理内容
                textFormat: Text.PlainText                         // HTMLとして解釈させない
                font.pixelSize: 16                                 // 文字サイズ
            }

            // キャンセル操作の案内 (要求後はstatusMessageの"Cancelling... Please wait"で状況を示す)
            Label {
                anchors.horizontalCenter: parent.horizontalCenter                  // 左右中央に配置する
                visible: refreshOverlay.cancellable && !refreshOverlay.cancelling  // キャンセル不可の更新、または要求済みの場合は案内しない
                text: qsTr("Press Esc or button to cancel")                        // キャンセル操作の案内文
                opacity: 0.6                                       // 補足情報として控えめに表示する
                font.pixelSize: 13                                 // 文字サイズ
            }

            // [Cancel]ボタン
            Button {
                anchors.horizontalCenter: parent.horizontalCenter  // 左右中央に配置する
                visible: refreshOverlay.cancellable && !refreshOverlay.cancelling  // キャンセル不可または要求済みなら押下できないようにする
                text: qsTr("Cancel")                               // ボタンの表示文字列

                // [Cancel]ボタンが押されたのでキャンセルを要求する
                onClicked: {
                    refreshOverlay.doCancel()
                }
            }
        }
    }

    // キャンセル時も含め、完了シグナルで閉じる
    Connections {
        target: PackageController  // 完了シグナルの送信元

        /**
         * @brief 成功またはキャンセル後の完了通知でモーダルを閉じて操作を戻す
         * @param success リフレッシュの完了結果 (成功ならtrue)
         * @note refreshOverlayを閉じて操作を再開させる
         */
        function onRefreshFinished(success) {
            refreshOverlay.close()
        }
    }
}
