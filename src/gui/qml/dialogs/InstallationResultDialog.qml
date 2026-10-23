/**
 * @file InstallationResultDialog.qml
 * @brief パッケージ変更の適用結果と注意事項を表示するダイアログ
 *
 * CommitProgressDialogの完了後にMain.qmlから開かれる
 *
 * 入力 : 成功・失敗・再起動推奨などの適用結果
 * 出力 : 閉じる操作
 * 依存 : PackageController (コミット結果)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// パッケージ変更の適用結果 (結果区分、パッケージ一覧、スナップショット、スクリプト出力、統計) を表示するダイアログ
Dialog {
    id: resultDialog  // ダイアログ自身の参照名

    // 旧バックエンド用の成功フラグ (outcomeが無い場合の推定に使う)
    property bool resultSuccess: true  // 成功フラグ (旧バックエンド用)
    // バックエンドから受け取った適用結果データ
    property var resultData: ({})      // 適用結果データ

    // 適用結果の区分 : バックエンドのoutcome (completed / partial / cancelled / failed)
    // 古いバックエンドではsuccessから推定する
    readonly property string outcome: resultData.outcome
                                      || (resultSuccess ? "completed" : "failed")
    // スクリプト出力とRPMメッセージを結合した行一覧
    readonly property var scriptLines: (resultData.scriptOutput || []).concat(resultData.rpmMessages || [])
    // 再起動が必要なサービス一覧
    readonly property var servicesToRestart: resultData.servicesToRestart || []  // 再起動が必要なサービス
    // 作成されたスナップショット一覧
    readonly property var snapshots: resultData.snapshots || []                  // 作成されたスナップショット
    readonly property var rejectedLicensePackages: resultData.rejectedLicensePackages || []  // ライセンス拒否で除外したパッケージ

    // 注意表示が必要か : 再起動・サービス再起動の案内やエラーがある場合は、サマリーページ設定に関わらず表示すべき
    readonly property bool needsAttention: outcome !== "completed"
                                           || resultData.rebootNeeded === true
                                           || resultData.restartSuggested === true
                                           || servicesToRestart.length > 0
                                           || rejectedLicensePackages.length > 0  // 除外理由を結果画面で案内する

    // パッケージ一覧の再検索を要求する
    signal refreshRequested()  // 再検索の要求

    /**
     * @brief outcomeに対応する結果の説明文を返す
     * @return 表示用の説明文
     */
    function outcomeText() {
        switch (outcome) {
        case "completed": return qsTr("All changes were applied successfully.")
        case "partial":   return qsTr("Some changes were applied, but others failed or were skipped.")
        case "cancelled": return qsTr("The operation was cancelled. Some changes may already have been applied.")
        default:          return qsTr("The changes could not be applied.")
        }
    }

    /**
     * @brief outcomeに対応する見出し色を返す
     * @return 見出しに使う色
     */
    function outcomeColor() {
        switch (outcome) {
        case "completed": return "#4CAF50"
        case "partial":   return "#FF9800"
        case "cancelled": return "#FF9800"
        default:          return "#F44336"
        }
    }

    /**
     * @brief スナップショット1件を1行の表示文字列へ整形する
     * @param s スナップショット情報
     * @return 整形済みの文字列
     */
    function formatSnapshot(s) {
        var text = "#" + (s.num || "?")

        // 種別がある場合は角括弧で付記する
        if (s.type) {
            text += " [" + s.type + "]"
        }

        // 日時がある場合は付記する
        if (s.date) {
            text += "  " + s.date
        }

        // 説明がある場合は付記する
        if (s.description) {
            text += "  " + s.description
        }

        return text
    }

    anchors.centerIn: parent                                      // 親ウィンドウの中央に表示する
    width: 600                                                    // ダイアログの幅
    // 縦サイズは親ウィンドウの85%を上限 (大量パッケージ時もコンパクトに)
    height: Math.min((parent ? parent.height : 760) * 0.85, 760)  // 親の高さの85% (最大760px)
    title: qsTr("Installation Report")                            // ダイアログのタイトル
    standardButtons: Dialog.Ok                                    // [OK]ボタンのみ表示する
    modal: true                                                   // 表示中は背後の画面を操作できないようにする

    // [OK]ボタンで閉じたとき
    onAccepted: {
        // パッケージリストを再検索して更新
        resultDialog.refreshRequested()
    }

    // ダイアログの背景 (強調色の枠線付き)
    background: Rectangle {
        color: palette.window            // ウィンドウと同じ背景色
        border.color: palette.highlight  // 枠線の色 (強調色)
        border.width: 2                  // 枠線の太さ
    }

    // ダイアログのタイトル行
    header: Label {
        padding: 10               // 文字の周囲の余白
        text: resultDialog.title  // ダイアログのタイトル
        font.bold: true           // 太字で表示する
    }

    // 見出しと項目一覧を表示するパッケージセクション (インラインコンポーネント)
    component PackageSection: ColumnLayout {
        // セクション見出しに表示する文字列
        property string heading: ""             // セクション見出し
        // 表示するデータの一覧
        property var items: []                  // 表示する項目一覧
        // 見出しに出す件数 (既定はitemsの件数)
        property int count: items.length        // 見出しに出す件数
        // 見出しと本文に使う文字色
        property color textColor: palette.text  // 本文の文字色

        Layout.fillWidth: true  // 親の幅いっぱいに広げる
        spacing: 4              // 見出しと項目一覧の間隔
        // 該当項目が無いセクションは表示しない
        visible: count > 0      // 件数が1件以上の場合のみ表示する

        // セクションの見出し (先頭に箇条書き記号を付ける)
        Label {
            Layout.fillWidth: true            // 親の幅いっぱいに広げる
            text: "\u2022 " + parent.heading  // 箇条書き記号付きの見出し
            color: parent.textColor           // セクションの文字色
        }

        // 項目一覧 (選択してコピーできる)
        TextEdit {
            Layout.fillWidth: true                    // 親の幅いっぱいに広げる
            Layout.leftMargin: 16                     // 見出しより字下げする
            // 詳細がある場合のみ表示する
            visible: parent.items.length > 0          // 項目がある場合のみ表示する
            text: parent.items.join("\n")             // 1行1項目の一覧
            textFormat: TextEdit.PlainText            // HTMLとして解釈させない
            color: parent.textColor                   // セクションの文字色
            font.pixelSize: 13                        // 文字サイズ
            font.family: "monospace"                  // 桁を揃えやすい等幅フォント
            wrapMode: TextEdit.NoWrap                 // 折り返さない
            readOnly: true                            // 表示専用で編集させない
            selectByMouse: true                       // マウスで文字列を選択できるようにする
            selectByKeyboard: true                    // キーボードで文字列を選択できるようにする
            Accessible.role: Accessible.EditableText  // スクリーンリーダー向けの役割 (テキスト欄)
            Accessible.name: parent.heading           // スクリーンリーダー向けの名前 (見出しと同じ)
        }
    }

    // セクション間の区切り線 (インラインコンポーネント)
    component Separator: Rectangle {
        Layout.fillWidth: true  // 親の幅いっぱいに広げる
        height: 1               // 線の太さ
        color: palette.mid      // 線の色
    }

    // 結果全体の縦スクロール領域
    Flickable {
        id: resultFlickable  // スクロール領域の参照名

        anchors.fill: parent                                          // ダイアログ内側全体を使う
        contentHeight: resultColumn.implicitHeight                    // 内容全体の高さ
        clip: true                                                    // 領域外にはみ出した部分を描画しない
        boundsMovement: Flickable.StopAtBounds                        // 端を越えてスクロールさせない
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する

        // 結果の各セクションを縦に並べる
        ColumnLayout {
            id: resultColumn  // 結果の縦並びの参照名

            width: resultFlickable.width  // スクロール領域の幅に合わせる
            spacing: 12                   // セクションの間隔

            // ヘッダ (結果区分)
            Label {
                Layout.fillWidth: true               // 親の幅いっぱいに広げる
                text: resultDialog.outcomeText()     // 結果区分の説明文
                color: resultDialog.outcomeColor()   // 結果区分に応じた文字色
                font.bold: true                      // 太字で表示する
                font.pixelSize: 15                   // 文字サイズ
                wrapMode: Text.WordWrap              // 幅に収まらない場合は単語単位で折り返す
                Accessible.role: Accessible.Heading  // スクリーンリーダー向けの役割 (見出し)
                Accessible.name: text                // スクリーンリーダー向けの名前 (説明文と同じ)
            }

            // 完了・エラー・スキップの件数
            Label {
                Layout.fillWidth: true                                      // 親の幅いっぱいに広げる
                visible: (resultDialog.resultData.doneCount !== undefined)  // 集計値がある場合のみ表示する
                // 完了ステップ数、エラー数、スキップ数を埋め込んだ文字列
                text: qsTr("Completed steps: %1, errors: %2, skipped: %3")
                      .arg(resultDialog.resultData.doneCount || 0)
                      .arg(resultDialog.resultData.errorCount || 0)
                      .arg(resultDialog.resultData.skippedCount || 0)
                font.pixelSize: 12                                          // 文字サイズ
            }

            // エラーメッセージ
            Label {
                Layout.fillWidth: true                            // 親の幅いっぱいに広げる
                // 完了以外の結果でエラーメッセージがある場合のみ表示する
                visible: resultDialog.outcome !== "completed" && (resultDialog.resultData.errorMessage || "") !== ""
                text: resultDialog.resultData.errorMessage || ""  // バックエンドのエラーメッセージ
                textFormat: Text.PlainText                        // 外部由来の文字列をHTMLとして解釈させない
                color: "#FF9800"                                  // 警告色 (オレンジ)
                font.pixelSize: 12                                // 文字サイズ
                wrapMode: Text.WordWrap                           // 幅に収まらない場合は単語単位で折り返す
            }

            // 再起動やサービス再起動の案内
            Frame {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる
                // 再起動が必要、再起動を推奨、または再起動が必要なサービスがある場合のみ表示する
                visible: resultDialog.resultData.rebootNeeded === true
                         || resultDialog.resultData.restartSuggested === true
                         || resultDialog.servicesToRestart.length > 0

                // 案内枠の背景 (薄いオレンジ)
                background: Rectangle {
                    color: Qt.rgba(1.0, 0.6, 0.0, 0.12)  // 薄いオレンジの背景色
                    radius: 4                            // 角の丸み
                    border.color: "#FF9800"              // 枠線の色 (オレンジ)
                }

                // 案内文を縦に並べる
                ColumnLayout {
                    anchors.fill: parent  // 枠の内側全体を使う
                    spacing: 6            // 案内文の間隔

                    // システム再起動が必要な旨の案内
                    Label {
                        Layout.fillWidth: true                                             // 親の幅いっぱいに広げる
                        visible: resultDialog.resultData.rebootNeeded === true             // 再起動が必要な場合のみ表示する
                        text: qsTr("A system reboot is required to complete the update.")  // 案内文
                        font.bold: true                                                    // 注意を引くため太字で表示する
                        wrapMode: Text.WordWrap                                            // 幅に収まらない場合は単語単位で折り返す
                    }

                    // アプリケーションの再起動を推奨する旨の案内
                    Label {
                        Layout.fillWidth: true   // 親の幅いっぱいに広げる
                        // システム再起動は不要だが、再起動を推奨する場合のみ表示する
                        visible: resultDialog.resultData.rebootNeeded !== true
                                 && resultDialog.resultData.restartSuggested === true
                        text: qsTr("Restarting the package manager or affected applications is recommended.")
                        wrapMode: Text.WordWrap  // 幅に収まらない場合は単語単位で折り返す
                    }

                    // 再起動が必要なサービスがある旨の案内
                    Label {
                        Layout.fillWidth: true                              // 親の幅いっぱいに広げる
                        visible: resultDialog.servicesToRestart.length > 0  // 該当サービスがある場合のみ表示する
                        // 再起動すべきサービスの案内文
                        text: qsTr("The following services use updated files and should be restarted:")
                        wrapMode: Text.WordWrap                             // 幅に収まらない場合は単語単位で折り返す
                    }

                    // 再起動が必要なサービスの一覧 (選択してコピーできる)
                    TextEdit {
                        Layout.fillWidth: true                              // 親の幅いっぱいに広げる
                        Layout.leftMargin: 16                               // 案内文より字下げする
                        visible: resultDialog.servicesToRestart.length > 0  // 該当サービスがある場合のみ表示する
                        text: resultDialog.servicesToRestart.join("\n")     // 1行1サービスの一覧
                        textFormat: TextEdit.PlainText                      // HTMLとして解釈させない
                        color: palette.text                                 // 通常の文字色
                        font.family: "monospace"                            // 等幅フォント
                        font.pixelSize: 12                                  // 文字サイズ
                        readOnly: true                                      // 表示専用で編集させない
                        selectByMouse: true                                 // マウスで文字列を選択できるようにする
                        Accessible.role: Accessible.EditableText            // スクリーンリーダー向けの役割 (テキスト欄)
                        Accessible.name: qsTr("Services to restart")        // スクリーンリーダー向けの名前
                    }
                }
            }

            // 案内とパッケージセクションの区切り線
            Separator {}

            // ライセンス拒否で今回の適用対象から除外したパッケージを表示する
            PackageSection {
                heading: qsTr("Excluded because the license was declined:")  // 除外したパッケージの見出し
                items: resultDialog.rejectedLicensePackages                    // 結果に記録した除外パッケージ一覧
            }

            // パッケージセクション
            Label {
                text: qsTr("Packages")    // セクション見出し
                color: palette.highlight  // 見出し用の強調色
                font.bold: true           // 太字で表示する
                font.pixelSize: 13        // 文字サイズ
            }

            // インストールしたパッケージ
            PackageSection {
                heading: qsTr("Installed packages: %1").arg(count)        // 件数付きの見出し
                items: resultDialog.resultData.installedPackages || []    // インストールしたパッケージの一覧
                count: resultDialog.resultData.installed || items.length  // バックエンドの件数 (無ければ一覧の件数)
            }

            // 更新したパッケージ
            PackageSection {
                heading: qsTr("Updated packages: %1").arg(count)        // 件数付きの見出し
                items: resultDialog.resultData.updatedPackages || []    // 更新したパッケージの一覧
                count: resultDialog.resultData.updated || items.length  // バックエンドの件数 (無ければ一覧の件数)
            }

            // 削除したパッケージ
            PackageSection {
                heading: qsTr("Removed packages: %1").arg(count)        // 件数付きの見出し
                items: resultDialog.resultData.removedPackages || []    // 削除したパッケージの一覧
                count: resultDialog.resultData.removed || items.length  // バックエンドの件数 (無ければ一覧の件数)
            }

            // 失敗したパッケージ
            PackageSection {
                heading: qsTr("Failed packages: %1").arg(count)      // 件数付きの見出し
                items: resultDialog.resultData.failedPackages || []  // 失敗したパッケージの一覧
                textColor: "#F44336"                                 // エラー色 (赤)
            }

            // 利用者が[Ignore]を選んだパッケージを一覧表示する
            PackageSection {
                heading: qsTr("Ignored packages: %1").arg(count)      // 件数付きの見出し
                items: resultDialog.resultData.ignoredPackages || []  // 無視したパッケージの一覧
                textColor: "#FF9800"                                  // 警告色 (オレンジ)
            }

            // スキップしたパッケージ
            PackageSection {
                heading: qsTr("Skipped packages: %1").arg(count)      // 件数付きの見出し
                items: resultDialog.resultData.skippedPackages || []  // スキップしたパッケージの一覧
                textColor: "#FF9800"                                  // 警告色 (オレンジ)
            }

            // スナップショット (snapper)
            Separator {
                visible: resultDialog.snapshots.length > 0  // スナップショットがある場合のみ表示する
            }

            // スナップショットセクションの見出し
            Label {
                visible: resultDialog.snapshots.length > 0  // スナップショットがある場合のみ表示する
                text: qsTr("Snapshots")                     // セクション見出し
                color: palette.highlight                    // 見出し用の強調色
                font.bold: true                             // 太字で表示する
                font.pixelSize: 13                          // 文字サイズ
            }

            // ロールバック方法の案内
            Label {
                Layout.fillWidth: true                      // 親の幅いっぱいに広げる
                visible: resultDialog.snapshots.length > 0  // スナップショットがある場合のみ表示する
                // snapper rollbackで戻せる旨の案内文
                text: qsTr("Snapper created the following snapshots. You can roll back with \"snapper rollback\" if needed.")
                font.pixelSize: 12                          // 文字サイズ
                wrapMode: Text.WordWrap                     // 幅に収まらない場合は単語単位で折り返す
            }

            // 作成されたスナップショットの一覧 (選択してコピーできる)
            TextEdit {
                Layout.fillWidth: true                                                    // 親の幅いっぱいに広げる
                Layout.leftMargin: 16                                                     // 案内文より字下げする
                visible: resultDialog.snapshots.length > 0                                // スナップショットがある場合のみ表示する
                text: resultDialog.snapshots.map(resultDialog.formatSnapshot).join("\n")  // 1行1件の一覧
                textFormat: TextEdit.PlainText                                            // HTMLとして解釈させない
                color: palette.text                                                       // 通常の文字色
                font.family: "monospace"                                                  // 等幅フォント
                font.pixelSize: 12                                                        // 文字サイズ
                readOnly: true                                                            // 表示専用で編集させない
                selectByMouse: true                                                       // マウスで文字列を選択できるようにする
                Accessible.role: Accessible.EditableText                                  // スクリーンリーダー向けの役割 (テキスト欄)
                Accessible.name: qsTr("Snapshots")                                        // スクリーンリーダー向けの名前
            }

            // RPMスクリプト出力
            Separator {
                visible: resultDialog.scriptLines.length > 0  // スクリプト出力がある場合のみ表示する
            }

            // スクリプト出力セクションの見出し
            Label {
                visible: resultDialog.scriptLines.length > 0  // スクリプト出力がある場合のみ表示する
                text: qsTr("Package script output")           // セクション見出し
                color: palette.highlight                      // 見出し用の強調色
                font.bold: true                               // 太字で表示する
                font.pixelSize: 13                            // 文字サイズ
            }

            // 出力が切り詰められた旨の案内
            Label {
                Layout.fillWidth: true                                                               // 親の幅いっぱいに広げる
                visible: resultDialog.resultData.scriptOutputTruncated === true                      // 出力が切り詰められた場合のみ表示する
                text: qsTr("The output was truncated. See /var/log/zypp/history for the full log.")  // 案内文
                color: "#FF9800"                                                                     // 警告色 (オレンジ)
                font.pixelSize: 12                                                                   // 文字サイズ
                wrapMode: Text.WordWrap                                                              // 幅に収まらない場合は単語単位で折り返す
            }

            // スクリプト出力のスクロール領域
            ScrollView {
                Layout.fillWidth: true                                                 // 親の幅いっぱいに広げる
                Layout.preferredHeight: Math.min(200, scriptText.implicitHeight + 12)  // 内容に合わせた高さ (最大200px)
                visible: resultDialog.scriptLines.length > 0                           // スクリプト出力がある場合のみ表示する

                // スクリプト出力の表示欄 (選択してコピーできる)
                TextArea {
                    id: scriptText  // スクリプト出力欄の参照名

                    text: resultDialog.scriptLines.join("\n")       // スクリプト出力とRPMメッセージ
                    textFormat: TextEdit.PlainText                  // HTMLとして解釈させない
                    font.family: "monospace"                        // 等幅フォント
                    font.pixelSize: 12                              // 文字サイズ
                    wrapMode: TextEdit.NoWrap                       // 折り返さない
                    readOnly: true                                  // 表示専用で編集させない
                    selectByMouse: true                             // マウスで文字列を選択できるようにする
                    Accessible.name: qsTr("Package script output")  // スクリーンリーダー向けの名前
                }
            }

            // スクリプト出力と統計セクションの区切り線
            Separator {}

            // 統計セクション
            Label {
                text: qsTr("Statistics")  // セクション見出し
                color: palette.highlight  // 見出し用の強調色
                font.bold: true           // 太字で表示する
                font.pixelSize: 13        // 文字サイズ
            }

            // 経過時間とサイズの統計を項目名と値の組で表示する領域
            GridLayout {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる
                columns: 2              // 項目名と値の2列
                columnSpacing: 12       // 列の間隔
                rowSpacing: 4           // 行の間隔

                // [Elapsed time:]の項目名
                Label {
                    text: "\u2022 " + qsTr("Elapsed time:")  // 箇条書き記号付きの項目名
                }

                // 経過時間 (分:秒)
                Label {
                    // 経過秒数を"mm:ss"形式へ整形する
                    text: {
                        var secs = resultDialog.resultData.elapsedSeconds || 0
                        var min = Math.floor(secs / 60)
                        var sec = Math.floor(secs % 60)
                        return String(min).padStart(2, '0') + ":" + String(sec).padStart(2, '0')
                    }
                }

                // [Total installed size:]の項目名
                Label {
                    text: "\u2022 " + qsTr("Total installed size:")  // 箇条書き記号付きの項目名
                }

                // インストールサイズの合計
                Label {
                    text: Util.formatBytes(resultDialog.resultData.totalInstalledSize || 0)  // 単位付きのサイズ
                }

                // [Total download size:]の項目名
                Label {
                    text: "\u2022 " + qsTr("Total download size:")  // 箇条書き記号付きの項目名
                }

                // ダウンロードサイズの合計
                Label {
                    text: Util.formatBytes(resultDialog.resultData.totalDownloadSize || 0)  // 単位付きのサイズ
                }
            }
        }
    }
}
