/**
 * @file RepositoryView.qml
 * @brief リポジトリ単位でパッケージを絞り込むビュー
 *
 * Main.qmlのStackLayout内で直接生成されるPaneで、リポジトリ一覧と選択中のリポジトリを表示する
 * 表示の切替はMain.qmlのfilterTabBarの選択インデックスで行う
 *
 * 入力 : なし
 * 出力 : packageLoadStarted (リポジトリ選択によるパッケージ読込の開始)
 * 依存 : PackageController (repos、connected、busy、upgradeRepos、loadPackagesByRepo()、setRepoUpgrade())
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// [Repository]タブで表示するビュー本体
Pane {
    id: repositoryView  // リポジトリビュー自身の参照名

    // リポジトリ選択でパッケージ読込を開始したことを通知する (Main.qmlのウェイトポップアップ用)
    signal packageLoadStarted()

    // 一覧で選択中のリポジトリ (PackageController.reposの1件分)。未選択・範囲外ならnull
    readonly property var selectedRepo: repoListView.currentIndex >= 0 && repoListView.currentIndex < PackageController.repos.length
                                        ? PackageController.repos[repoListView.currentIndex] : null

    // 利用者が選択したリポジトリのエイリアス。未選択なら空
    // 一覧の再読込後に同じリポジトリを選び直すための目印で、行番号は再読込で別のリポジトリを指しうるため使えない
    property string selectedAlias: ""

    padding: 0  // リスト表示を枠いっぱいに広げるため余白なし

    // 再読込後の一覧から選択済みのエイリアスを探し、その行を選択し直す。見つからなければ未選択に戻す
    // ListViewはモデルが差し替わっても行番号をそのまま保持するため、並びや件数が変わると別のリポジトリが選択扱いになる。
    // エイリアスで選び直せば、強調表示・切り替えボタン・表示中のパッケージ一覧が同じリポジトリを指し続ける
    function reselectByAlias() {
        var repos = PackageController.repos
        var found = -1

        if (selectedAlias !== "") {
            for (var i = 0; i < repos.length; ++i) {
                if (repos[i].alias === selectedAlias) {
                    found = i
                    break
                }
            }
        }

        if (found < 0) {
            selectedAlias = ""  // 一覧から消えたリポジトリは選択対象から外す
        }

        repoListView.currentIndex = found
    }

    // 一覧と切り替え操作のボタンを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ビュー全体を使う
        spacing: 0            // 一覧とボタン領域を隙間なく接する

        // リポジトリ名の一覧 (modelはC++側のQVariantList)
        ListView {
            id: repoListView  // リポジトリ一覧の参照名

            Layout.fillWidth: true                                        // 幅いっぱいに広げる
            Layout.fillHeight: true                                       // ボタンの残りの高さをすべて使う
            clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
            currentIndex: -1                                              // 利用者が選ぶまでは未選択にする (既定の0だと先頭行が選択扱いになり切り替えボタンが表示される)
            model: PackageController.repos                                // PackageController::reposのリポジトリ一覧 (reposChangedで更新)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

            // 一覧の差し替えが済んだ後で選択を復元する
            // reposChangedを直接受けると、ListView側のモデル更新より先に実行されて行番号を上書きされる恐れがあるため、モデル差し替え通知を使う
            onModelChanged: repositoryView.reselectByAlias()

            // リポジトリ1件分の行
            delegate: ItemDelegate {
                id: repoDelegate  // 行自身の参照名

                width: repoListView.width                      // 一覧の幅に合わせる
                text: modelData.name || modelData.alias || ""  // 表示名 (名前がなければエイリアス)
                highlighted: ListView.isCurrentItem            // 選択中の行を強調表示する

                // 選択したリポジトリのパッケージだけを読み込ませる
                // PackageController::loadPackagesByRepoを呼び出し、結果はpackagesChangedで一覧へ反映される
                onClicked: {
                    // 未接続/処理中は読込要求が破棄されるため、選択行も変えない (選択行と一覧の内容がずれるのを防ぐ)
                    if (!PackageController.connected || PackageController.busy) {
                        return
                    }

                    repoListView.currentIndex = index
                    repositoryView.selectedAlias = modelData.alias  // 再読込後も同じリポジトリを選び直せるように覚えておく
                    PackageController.loadPackagesByRepo(modelData.alias)
                    repositoryView.packageLoadStarted()
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

        // 選択中のリポジトリへシステムのパッケージを切り替える (すでに切り替え対象のリポジトリでは表示しない)
        // 無効なリポジトリは切り替え対象にできないため、ボタンも無効にする
        Button {
            Layout.fillWidth: true  // 幅いっぱいに広げる
            Layout.margins: 8       // 周囲の余白
            visible: repositoryView.selectedRepo !== null
                     && PackageController.upgradeRepos.indexOf(repositoryView.selectedRepo.alias) < 0  // 選択中で未切り替えのときだけ表示する
            enabled: PackageController.connected && !PackageController.busy
                     && repositoryView.selectedRepo !== null && repositoryView.selectedRepo.enabled === true  // 未接続・処理中・無効なリポジトリでは押せない
            activeFocusOnTab: true  // [Tab]キーでフォーカスを移動できるようにする
            text: qsTr("Switch system packages to the versions in this repository")  // ボタンの文字列

            // 切り替えを有効にして依存関係を解決し直す
            onClicked: {
                PackageController.setRepoUpgrade(repositoryView.selectedRepo.alias, true)
            }
        }

        // 切り替え対象のリポジトリごとに取り消しボタンを表示する
        Repeater {
            model: PackageController.upgradeRepos  // 切り替え対象リポジトリのエイリアス一覧

            // 取り消しボタン1件分
            Button {
                id: cancelSwitchButton  // 取り消しボタン自身の参照名

                required property string modelData  // 切り替え対象リポジトリのエイリアス

                // 表示名: reposに同じエイリアスがあればその名前、なければエイリアス
                readonly property string displayName: {
                    var repos = PackageController.repos
                    for (var i = 0; i < repos.length; ++i) {
                        if (repos[i].alias === modelData) {
                            return repos[i].name || modelData
                        }
                    }
                    return modelData
                }

                Layout.fillWidth: true  // 幅いっぱいに広げる
                Layout.margins: 8       // 周囲の余白
                enabled: PackageController.connected && !PackageController.busy  // 未接続・処理中は押せない
                activeFocusOnTab: true  // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Cancel switching to %1").arg(displayName)  // ボタンの文字列

                // 表示名はメタデータ由来のためPlainTextで表示する
                // (既定のIconLabelはAutoTextでHTMLを解釈する)
                contentItem: Label {
                    text: cancelSwitchButton.text                          // ボタンに設定した文字列
                    textFormat: Text.PlainText                             // HTMLとして解釈させない
                    font: cancelSwitchButton.font                          // ボタンのフォントを引き継ぐ
                    color: cancelSwitchButton.palette.buttonText           // 無効時は無効用パレットの文字色になる
                    elide: Text.ElideRight                                 // 長い名前は末尾を省略する
                    horizontalAlignment: Text.AlignHCenter                 // 既定のボタンと同じく中央に配置する
                    verticalAlignment: Text.AlignVCenter                   // 高さ方向の中央に配置する
                }

                // 切り替えを取り消して依存関係を解決し直す
                onClicked: {
                    PackageController.setRepoUpgrade(modelData, false)
                }
            }
        }
    }
}
