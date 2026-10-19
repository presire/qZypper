/**
 * @file AboutqZypperDialog.qml
 * @brief qZypperのアプリケーション情報を表示するダイアログ
 *
 * Main.qmlのヘルプメニューから開かれる
 *
 * 入力 : なし
 * 出力 : なし
 * 依存 : PackageController (アプリケーション情報)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// qZypperについてダイアログ
// アプリケーション情報、バージョン、ライセンス、リンク情報を表示
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
    title: qsTr("About qZypper")   // ダイアログのタイトル
    standardButtons: Dialog.Close  // [Close]ボタンのみ表示する
    modal: true                    // 表示中は背後の画面を操作できないようにする

    // 内容が収まらない場合に縦スクロールできる領域
    ScrollView {
        anchors.fill: parent                      // ダイアログ内側全体を使う
        contentWidth: availableWidth              // 横スクロールさせずに表示幅へ合わせる
        contentHeight: aboutqZypperLayout.height  // 内容の高さに合わせる
        clip: true                                // 領域外にはみ出した部分を描画しない

        // ロゴ画像と情報カラムをまとめるレイアウト
        ColumnLayout {
            id: aboutqZypperLayout  // 内容レイアウトの参照名

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

                // qZypperロゴ画像
                Image {
                    id: qZypperLogo  // qZypperロゴ画像の参照名

                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop                      // 左右中央・上寄せに配置する
                    Layout.preferredWidth: 200                                           // ロゴの幅
                    Layout.preferredHeight: 200                                          // ロゴの高さ
                    source: "qrc:/qt/qml/org/presire/qzypper/gui/icons/qZypper@256.png"  // qZypperロゴ画像のリソースパス
                    sourceSize.width: 256                                                // 読み込む画像の幅
                    sourceSize.height: 256                                               // 読み込む画像の高さ
                    fillMode: Image.PreserveAspectFit                                    // 縦横比を保って表示する
                }

                // アプリケーション情報カラム
                ColumnLayout {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                    spacing: 20             // 項目の間隔

                    // アプリケーション名とバージョン
                    Label {
                        Layout.fillWidth: true                     // 親の幅いっぱいに広げる
                        text: "qZypper " + Qt.application.version  // アプリケーション名とバージョン
                        font.bold: true                            // 太字で表示する
                        font.pixelSize: 20                         // 文字サイズ
                    }

                    // アプリケーション説明
                    Label {
                        Layout.fillWidth: true                                     // 親の幅いっぱいに広げる
                        text: qsTr("Software management tool for openSUSE / SLE")  // アプリケーションの説明文
                        wrapMode: Text.WordWrap                                    // 幅に収まらない場合は単語単位で折り返す
                    }

                    // 著作権情報
                    Label {
                        Layout.fillWidth: true              // 親の幅いっぱいに広げる
                        Layout.topMargin: 10                // 上側の間隔
                        text: "Copyright (C) 2026 Presire"  // 著作権表示
                    }

                    // ライセンス情報
                    Label {
                        Layout.fillWidth: true   // 親の幅いっぱいに広げる
                        Layout.topMargin: 10     // 上側の間隔
                        // ライセンスの説明文
                        text: qsTr("This program is licensed under the GNU General Public License v3.0 or later.")
                        wrapMode: Text.WordWrap  // 幅に収まらない場合は単語単位で折り返す
                    }

                    // GPLライセンスリンク
                    Label {
                        Layout.fillWidth: true     // 親の幅いっぱいに広げる
                        // リンクを含む案内文
                        text: qsTr("For more information, see <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">gnu.org/licenses/gpl-3.0</a>.")
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

                    // GitHubリポジトリリンク
                    Label {
                        Layout.fillWidth: true     // 親の幅いっぱいに広げる
                        Layout.topMargin: 10       // 上側の間隔
                        // リンクを含む案内文
                        text: qsTr("GitHub: <a href=\"https://github.com/presire/qZypper\">github.com/presire/qZypper</a>")
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

                    // Issuesページリンク
                    Label {
                        Layout.fillWidth: true     // 親の幅いっぱいに広げる
                        // リンクを含む案内文
                        text: qsTr("Issues: <a href=\"https://github.com/presire/qZypper/issues\">github.com/presire/qZypper/issues</a>")
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
