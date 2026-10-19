/**
 * @file RepositoryView.qml
 * @brief リポジトリ単位でパッケージを絞り込むビュー
 *
 * Main.qmlのStackLayout内で直接生成されるPaneで、リポジトリ一覧と選択中のリポジトリを表示する
 * 表示の切替はMain.qmlのfilterTabBarの選択インデックスで行う
 *
 * 入力 : なし
 * 出力 : なし
 * 依存 : PackageController (repos、loadPackagesByRepo())
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// [Repository]タブで表示するビュー本体
Pane {
    padding: 0  // リスト表示を枠いっぱいに広げるため余白なし

    // リポジトリ名の一覧 (modelはC++側のQVariantList)
    ListView {
        id: repoListView  // リポジトリ一覧の参照名

        anchors.fill: parent                                          // ビュー全体に一覧を広げる
        clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
        model: PackageController.repos                                // PackageController::reposのリポジトリ一覧 (reposChangedで更新)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

        // リポジトリ1件分の行
        delegate: ItemDelegate {
            id: repoDelegate  // 行自身の参照名

            width: repoListView.width                      // 一覧の幅に合わせる
            text: modelData.name || modelData.alias || ""  // 表示名 (名前がなければエイリアス)
            highlighted: ListView.isCurrentItem            // 選択中の行を強調表示する

            // 選択したリポジトリのパッケージだけを読み込ませる
            // PackageController::loadPackagesByRepoを呼び出し、結果はpackagesChangedで一覧へ反映される
            onClicked: {
                repoListView.currentIndex = index
                PackageController.loadPackagesByRepo(modelData.alias)
            }

            // リポジトリ名はメタデータ由来のためPlainTextで表示する
            // (既定のIconLabelはAutoTextでHTMLを解釈する)
            contentItem: Text {
                text: repoDelegate.text               // 行に設定した表示名
                textFormat: Text.PlainText            // HTMLとして解釈させない
                font: repoDelegate.font               // 行のフォントを引き継ぐ
                color: repoDelegate.highlighted ? repoDelegate.palette.highlightedText  // 選択中の行は強調色の上に表示するため、文字色も強調表示用に切り替える
                                                : repoDelegate.palette.text
                elide: Text.ElideRight                // 長い名前は末尾を省略する
                verticalAlignment: Text.AlignVCenter  // 行の高さ方向の中央に配置する
            }
        }
    }
}
