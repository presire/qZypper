/**
 * @file ProductView.qml
 * @brief インストール済み製品と利用可能な製品を一覧表示する読み取り専用ビュー
 *
 * Main.qmlの左ペインStackLayoutの末尾に配置するPane
 * 製品名・バージョン・ベンダー・リポジトリを2行で表示する
 * 表示の切替はMain.qmlのfilterTabBarの選択インデックスで行う
 *
 * 入力 : なし (PackageController::productsを参照する)
 * 出力 : なし (選択状態のみ内部で保持する)
 * 依存 : PackageController (products、loadProducts)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// [Products]タブで表示するビュー本体 (参照のみでインストール/削除操作は行わない)
Pane {
    id: productView  // ビュー自身の参照名

    property bool loading: false  // 製品一覧を読み込み中か
    property bool loaded: false   // 製品一覧を読み込み済みか (2回目以降のタブ表示では再取得しない)

    /**
     * @brief 製品一覧を読み込む (タブを初めて表示したときにMain.qmlから呼ばれる)
     *
     * 2回目以降は何もしない
     * プール再読込時の更新はPackageController側が行う
     */
    function ensureLoaded() {
        // 読み込み済みなら再取得しない
        if (loaded) {
            return
        }

        loaded = true
        loading = true

        // PackageController::loadProductsでバックエンドから製品一覧を取得する
        PackageController.loadProducts()
    }

    padding: 0  // リスト表示を枠いっぱいに広げるため余白なし

    // 製品の一覧 (2行表示、左ペインが狭いため列は使わない)
    ListView {
        id: productListView  // 製品一覧の参照名

        anchors.fill: parent               // ビュー全体に一覧を広げる
        clip: true                         // スクロール時に枠外へはみ出した行を描画しない
        model: PackageController.products  // PackageController::productsの製品一覧

        // 行が収まらない場合のみ縦スクロールバーを表示する
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        // 製品1件分の行 (アイコン + 2行のテキスト)
        delegate: ItemDelegate {
            id: productDelegate  // 行自身の参照名

            required property int index      // 行番号 (選択に使用)
            required property var modelData  // 製品1件分のデータ

            width: productListView.width  // 一覧の幅に合わせる
            height: 52                    // 2行表示の行の高さ
            highlighted: ListView.isCurrentItem

            // クリックされた行を選択状態にする (削除やインストールは行わない)
            onClicked: {
                productListView.currentIndex = index
            }

            // 行の内容 (状態アイコン + 2行のテキスト)
            contentItem: RowLayout {
                spacing: 6  // アイコンとテキストの間隔

                // インストール状態のアイコン
                StatusIcon {
                    status: productDelegate.modelData.status || 0
                    iconSize: 16
                    showToolTip: false
                }

                // 製品名と補足情報の2行
                ColumnLayout {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                    spacing: 1              // 行間を詰める

                    // 1行目: 概要 (無ければ短い名前や名前) とベース製品のタグ
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        // 製品名 (概要が空なら短い名前や名前で代用する)
                        Label {
                            Layout.fillWidth: true      // 残りの幅をすべて使う
                            text: productDelegate.modelData.summary
                                  || productDelegate.modelData.shortName
                                  || productDelegate.modelData.name || ""
                            textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                            font.bold: true             // 製品名を強調する
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        // ベース製品 (ターゲットディストリビューション) の目印
                        Rectangle {
                            implicitWidth: baseTag.implicitWidth + 8     // 文字幅に余白を加える
                            implicitHeight: baseTag.implicitHeight + 2   // 文字高に余白を加える
                            visible: productDelegate.modelData.isBase === true
                            radius: 3
                            color: palette.highlight

                            // [Base]タグの文字列
                            Label {
                                id: baseTag  // タグ幅の算出に使う参照名

                                anchors.centerIn: parent
                                text: qsTr("Base")
                                font.pixelSize: 10
                                color: palette.highlightedText
                            }
                        }
                    }

                    // 2行目: バージョン · ベンダー · リポジトリ
                    Label {
                        Layout.fillWidth: true  // 残りの幅をすべて使う
                        opacity: 0.8            // 補足行を控えめに表示する
                        text: {
                            var parts = []

                            // 空でない項目だけを中黒で連結する
                            if (productDelegate.modelData.version) {
                                parts.push(productDelegate.modelData.version)
                            }

                            if (productDelegate.modelData.vendor) {
                                parts.push(productDelegate.modelData.vendor)
                            }

                            if (productDelegate.modelData.repoName) {
                                parts.push(productDelegate.modelData.repoName)
                            }

                            return parts.join(" · ")
                        }
                        textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // 一覧が空のときのプレースホルダ (読込中はその旨を表示する)
    Label {
        anchors.centerIn: parent
        visible: PackageController.products.length === 0
        opacity: 0.7
        text: productView.loading ? qsTr("Loading products...") : qsTr("No products found")
    }

    // 製品一覧の更新通知を受け取り、読み込み中の表示を解除する
    Connections {
        target: PackageController

        // 製品一覧が更新されたらプレースホルダの読み込み中表示を消す
        function onProductsChanged() {
            productView.loading = false
        }
    }
}
