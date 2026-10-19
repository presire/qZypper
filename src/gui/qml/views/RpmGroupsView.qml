/**
 * @file RpmGroupsView.qml
 * @brief RPMグループの階層でパッケージを絞り込むビュー
 *
 * Main.qmlの左ペインのStackLayout内で生成され、[RPM Groups]タブの選択時に表示される
 * グループを階層の深さに応じて字下げして並べ、選択したグループ (配下を含む) のパッケージを右ペインの一覧へ読み込ませる
 *
 * 入力 : なし
 * 出力 : packageLoadStarted() シグナル (グループ選択によるパッケージ読込の開始)
 * 依存 : PackageController (rpmGroups、connected、busy、loadPackagesByRpmGroup())
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// [RPM Groups]タブで表示するビュー本体
Pane {
    id: rpmGroupsView  // RPMグループビュー自身の参照名

    signal packageLoadStarted()  // グループ選択でパッケージ読込を開始したことの通知 (Main.qmlのウェイトポップアップ用)

    padding: 0  // リスト表示を枠いっぱいに広げるため余白なし

    // RPMグループの一覧 (PackageController::rpmGroupsの{path, label, depth, count})
    ListView {
        id: groupListView  // グループ一覧の参照名

        anchors.fill: parent                                          // ビュー全体に一覧を広げる
        clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
        model: PackageController.rpmGroups                            // rpmGroupsChangedで更新されるグループ一覧
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

        // グループ1件分の行
        delegate: ItemDelegate {
            id: groupDelegate  // 行自身の参照名

            required property var modelData  // グループ1件分のデータ
            required property int index      // 一覧内の行番号

            width: groupListView.width                                                        // 一覧の幅に合わせる
            leftPadding: 8 + groupDelegate.modelData.depth * 16                               // 階層の深さに応じて字下げする
            text: groupDelegate.modelData.label + " (" + groupDelegate.modelData.count + ")"  // グループ名と件数
            highlighted: ListView.isCurrentItem                                               // 選択中の行を強調表示する

            // 選択したグループ (配下のグループを含む) のパッケージだけを読み込ませる
            // PackageController::loadPackagesByRpmGroupを呼び出し、結果はpackagesChangedで一覧へ反映される
            onClicked: {
                // 未接続/処理中は読込要求が破棄されるため、選択行も変えない (選択行と一覧の内容がずれるのを防ぐ)
                if (!PackageController.connected || PackageController.busy) {
                    return
                }

                groupListView.currentIndex = groupDelegate.index
                PackageController.loadPackagesByRpmGroup(groupDelegate.modelData.path)
                rpmGroupsView.packageLoadStarted()
            }

            // グループ名はメタデータ由来のためPlainTextで表示する
            // (既定のIconLabelはAutoTextでHTMLを解釈する)
            contentItem: Text {
                text: groupDelegate.text              // 行に設定した表示名
                textFormat: Text.PlainText            // HTMLとして解釈させない
                font: groupDelegate.font              // 行のフォントを引き継ぐ
                // 選択中の行は強調色の上に表示するため、文字色も強調表示用に切り替える
                color: groupDelegate.highlighted ? groupDelegate.palette.highlightedText : groupDelegate.palette.text
                elide: Text.ElideRight                // 長い名前は末尾を省略する
                verticalAlignment: Text.AlignVCenter  // 行の高さ方向の中央に配置する
            }
        }
    }

    // グループが無い場合 (未取得または旧版のバックエンド) の案内
    Label {
        anchors.centerIn: parent               // ビューの中央に表示する
        visible: groupListView.count === 0     // グループが無い場合のみ表示する
        text: qsTr("No RPM groups available")  // 案内の文字列
        opacity: 0.7                           // 補足情報として控えめに表示する
    }
}
