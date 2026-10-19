/**
 * @file CommitProgressDialog.qml
 * @brief パッケージ変更の適用中に進捗と確認要求を表示するダイアログ
 *
 * Main.qmlから開かれ、完了まで操作を抑止する
 *
 * 入力 : PackageControllerのコミット進捗とプロンプト情報
 * 出力 : ユーザー応答と中止要求
 * 依存 : PackageController (D-Busバックエンドのコミット進捗シグナル)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// パッケージ変更の適用中に、現在の処理、未処理/処理済みの一覧、スクリプト出力、全体の進捗を表示するダイアログ
Dialog {
    id: commitProgressDialog  // ダイアログ自身の参照名

    // 完了後にサマリーページを表示するか (初期値は保存設定)
    property bool showSummaryPage: commitSettings.showSummaryPage  // サマリー表示設定
    // 全体の進捗率 (0-100)
    property int overallPercent: 0                                 // 全体の進捗率

    // 現在実行中の単一アクティビティ (カード表示用)
    property string currentPackage: ""   // 現在のパッケージ名
    // 現在の処理段階 (downloading/installing/removing)
    property string currentStage: ""     // 現在の処理段階
    // 現在パッケージの進捗率 (0-100)
    property int currentPercent: 0       // 現在の進捗率
    // 現在の処理が削除かどうか
    property bool currentRemoval: false  // 削除処理中かどうか

    // PackageStateChanged未対応の旧バックエンド用:
    // CommitProgressChangedのstage/packageName変化から状態遷移を推定
    property string _prevPkg: ""          // 直前のパッケージ名
    // 直前の処理段階 (状態遷移の推定用)
    property string _prevStage: ""        // 直前の処理段階
    // PackageStateChangedを受信済みかどうか (受信後は推定を行わない)
    property bool _hasStateSignal: false  // 状態シグナル受信済みか

    // 蓄積したスクリプト出力の行
    property var scriptLines: []                 // 蓄積したスクリプト出力
    // スクリプト出力のうちエラー行の数
    property int scriptErrorCount: 0             // 出力中のエラー行数
    // RPMスクリプト出力 (メモリ肥大化を防ぐため最新kMaxScriptLines行のみ保持)
    readonly property int kMaxScriptLines: 2000  // 保持する出力行の上限

    /**
     * @brief スクリプト出力の1行を蓄積し、保持上限を超えた分は古い行から捨てる
     * @param entry パッケージ名と行とエラー種別を持つ出力要素
     */
    function appendScriptLine(entry) {
        var prefix = entry.package ? ("[" + entry.package + "] ") : ""
        var lines = scriptLines
        lines.push(prefix + (entry.line || ""))

        // 保持上限を超えた場合は古い行から捨てる
        if (lines.length > kMaxScriptLines) {
            lines.splice(0, lines.length - kMaxScriptLines)
        }

        // エラー行の場合はエラー件数を数える
        if (entry.isError === true) {
            scriptErrorCount++
        }

        // 配列を再代入して表示を更新させる
        scriptLines = lines
    }

    // --- モデル操作ヘルパー (重複なし / 現在カードを持つ堅牢な検索・移動) ---

    /**
     * @brief モデル内で名前が一致する行のインデックスを返す
     * @param model 検索対象のモデル
     * @param name 検索するパッケージ名
     * @return 見つかった行番号、無ければ-1
     */
    function indexOf(model, name) {
        for (var i = 0; i < model.count; i++) {
            // 名前が一致した行の番号を返す
            if (model.get(i).name === name) {
                return i
            }
        }
        return -1
    }

    /**
     * @brief 指定パッケージをPending一覧から取り除く
     * @param name 対象のパッケージ名
     */
    function removeFromPending(name) {
        var idx = indexOf(pendingModel, name)

        // Pending一覧にある場合のみ取り除く
        if (idx >= 0) {
            pendingModel.remove(idx)
        }
    }

    /**
     * @brief Pending一覧の対象行のダウンロード完了フラグを更新する
     * @param name 対象のパッケージ名
     * @param done ダウンロード完了かどうか
     */
    function setPendingDownloadDone(name, done) {
        var idx = indexOf(pendingModel, name)

        // Pending一覧にある場合のみ更新する
        if (idx >= 0) {
            pendingModel.setProperty(idx, "downloadDone", done)
        }
    }

    /**
     * @brief ダウンロード完了を記録する
     *
     * 現在カードが同一パッケージならPendingへ戻し、Pendingに無ければ
     * ダウンロード済みとして追加する (重複を防ぐ)
     *
     * @param name 対象のパッケージ名
     * @note 戻り値は無く、現在カードが同一パッケージなら早期に戻る
     */
    function markDownloaded(name) {
        // 現在カードでダウンロード中のパッケージなら、ダウンロード済みとしてPendingへ戻す
        if (currentPackage === name && currentStage === "downloading") {
            appendPending(name, false, true)
            clearCurrent()
            return
        }

        // Pendingにあればフラグを更新し、どちらの一覧にも無ければダウンロード済みとして追加する
        if (indexOf(pendingModel, name) >= 0) {
            setPendingDownloadDone(name, true)
        }
        else if (indexOf(doneModel, name) < 0) {
            appendPending(name, false, true)
        }
    }

    /**
     * @brief パッケージをPending一覧へ追加する (既存なら何もしない)
     * @param name 対象のパッケージ名
     * @param isRemoval 削除対象かどうか
     * @param downloadDone ダウンロード完了かどうか
     */
    function appendPending(name, isRemoval, downloadDone) {
        // 重複を防ぐため、Pending一覧に無い場合のみ追加する
        if (indexOf(pendingModel, name) < 0) {
            pendingModel.append({
                name: name,
                isRemoval: isRemoval,
                downloadDone: downloadDone
            })
        }
    }

    /**
     * @brief パッケージをDone一覧へ追加する (既存なら何もしない)
     * @param name 対象のパッケージ名
     * @param isRemoval 削除対象かどうか
     */
    function appendDone(name, isRemoval) {
        // 重複を防ぐため、Done一覧に無い場合のみ追加する
        if (indexOf(doneModel, name) < 0) {
            doneModel.append({ name: name, isRemoval: isRemoval })
        }
    }

    /**
     * @brief 現在カードの表示内容を初期化する
     */
    function clearCurrent() {
        currentPackage = ""
        currentStage = ""
        currentPercent = 0
        currentRemoval = false
    }

    /**
     * @brief 現在カードの処理を完了させる
     *
     * ダウンロード途中ならPendingへ返し、install/remove完了ならDoneへ移す
     *
     * @note 戻り値は無く、現在カードが空なら早期に戻る
     */
    function finalizeCurrent() {
        // 現在カードが空の場合は何もしない
        if (currentPackage === "") {
            return
        }

        if (currentStage === "downloading") {
            // ダウンロードのみ完了 → ダウンロード済みとしてPendingへ戻す
            appendPending(currentPackage, false, true)
        }
        else {
            // install / remove完了 → Doneへ
            appendDone(currentPackage, currentRemoval)
        }

        clearCurrent()
    }

    /**
     * @brief 新規アクティビティを現在カードに表示する
     *
     * カードが別パッケージで占有中なら先に完了させる
     *
     * @param name 対象のパッケージ名
     * @param stage 処理段階 (downloading/installing/removing)
     * @param isRemoval 削除対象かどうか
     */
    function startCurrent(name, stage, isRemoval) {
        // 現在カードが別パッケージで占有中なら先に完了させる
        if (currentPackage !== "" && currentPackage !== name) {
            finalizeCurrent()
        }

        removeFromPending(name)
        currentPackage = name
        currentStage = stage
        currentRemoval = isRemoval
    }

    /**
     * @brief 処理段階に対応する表示用の文言を返す
     * @param stage 処理段階
     * @return 表示用の段階名
     */
    function phaseText(stage) {
        switch (stage) {
            case "downloading": return qsTr("Downloading")
            case "installing": return qsTr("Installing")
            case "removing": return qsTr("Removing")
            default: return qsTr("Working")
        }
    }

    /**
     * @brief Pending行に表示するタグ文言を返す
     * @param isRemoval 削除対象かどうか
     * @param downloadDone ダウンロード完了かどうか
     * @return 状態タグの文言
     */
    function tagText(isRemoval, downloadDone) {
        // 削除対象の場合
        if (isRemoval) {
            return qsTr("to remove")
        }

        // ダウンロード済みの場合
        if (downloadDone) {
            return qsTr("downloaded")
        }

        return qsTr("to download")
    }

    /**
     * @brief 保留中の変更一覧から初期状態を組み立てる
     *
     * 名前順に並べ替えてPendingへ投入し、進捗と状態をリセットする
     *
     * @param pendingList バックエンドの保留中変更一覧
     * @note 戻り値は無く、内部の並べ替え比較関数のみ値を返す
     */
    function initFromPendingChanges(pendingList) {
        pendingModel.clear()
        doneModel.clear()
        overallPercent = 0
        currentPackage = ""
        currentStage = ""
        currentPercent = 0
        currentRemoval = false
        _prevPkg = ""
        _prevStage = ""
        _hasStateSignal = false
        scriptLines = []
        scriptErrorCount = 0

        var sorted = pendingList.slice().sort(function(a, b) {
            return a.name.localeCompare(b.name)
        })

        for (var i = 0; i < sorted.length; i++) {
            var statusVal = sorted[i].status || 0
            // ステータス値6と7は削除系の操作
            var isRemoval = (statusVal === 6 || statusVal === 7)
            pendingModel.append({
                name: sorted[i].name,
                isRemoval: isRemoval,
                downloadDone: false
            })
        }
    }

    anchors.centerIn: parent                       // 親ウィンドウの中央に表示する
    width: Math.min(root.width * 0.85, 800)        // メインウィンドウの幅の85% (最大800px)
    height: Math.min(root.height * 0.9, 800)       // メインウィンドウの高さの90% (最大800px)
    title: qsTr("Installing / Removing Packages")  // ダイアログのタイトル
    standardButtons: Dialog.NoButton               // 標準ボタンは使わず、独自のボタン行を使う
    modal: true                                    // 表示中は背後の画面を操作できないようにする
    closePolicy: Dialog.NoAutoClose                // 処理中は[Esc]キーや外側のクリックでは閉じない

    // ダイアログの背景 (強調色の枠線付き)
    background: Rectangle {
        color: palette.window            // ウィンドウと同じ背景色
        border.color: palette.highlight  // 枠線の色 (強調色)
        border.width: 2                  // 枠線の太さ
    }

    // ダイアログのタイトル行
    header: Label {
        padding: 10                       // 文字の周囲の余白
        text: commitProgressDialog.title  // ダイアログのタイトル
        font.bold: true                   // 太字で表示する
    }

    // 標準のフッター領域は使わないため、高さ0の空アイテムにする
    footer: Item {
        implicitHeight: 0  // フッターの高さを0にする
    }

    // 未処理のパッケージを表示するモデル
    ListModel {
        id: pendingModel  // 未処理パッケージのモデル
    }

    // 処理済みのパッケージを表示するモデル
    ListModel {
        id: doneModel  // 処理済みパッケージのモデル
    }

    // サマリーページ表示の設定を永続化する
    Settings {
        id: commitSettings  // 設定の参照名

        // サマリーページを表示するかの保存値
        property bool showSummaryPage: true  // サマリー表示設定を保存

        category: "commitProgress"  // 設定ファイル内のグループ名
    }

    // 現在の操作カード、PendingとDoneの2列、スクリプト出力、フッター行を縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 10           // 項目の間隔

        // 現在の操作カード (常時表示)
        Frame {
            Layout.fillWidth: true       // 親の幅いっぱいに広げる
            Layout.preferredHeight: 100  // カードの高さ

            // カードの背景 (薄い強調色)
            background: Rectangle {
                // 強調色を薄くした背景色
                color: Qt.rgba(palette.highlight.r, palette.highlight.g,
                               palette.highlight.b, 0.12)
                radius: 4                        // 角の丸み
                border.color: palette.highlight  // 枠線の色 (強調色)
                border.width: 1                  // 枠線の太さ
            }

            // パッケージ名と段階、進捗バー、進捗率を縦に並べる
            ColumnLayout {
                anchors.fill: parent  // カード全体を使う
                anchors.margins: 10   // 枠との余白
                spacing: 6            // 項目の間隔

                // パッケージ名と処理段階の行
                RowLayout {
                    Layout.fillWidth: true  // 親の幅いっぱいに広げる

                    // 現在のパッケージ名 (削除の場合は先頭に-を付ける)
                    Label {
                        Layout.fillWidth: true      // 残りの幅をすべて使う
                        // 処理中のパッケージ名 (削除なら"- 名前")、処理対象が無い場合は準備中と表示する
                        text: commitProgressDialog.currentPackage !== ""
                            ? (commitProgressDialog.currentRemoval
                               ? ("- " + commitProgressDialog.currentPackage)
                               : commitProgressDialog.currentPackage)
                            : qsTr("Preparing...")
                        textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                        color: palette.text         // 通常の文字色
                        font.bold: true             // 太字で表示する
                        font.pixelSize: 15          // 文字サイズ
                        elide: Text.ElideRight      // 収まらない場合は末尾を省略する
                    }

                    // 現在の処理段階
                    Label {
                        Layout.preferredWidth: 84             // 列の幅
                        // 処理中なら段階名を、処理対象が無い場合は待機中と表示する
                        text: commitProgressDialog.currentPackage !== ""
                            ? commitProgressDialog.phaseText(commitProgressDialog.currentStage)
                            : qsTr("Idle")
                        // 処理中なら強調色、待機中なら控えめな文字色
                        color: commitProgressDialog.currentPackage !== ""
                            ? palette.highlight
                            : palette.placeholderText
                        font.pixelSize: 12                    // 文字サイズ
                        horizontalAlignment: Text.AlignRight  // 右寄せで表示する
                    }
                }

                // 現在パッケージの進捗バー (対象が無いときはインデターミネート表示)
                ProgressBar {
                    id: packageProgressBar  // パッケージ進捗バーの参照名

                    Layout.fillWidth: true                      // 親の幅いっぱいに広げる
                    from: 0                                     // 最小値
                    to: 100                                     // 最大値
                    value: commitProgressDialog.currentPercent  // 現在パッケージの進捗率
                    // 処理対象が無い場合、または進捗率が0の場合は進捗不明の表示にする
                    indeterminate: commitProgressDialog.currentPackage === ""
                        || commitProgressDialog.currentPercent === 0
                }

                // 進捗率を右寄せで表示する行
                RowLayout {
                    Layout.fillWidth: true  // 親の幅いっぱいに広げる

                    // 進捗率を右寄せにするための余白
                    Item {
                        Layout.fillWidth: true  // 残りの幅をすべて使う
                    }

                    // 現在パッケージの進捗率
                    Label {
                        // 処理中の場合のみ"nn%"を表示する
                        text: commitProgressDialog.currentPackage !== ""
                            ? (commitProgressDialog.currentPercent + "%")
                            : ""
                        color: palette.text  // 通常の文字色
                        font.pixelSize: 12   // 文字サイズ
                    }
                }
            }
        }

        // PendingとDoneの2列 (下部を占有)
        RowLayout {
            Layout.fillWidth: true   // 親の幅いっぱいに広げる
            Layout.fillHeight: true  // 残りの高さをすべて使う
            spacing: 10              // 列の間隔

            // Pending列
            ColumnLayout {
                Layout.fillWidth: true    // 残りの幅を使う
                Layout.fillHeight: true   // 親の高さいっぱいに広げる
                Layout.preferredWidth: 1  // Done列と同じ比率で幅を分け合う

                // Pending列の見出し (件数付き)
                Label {
                    Layout.fillWidth: true                  // 親の幅いっぱいに広げる
                    // 未処理が1件以上なら件数付きの見出しにする
                    text: pendingModel.count > 0
                        ? qsTr("Pending (%1)").arg(pendingModel.count)
                        : qsTr("Pending")
                    color: palette.text                     // 通常の文字色
                    font.bold: true                         // 太字で表示する
                    horizontalAlignment: Text.AlignHCenter  // 中央に表示する
                }

                // 未処理パッケージの一覧
                ListView {
                    id: pendingListView  // 未処理一覧の参照名

                    Layout.fillWidth: true                                        // 親の幅いっぱいに広げる
                    Layout.fillHeight: true                                       // 残りの高さをすべて使う
                    clip: true                                                    // 領域外にはみ出した部分を描画しない
                    model: pendingModel                                           // 未処理パッケージのモデル
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する

                    // 未処理パッケージ1件分の行
                    delegate: ItemDelegate {
                        width: pendingListView.width  // 一覧の幅に合わせる
                        height: 24                    // 行の高さ

                        // 状態タグとパッケージ名を横に並べる
                        contentItem: RowLayout {
                            spacing: 6  // 列の間隔

                            // 状態タグ (削除予定/ダウンロード済み/ダウンロード予定)
                            Label {
                                Layout.preferredWidth: 88  // 列の幅
                                // 状態タグの文言
                                text: commitProgressDialog.tagText(model.isRemoval, model.downloadDone)
                                // ダウンロード済みまたは削除予定の場合は強調色にする
                                color: (model.downloadDone || model.isRemoval)
                                    ? palette.highlight
                                    : palette.text
                                font.pixelSize: 11         // 文字サイズ
                                elide: Text.ElideRight     // 収まらない場合は末尾を省略する
                            }

                            // パッケージ名 (削除の場合は先頭に-を付ける)
                            Label {
                                Layout.fillWidth: true                                    // 残りの幅をすべて使う
                                text: model.isRemoval ? ("- " + model.name) : model.name  // パッケージ名
                                textFormat: Text.PlainText                                // 外部由来の文字列をHTMLとして解釈させない
                                color: palette.text                                       // 通常の文字色
                                font.pixelSize: 12                                        // 文字サイズ
                                verticalAlignment: Text.AlignVCenter                      // 上下中央に表示する
                                elide: Text.ElideRight                                    // 収まらない場合は末尾を省略する
                            }
                        }

                        // 行の背景は描画しない
                        background: null
                    }
                }
            }

            // Pending列とDone列の区切り線
            Rectangle {
                Layout.fillHeight: true   // 親の高さいっぱいに広げる
                Layout.preferredWidth: 1  // 線の太さ
                Layout.minimumWidth: 1    // 線が消えないよう最小幅を1にする
                color: palette.mid        // 線の色
            }

            // Done列
            ColumnLayout {
                Layout.fillWidth: true    // 残りの幅を使う
                Layout.fillHeight: true   // 親の高さいっぱいに広げる
                Layout.preferredWidth: 1  // Pending列と同じ比率で幅を分け合う

                // Done列の見出し (件数付き)
                Label {
                    Layout.fillWidth: true                  // 親の幅いっぱいに広げる
                    // 処理済みが1件以上なら件数付きの見出しにする
                    text: doneModel.count > 0
                        ? qsTr("Done (%1)").arg(doneModel.count)
                        : qsTr("Done")
                    color: palette.placeholderText          // 控えめな文字色
                    font.bold: true                         // 太字で表示する
                    horizontalAlignment: Text.AlignHCenter  // 中央に表示する
                }

                // 処理済みパッケージの一覧
                ListView {
                    id: doneListView  // 処理済み一覧の参照名

                    Layout.fillWidth: true                                        // 親の幅いっぱいに広げる
                    Layout.fillHeight: true                                       // 残りの高さをすべて使う
                    clip: true                                                    // 領域外にはみ出した部分を描画しない
                    model: doneModel                                              // 処理済みパッケージのモデル
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する

                    // 処理済みパッケージ1件分の行
                    delegate: ItemDelegate {
                        width: doneListView.width  // 一覧の幅に合わせる
                        height: 24                 // 行の高さ

                        // 状態タグとパッケージ名を横に並べる
                        contentItem: RowLayout {
                            spacing: 6  // 列の間隔

                            // 状態タグ (完了)
                            Label {
                                Layout.preferredWidth: 88       // 列の幅
                                text: qsTr("Done")              // 状態タグの文言
                                color: palette.placeholderText  // 控えめな文字色
                                font.pixelSize: 11              // 文字サイズ
                                elide: Text.ElideRight          // 収まらない場合は末尾を省略する
                            }

                            // パッケージ名 (削除の場合は先頭に-を付ける)
                            Label {
                                Layout.fillWidth: true                                    // 残りの幅をすべて使う
                                text: model.isRemoval ? ("- " + model.name) : model.name  // パッケージ名
                                textFormat: Text.PlainText                                // 外部由来の文字列をHTMLとして解釈させない
                                color: palette.placeholderText                            // 控えめな文字色
                                font.pixelSize: 12                                        // 文字サイズ
                                verticalAlignment: Text.AlignVCenter                      // 上下中央に表示する
                                elide: Text.ElideRight                                    // 収まらない場合は末尾を省略する
                            }
                        }

                        // 行の背景は描画しない
                        background: null
                    }
                }
            }
        }

        // RPMスクリプト出力 (出力があった場合のみ表示)
        ColumnLayout {
            Layout.fillWidth: true                                // 親の幅いっぱいに広げる
            spacing: 4                                            // 項目の間隔
            // スクリプト出力がある場合のみ表示する
            visible: commitProgressDialog.scriptLines.length > 0  // 出力がある場合のみ表示する

            // 開閉ボタンとエラー件数の行
            RowLayout {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる

                // スクリプト出力の表示/非表示を切り替えるボタン
                ToolButton {
                    id: scriptToggle  // 開閉ボタンの参照名

                    // 開閉状態を表す三角記号と行数付きの文字列
                    text: (checked ? "\u25BE " : "\u25B8 ")
                          + qsTr("Package script output (%1 lines)").arg(commitProgressDialog.scriptLines.length)
                    checkable: true                                      // 押すたびにオン/オフを切り替える
                    checked: false                                       // 初期状態は閉じておく
                    Accessible.name: qsTr("Show package script output")  // スクリーンリーダー向けの名前
                }

                // エラー行の件数バッジ
                Label {
                    // エラー行がある場合のみ件数バッジを表示する
                    visible: commitProgressDialog.scriptErrorCount > 0                       // エラー行がある場合のみ表示する
                    text: qsTr("%1 error lines").arg(commitProgressDialog.scriptErrorCount)  // エラー行の件数
                    color: "#F44336"                                                         // エラー色 (赤)
                    font.pixelSize: 12                                                       // 文字サイズ
                }

                // ボタンとバッジを左寄せにするための余白
                Item {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                }
            }

            // スクリプト出力のスクロール領域
            ScrollView {
                id: scriptScroll  // スクロール領域の参照名

                Layout.fillWidth: true         // 親の幅いっぱいに広げる
                Layout.preferredHeight: 140    // 表示領域の高さ
                visible: scriptToggle.checked  // 開閉ボタンがオンの場合のみ表示する

                // スクリプト出力の表示欄 (選択してコピーできる)
                TextArea {
                    id: scriptArea  // スクリプト出力欄の参照名

                    text: commitProgressDialog.scriptLines.join("\n")  // 蓄積したスクリプト出力
                    textFormat: TextEdit.PlainText                     // HTMLとして解釈させない
                    font.family: "monospace"                           // 等幅フォント
                    font.pixelSize: 12                                 // 文字サイズ
                    wrapMode: TextEdit.NoWrap                          // 折り返さない
                    readOnly: true                                     // 表示専用で編集させない
                    selectByMouse: true                                // マウスで文字列を選択できるようにする
                    Accessible.name: qsTr("Package script output")     // スクリーンリーダー向けの名前

                    // 追記時に末尾へスクロール
                    onTextChanged: {
                        cursorPosition = length
                    }
                }
            }
        }

        // コンパクトなフッター行 : 全体進捗+パーセント+キャンセル+サマリー表示
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 10             // 項目の間隔

            // 全体の進捗バー (開始前はインデターミネート表示)
            ProgressBar {
                id: totalProgressBar  // 全体進捗バーの参照名

                Layout.fillWidth: true                                    // 残りの幅をすべて使う
                from: 0                                                   // 最小値
                to: 100                                                   // 最大値
                value: commitProgressDialog.overallPercent                // 全体の進捗率
                indeterminate: commitProgressDialog.overallPercent === 0  // 進捗率が0の間は進捗不明の表示にする
            }

            // 全体の進捗率
            Label {
                Layout.preferredWidth: 44                        // 列の幅
                text: commitProgressDialog.overallPercent + "%"  // "nn%"形式の進捗率
                font.pixelSize: 13                               // 文字サイズ
                horizontalAlignment: Text.AlignRight             // 右寄せで表示する
            }

            // [Cancel]ボタン
            Button {
                text: qsTr("Cancel")                                   // ボタンの文字列
                Accessible.name: qsTr("Cancel the running operation")  // スクリーンリーダー向けの名前

                // 実行中の処理の中止をPackageControllerへ要求する
                onClicked: {
                    PackageController.cancelOperation()
                }
            }

            // サマリーページ表示の切替 (設定にも保存)
            CheckBox {
                text: qsTr("Show Summary Page")                // チェックボックスの文字列
                checked: commitProgressDialog.showSummaryPage  // 現在のサマリー表示設定

                // 切り替えた値をダイアログと保存設定の両方へ反映する
                onToggled: {
                    commitProgressDialog.showSummaryPage = checked
                    commitSettings.showSummaryPage = checked
                }
            }
        }
    }

    // コミット進捗と出力と完了をバックエンドから受け取る
    Connections {
        target: PackageController  // コミット進捗を通知するPackageController

        /**
         * @brief パッケージ状態の変化イベントを現在カードと各一覧へ反映する
         * @param packageName 対象のパッケージ名
         * @param event 発生したイベント名 (download_startやinstall_endなど)
         */
        function onPackageStateChanged(packageName, event) {
            commitProgressDialog._hasStateSignal = true
            switch (event) {
                // ダウンロード開始 : 現在カードにダウンロード中として表示する
                case "download_start":
                    commitProgressDialog.currentPercent = 0
                    commitProgressDialog.startCurrent(packageName, "downloading", false)
                    break
                // ダウンロード完了 : ダウンロード済みとして記録する
                case "download_end":
                    commitProgressDialog.markDownloaded(packageName)
                    break
                // キャッシュ済み : ダウンロード不要のため、ダウンロード済みとして記録する
                case "cached":
                    commitProgressDialog.markDownloaded(packageName)
                    break
                // インストール開始 : 現在カードにインストール中として表示する
                case "install_start":
                    commitProgressDialog.currentPercent = 0
                    commitProgressDialog.startCurrent(packageName, "installing", false)
                    break
                // インストール完了 : Done一覧へ移し、現在カードが同じパッケージなら空にする
                case "install_end":
                    commitProgressDialog.appendDone(packageName, false)
                    commitProgressDialog.removeFromPending(packageName)
                    if (commitProgressDialog.currentPackage === packageName) {
                        commitProgressDialog.clearCurrent()
                    }
                    break
                // 削除開始 : 現在カードに削除中として表示する
                case "remove_start":
                    commitProgressDialog.currentPercent = 0
                    commitProgressDialog.startCurrent(packageName, "removing", true)
                    break
                // 削除完了 : Done一覧へ移し、現在カードが同じパッケージなら空にする
                case "remove_end":
                    commitProgressDialog.appendDone(packageName, true)
                    commitProgressDialog.removeFromPending(packageName)
                    if (commitProgressDialog.currentPackage === packageName) {
                        commitProgressDialog.clearCurrent()
                    }
                    break
            }
        }

        /**
         * @brief コミット進捗の変化を進捗バーと現在カードへ反映する
         *
         * PackageStateChanged未対応の旧バックエンドではstageとpackageNameの変化から状態遷移を推定する
         *
         * @param packageName 対象のパッケージ名
         * @param percentage 現在パッケージの進捗率
         * @param stage 処理段階
         * @param totalSteps 全ステップ数
         * @param completedSteps 完了ステップ数
         * @param overallPercentage 全体の進捗率
         */
        function onCommitProgressChanged(packageName, percentage, stage,
                                          totalSteps, completedSteps,
                                          overallPercentage) {
            commitProgressDialog.overallPercent = overallPercentage
            commitProgressDialog.currentPercent = percentage
            totalProgressBar.indeterminate = false

            // 状態シグナルを受信済みの場合、またはパッケージ名が無い場合は推定を行わない
            if (commitProgressDialog._hasStateSignal || packageName === "") {
                return
            }

            // PackageStateChanged未対応バックエンド用フォールバック:
            // stageとpackageNameの変化から状態遷移を推定
            var prev = commitProgressDialog._prevPkg
            var prevStage = commitProgressDialog._prevStage

            // 段階に応じて現在カードを切り替える (インストール・削除では、別パッケージに移ったら直前のカードを完了させる)
            if (stage === "downloading") {
                commitProgressDialog.startCurrent(packageName, "downloading", false)
            }
            else if (stage === "installing") {
                // 別パッケージに移った場合は直前のカードを完了させる
                if (prev !== "" && prev !== packageName) {
                    commitProgressDialog.finalizeCurrent()
                }

                commitProgressDialog.startCurrent(packageName, "installing", false)
            }
            else if (stage === "removing") {
                // 別パッケージに移った場合は直前のカードを完了させる
                if (prev !== "" && prev !== packageName) {
                    commitProgressDialog.finalizeCurrent()
                }

                commitProgressDialog.startCurrent(packageName, "removing", true)
            }

            commitProgressDialog._prevPkg = packageName
            commitProgressDialog._prevStage = stage
        }

        /**
         * @brief スクリプト出力を受け取り、表示中なら蓄積する
         * @param entry 出力要素
         */
        function onScriptOutputReceived(entry) {
            // ダイアログを表示中の場合のみ蓄積する
            if (commitProgressDialog.visible) {
                commitProgressDialog.appendScriptLine(entry)
            }
        }

        /**
         * @brief コミット完了を受け、現在カードを完了させて結果ダイアログへ引き継ぐ
         */
        function onCommitResultChanged() {
            // 残りの現在アクティビティを完了させる
            commitProgressDialog.finalizeCurrent()

            commitProgressDialog.close()
            var cr = PackageController.commitResult
            resultDialog.resultSuccess = cr.success || false
            resultDialog.resultData = cr
            // 失敗・再起動要求などがある場合は設定に関わらず結果を表示する
            if (commitProgressDialog.showSummaryPage || resultDialog.needsAttention) {
                resultDialog.open()
            }
            else {
                // 結果を表示しない場合は、パッケージ一覧の再検索だけを要求する
                resultDialog.refreshRequested()
            }
        }
    }
}
