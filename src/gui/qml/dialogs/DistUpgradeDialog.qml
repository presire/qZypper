/**
 * @file DistUpgradeDialog.qml
 * @brief ディストリビューションアップグレードの確認を行うダイアログ
 *
 * Main.qmlから開かれ、広範囲な更新前に明示確認を求める
 *
 * 入力 : アップグレード対象の概要
 * 出力 : 実行または取消の要求
 * 依存 : PackageController (distUpgrade API)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// ディストリビューション更新の確認ダイアログ
// ベンダー変更を許可するかを選んでからdist-upgradeを開始する
Dialog {
    id: distUpgradeDialog  // ダイアログ自身の参照名

    // 承認時にallowVendorChangeを添えて通知
    signal confirmed(bool allowVendorChange)  // 承認の通知 (allowVendorChange付き)

    anchors.centerIn: parent                                   // 親ウィンドウの中央に表示する
    width: Math.min((parent ? parent.width : 600) * 0.7, 520)  // 親幅の7割 (最大520px)
    title: qsTr("Distribution Upgrade")                        // ダイアログのタイトル
    standardButtons: Dialog.NoButton                           // 標準ボタンは使わず、独自の[Cancel]・[Start Upgrade]ボタンを使う
    modal: true                                                // 表示中は背後の画面を操作できないようにする

    // 説明文、ベンダー変更の選択、操作ボタンを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 10           // 項目の間隔

        // ディストリビューション更新の影響を説明する文
        Label {
            Layout.fillWidth: true   // 親の幅いっぱいに広げる
            // 更新・削除・ベンダー変更が起こり得ることの説明
            text: qsTr("A distribution upgrade resolves the entire system to the latest available versions. Packages may be installed, updated, removed, or switched to a different vendor.")
            wrapMode: Text.WordWrap  // 幅に収まらない場合は単語単位で折り返す
        }

        // ベンダー変更を許可するかの選択
        CheckBox {
            id: vendorChangeCheck  // ベンダー変更チェックボックスの参照名

            activeFocusOnTab: true                        // [Tab]キーでフォーカスを移動できるようにする
            text: qsTr("Allow vendor change")             // チェックボックスの文字列
            Accessible.name: qsTr("Allow vendor change")  // スクリーンリーダー向けの名前
            // スクリーンリーダー向けの説明
            Accessible.description: qsTr("Permit packages to switch to a different vendor during the upgrade")
        }

        // 操作ボタンを右寄せで横に並べる
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 8              // ボタンの間隔

            // ボタンを右寄せにするための余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [Cancel]ボタン
            Button {
                activeFocusOnTab: true           // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Cancel")             // ボタンの文字列
                Accessible.name: qsTr("Cancel")  // スクリーンリーダー向けの名前

                // 更新を開始せずにダイアログを閉じる
                onClicked: {
                    distUpgradeDialog.reject()
                }
            }

            // [Start Upgrade]ボタン
            Button {
                enabled: !PackageController.busy                     // 処理中は開始を無効化する
                activeFocusOnTab: true                               // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Start Upgrade")                          // ボタンの文字列
                highlighted: true                                    // 推奨操作として強調表示する
                Accessible.name: qsTr("Start distribution upgrade")  // スクリーンリーダー向けの名前

                // 承認後にベンダー変更の指定を添えて通知する
                onClicked: {
                    var allowVendorChange = vendorChangeCheck.checked
                    distUpgradeDialog.accept()
                    distUpgradeDialog.confirmed(allowVendorChange)
                }
            }
        }
    }
}
