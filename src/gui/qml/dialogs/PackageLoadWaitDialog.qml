/**
 * @file PackageLoadWaitDialog.qml
 * @brief パッケージ一覧の読込中に待機を示すモーダルダイアログ
 *
 * Main.qmlからstart()で読込の開始を通知され、一定時間 (showDelay) を超えた読込でのみ開く
 * 短時間で終わる読込ではポップアップを出さず、画面のちらつきを防ぐ
 * 一度開いた場合は最短表示時間 (minimumShowTime) が経過するまで閉じず、表示直後に消える点滅を防ぐ
 *
 * 入力 : start()による読込開始の通知、PackageControllerの処理中状態
 * 出力 : なし
 * 依存 : PackageController (busy / statusMessage)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// パッケージ一覧読込中のウェイトダイアログ
Dialog {
    id: packageLoadWaitDialog  // ダイアログ自身の参照名

    property int showDelay: 500        // 表示を開始するまでの待機時間 (ミリ秒)。これより早く終わる読込では表示しない
    property int minimumShowTime: 300  // 一度表示した場合に閉じるまでの最短時間 (ミリ秒)
    property bool waiting: false       // 読込の完了 (PackageController.busyの解除) を待っているか

    /**
     * @brief 読込の開始を通知し、表示までの待機を始める
     * @note 呼び出し側は読込要求が受理された (PackageController.busyがtrueになった) 後に呼ぶこと
     */
    function start() {
        // 未接続時はDBusClientが応答を同期で返すため、呼び出し時点で読込が完了している場合がある
        // その場合はbusyの変化通知が既に過ぎているので、待機状態にせず終了させる
        if (!PackageController.busy) {
            packageLoadWaitDialog.finish()
            return
        }

        packageLoadWaitDialog.waiting = true
        delayTimer.restart()
    }

    /**
     * @brief 読込の完了を受けて表示待ちを取り消し、最短表示時間を過ぎていれば閉じる
     * @note 最短表示時間内の場合はminimumTimerの満了時に閉じる
     */
    function finish() {
        packageLoadWaitDialog.waiting = false
        delayTimer.stop()

        if (!minimumTimer.running) {
            packageLoadWaitDialog.close()
        }
    }

    anchors.centerIn: parent          // 親ウィンドウの中央に表示する
    width: 320                        // ダイアログの幅
    standardButtons: Dialog.NoButton  // 読込が終わるまで閉じさせないためボタンは表示しない
    modal: true                       // 表示中は背後の画面を操作できないようにする
    closePolicy: Dialog.NoAutoClose   // [Esc]キーや外側のクリックでは閉じない

    // ダイアログ本体 (処理中インジケータとメッセージ)
    contentItem: RowLayout {
        spacing: 12  // インジケータとメッセージの間隔

        // 処理中インジケータ
        BusyIndicator {
            Layout.preferredWidth: 32                // インジケータの幅
            Layout.preferredHeight: 32               // インジケータの高さ
            running: packageLoadWaitDialog.visible   // ダイアログ表示中のみ回転させる
        }

        // 読込中であることを示すメッセージ (C++側で翻訳済みの状態メッセージを使う)
        Label {
            Layout.fillWidth: true                 // 残りの幅をすべて使う
            text: PackageController.statusMessage  // PackageController::statusMessageの状態メッセージ
            textFormat: Text.PlainText             // HTMLとして解釈させない
            wrapMode: Text.WordWrap                // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // 表示開始までの待機タイマ
    Timer {
        id: delayTimer  // 表示待機タイマの参照名

        interval: packageLoadWaitDialog.showDelay  // 表示を開始するまでの待機時間

        // 待機時間を過ぎても読込が続いている場合のみ開き、最短表示時間の計測を始める
        onTriggered: {
            if (packageLoadWaitDialog.waiting && PackageController.busy) {
                packageLoadWaitDialog.open()
                minimumTimer.restart()
            }
        }
    }

    // 最短表示時間のタイマ
    Timer {
        id: minimumTimer  // 最短表示タイマの参照名

        interval: packageLoadWaitDialog.minimumShowTime  // 一度表示した場合に閉じるまでの最短時間

        // 最短表示時間が過ぎた時点で読込が終わっていれば閉じる (続いている場合はfinish()で閉じる)
        onTriggered: {
            if (!packageLoadWaitDialog.waiting) {
                packageLoadWaitDialog.close()
            }
        }
    }

    // PackageControllerの処理中状態に追従して閉じる
    Connections {
        target: PackageController  // 状態変化を監視するPackageController

        /** @brief 読込の完了 (切断による解除を含む) で表示待ちを終える */
        function onBusyChanged() {
            if (packageLoadWaitDialog.waiting && !PackageController.busy) {
                packageLoadWaitDialog.finish()
            }
        }
    }
}
