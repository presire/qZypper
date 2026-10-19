/**
 * @file PatternView.qml
 * @brief パターン (役割別パッケージ群) を一覧して有効・無効を切り替えるビュー
 *
 * Main.qmlのStackLayout内で直接生成されるPaneで、パターン一覧とチェック状態を表示する
 * 表示の切替はMain.qmlのfilterTabBarの選択インデックスで行う
 *
 * 入力 : なし
 * 出力 : なし
 * 依存 : PackageController (patterns, togglePatternStatus(), loadPackagesByPattern())
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// [Patterns]タブで表示するビュー本体
Pane {
    // スクロール位置保存用
    // パターンをトグルするとモデルが作り直され、位置が先頭へ戻るため退避しておく
    property real savedContentY: 0       // トグル前のスクロール位置
    property int  savedIndex: -1         // トグル前の選択行
    property bool restorePending: false  // モデル再構築後に位置を戻す予定か

    padding: 0  // リスト表示を枠いっぱいに広げるため余白なし

    // パターン名の一覧 (カテゴリごとに見出しを挟んで表示する)
    ListView {
        id: patternListView  // パターン一覧の参照名

        /**
         * @brief パターンのチェック状態をトグルする
         * @param name 対象パターンの名前
         * @note PackageController::togglePatternStatusを呼び出す
         * @note モデル再構築後の位置復元のため現在のスクロール位置と選択行を退避する
         */
        function togglePattern(name) {
            parent.savedContentY = patternListView.contentY
            parent.savedIndex    = patternListView.currentIndex
            parent.restorePending = true
            PackageController.togglePatternStatus(name)
        }

        anchors.fill: parent                                          // ビュー全体に一覧を広げる
        clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
        model: PackageController.patterns                             // PackageController::patternsのパターン一覧
        section.property: "category"                                  // カテゴリごとにセクションを分ける
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

        // modelの再構築後、退避した位置へ戻す
        // (トグル操作による再構築のときだけ復元し、それ以外の更新では位置を変えない)
        onModelChanged: {
            if (parent.restorePending) {
                parent.restorePending = false
                patternListView.currentIndex = parent.savedIndex
                patternListView.contentY = parent.savedContentY
            }
        }

        // カテゴリ名の見出し
        section.delegate: Rectangle {
            width: patternListView.width  // 一覧の幅に合わせる
            height: 28                    // 見出し行の高さ
            color: palette.alternateBase  // 通常の行と区別するための背景色

            // 見出しの文字 (カテゴリ名はメタデータ由来のためPlainText)
            Label {
                anchors.left: parent.left                      // 見出し行の左端を基準にする
                anchors.leftMargin: 8                          // 左端からの余白
                anchors.verticalCenter: parent.verticalCenter  // 見出し行の高さ方向の中央に配置する
                text: section                                  // カテゴリ名
                textFormat: Text.PlainText                     // HTMLとして解釈させない
                font.bold: true                                // 見出しとして太字にする
                font.pixelSize: 13                             // 見出しの文字サイズ
            }
        }

        // 1パターン分の行 (チェックボックスと説明)
        delegate: ItemDelegate {
            id: patternDelegate  // 行自身の参照名

            readonly property int pStatus: modelData.status || 0  // パターンの変更状態

            // インストール/対象/更新などの有効状態か
            readonly property bool pChecked:
                pStatus === 1 || pStatus === 2 || pStatus === 3 ||
                pStatus === 4 || pStatus === 5 || pStatus === 9

            // 状態ごとの表示色 (チェックボックスの色を変えて変更内容を示す)
            readonly property color statusColor: {
                // KeepInstalled/Protected
                if (pStatus === 3 || pStatus === 9) {
                    return "#4CAF50"
                }

                // Install/AutoInstall
                if (pStatus === 1 || pStatus === 2) {
                    return "#2196F3"
                }

                // Update/AutoUpdate
                if (pStatus === 4 || pStatus === 5) {
                    return "#FF9800"
                }

                // Del/AutoDel
                if (pStatus === 6 || pStatus === 7) {
                    return "#F44336"
                }

                // 上記以外 (変更なし・未インストール) は控えめな色
                return palette.mid
            }

            width: patternListView.width         // 一覧の幅に合わせる
            highlighted: ListView.isCurrentItem  // 選択中の行を強調表示する

            // 行を選択するとそのパターンのパッケージを一覧へ読み込む
            // PackageController::loadPackagesByPatternを呼び出し、結果はpackagesChangedで一覧へ反映される
            onClicked: {
                patternListView.currentIndex = index
                PackageController.loadPackagesByPattern(modelData.name)
            }

            // チェックボックスと説明を横に並べる行の中身
            contentItem: RowLayout {
                spacing: 6  // チェックボックスと説明の間隔

                // カスタムチェックボックス (状態ごとの色で塗りつぶす)
                Rectangle {
                    width: 18                                                                      // チェックボックスの幅
                    height: 18                                                                     // チェックボックスの高さ
                    color: patternDelegate.pChecked ? patternDelegate.statusColor : "transparent"  // 有効状態なら状態色で塗る
                    radius: 3                                                                      // 角の丸み
                    // 枠線の色
                    border.color: patternDelegate.pChecked ? patternDelegate.statusColor : palette.mid
                    border.width: patternDelegate.pChecked ? 0 : 1.5                               // 有効状態では枠線を消す

                    // チェックマーク (有効状態のときだけ表示する)
                    Label {
                        anchors.centerIn: parent           // チェックボックスの中央に表示する
                        visible: patternDelegate.pChecked  // 有効状態の場合のみ表示する
                        text: "\u2713"                     // チェックマークの文字
                        font.pixelSize: 13                 // チェックマークの文字サイズ
                        font.bold: true                    // 見やすいよう太字にする
                        color: "white"                     // 状態色の背景上で読みやすい白文字
                    }

                    // チェックボックス自体を押したときだけトグルする
                    // (行のクリックはパッケージ読み込みに割り当てているため)
                    MouseArea {
                        anchors.fill: parent                // チェックボックス全体で操作を受け付ける
                        anchors.margins: -4                 // 押しやすいよう周囲4pxまで反応範囲を広げる
                        cursorShape: Qt.PointingHandCursor  // 押せることを示す指カーソル

                        // チェック状態をトグルする
                        onClicked: {
                            patternListView.togglePattern(modelData.name)
                        }
                    }
                }

                // パターンの説明 (なければ名前)
                Label {
                    Layout.fillWidth: true                                                       // 残りの幅をすべて使う
                    text: modelData.summary || modelData.name || ""                              // パターンの説明 (なければ名前)
                    textFormat: Text.PlainText                                                   // HTMLとして解釈させない
                    color: patternDelegate.highlighted ? palette.highlightedText : palette.text  // 選択中は強調表示用の文字色
                    elide: Text.ElideRight                                                       // 長い説明は末尾を省略する
                }
            }
        }

        // 読み込みが終わるまでの案内表示
        Label {
            anchors.centerIn: parent              // 一覧の中央に表示する
            visible: patternListView.count === 0  // パターンが1件も読み込まれていない場合のみ表示する
            text: qsTr("Loading patterns...")     // 案内メッセージ
            color: palette.placeholderText        // 控えめな文字色
        }
    }
}
