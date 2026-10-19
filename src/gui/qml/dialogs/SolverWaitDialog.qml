/**
 * @file SolverWaitDialog.qml
 * @brief 依存関係ソルバの処理中に待機を示すモーダルダイアログ
 *
 * Main.qmlから開かれ、ソルバ完了まで追加操作を防ぐ
 *
 * 入力 : 処理中状態とバックエンドの操作状態
 * 出力 : なし
 * 依存 : PackageController (busy / operationState)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// ソルバー実行中のウェイトダイアログ
// バックエンドが依存解決段階を実行中のときだけ開き、PackageController.busyの解除で閉じる
// 一覧の読込など依存解決を伴わない処理では開かない
Dialog {
    id: solverWaitDialog  // ダイアログ自身の参照名

    // 他ダイアログ (起動/コミット/コンフリクト/リフレッシュ) 表示中は開かないためのフラグ
    property bool blocked: false  // 他ダイアログ表示中のため抑止する

    /**
     * @brief ビジー状態と操作状態から開閉を決める
     * @note 一度開いたらビジー解除まで閉じない (依存解決後の再読込で点滅させないため)
     * @note 前の操作の終了処理中 (finishing) の状態では開かない
     */
    function updateVisibility() {
        // 処理が終わった場合は閉じる
        if (!PackageController.busy) {
            solverWaitDialog.close()
            return
        }
        const s = PackageController.operationState
        // 未表示かつ抑止されておらず、バックエンドが依存解決段階を実行中の場合のみ開く
        if (!solverWaitDialog.visible && !solverWaitDialog.blocked
                && s && s.state === "running" && s.stage === "resolving") {
            solverWaitDialog.open()
        }
    }

    anchors.centerIn: parent                  // 親ウィンドウの中央に表示する
    width: 320                                // ダイアログの幅
    title: qsTr("Resolving dependencies...")  // ダイアログのタイトル
    standardButtons: Dialog.NoButton          // 依存解決が終わるまで閉じさせないためボタンは表示しない
    modal: true                               // 表示中は背後の画面を操作できないようにする
    closePolicy: Dialog.NoAutoClose           // [Esc]キーや外側のクリックでは閉じない

    // ダイアログ本体 (処理中インジケータとメッセージ)
    contentItem: RowLayout {
        spacing: 12  // インジケータとメッセージの間隔

        // 処理中インジケータ
        BusyIndicator {
            Layout.preferredWidth: 32          // インジケータの幅
            Layout.preferredHeight: 32         // インジケータの高さ
            running: solverWaitDialog.visible  // ダイアログ表示中のみ回転させる
        }

        // 依存解決中であることを示すメッセージ
        Label {
            Layout.fillWidth: true                                 // 残りの幅をすべて使う
            text: qsTr("Resolving dependencies...\nPlease wait.")  // 依存解決中のメッセージ
            wrapMode: Text.WordWrap                                // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // PackageControllerの状態変化に追従して開閉する
    Connections {
        target: PackageController  // 状態変化を監視するPackageController

        /** @brief ビジー状態の変化に追従する */
        function onBusyChanged() {
            solverWaitDialog.updateVisibility()
        }

        /** @brief バックエンドの操作段階の変化に追従する */
        function onOperationStateChanged() {
            solverWaitDialog.updateVisibility()
        }
    }
}
