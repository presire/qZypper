/**
 * @file HistoryDialog.qml
 * @brief パッケージ操作の履歴を検索して表示するダイアログ
 *
 * Main.qmlから開かれる
 *
 * 入力 : PackageControllerの履歴モデル
 * 出力 : なし
 * 依存 : PackageController (D-Busバックエンドの履歴取得API)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

/*
 * libzyppインストール履歴ビューア (GetHistory)
 * offset/limit (500件) でページング取得し、テキスト/アクション/日付範囲で絞り込む
 * バックエンドフィルタキー : since, until (epoch ms), limit, offset, actions (as)
 */
Dialog {
    id: historyDialog  // ダイアログ自身の参照名

    // ページング状態
    property int pageSize: 500         // 1ページの取得件数
    property var entries: []           // サーバから取得した全件 (未フィルタ)
    property bool appending: false     // true: 追記モード
    // 直近の取得バッチの件数 (Load Moreの表示判定に使う)
    property int lastBatch: 0          // 直近バッチの件数
    // フィルタ用のアクション名一覧 (先頭は"All")
    property var actionNames: ["All"]  // フィルタのアクション名一覧

    /*
     * @brief フィルタを適用した表示用行配列を返す (クライアント側絞り込み)
     * @return フィルタ適用後の表示用行配列
     */
    function displayRows() {
        var out = []
        var needle = filterField.text.trim().toLowerCase()
        var act = actionCombo.currentIndex > 0 ? actionNames[actionCombo.currentIndex] : ""
        for (var i = 0; i < entries.length; ++i) {
            var e = entries[i]

            // アクションで絞り込み中の場合は、一致しない行を除外する
            if (act && String(e.action || "") !== act) {
                continue
            }

            // テキストで絞り込み中の場合は、名前・操作・エディション・リポジトリ・要求者・コマンドのいずれにも含まれない行を除外する
            if (needle !== "") {
                var hay = (String(e.name || "") + " " + String(e.action || "") + " "
                           + String(e.edition || "") + " " + String(e.repoAlias || "") + " "
                           + String(e.requestedBy || "") + " " + String(e.command || "")).toLowerCase()

                // 検索文字列を含まない行は除外する
                if (hay.indexOf(needle) < 0) {
                    continue
                }
            }

            out.push(e)
        }
        return out
    }

    /*
     * @brief GetHistoryを発行する、append=trueのときoffsetを進めて追記取得する
     * @param append trueなら既存分の続きを追記取得する
     */
    function runQuery(append) {
        var filter = { limit: pageSize, offset: append ? entries.length : 0 }
        var s = sinceField.text.trim()
        var u = untilField.text.trim()

        // 開始日 (YYYY-MM-DD) が入力されている場合は、その日の0時以降に絞り込む
        if (s.length >= 10) {
            var ms = new Date(s + "T00:00:00").getTime()

            // 日付として解釈できない場合は条件に加えない
            if (!isNaN(ms)) {
                filter.since = ms
            }
        }

        // 終了日 (YYYY-MM-DD) が入力されている場合は、その日の23時59分59秒以前に絞り込む
        if (u.length >= 10) {
            var mu = new Date(u + "T23:59:59").getTime()

            // 日付として解釈できない場合は条件に加えない
            if (!isNaN(mu)) {
                filter.until = mu
            }
        }

        // アクションを選択している場合は、バックエンド側でも絞り込む
        if (actionCombo.currentIndex > 0) {
            filter.actions = [actionNames[actionCombo.currentIndex]]
        }

        appending = append
        PackageController.loadHistory(filter)
    }

    anchors.centerIn: parent                     // 親ウィンドウの中央に表示する
    width: Math.min(parent.width * 0.92, 1040)   // 親幅の92% (最大1040px)
    height: Math.min(parent.height * 0.85, 700)  // 親の高さの85% (最大700px)
    title: qsTr("Installation History")          // ダイアログのタイトル
    standardButtons: Dialog.Close                // [Close]ボタンのみ表示する
    modal: true                                  // 表示中は背後の画面を操作できないようにする

    // 開くたびに状態を初期化して最新の履歴を取得する
    onOpened: {
        entries = []
        lastBatch = 0
        actionNames = ["All"]
        runQuery(false)
    }

    // フィルタ行、見出し行、履歴一覧、進捗バーを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 8            // 項目の間隔

        // --- フィルタ行 ---
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 8              // 項目の間隔

            // [Filter:]の項目名
            Label {
                text: qsTr("Filter:")  // 項目名の文字列
            }

            // テキストによる絞り込みの入力欄
            TextField {
                id: filterField  // テキスト絞り込み欄の参照名

                Layout.preferredWidth: 180                           // 入力欄の幅
                activeFocusOnTab: true                               // [Tab]キーでフォーカスを移動できるようにする
                placeholderText: qsTr("Text (name, repo, user...)")  // 未入力時に表示する案内
                Accessible.name: qsTr("History text filter")         // スクリーンリーダー向けの名前

                // テキスト変更のたびにクライアント側で絞り込む
                onTextChanged: {
                    historyTable.model = historyDialog.displayRows()
                }
            }

            // [Action:]の項目名
            Label {
                text: qsTr("Action:")  // 項目名の文字列
            }

            // アクションによる絞り込みの選択欄
            ComboBox {
                id: actionCombo  // アクション選択欄の参照名

                Layout.preferredWidth: 140                      // 選択欄の幅
                activeFocusOnTab: true                          // [Tab]キーでフォーカスを移動できるようにする
                model: historyDialog.actionNames                // 取得済み履歴に含まれるアクション名の一覧
                Accessible.name: qsTr("History action filter")  // スクリーンリーダー向けの名前

                // アクション変更のたびに絞り込む
                onActivated: {
                    historyTable.model = historyDialog.displayRows()
                }

                // バックエンド履歴のaction文字列は外部由来
                // Fusion ComboBoxの既定delegate (MenuItemの内部IconLabel) はAutoTextで描画するため、
                // contentItemをPlainTextで上書きする
                // 色・フォント・ハイライトはFusion/MenuItem.qmlの既定ロジック (paletteベース) を再現
                delegate: MenuItem {
                    id: actionItem  // 選択肢1件分の参照名

                    required property var modelData  // 選択肢の表示文字列
                    required property int index      // 選択肢の行番号

                    width: ListView.view.width                                                     // 選択肢一覧の幅いっぱいに広げる
                    text: modelData                                                                // アクション名
                    highlighted: actionCombo.highlightedIndex === index                            // キー操作やホバーで選択中の項目を強調表示する
                    font.weight: actionCombo.currentIndex === index ? Font.DemiBold : Font.Normal  // 現在選択中の項目はやや太字にする
                    hoverEnabled: actionCombo.hoverEnabled                                         // ホバー検出はComboBoxの設定に合わせる

                    // 選択肢の文字列 (外部由来のためPlainTextで表示する)
                    contentItem: Text {
                        text: actionItem.text                 // 選択肢の文字列
                        textFormat: Text.PlainText            // HTMLとして解釈させない
                        // 押下中・強調表示中は強調用の文字色、それ以外は通常の文字色
                        color: actionItem.down || actionItem.highlighted
                               ? actionItem.palette.highlightedText : actionItem.palette.text
                        font: actionItem.font                 // 選択肢と同じフォント
                        verticalAlignment: Text.AlignVCenter  // 上下中央に揃える
                    }
                }
            }

            // [Since:]の項目名
            Label {
                text: qsTr("Since:")  // 項目名の文字列
            }

            // 開始日の入力欄
            TextField {
                id: sinceField  // 開始日入力欄の参照名

                Layout.preferredWidth: 100                                               // 入力欄の幅
                activeFocusOnTab: true                                                   // [Tab]キーでフォーカスを移動できるようにする
                placeholderText: qsTr("YYYY-MM-DD")                                      // 入力形式の案内
                Accessible.name: qsTr("History start date")                              // スクリーンリーダー向けの名前
                Accessible.description: qsTr("Only show entries at or after this date")  // スクリーンリーダー向けの説明
            }

            // [Until:]の項目名
            Label {
                text: qsTr("Until:")  // 項目名の文字列
            }

            // 終了日の入力欄
            TextField {
                id: untilField  // 終了日入力欄の参照名

                Layout.preferredWidth: 100                                                // 入力欄の幅
                activeFocusOnTab: true                                                    // [Tab]キーでフォーカスを移動できるようにする
                placeholderText: qsTr("YYYY-MM-DD")                                       // 入力形式の案内
                Accessible.name: qsTr("History end date")                                 // スクリーンリーダー向けの名前
                Accessible.description: qsTr("Only show entries at or before this date")  // スクリーンリーダー向けの説明
            }

            // [Query]ボタン
            Button {
                enabled: !PackageController.busy            // 処理中は無効化する
                activeFocusOnTab: true                      // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Query")                         // ボタンの文字列
                Accessible.name: qsTr("Run history query")  // スクリーンリーダー向けの名前

                // 現在の条件で履歴を先頭から取得し直す
                onClicked: {
                    historyDialog.runQuery(false)
                }
            }

            // [Load More]ボタンを右寄せにするための余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [Load More]ボタン
            Button {
                // 直近の取得が上限まで達したときだけ追記取得を提示する
                visible: historyDialog.lastBatch >= historyDialog.pageSize  // 続きがある可能性がある場合のみ表示する
                enabled: !PackageController.busy                            // 処理中は無効化する
                activeFocusOnTab: true                                      // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Load More")                                     // ボタンの文字列
                Accessible.name: qsTr("Load more history entries")          // スクリーンリーダー向けの名前

                // 取得済みの続きから追記取得する
                onClicked: {
                    historyDialog.runQuery(true)
                }
            }
        }

        // --- 見出し行 ---
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 6              // 列の間隔

            // [Timestamp]列の見出し
            Label {
                Layout.preferredWidth: 150  // 日時列の幅
                text: qsTr("Timestamp")     // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Action]列の見出し
            Label {
                Layout.preferredWidth: 130  // 操作列の幅
                text: qsTr("Action")        // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Name]列の見出し
            Label {
                Layout.fillWidth: true  // 残りの幅をすべて使う
                text: qsTr("Name")      // 見出しの文字列
                font.bold: true         // 太字で表示する
            }

            // [Edition]列の見出し
            Label {
                Layout.preferredWidth: 120  // エディション列の幅
                text: qsTr("Edition")       // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Arch]列の見出し
            Label {
                Layout.preferredWidth: 60  // アーキテクチャ列の幅
                text: qsTr("Arch")         // 見出しの文字列
                font.bold: true            // 太字で表示する
            }

            // [Repo]列の見出し
            Label {
                Layout.preferredWidth: 100  // リポジトリ列の幅
                text: qsTr("Repo")          // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Requested By]列の見出し
            Label {
                Layout.preferredWidth: 90   // 要求者列の幅
                text: qsTr("Requested By")  // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Command]列の見出し
            Label {
                Layout.preferredWidth: 120  // コマンド列の幅
                text: qsTr("Command")       // 見出しの文字列
                font.bold: true             // 太字で表示する
            }
        }

        // 履歴一覧の枠
        Frame {
            Layout.fillWidth: true   // 親の幅いっぱいに広げる
            Layout.fillHeight: true  // 残りの高さをすべて使う

            // 履歴一覧のスクロール領域
            ScrollView {
                anchors.fill: parent          // 枠の内側全体を使う
                clip: true                    // 領域外にはみ出した部分を描画しない
                contentWidth: availableWidth  // 横スクロールさせず幅に合わせる

                // 履歴の一覧
                ListView {
                    id: historyTable  // 履歴一覧の参照名

                    clip: true                                                    // 領域外にはみ出した部分を描画しない
                    model: historyDialog.displayRows()                            // 絞り込み後の履歴行
                    boundsBehavior: Flickable.StopAtBounds                        // 端を越えてスクロールさせない
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する

                    // 履歴1件分の行
                    delegate: RowLayout {
                        required property var modelData  // 1行分の履歴データ

                        /**
                         * @brief 履歴行の時刻を表示用の文字列へ整形する
                         * @param e 履歴行
                         * @return "yyyy-MM-dd hh:mm:ss"形式の時刻、無効なら空文字
                         */
                        function fmtTime(e) {
                            var t = Number(e.timestamp !== undefined ? e.timestamp
                                              : (e.time !== undefined ? e.time : e.date))

                            // 時刻が無効または未設定の場合は空文字を返す
                            if (isNaN(t) || t === 0) {
                                return ""
                            }

                            // 1e11未満は秒単位とみなしてミリ秒へ変換する
                            if (t < 1e11) {
                                t *= 1000
                            }

                            return Qt.formatDateTime(new Date(t), "yyyy-MM-dd hh:mm:ss")
                        }

                        width: historyTable.width - 4  // 一覧の幅から右側の余白を除いた幅

                        // 操作の日時
                        Label {
                            Layout.preferredWidth: 150  // 見出しの列幅に合わせる
                            text: fmtTime(modelData)    // 整形済みの日時
                            elide: Text.ElideRight      // 幅に収まらない場合は末尾を省略する
                        }

                        // 操作の種類
                        Label {
                            Layout.preferredWidth: 130            // 見出しの列幅に合わせる
                            text: String(modelData.action || "")  // 操作の種類
                            textFormat: Text.PlainText            // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight                // 幅に収まらない場合は末尾を省略する
                        }

                        // パッケージ名
                        Label {
                            Layout.fillWidth: true              // 残りの幅をすべて使う
                            text: String(modelData.name || "")  // パッケージ名
                            textFormat: Text.PlainText          // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight              // 幅に収まらない場合は末尾を省略する
                        }

                        // エディション (バージョン-リリース)
                        Label {
                            Layout.preferredWidth: 120             // 見出しの列幅に合わせる
                            text: String(modelData.edition || "")  // エディション (バージョン-リリース)
                            textFormat: Text.PlainText             // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight                 // 幅に収まらない場合は末尾を省略する
                        }

                        // アーキテクチャ
                        Label {
                            Layout.preferredWidth: 60           // 見出しの列幅に合わせる
                            text: String(modelData.arch || "")  // アーキテクチャ
                            textFormat: Text.PlainText          // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight              // 幅に収まらない場合は末尾を省略する
                        }

                        // リポジトリの別名
                        Label {
                            Layout.preferredWidth: 100               // 見出しの列幅に合わせる
                            text: String(modelData.repoAlias || "")  // リポジトリの別名
                            textFormat: Text.PlainText               // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight                   // 幅に収まらない場合は末尾を省略する
                        }

                        // 操作を要求したユーザー
                        Label {
                            Layout.preferredWidth: 90                  // 見出しの列幅に合わせる
                            text: String(modelData.requestedBy || "")  // 操作を要求したユーザー
                            textFormat: Text.PlainText                 // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight                     // 幅に収まらない場合は末尾を省略する
                        }

                        // 操作に使われたコマンド
                        Label {
                            Layout.preferredWidth: 120             // 見出しの列幅に合わせる
                            text: String(modelData.command || "")  // 操作に使われたコマンド
                            textFormat: Text.PlainText             // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight                 // 幅に収まらない場合は末尾を省略する
                        }
                    }
                }
            }

            // 履歴がない場合のメッセージ
            Label {
                anchors.centerIn: parent                                      // 枠の中央に配置する
                // 空かつ非処理中にのみ空表示を出す
                visible: historyTable.count === 0 && !PackageController.busy  // 履歴が空で処理中でない場合のみ表示する
                text: qsTr("No history entries found")                        // メッセージの文字列
                color: palette.placeholderText                                // 補足情報として控えめな文字色
            }
        }

        // 履歴取得中の進捗バー
        ProgressBar {
            Layout.fillWidth: true                    // 親の幅いっぱいに広げる
            // 取得中のみ進捗バーを表示する
            visible: PackageController.busy           // 処理中のみ表示する
            indeterminate: true                       // 進捗率が不明なため往復表示にする
            Accessible.name: qsTr("Loading history")  // スクリーンリーダー向けの名前
        }
    }

    // 履歴取得完了の受け口
    Connections {
        target: PackageController  // 履歴取得の結果を通知するPackageController

        /**
         * @brief 履歴取得の結果を反映し、追記時は既存分へ連結する
         *
         * 取得後にaction列のdistinct集合を再構築し、選択位置を維持したままフィルタを更新する
         *
         * @param result 取得した履歴行の配列
         */
        function onHistoryResultReady(result) {
            // 追記モードでは既存分へ連結し、それ以外は置き換える
            if (historyDialog.appending) {
                historyDialog.entries = historyDialog.entries.concat(result)
            }
            else {
                historyDialog.entries = result
            }
            historyDialog.lastBatch = result.length

            // 取得済みの全履歴からアクション名の重複を除いて集める
            var seen = {}
            for (var i = 0; i < historyDialog.entries.length; ++i) {
                var a = String(historyDialog.entries[i].action || "")

                // 空のアクション名は除外する
                if (a !== "") {
                    seen[a] = true
                }
            }

            // 先頭を"All"とし、アクション名を昇順に並べる
            var names = ["All"]
            var keys = Object.keys(seen).sort()
            for (var k = 0; k < keys.length; ++k) {
                names.push(keys[k])
            }

            var keep = actionCombo.currentIndex
            actionNames = names
            actionCombo.currentIndex = Math.min(keep, names.length - 1)
        }
    }
}
