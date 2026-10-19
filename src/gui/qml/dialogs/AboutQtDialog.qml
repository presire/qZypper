/**
 * @file AboutQtDialog.qml
 * @brief Qtのライセンスとバージョン情報を表示するダイアログ
 *
 * Main.qmlのヘルプメニューから開かれる
 *
 * 入力 : なし
 * 出力 : なし
 * 依存 : Qt.application (Qt実行環境情報)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// Qtライセンス情報ダイアログ
// Qt Frameworkのライセンス情報を表示
Dialog {
    id: root  // ダイアログ自身の参照名

    /**
     * @brief http/httpsスキームのリンクだけを外部ブラウザで開く
     *
     * javascript:やfile:などの危険なスキームは無視する
     *
     * @param link クリックされたリンクのURL
     */
    function openSafeUrl(link) {
        // http/httpsスキームの場合のみPackageController::openExternalUrl()で開く
        if (/^https?:\/\/\S/i.test(String(link).trim())) {
            PackageController.openExternalUrl(link)
        }
    }

    anchors.centerIn: Overlay.overlay  // ウィンドウ全体の中央に表示する
    // ダイアログの幅 (親ウィンドウの7割、800〜1000pxの範囲に収める)
    width: {
        // 親ウィンドウを取得できない場合は最小値を使う
        if (!ApplicationWindow.window) {
            return 800
        }

        var calculated = ApplicationWindow.window.width * 0.7
        return Math.min(Math.max(calculated, 800), 1000)
    }
    // ダイアログの高さ (親ウィンドウの7割、600〜800pxの範囲に収める)
    height: {
        // 親ウィンドウを取得できない場合は最小値を使う
        if (!ApplicationWindow.window) {
            return 600
        }

        var calculated = ApplicationWindow.window.height * 0.7
        return Math.min(Math.max(calculated, 600), 800)
    }
    title: qsTr("About Qt")        // ダイアログのタイトル
    standardButtons: Dialog.Close  // [Close]ボタンのみ表示する
    modal: true                    // 表示中は背後の画面を操作できないようにする

    // 内容が収まらない場合に縦スクロールできる領域
    ScrollView {
        anchors.fill: parent                 // ダイアログ内側全体を使う
        contentWidth: availableWidth         // 横スクロールさせずに表示幅へ合わせる
        contentHeight: aboutQtLayout.height  // 内容の高さに合わせる
        clip: true                           // 領域外にはみ出した部分を描画しない

        // ロゴ画像と情報カラムをまとめるレイアウト
        ColumnLayout {
            id: aboutQtLayout  // 内容レイアウトの参照名

            width: parent.width  // スクロール領域の幅に合わせる
            spacing: 20          // 項目の間隔

            // ロゴ画像と情報カラムを横に並べる
            RowLayout {
                Layout.fillWidth: true   // 親の幅いっぱいに広げる
                Layout.topMargin: 20     // 上側の余白
                Layout.bottomMargin: 20  // 下側の余白
                Layout.leftMargin: 20    // 左側の余白
                Layout.rightMargin: 20   // 右側の余白
                spacing: 20              // ロゴと情報カラムの間隔

                // Qtロゴ画像
                Image {
                    id: qtLogo  // Qtロゴ画像の参照名

                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop             // 左右中央・上寄せに配置する
                    Layout.preferredWidth: 200                                  // ロゴの幅
                    source: "qrc:/qt/qml/org/presire/qzypper/gui/icons/Qt.png"  // Qtロゴ画像のリソースパス
                    fillMode: Image.PreserveAspectFit                           // 縦横比を保って表示する
                }

                // ライセンス情報カラム
                ColumnLayout {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                    spacing: 20             // 項目の間隔

                    // Qtの説明テキスト
                    Label {
                        Layout.fillWidth: true   // 親の幅いっぱいに広げる
                        // Qtの概要とライセンス形態の説明文
                        text: qsTr("This software is developed with Qt 6.") + "\n\n" +
                              "Qt is a C++ toolkit for cross-platform application development." + "\n" +
                              "Qt provides single-source portability across all major desktop operating systems." + "\n" +
                              "It is also available for embedded Linux and other embedded and mobile operating systems." + "\n\n" +
                              "Qt is available under multiple licensing options designed to accommodate the needs of our various users." + "\n\n" +
                              "Qt licensed under our commercial license agreement is appropriate for development of proprietary/commercial software " +
                              "where you do not want to share any source code with third parties or otherwise cannot comply with the terms of GNU (L)GPL." + "\n\n" +
                              "Qt licensed under GNU (L)GPL is appropriate for the development of Qt applications provided you can comply with the terms and " +
                              "conditions of the respective licenses."
                        wrapMode: Text.WordWrap  // 幅に収まらない場合は単語単位で折り返す
                    }

                    // ライセンス詳細リンク
                    Label {
                        Layout.fillWidth: true     // 親の幅いっぱいに広げる
                        Layout.topMargin: 10       // 上側の間隔
                        // リンクを含む案内文
                        text: qsTr("For more information, see <a href=\"https://www.qt.io/licensing/\">qt.io/licensing</a>.")
                        textFormat: Text.RichText  // リンク (aタグ) を表示するためリッチテキストとして解釈する
                        wrapMode: Text.WordWrap    // 幅に収まらない場合は単語単位で折り返す

                        // クリックされたリンクを、安全なスキームの場合のみ外部ブラウザで開く
                        onLinkActivated: function(link) {
                            root.openSafeUrl(link)
                        }

                        // リンク上で指差しカーソルを表示する
                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指差しカーソル
                        }
                    }

                    // 著作権情報
                    Label {
                        Layout.fillWidth: true   // 親の幅いっぱいに広げる
                        Layout.topMargin: 10     // 上側の間隔
                        // Qtの著作権表示と商標表示
                        text: "Copyright (C) 2026 The Qt Company Ltd and other contributors." + "\n" +
                              "Qt and the Qt logo are trademarks of The Qt Company Ltd." + "\n" +
                              "Qt is The Qt Company Ltd product developed as an open source project."
                        wrapMode: Text.WordWrap  // 幅に収まらない場合は単語単位で折り返す
                    }

                    // Qt公式サイトリンク
                    Label {
                        Layout.fillWidth: true     // 親の幅いっぱいに広げる
                        Layout.topMargin: 10       // 上側の間隔
                        // リンクを含む案内文
                        text: qsTr("For more information, see <a href=\"https://www.qt.io/\">qt.io</a>.")
                        textFormat: Text.RichText  // リンク (aタグ) を表示するためリッチテキストとして解釈する
                        wrapMode: Text.WordWrap    // 幅に収まらない場合は単語単位で折り返す

                        // クリックされたリンクを、安全なスキームの場合のみ外部ブラウザで開く
                        onLinkActivated: function(link) {
                            root.openSafeUrl(link)
                        }

                        // リンク上で指差しカーソルを表示する
                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指差しカーソル
                        }
                    }
                }
            }
        }
    }
}
