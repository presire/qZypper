/**
 * @file SummaryView.qml
 * @brief インストール・更新・削除の予定をまとめて表示するビュー
 *
 * Main.qmlのStackLayout内で直接生成されるPaneで、確定前の変更内容を一覧表示する
 * 表示の切替はMain.qmlのfilterTabBarの選択インデックスで行う
 *
 * 入力 : なし (モデルはUtil.buildSummaryModel()が生成する)
 * 出力 : なし
 * 依存 : Util (buildSummaryModel)、PackageController (変更通知の間接参照)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// [Installation Summary]タブで表示するビュー本体
Pane {
    padding: 0  // リスト表示を枠いっぱいに広げるため余白なし

    // カテゴリごとにセクション見出しを挟んで予定を並べる
    ListView {
        id: summaryListView  // 変更予定一覧の参照名

        anchors.fill: parent                                          // ビュー全体に一覧を広げる
        clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
        // Util.buildSummaryModelはPackageController.packagesを参照しているため、
        // パッケージ一覧が更新されるたびにバインディングが再評価されて一覧も作り直される
        model: Util.buildSummaryModel()                               // 変更予定をカテゴリ別に並べたモデル
        section.property: "category"                                  // カテゴリ (install / update / delete) ごとにセクションを分ける
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

        // カテゴリ名の見出し (色は操作の種類を示す)
        section.delegate: Rectangle {
            width: summaryListView.width  // 一覧の幅に合わせる
            height: 28                    // 見出し行の高さ
            // インストールは緑、更新は青、削除は赤、それ以外は灰色で塗り分ける
            color: section === "install" ? "#4caf50"
                 : section === "update"  ? "#2196f3"
                 : section === "delete"  ? "#f44336" : "#9e9e9e"

            // 見出しの文字 (カテゴリ名を翻訳済みの表示名へ置き換える)
            Label {
                anchors.verticalCenter: parent.verticalCenter  // 見出し行の高さ方向の中央に配置する
                anchors.left: parent.left                      // 見出し行の左端を基準にする
                anchors.leftMargin: 8                          // 左端からの余白
                // 既知のカテゴリは翻訳済みの表示名、未知のカテゴリはそのまま表示する
                text: section === "install" ? qsTr("Install")
                    : section === "update"  ? qsTr("Update")
                    : section === "delete"  ? qsTr("Delete") : section
                font.bold: true                                // 見出しとして太字にする
                color: "white"                                 // 色付きの背景上で読みやすい白文字
            }
        }

        // 1パッケージ分の行 (名前と変更後のバージョン)
        delegate: ItemDelegate {
            required property var modelData  // パッケージ1件分のデータ

            width: summaryListView.width  // 一覧の幅に合わせる
            height: 24                    // 1行の高さ

            // パッケージ名とバージョンを横に並べる行の中身
            RowLayout {
                anchors.fill: parent    // 行全体に広げる
                anchors.leftMargin: 16  // 見出しより字下げして表示する
                spacing: 8              // 名前とバージョンの間隔

                // パッケージ名 (メタデータ由来のためPlainText)
                Label {
                    Layout.preferredWidth: 200  // 名前の列幅
                    text: modelData.name || ""  // パッケージ名
                    textFormat: Text.PlainText  // HTMLとして解釈させない
                    elide: Text.ElideRight      // 長い名前は末尾を省略する
                }

                // 変更後のバージョン
                Label {
                    Layout.fillWidth: true          // 残りの幅をすべて使う
                    text: modelData.version || ""   // 変更後のバージョン
                    textFormat: Text.PlainText      // HTMLとして解釈させない
                    color: palette.placeholderText  // 名前より控えめな文字色
                }
            }
        }

        // 予定が1件もないときの案内表示
        Label {
            anchors.centerIn: parent                  // 一覧の中央に表示する
            visible: summaryListView.count === 0      // 変更予定が1件もない場合のみ表示する
            text: qsTr("No pending package changes")  // 案内メッセージ
            color: palette.placeholderText            // 控えめな文字色
        }
    }
}
