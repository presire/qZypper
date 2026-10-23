/**
 * @file HistoryDialog.qml
 * @brief パッケージ操作の履歴をコマンド単位にまとめて検索、表示するダイアログ
 *
 * Main.qmlのメニューから開かれる
 * 左に日付ナビゲーション (年 > 月 > 日)、右にコマンドごとのグループ一覧を表示する
 * 履歴はバックエンドがコマンド単位のグループとして新しい順に返し、日付ナビゲーションは読み込み済みのグループから作る
 *
 * 入力 : PackageControllerのGetHistoryGroups結果 (historyGroupsReadyシグナル)
 * 出力 : なし
 * 依存 : PackageController (D-Busバックエンドの履歴グループ取得API)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// 履歴ビューアのダイアログ (libzyppの履歴をコマンド単位のグループとして表示する)
// グループ単位のoffsetとlimit (100グループ) でページングし、テキスト、アクション、日付範囲はバックエンドで絞り込む
// 日付ナビゲーションは読み込み済みのグループから作るため、未読み込みの範囲は含まれない ([Load More]で広げる)
// バックエンドのフィルタキー : since、until (エポックミリ秒)、limit、offset、actions、text
// グループのtimestampはエポック秒、sinceとuntilはエポックミリ秒である点に注意する
Dialog {
    id: historyDialog  // ダイアログ自身の参照名

    property int pageSize: 100          // 1ページで取得するグループ数
    property var groups: []             // 読み込み済みのグループ (新しい順)
    property int totalGroups: 0         // 絞り込み条件に一致するグループの総数 (バックエンド報告値)
    property bool hasMore: false        // 未読み込みの古いグループがあるか
    property int nextOffset: 0          // 次に取得するグループのoffset
    property bool appending: false      // true : 既存分への追記取得
    property bool loading: false        // 取得中か
    property int activeRequest: -1      // 最新の取得要求番号 (古い応答の破棄に使う)
    property int requestSeq: 0          // 取得要求番号の採番用カウンタ
    property bool failed: false         // 直近の取得が失敗したか
    property string selectedKey: ""     // 日付ナビゲーションの選択 ("" : 読み込み済み全体、"yyyy"、"yyyy-MM"、"yyyy-MM-dd")
    property var collapsedGroups: ({})  // 折りたたんだグループのid (未登録は展開)
    property var treeState: ({})        // 日付ツリーの開閉を利用者が切り替えた記録 (キー -> 真偽値)
    property var treeRows: []           // 日付ツリーの表示行 (平坦化済み)
    property var listRows: []           // グループ一覧の表示行 (平坦化済み)
    property string newestYear: ""      // 読み込み済みで最も新しい年 (既定で展開する)
    property string newestMonth: ""     // 読み込み済みで最も新しい月 (既定で展開する)

    // アクション絞り込みの選択肢の表示名
    readonly property var actionLabels: [qsTr("All"), qsTr("install"), qsTr("remove"), qsTr("patch"),
                                         qsTr("Repo+"), qsTr("Repo-"), qsTr("Repo-Alias"), qsTr("Repo-URL")]

    // アクション絞り込みの選択肢に対応する、バックエンドのaction名
    readonly property var actionValues: ["", "install", "remove", "patch", "radd", "rremove", "rchange", "url"]

    /**
     * @brief エポック秒の時刻をJavaScriptのDateへ変換する
     * @param seconds エポック秒
     * @return Dateオブジェクト
     */
    function dateOf(seconds) {
        return new Date(Number(seconds) * 1000)
    }

    /**
     * @brief グループの日付キーを返す
     * @param group グループのマップ
     * @return "yyyy-MM-dd"形式の文字列
     */
    function dayKeyOf(group) {
        return Qt.formatDate(dateOf(group.timestamp), "yyyy-MM-dd")
    }

    /**
     * @brief 操作の種類を表示用の名前へ変換する
     * @param action バックエンドのaction名
     * @return 表示名 (選択肢に無い値はそのまま返す)
     */
    function actionLabel(action) {
        var index = actionValues.indexOf(String(action || ""))
        return index > 0 ? actionLabels[index] : String(action || "")
    }

    /**
     * @brief 履歴イベント1件の表示用の詳細文字列を返す (リポジトリ操作用)
     * @param e イベントのマップ
     * @return 詳細文字列 (パッケージ操作では空)
     */
    function repoDetail(e) {
        var alias = String(e.name || e.repoAlias || "")

        // リポジトリ操作の種類ごとに、Myrlynと同じ形式の詳細文字列を作る
        switch (String(e.action || "")) {
        case "radd":
            return alias + (e.url ? "  " + e.url : "")
        case "rremove":
            return alias
        case "url":
            return alias + "  " + String(e.oldUrl || "?") + " -> " + String(e.url || "?")
        case "rchange":
            return String(e.oldAlias || "?") + " -> " + alias
        default:
            return ""
        }
    }

    /**
     * @brief グループの見出し文字列を返す
     * @param group グループのマップ
     * @return "yyyy-MM-dd hh:mm:ss  <command>  (+n/-n)"形式の文字列
     */
    function headerText(group) {
        var command = group.artificial ? qsTr("<unknown command>") : String(group.command || "")
        return Qt.formatDateTime(dateOf(group.timestamp), "yyyy-MM-dd hh:mm:ss") + "  " + command
               + "  (+" + Number(group.installCount) + "/-" + Number(group.removeCount) + ")"
    }

    /**
     * @brief 日付ツリーの項目が展開されているかを返す
     * @param key 項目のキー
     * @return 展開中ならtrue (利用者が切り替えていない項目は最新の年と月だけ展開)
     */
    function isTreeExpanded(key) {
        // 利用者が開閉した項目は、その記録を優先する
        if (treeState[key] !== undefined) {
            return treeState[key]
        }

        return key === newestYear || key === newestMonth
    }

    /**
     * @brief 読み込み済みのグループから日付ツリーの表示行を作り直す
     * @note treeRowsを書き換え、ListViewの表示を更新する
     */
    function rebuildTree() {
        var years = {}

        // 読み込み済みのグループを年、月、日ごとに数える
        for (var i = 0; i < groups.length; ++i) {
            var day = dayKeyOf(groups[i])
            var month = day.substring(0, 7)
            var year = day.substring(0, 4)
            years[year] = years[year] || { count: 0, months: {} }
            years[year].count++
            years[year].months[month] = years[year].months[month] || { count: 0, days: {} }
            years[year].months[month].count++
            years[year].months[month].days[day] = (years[year].months[month].days[day] || 0) + 1
        }

        var rows = [{ key: "", label: qsTr("All loaded (%1)").arg(groups.length), level: 0, expandable: false, expanded: false }]
        var yearKeys = Object.keys(years).sort().reverse()

        // 新しい年から順に、展開中の年の月と日の行を追加する
        for (var y = 0; y < yearKeys.length; ++y) {
            var yk = yearKeys[y]
            var yExpanded = isTreeExpanded(yk)
            rows.push({ key: yk, label: yk + " (" + years[yk].count + ")", level: 0, expandable: true, expanded: yExpanded })

            // 折りたたまれた年は、月の行を追加しない
            if (!yExpanded) {
                continue
            }

            var monthKeys = Object.keys(years[yk].months).sort().reverse()

            for (var m = 0; m < monthKeys.length; ++m) {
                var mk = monthKeys[m]
                var mExpanded = isTreeExpanded(mk)
                rows.push({ key: mk, label: mk + " (" + years[yk].months[mk].count + ")", level: 1, expandable: true, expanded: mExpanded })

                // 折りたたまれた月は、日の行を追加しない
                if (!mExpanded) {
                    continue
                }

                var dayKeys = Object.keys(years[yk].months[mk].days).sort().reverse()

                for (var d = 0; d < dayKeys.length; ++d) {
                    var dk = dayKeys[d]
                    rows.push({ key: dk, label: dk + " (" + years[yk].months[mk].days[dk] + ")", level: 2, expandable: false, expanded: false })
                }
            }
        }

        treeRows = rows
    }

    /**
     * @brief 選択中の日付に合うグループから一覧の表示行を作り直す
     * @note listRowsを書き換え、スクロール位置は可能な限り維持する
     */
    function rebuildList() {
        var savedY = groupList.contentY
        var rows = []

        for (var i = 0; i < groups.length; ++i) {
            var g = groups[i]

            // 日付ナビゲーションで選択した範囲外のグループは表示しない
            if (selectedKey !== "" && dayKeyOf(g).indexOf(selectedKey) !== 0) {
                continue
            }

            var collapsed = collapsedGroups[g.id] === true
            rows.push({ kind: "header", gid: g.id, text: headerText(g), collapsed: collapsed, count: g.events.length })

            // 折りたたまれたグループは、配下のイベント行を追加しない
            if (collapsed) {
                continue
            }

            for (var k = 0; k < g.events.length; ++k) {
                var e = g.events[k]
                var detail = repoDetail(e)
                rows.push({ kind: "event", gid: g.id, action: actionLabel(e.action), name: String(e.name || ""),
                            edition: String(e.edition || ""), arch: String(e.arch || ""), repo: String(e.repoAlias || ""),
                            detail: detail, isRepo: detail !== "" })
            }

            // 件数上限で表示しきれなかった分は、黙って省略せず行として示す
            if (Number(g.hiddenCount) > 0) {
                rows.push({ kind: "more", gid: g.id, count: Number(g.hiddenCount) })
            }
        }

        listRows = rows

        // モデルの差し替え直後はcontentHeightが更新されていないため、次のイベントループで元の位置へ戻す
        Qt.callLater(function () {
            groupList.contentY = Math.min(savedY, Math.max(0, groupList.contentHeight - groupList.height))
        })
    }

    /**
     * @brief 読み込み済みグループの変化に伴い、日付ツリーと一覧の両方を作り直す
     * @note newestYear、newestMonth、selectedKeyを更新し、treeRowsとlistRowsを書き換える
     */
    function rebuildAll() {
        // 最も新しい年と月は既定で展開する
        if (groups.length > 0) {
            var newest = dayKeyOf(groups[0])
            newestMonth = newest.substring(0, 7)
            newestYear = newest.substring(0, 4)
        }
        else {
            newestMonth = ""
            newestYear = ""
        }

        // 選択中の日付が読み込み済みのグループに無くなった場合は、選択を解除する
        if (selectedKey !== "") {
            var found = false

            for (var i = 0; i < groups.length && !found; ++i) {
                found = dayKeyOf(groups[i]).indexOf(selectedKey) === 0
            }

            if (!found) {
                selectedKey = ""
            }
        }

        rebuildTree()
        rebuildList()
    }

    /**
     * @brief 日付ツリーの項目を選択する (年と月は開閉も切り替える)
     * @param row 日付ツリーの行
     * @note selectedKeyとtreeStateを更新し、日付ツリーと一覧を作り直す
     */
    function selectTreeRow(row) {
        selectedKey = row.key

        // 年と月の行は、選択と同時に開閉を切り替える
        if (row.expandable) {
            var state = Object.assign({}, treeState)
            state[row.key] = !row.expanded
            treeState = state
        }

        rebuildTree()
        rebuildList()
    }

    /**
     * @brief グループの開閉を切り替える
     * @param gid グループのid
     * @note collapsedGroupsを更新し、一覧を作り直す
     */
    function toggleGroup(gid) {
        var state = Object.assign({}, collapsedGroups)

        // 折りたたみ済みなら展開へ戻し、そうでなければ折りたたむ
        if (state[gid] === true) {
            delete state[gid]
        }
        else {
            state[gid] = true
        }

        collapsedGroups = state
        rebuildList()
    }

    /**
     * @brief GetHistoryGroupsを発行する、append=trueのとき読み込み済みの続きを追記取得する
     * @param append trueなら既存分の続き (次のoffset) を追記取得する
     * @note PackageController::loadHistoryGroupsを呼び出し、完了はhistoryGroupsReadyシグナルで受け取る
     *       appending、loading、failed、activeRequestを更新する
     */
    function runQuery(append) {
        var filter = { limit: pageSize, offset: append ? nextOffset : 0 }
        var s = sinceField.text.trim()
        var u = untilField.text.trim()
        var text = filterField.text.trim()

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

        // アクションを選択している場合は、バックエンドで絞り込む
        if (actionCombo.currentIndex > 0) {
            filter.actions = [actionValues[actionCombo.currentIndex]]
        }

        // テキストが入力されている場合は、バックエンドで絞り込む
        if (text !== "") {
            filter.text = text
        }

        appending = append
        loading = true
        failed = false
        requestSeq += 1
        activeRequest = requestSeq
        PackageController.loadHistoryGroups(filter, activeRequest)
    }

    anchors.centerIn: parent                     // 親ウィンドウの中央に表示する
    width: Math.min(parent.width * 0.92, 1100)   // 親の幅の92% (最大1100px)
    height: Math.min(parent.height * 0.85, 720)  // 親の高さの85% (最大720px)
    title: qsTr("Installation History")          // ダイアログのタイトル
    standardButtons: Dialog.Close                // [Close]ボタンのみ表示する
    modal: true                                  // 表示中は背後の画面を操作できないようにする

    // 開くたびに状態を初期化して最新の履歴を取得する
    onOpened: {
        groups = []
        totalGroups = 0
        hasMore = false
        nextOffset = 0
        selectedKey = ""
        collapsedGroups = ({})
        treeState = ({})
        rebuildAll()
        runQuery(false)
    }

    // フィルタ行、本体 (日付ナビゲーションとグループ一覧)、進捗バーを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 8            // 項目の間隔

        // フィルタ行 (テキスト、アクション、期間の絞り込みと[Query]ボタン)
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 8              // 項目の間隔

            // [Filter:]の項目名
            Label {
                text: qsTr("Filter:")  // 項目名の文字列
            }

            // テキストによる絞り込みの入力欄 (入力が止まってからバックエンドで絞り込む)
            TextField {
                id: filterField  // テキスト絞り込み欄の参照名

                Layout.preferredWidth: 180                           // 入力欄の幅
                activeFocusOnTab: true                               // [Tab]キーでフォーカスを移動できるようにする
                placeholderText: qsTr("Text (name, repo, user...)")  // 未入力時に表示する案内
                Accessible.name: qsTr("History text filter")         // スクリーンリーダー向けの名前

                // テキスト変更のたびに、入力が止まってから再取得する
                onTextChanged: {
                    textDebounce.restart()
                }

                // [Enter]キーで即時に再取得する
                onAccepted: {
                    textDebounce.stop()
                    historyDialog.runQuery(false)
                }
            }

            // テキスト入力のデバウンスタイマー
            Timer {
                id: textDebounce  // デバウンスタイマーの参照名

                interval: 500  // 最後の入力から再取得までの待ち時間 (ミリ秒)

                // 開いている間だけ再取得する (ダイアログの初期化中は発行しない)
                onTriggered: {
                    // 閉じている間は履歴を取得しても表示先が無いため、何もしない
                    if (historyDialog.opened) {
                        historyDialog.runQuery(false)
                    }
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
                model: historyDialog.actionLabels               // アクションの表示名の一覧
                activeFocusOnTab: true                          // [Tab]キーでフォーカスを移動できるようにする
                Accessible.name: qsTr("History action filter")  // スクリーンリーダー向けの名前

                // アクション変更のたびに再取得する
                onActivated: {
                    historyDialog.runQuery(false)
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
                placeholderText: qsTr("YYYY-MM-DD")                                      // 入力形式の案内
                activeFocusOnTab: true                                                   // [Tab]キーでフォーカスを移動できるようにする
                Accessible.name: qsTr("History start date")                              // スクリーンリーダー向けの名前
                Accessible.description: qsTr("Only show entries at or after this date")  // スクリーンリーダー向けの説明

                // [Enter]キーで再取得する
                onAccepted: {
                    historyDialog.runQuery(false)
                }
            }

            // [Until:]の項目名
            Label {
                text: qsTr("Until:")  // 項目名の文字列
            }

            // 終了日の入力欄
            TextField {
                id: untilField  // 終了日入力欄の参照名

                Layout.preferredWidth: 100                                                // 入力欄の幅
                placeholderText: qsTr("YYYY-MM-DD")                                       // 入力形式の案内
                activeFocusOnTab: true                                                    // [Tab]キーでフォーカスを移動できるようにする
                Accessible.name: qsTr("History end date")                                 // スクリーンリーダー向けの名前
                Accessible.description: qsTr("Only show entries at or before this date")  // スクリーンリーダー向けの説明

                // [Enter]キーで再取得する
                onAccepted: {
                    historyDialog.runQuery(false)
                }
            }

            // [Query]ボタン
            Button {
                enabled: !historyDialog.loading             // 取得中は無効化する
                text: qsTr("Query")                         // ボタンの文字列
                activeFocusOnTab: true                      // [Tab]キーでフォーカスを移動できるようにする
                Accessible.name: qsTr("Run history query")  // スクリーンリーダー向けの名前

                // 現在の条件で履歴を先頭から取得し直す
                onClicked: {
                    historyDialog.runQuery(false)
                }
            }

            // 右端へ寄せるための余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }
        }

        // 本体 (左 : 日付ナビゲーション、右 : コマンドごとのグループ一覧)
        SplitView {
            Layout.fillWidth: true      // 親の幅いっぱいに広げる
            Layout.fillHeight: true     // 残りの高さをすべて使う
            orientation: Qt.Horizontal  // 左右に分割する

            // 左ペイン : 日付ナビゲーション
            ColumnLayout {
                SplitView.preferredWidth: 240  // 左ペインの初期幅
                SplitView.minimumWidth: 160    // 左ペインの最小幅
                spacing: 6                     // 項目の間隔

                // 日付ツリーの枠
                Frame {
                    Layout.fillWidth: true   // 親の幅いっぱいに広げる
                    Layout.fillHeight: true  // 残りの高さをすべて使う
                    padding: 2               // 内側の余白

                    // 日付ツリー (年 > 月 > 日)
                    ListView {
                        id: dateTree  // 日付ツリーの参照名

                        anchors.fill: parent                                          // 枠の内側全体を使う
                        clip: true                                                    // 領域外にはみ出した部分を描画しない
                        model: historyDialog.treeRows                                 // 平坦化した日付ツリーの行
                        boundsBehavior: Flickable.StopAtBounds                        // 端を越えてスクロールさせない
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する
                        Accessible.name: qsTr("History date navigation")              // スクリーンリーダー向けの名前

                        // 日付ツリーの1行 (クリックで絞り込み、年と月は開閉も切り替える)
                        delegate: ItemDelegate {
                            id: treeItem  // 1行分の参照名

                            required property var modelData  // 1行分のデータ

                            width: dateTree.width - 4                                 // 一覧の幅から右側の余白を除いた幅
                            leftPadding: 8 + modelData.level * 16                     // 階層に応じてインデントする
                            highlighted: historyDialog.selectedKey === modelData.key  // 選択中の行を強調表示する
                            font.bold: modelData.level === 0 && modelData.key !== ""  // 年の行は太字にする
                            activeFocusOnTab: true                                    // [Tab]キーでフォーカスを移動できるようにする
                            Accessible.name: modelData.label                          // スクリーンリーダー向けの名前

                            // 開閉記号と表示文字列
                            text: (modelData.expandable ? (modelData.expanded ? "\u25be " : "\u25b8 ") : "") + modelData.label

                            // 行を選択して一覧を絞り込む
                            onClicked: {
                                historyDialog.selectTreeRow(modelData)
                            }
                        }
                    }
                }

                // 読み込み範囲の説明 (未読み込みの範囲が含まれないことを示す)
                Label {
                    Layout.fillWidth: true          // 親の幅いっぱいに広げる
                    color: palette.placeholderText  // 補足情報として控えめな文字色
                    font.pixelSize: 11              // 文字サイズ
                    wrapMode: Text.WordWrap         // 幅に収まらない場合は単語単位で折り返す

                    // 読み込み済みの件数と、未読み込みの古い履歴の有無を示す文
                    text: historyDialog.groups.length === 0 ? "" : (historyDialog.hasMore
                          ? qsTr("Only the loaded range (%1 of %2 command groups) is shown. Older history is not loaded yet.")
                                .arg(historyDialog.groups.length).arg(historyDialog.totalGroups)
                          : qsTr("All %1 command groups are loaded.").arg(historyDialog.groups.length))
                }

                // [Load More]ボタン (未読み込みの古いグループがある場合だけ表示する)
                Button {
                    Layout.fillWidth: true                             // 親の幅いっぱいに広げる
                    visible: historyDialog.hasMore                     // 続きがある場合のみ表示する
                    enabled: !historyDialog.loading                    // 取得中は無効化する
                    text: qsTr("Load More")                            // ボタンの文字列
                    activeFocusOnTab: true                             // [Tab]キーでフォーカスを移動できるようにする
                    Accessible.name: qsTr("Load more history groups")  // スクリーンリーダー向けの名前

                    // 読み込み済みの続きから追記取得する
                    onClicked: {
                        historyDialog.runQuery(true)
                    }
                }
            }

            // 右ペイン : コマンドごとのグループ一覧
            ColumnLayout {
                SplitView.fillWidth: true    // 残りの幅をすべて使う
                SplitView.minimumWidth: 320  // 右ペインの最小幅
                spacing: 4                   // 項目の間隔

                // 列見出し
                RowLayout {
                    Layout.fillWidth: true  // 親の幅いっぱいに広げる
                    Layout.leftMargin: 28   // 一覧のイベント行のインデントに合わせる
                    Layout.rightMargin: 12  // スクロールバーの分だけ右を空ける
                    spacing: 6              // 列の間隔

                    // [Action]列の見出し
                    Label {
                        Layout.preferredWidth: 90  // 操作列の幅
                        text: qsTr("Action")       // 見出しの文字列
                        font.bold: true            // 太字で表示する
                    }

                    // [Name]列の見出し
                    Label {
                        Layout.fillWidth: true  // 残りの幅をすべて使う
                        text: qsTr("Name")      // 見出しの文字列
                        font.bold: true         // 太字で表示する
                    }

                    // [Version]列の見出し
                    Label {
                        Layout.preferredWidth: 130  // バージョン列の幅
                        text: qsTr("Version")       // 見出しの文字列
                        font.bold: true             // 太字で表示する
                    }

                    // [Arch]列の見出し
                    Label {
                        Layout.preferredWidth: 70  // アーキテクチャ列の幅
                        text: qsTr("Arch")         // 見出しの文字列
                        font.bold: true            // 太字で表示する
                    }

                    // [Repo]列の見出し
                    Label {
                        Layout.preferredWidth: 120  // リポジトリ列の幅
                        text: qsTr("Repo")          // 見出しの文字列
                        font.bold: true             // 太字で表示する
                    }
                }

                // グループ一覧の枠
                Frame {
                    Layout.fillWidth: true   // 親の幅いっぱいに広げる
                    Layout.fillHeight: true  // 残りの高さをすべて使う
                    padding: 2               // 内側の余白

                    // コマンドごとのグループ一覧 (見出し行、イベント行、省略行を平坦化したモデル)
                    ListView {
                        id: groupList  // グループ一覧の参照名

                        anchors.fill: parent                                          // 枠の内側全体を使う
                        clip: true                                                    // 領域外にはみ出した部分を描画しない
                        model: historyDialog.listRows                                 // 平坦化した表示行
                        boundsBehavior: Flickable.StopAtBounds                        // 端を越えてスクロールさせない
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する
                        Accessible.name: qsTr("History command groups")               // スクリーンリーダー向けの名前

                        // 1行分 (行の種類に応じて見出し、イベント、省略のいずれかを表示する)
                        delegate: Loader {
                            id: rowLoader  // 1行分の参照名

                            required property var modelData  // 1行分のデータ

                            width: groupList.width - 12  // 一覧の幅から右側の余白を除いた幅

                            // 行の種類に応じた部品
                            sourceComponent: rowLoader.modelData.kind === "header" ? headerRow
                                             : (rowLoader.modelData.kind === "event" ? eventRow : moreRow)

                            // コマンドの見出し行 (クリックで開閉する)
                            Component {
                                id: headerRow  // 見出し行の部品の参照名

                                // 見出し行の本体 (開閉記号とコマンド)
                                ItemDelegate {
                                    id: headerItem  // 見出し行の参照名

                                    activeFocusOnTab: true                     // [Tab]キーでフォーカスを移動できるようにする
                                    Accessible.name: rowLoader.modelData.text  // スクリーンリーダー向けの名前
                                    // 開閉状態の説明
                                    Accessible.description: rowLoader.modelData.collapsed ? qsTr("Collapsed") : qsTr("Expanded")

                                    // クリックでグループを開閉する
                                    onClicked: {
                                        historyDialog.toggleGroup(rowLoader.modelData.gid)
                                    }

                                    // 見出し行の背景 (イベント行と区別する)
                                    background: Rectangle {
                                        // 通常時とホバー時の背景色
                                        color: headerItem.hovered || headerItem.visualFocus ? palette.midlight : palette.alternateBase
                                    }

                                    // 開閉記号と見出し文字列 (コマンドは外部由来のためPlainTextで表示する)
                                    contentItem: Label {
                                        textFormat: Text.PlainText  // HTMLとして解釈させない
                                        font.bold: true             // 太字で表示する
                                        elide: Text.ElideRight      // 幅に収まらない場合は末尾を省略する

                                        text: (rowLoader.modelData.collapsed ? "\u25b8 " : "\u25be ") + rowLoader.modelData.text
                                    }
                                }
                            }

                            // 履歴イベント行 (パッケージ操作は列表示、リポジトリ操作は詳細文字列を表示する)
                            Component {
                                id: eventRow  // イベント行の部品の参照名

                                // イベント行の列の並び
                                RowLayout {
                                    spacing: 6  // 列の間隔

                                    // 左のインデント (見出し行の開閉記号の分)
                                    Item {
                                        Layout.preferredWidth: 22  // インデントの幅
                                    }

                                    // 操作の種類
                                    Label {
                                        Layout.preferredWidth: 90         // 見出しの列幅に合わせる
                                        text: rowLoader.modelData.action  // 操作の種類 (外部由来の値を含む)
                                        textFormat: Text.PlainText        // HTMLとして解釈させない
                                        elide: Text.ElideRight            // 幅に収まらない場合は末尾を省略する
                                    }

                                    // パッケージ名 (リポジトリ操作では詳細文字列)
                                    Label {
                                        Layout.fillWidth: true      // 残りの幅をすべて使う
                                        textFormat: Text.PlainText  // HTMLとして解釈させない
                                        elide: Text.ElideRight      // 幅に収まらない場合は末尾を省略する

                                        // リポジトリ操作は詳細文字列、それ以外はパッケージ名
                                        text: rowLoader.modelData.isRepo ? rowLoader.modelData.detail : rowLoader.modelData.name
                                    }

                                    // バージョン (バージョンとリリース)
                                    Label {
                                        Layout.preferredWidth: 130            // 見出しの列幅に合わせる
                                        visible: !rowLoader.modelData.isRepo  // リポジトリ操作では表示しない
                                        text: rowLoader.modelData.edition     // バージョン
                                        textFormat: Text.PlainText            // HTMLとして解釈させない
                                        elide: Text.ElideRight                // 幅に収まらない場合は末尾を省略する
                                    }

                                    // アーキテクチャ
                                    Label {
                                        Layout.preferredWidth: 70             // 見出しの列幅に合わせる
                                        visible: !rowLoader.modelData.isRepo  // リポジトリ操作では表示しない
                                        text: rowLoader.modelData.arch        // アーキテクチャ
                                        textFormat: Text.PlainText            // HTMLとして解釈させない
                                        elide: Text.ElideRight                // 幅に収まらない場合は末尾を省略する
                                    }

                                    // リポジトリの別名
                                    Label {
                                        Layout.preferredWidth: 120            // 見出しの列幅に合わせる
                                        visible: !rowLoader.modelData.isRepo  // リポジトリ操作では詳細に含めるため表示しない
                                        text: rowLoader.modelData.repo        // リポジトリの別名
                                        textFormat: Text.PlainText            // HTMLとして解釈させない
                                        elide: Text.ElideRight                // 幅に収まらない場合は末尾を省略する
                                    }
                                }
                            }

                            // 表示しきれなかったイベントの件数を示す行
                            Component {
                                id: moreRow  // 省略行の部品の参照名

                                // 省略された件数の表示
                                Label {
                                    leftPadding: 28                 // イベント行のインデントに合わせる
                                    color: palette.placeholderText  // 補足情報として控えめな文字色
                                    font.italic: true               // 斜体で表示する

                                    // 省略された件数の文字列
                                    text: qsTr("%n more event(s) not shown", "", rowLoader.modelData.count)
                                }
                            }
                        }
                    }

                    // 履歴がない場合のメッセージ
                    Label {
                        anchors.centerIn: parent                                  // 枠の中央に配置する
                        visible: groupList.count === 0 && !historyDialog.loading  // 一覧が空で取得中でない場合のみ表示する
                        color: palette.placeholderText                            // 補足情報として控えめな文字色

                        // 取得に失敗したときは失敗を、それ以外は該当なしを示す
                        text: historyDialog.failed ? qsTr("Failed to load history") : qsTr("No history entries found")
                    }
                }
            }
        }

        // 履歴取得中の進捗バー
        ProgressBar {
            Layout.fillWidth: true                    // 親の幅いっぱいに広げる
            visible: historyDialog.loading            // 取得中のみ表示する
            indeterminate: true                       // 進捗率が不明なため往復表示にする
            Accessible.name: qsTr("Loading history")  // スクリーンリーダー向けの名前
        }
    }

    // 履歴取得完了の受け口
    Connections {
        target: PackageController  // 履歴取得の結果を通知するPackageController

        /**
         * @brief 履歴グループ取得の結果を反映し、追記時は既存分へ連結する
         * @param requestId 応答の要求番号
         * @param result グループ取得結果 (groups、totalGroups、offset、hasMore等、失敗時は空のマップ)
         * @note 最新の要求番号と一致しない古い応答は破棄する
         *       失敗時は読み込み済みの表示を維持し、エラーはPackageController側で通知される
         *       成功時はgroups、totalGroups、hasMore、nextOffsetを更新し、日付ツリーと一覧を作り直す
         */
        function onHistoryGroupsReady(requestId, result) {
            // 条件を変えて再取得した後に届いた古い応答は、表示を乱さないよう破棄する
            if (requestId !== historyDialog.activeRequest) {
                return
            }

            historyDialog.loading = false

            // 失敗時は表示を変えずに失敗状態だけ記録する
            if (result.groups === undefined) {
                historyDialog.failed = true
                return
            }

            // 追記モードでは既存分へ連結し、それ以外は置き換える
            if (historyDialog.appending) {
                historyDialog.groups = historyDialog.groups.concat(result.groups)
            }
            else {
                historyDialog.groups = result.groups
                historyDialog.collapsedGroups = ({})
                historyDialog.treeState = ({})
            }

            historyDialog.totalGroups = Number(result.totalGroups)
            historyDialog.hasMore = result.hasMore === true
            historyDialog.nextOffset = Number(result.offset) + result.groups.length
            historyDialog.rebuildAll()
        }
    }
}
