/**
 * @file ServiceView.qml
 * @brief サービス単位でパッケージを絞り込むビュー
 *
 * Main.qmlの左ペインのStackLayout内で直接生成されるPaneで、サービス一覧を表示する
 * 一覧からサービスを選ぶと、そのサービスのリポジトリのパッケージだけを右ペインの一覧へ読み込む
 * サービスが1件もない場合は、一覧の代わりに案内文を表示する
 * 表示の切り替えはMain.qmlのfilterTabBarの選択インデックス ([Services]タブ) で行う
 *
 * 入力 : なし (サービス一覧はPackageController.servicesから取得する)
 * 出力 : packageLoadStarted() シグナル (サービス選択によるパッケージ読込の開始)
 * 依存 : PackageController (services、connected、busy、loadPackagesByService())
 */
import QtQuick
import QtQuick.Controls
import org.presire.qzypper.gui

// [Services]タブに表示するビュー本体
Pane {
    id: serviceView  // サービスビュー自身の参照名

    // サービス選択でパッケージ読込を開始したことを通知する (Main.qmlのウェイトポップアップ表示用)
    signal packageLoadStarted()  // サービス選択によるパッケージ読込の開始

    // 利用者が選択したサービスのエイリアス
    // 一覧の再読込後に同じサービスを選び直すための目印で、行番号は再読込で別のサービスを指しうるため使えない
    property string selectedAlias: ""  // 選択中のサービスのエイリアス (未選択なら空)

    padding: 0  // リスト表示を枠いっぱいに広げるため余白をなくす

    /**
     * @brief 再読込後のサービス一覧から選択済みのエイリアスを探し、その行を選択し直す
     * @note 再読込で並びや件数が変わると、行番号のままでは別のサービスが選択扱いになる
     *       エイリアスで選び直すことで、強調表示と右ペインのパッケージ一覧が同じサービスを指し続ける
     * @note 見つからなければselectedAliasを空に戻し、未選択 (currentIndex: -1) にする
     *       serviceListView.currentIndexを変更する
     */
    function reselectByAlias() {
        var services = PackageController.services
        var found = -1

        if (selectedAlias !== "") {
            for (var i = 0; i < services.length; ++i) {
                if (services[i].alias === selectedAlias) {
                    found = i
                    break
                }
            }
        }

        if (found < 0) {
            selectedAlias = ""  // 一覧から消えたサービスは選択対象から外す
        }

        serviceListView.currentIndex = found
    }

    // サービス名の一覧
    // modelはPackageController::servicesのQVariantListで、各要素はalias・name・enabledを持つ
    ListView {
        id: serviceListView  // サービス一覧の参照名

        anchors.fill: parent                                          // ビュー全体を使う
        visible: PackageController.services.length > 0                // サービスがない場合は案内文を表示するため隠す
        clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
        currentIndex: -1                                              // 利用者が選ぶまでは未選択にする (既定の0だと先頭行が選択扱いになる)
        model: PackageController.services                             // サービス一覧 (servicesChangedで更新される)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

        // 一覧の差し替えが済んだ後で選択を復元する
        // servicesChangedを直接受けると、ListView側のモデル更新より先に実行されて行番号を上書きされる恐れがあるため、モデル差し替え通知を使う
        onModelChanged: {
            serviceView.reselectByAlias()
        }

        // サービス1件分の行
        delegate: ItemDelegate {
            id: serviceDelegate  // 行自身の参照名

            width: serviceListView.width         // 一覧の幅に合わせる
            // 表示名は、名前がなければエイリアスを使う
            // enabledが明示的にfalseのサービスにだけ、無効であることを併記する
            text: modelData.enabled === false ? qsTr("%1 (disabled)").arg(modelData.name || modelData.alias || "")
                                              : (modelData.name || modelData.alias || "")
            highlighted: ListView.isCurrentItem  // 選択中の行を強調表示する

            // 選択したサービスのリポジトリのパッケージだけを右ペインの一覧へ読み込ませる
            // PackageController::loadPackagesByServiceを呼び出し、結果は右ペインのパッケージ一覧へ反映される
            onClicked: {
                // 未接続または処理中は読込要求が破棄されるため、選択行も変えない (選択行と一覧の内容がずれるのを防ぐ)
                if (!PackageController.connected || PackageController.busy) {
                    return
                }

                serviceListView.currentIndex = index
                // 再読込後も同じサービスを選び直せるように、エイリアスを覚えておく
                serviceView.selectedAlias = modelData.alias
                PackageController.loadPackagesByService(modelData.alias)
                serviceView.packageLoadStarted()
            }

            // サービス名はメタデータ由来のため、HTMLを解釈させずPlainTextで表示する
            // (既定のcontentItemはテキストをHTMLとして解釈する可能性がある)
            contentItem: Text {
                text: serviceDelegate.text            // 行に設定した表示名
                textFormat: Text.PlainText            // HTMLとして解釈させない
                font: serviceDelegate.font            // 行のフォントを引き継ぐ
                // 選択中の行は強調色の上に表示されるため、文字色も強調表示用に切り替える
                color: serviceDelegate.highlighted ? serviceDelegate.palette.highlightedText
                                                   : serviceDelegate.palette.text
                elide: Text.ElideRight                // 長い名前は末尾を省略する
                verticalAlignment: Text.AlignVCenter  // 行の高さ方向の中央に配置する
            }
        }
    }

    // サービスが1件もないときの案内文
    Label {
        anchors.centerIn: parent                                    // ビューの中央に配置する
        width: parent.width - 24                                    // 左右に余白を残して折り返す
        visible: PackageController.services.length === 0            // サービスがない場合だけ表示する
        text: qsTr("No repository index services are configured.")  // 案内文
        textFormat: Text.PlainText                                  // HTMLとして解釈させない
        wrapMode: Text.WordWrap                                     // 幅に収まらない場合は折り返す
        horizontalAlignment: Text.AlignHCenter                      // 文字を中央に揃える
        color: palette.text                                         // パレットの文字色を使う (テーマの切り替えに追従する)
    }
}
