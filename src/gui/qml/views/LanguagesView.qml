/**
 * @file LanguagesView.qml
 * @brief 言語サポート (ロケール) の要求状態を切り替え、ロケール別にパッケージを絞り込むビュー
 *
 * Main.qmlの左ペインのStackLayout内で生成され、[Languages]タブの選択時に表示される
 * チェックボックスで言語サポートの要求を切り替え、言語名の選択でそのロケールのパッケージを右ペインの一覧へ読み込ませる
 *
 * 入力 : なし
 * 出力 : packageLoadStarted() シグナル (ロケール選択によるパッケージ読込の開始)
 * 依存 : PackageController (locales、connected、busy、setLocaleRequested()、loadPackagesByLocale())
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// [Languages]タブで表示するビュー本体
Pane {
    id: languagesView  // 言語ビュー自身の参照名

    signal packageLoadStarted()  // ロケール選択でパッケージ読込を開始したことの通知 (Main.qmlのウェイトポップアップ用)

    // ロケール一覧を一覧表示へ反映する
    // モデルを置き換えると一覧は先頭へ戻り選択行も先頭になるため、置き換える直前の表示位置を保存して置き換え後に戻す
    // 選択行を先に戻し、その後にスクロール位置を一覧の範囲内に収めて戻す (選択行の変更で表示位置が動くため)
    function applyLocales() {
        const savedContentY = localeListView.contentY
        const savedCurrentIndex = localeListView.currentIndex

        localeListView.model = PackageController.locales
        localeListView.forceLayout()

        if (savedCurrentIndex < localeListView.count) {
            localeListView.currentIndex = savedCurrentIndex
        }

        const maxContentY = localeListView.originY + localeListView.contentHeight - localeListView.height
        localeListView.contentY = Math.max(localeListView.originY, Math.min(savedContentY, maxContentY))
    }

    padding: 0  // リスト表示を枠いっぱいに広げるため余白なし

    // ロケールの一覧 (PackageController::localesの{code, name, requested, available})
    ListView {
        id: localeListView  // ロケール一覧の参照名

        anchors.fill: parent                                          // ビュー全体に一覧を広げる
        clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

        // 生成時に現在のロケール一覧を表示する (以降の更新はlocalesChangedで反映する)
        Component.onCompleted: {
            languagesView.applyLocales()
        }

        // ロケール1件分の行 (要求状態のチェックボックスと言語名)
        delegate: ItemDelegate {
            id: localeDelegate  // 行自身の参照名

            required property var modelData  // ロケール1件分のデータ
            required property int index      // 一覧内の行番号

            width: localeListView.width                                                       // 一覧の幅に合わせる
            text: localeDelegate.modelData.name + " (" + localeDelegate.modelData.code + ")"  // 言語名とロケールコード
            highlighted: ListView.isCurrentItem                                               // 選択中の行を強調表示する

            // 選択したロケールのパッケージだけを読み込ませる
            // PackageController::loadPackagesByLocaleを呼び出し、結果はpackagesChangedで一覧へ反映される
            onClicked: {
                // 未接続/処理中は読込要求が破棄されるため、選択行も変えない (選択行と一覧の内容がずれるのを防ぐ)
                if (!PackageController.connected || PackageController.busy) {
                    return
                }

                localeListView.currentIndex = localeDelegate.index
                PackageController.loadPackagesByLocale(localeDelegate.modelData.code)
                languagesView.packageLoadStarted()
            }

            // 要求状態のチェックボックスと言語名を横に並べる
            contentItem: RowLayout {
                spacing: 6  // チェックボックスと言語名の間隔

                // 言語サポートの要求状態
                CheckBox {
                    // 接続中かつ非実行中のみ変更できる (未接続や実行中はPackageController側で無視されるため)
                    enabled: PackageController.connected && !PackageController.busy
                    checked: localeDelegate.modelData.requested === true  // 要求済みのロケールはオン
                    Accessible.name: localeDelegate.text                  // スクリーンリーダー向けの名前

                    // PackageController::setLocaleRequestedを呼び出し、完了後にlocalesを再読込させる
                    // 再読込の結果で表示を合わせるため、クリックで外れたバインディングを張り直す
                    onToggled: {
                        PackageController.setLocaleRequested(localeDelegate.modelData.code, checked)
                        checked = Qt.binding(function() {
                            return localeDelegate.modelData.requested === true
                        })
                    }
                }

                // 言語名とロケールコード (メタデータ由来のためPlainTextで表示する)
                Text {
                    Layout.fillWidth: true                // 残りの幅をすべて使う
                    text: localeDelegate.text             // 行に設定した表示名
                    textFormat: Text.PlainText            // HTMLとして解釈させない
                    font: localeDelegate.font             // 行のフォントを引き継ぐ
                    // 選択中の行は強調色の上に表示するため、文字色も強調表示用に切り替える
                    color: localeDelegate.highlighted ? localeDelegate.palette.highlightedText : localeDelegate.palette.text
                    elide: Text.ElideRight                // 長い名前は末尾を省略する
                    verticalAlignment: Text.AlignVCenter  // 行の高さ方向の中央に配置する
                }
            }
        }
    }

    // ロケールが無い場合 (未取得または旧版のバックエンド) の案内
    Label {
        anchors.centerIn: parent              // ビューの中央に表示する
        visible: localeListView.count === 0   // ロケールが無い場合のみ表示する
        text: qsTr("No languages available")  // 案内の文字列
        opacity: 0.7                          // 補足情報として控えめに表示する
    }

    // ロケール一覧の再読込 (言語サポートの切り替え後や[Languages]タブの表示時) を、表示位置を保ったまま一覧へ反映する
    Connections {
        target: PackageController  // ロケール一覧を保持するコントローラ

        function onLocalesChanged() {
            languagesView.applyLocales()
        }
    }
}
