/**
 * @file PackageTable.qml
 * @brief パッケージ検索結果を表形式で表示して状態変更を受け付ける一覧
 *
 * Main.qmlで使用され、選択行をPackageDetailsPaneへ伝える
 *
 * 入力 : PackageControllerのパッケージモデルとフィルタ状態
 * 出力 : パッケージ選択と状態変更の要求
 * 依存 : PackageController, PackageStatusInfo
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// パッケージ一覧ペイン (列ヘッダ、パッケージリスト、コンテキストメニューで構成する)
Pane {
    id: pkgPane  // パッケージ一覧ペイン自身の参照名

    // ダークモード連動 (typeaheadポップアップ配色用)
    property bool darkMode: false                // ダークモード連動
    // --- パッケージ一覧のtypeahead検索バッファ ---
    property string pkgSearchBuffer: ""          // typeahead検索バッファ
    // リサイズ可能な列幅プロパティ
    property real colName: 200                   // パッケージ名列の現在幅
    property real colSummary: 200                // 概要列の現在幅
    property real colInstalled: 140              // 導入済み列の現在幅
    property real colAvailable: 120              // 利用可能列の現在幅
    // 全列の合計幅 (固定幅の列と区切り線を含めて動的に計算する)
    property real pkgMinRowWidth: 4 + 36 + 1 + colName + 1 + colSummary + 1 + colInstalled + 1 + colAvailable + 1 + 88
    readonly property real minColName: 120       // パッケージ名列の縮小下限
    readonly property real minColSummary: 120    // 概要列の縮小下限
    readonly property real minColInstalled: 100  // 導入済み列の縮小下限
    readonly property real minColAvailable: 100  // 利用可能列の縮小下限

    // 状態変更キーの文字とPackageController::setPackagesActionの操作名の対応表
    readonly property var statusKeyActions: ({ "+": "install", "-": "remove", ">": "update", "<": "keep", "!": "taboo", "*": "protect" })

    // 一括操作メニューに並べる操作 (表示名、操作名、対応する状態変更キー)
    readonly property var bulkActions: [
        { label: qsTr("Install/Update"), action: "install", key: "+" },
        { label: qsTr("Delete"), action: "remove", key: "-" },
        { label: qsTr("Update"), action: "update", key: ">" },
        { label: qsTr("Update Unconditionally"), action: "updateForce", key: "" },
        { label: qsTr("Keep / Undo"), action: "keep", key: "<" },
        { label: qsTr("Taboo"), action: "taboo", key: "!" },
        { label: qsTr("Protected"), action: "protect", key: "*" }
    ]

    // 一括操作メニューの1項目
    // 選択すると、選択行または一覧の表示中全件を対象にsetPackagesActionを呼び出す
    component BulkActionMenuItem: MenuItem {
        required property var modelData  // bulkActionsの1要素 (label、action、key)
        property bool wholeList: false   // trueなら一覧の表示中全件、falseなら表示中の選択行が対象

        // 状態変更キーがある操作は、表示名の末尾にキーのヒントを付ける
        text: modelData.key.length > 0 ? modelData.label + "  [" + modelData.key + "]" : modelData.label

        // 対象のパッケージ名を集めて一括変更を要求する
        onTriggered: {
            var proxy = PackageController.packageProxy
            var names = wholeList ? proxy.visibleNames() : proxy.selectedVisibleNames()
            pkgPane.runPackagesAction(names, modelData.action)
        }
    }

    /**
     * @brief typeahead文字列の接頭辞に一致する最初のパッケージ行を選択する
     * @note 戻り値は無く、空文字列では早期に戻る
     * @note 一致時はcurrentIndexと表示位置を更新し、モデルの選択とアンカーも一致した行だけにする
     */
    function performPkgSearch() {
        var q = pkgSearchBuffer.toLowerCase()

        // 検索文字列が空の場合は何もしない
        if (q.length === 0) {
            return
        }

        var proxy = PackageController.packageProxy
        var idx = proxy.indexOfNamePrefix(q)

        // 一致する行が見つかった場合は、その行を選択して中央に表示する
        // 状態変更キーは選択行を優先するため、移動前の選択が残らないよう選択も一致した行へ移す
        if (idx >= 0) {
            packageListView.currentIndex = idx
            pkgPane.selectAfterMove(idx, false)
            packageListView.positionViewAtIndex(idx, ListView.Center)
        }
    }

    /**
     * @brief 指定列を昇順または降順でPackageProxyへソートさせる
     * @param col 並べ替えの基準にする列番号
     * @note PackageProxyのsortColumnとsortAscendingを更新する
     */
    function toggleSort(col) {
        var proxy = PackageController.packageProxy

        // 同じ列の場合は昇順と降順を切り替え、別の列の場合はその列の昇順にする
        if (proxy.sortColumn === col) {
            proxy.sortAscending = !proxy.sortAscending
        }
        else {
            proxy.sortColumn = col
            proxy.sortAscending = true
        }
    }

    /**
     * @brief PackageControllerへパッケージの選択状態変更を要求する
     * @param packageName 変更するパッケージ名
     * @param status 設定するPackageStatus値
     * @note PackageController::setPackageStatusを呼び出す
     */
    function setStatus(packageName, status) {
        PackageController.setPackageStatus(packageName, status)
    }

    /**
     * @brief 状態変更キーの文字に対応する操作名を返す
     * @param text 押されたキーの文字
     * @return setPackagesActionの操作名、状態変更キーでない文字では空文字列
     */
    function statusKeyAction(text) {
        // 対応表に無い文字 (オブジェクトの組み込みプロパティ名を含む) は状態変更キーとして扱わない
        if (!Object.prototype.hasOwnProperty.call(pkgPane.statusKeyActions, text)) {
            return ""
        }

        return pkgPane.statusKeyActions[text]
    }

    /**
     * @brief 状態変更キーで操作する対象のパッケージ名を返す
     * @return 表示中の選択行があればその名前の一覧、無ければ現在行の名前だけの一覧 (現在行も無ければ空の一覧)
     */
    function actionTargetNames() {
        var proxy = PackageController.packageProxy

        // 表示中の選択行がある場合は、選択行すべてを対象にする
        if (proxy.selectedVisibleCount > 0) {
            return proxy.selectedVisibleNames()
        }

        var name = proxy.nameAt(packageListView.currentIndex)

        // 現在行が無い場合は対象なし
        if (name.length === 0) {
            return []
        }

        return [name]
    }

    /**
     * @brief 複数パッケージの状態を一括で変更する
     * @param names 対象のパッケージ名の一覧
     * @param action setPackagesActionの操作名
     * @note PackageController::setPackagesActionを呼び出し、完了はpackagesActionFinishedシグナルで通知される
     * @note 対象が0件の場合は何も要求しない
     */
    function runPackagesAction(names, action) {
        // 対象が無い場合はバックエンドへ空の要求を送らない
        if (names.length === 0) {
            return
        }

        PackageController.setPackagesAction(names, action)
    }

    /**
     * @brief キー操作で移動した行に合わせて選択を更新する
     * @param row 移動先の行番号
     * @param extend trueなら起点から移動先までの範囲へ選択を広げ、falseなら移動先の行だけを選択して起点にする
     * @note 起点はPackageProxy側で保持する
     */
    function selectAfterMove(row, extend) {
        var proxy = PackageController.packageProxy

        // 一覧が空で移動先の行が無い場合は何もしない
        if (row < 0 || row >= packageListView.count) {
            return
        }

        if (extend) {
            proxy.extendRange(row)
        }
        else {
            proxy.selectOnly(row)
            proxy.setAnchorRow(row)
        }
    }

    SplitView.fillHeight: true    // SplitViewの残りの高さをすべて使う
    SplitView.minimumHeight: 200  // SplitViewで縮められる最小の高さ
    padding: 0                    // ペイン内側の余白は設けない

    // 列幅の保存先 (ペインの列幅プロパティをそのまま保存・復元する)
    Settings {
        id: pkgSettings  // 保存設定の参照名

        property alias colName: pkgPane.colName            // 保存するパッケージ名列幅
        property alias colSummary: pkgPane.colSummary      // 保存する概要列幅
        property alias colInstalled: pkgPane.colInstalled  // 保存する導入済み列幅
        property alias colAvailable: pkgPane.colAvailable  // 保存する利用可能列幅

        category: "packageTable"  // 設定ファイル内のグループ名
    }

    // typeahead検索バッファを一定時間後に消去するタイマ
    Timer {
        id: pkgSearchClearTimer  // 検索バッファ消去タイマの参照名

        interval: 5000  // 最後の入力から消去までの時間 (ミリ秒)
        repeat: false   // 1回だけ動作する

        // 入力が途切れたので検索バッファを消去する
        onTriggered: {
            pkgSearchBuffer = ""
        }
    }

    // 列ヘッダ、パッケージリスト、コンテキストメニューを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ペイン全体を使う
        spacing: 0            // ヘッダとリストの間隔は設けない

        // テーブルヘッダ (横スクロール同期)
        Rectangle {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            height: 32              // ヘッダの高さ
            clip: true              // 横スクロールではみ出した部分を描画しない
            color: palette.button   // ヘッダの背景色

            // パッケージリストの横スクロールに追従するヘッダ領域
            Flickable {
                id: headerFlickable  // ヘッダ領域の参照名

                anchors.fill: parent                         // ヘッダ全体を使う
                contentWidth: packageFlickable.contentWidth  // パッケージリストと同じ内容幅
                contentX: packageFlickable.contentX          // パッケージリストの横スクロール位置に合わせる
                interactive: false                           // ヘッダ自体はドラッグでスクロールさせない

                // 各列のヘッダを横に並べる
                Row {
                    x: 4                                                                // パッケージリストの行の左余白に合わせる
                    width: Math.max(headerFlickable.width, pkgPane.pkgMinRowWidth) - 4  // 表示幅と全列の合計幅の大きい方 (左余白を除く)
                    height: 32                                                          // ヘッダの高さ
                    spacing: 0                                                          // 列の間隔は区切り線で表現する

                    // チェックボックス列ヘッダ (ソート可能)
                    Item {
                        width: 36              // チェックボックス列の幅
                        height: parent.height  // ヘッダの高さに合わせる

                        // クリックでチェックボックス列 (列番号0) を基準に並べ替える
                        MouseArea {
                            anchors.fill: parent                // 列ヘッダ全体で受け付ける
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指差しカーソル

                            // チェックボックス列で並べ替える
                            onClicked: {
                                toggleSort(0)
                            }
                        }

                        // ソート方向の表示 (この列で並べ替え中の場合のみ)
                        Label {
                            anchors.centerIn: parent  // 列ヘッダの中央に表示する
                            // 昇順は▲、降順は▼
                            text: PackageController.packageProxy.sortColumn === 0 ? (PackageController.packageProxy.sortAscending ? "▲" : "▼") : ""
                            font.pixelSize: 10        // 文字サイズ
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // ヘッダの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // パッケージ列ヘッダ (ソート可能・リサイズ可能)
                    Item {
                        width: pkgPane.colName  // パッケージ名列の幅
                        height: parent.height   // ヘッダの高さに合わせる
                        clip: true              // 列幅を狭めた場合に列名がはみ出さないようにする

                        // クリックでパッケージ名列 (列番号1) を基準に並べ替える (右端のリサイズハンドルは除く)
                        MouseArea {
                            anchors.left: parent.left           // 列ヘッダの左端を基準にする
                            anchors.top: parent.top             // 列ヘッダの上端を基準にする
                            anchors.bottom: parent.bottom       // 列ヘッダの下端を基準にする
                            width: parent.width - 4             // リサイズハンドルの幅を除く
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指差しカーソル

                            // パッケージ名列で並べ替える
                            onClicked: {
                                toggleSort(1)
                            }
                        }

                        // 列名とソート方向を横に並べる
                        Row {
                            anchors.centerIn: parent  // 列ヘッダの中央に表示する
                            spacing: 4                // 列名とソート方向の間隔

                            // 列名
                            Label {
                                text: qsTr("Packages")  // 列名の文字列
                                font.bold: true         // 太字で表示する
                            }

                            // ソート方向の表示 (この列で並べ替え中の場合のみ)
                            Label {
                                anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                // 昇順は▲、降順は▼
                                text: PackageController.packageProxy.sortColumn === 1 ? (PackageController.packageProxy.sortAscending ? "▲" : "▼") : ""
                                font.pixelSize: 10                             // 文字サイズ
                            }
                        }

                        // リサイズハンドル (ホバー中またはドラッグ中は選択色で表示する)
                        Rectangle {
                            anchors.right: parent.right  // 列ヘッダの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // ヘッダの高さに合わせる
                            // 操作中のみ見える色
                            color: phResize1.hovered || phResize1.pressed ? palette.highlight : "transparent"

                            // ドラッグでパッケージ名列の幅を変更する
                            MouseArea {
                                id: phResize1  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時の画面X座標
                                property real startW: 0        // ドラッグ開始時のパッケージ名列幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー開始
                                onEntered: {
                                    hovered = true
                                }

                                // ホバー終了
                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の画面座標と列幅を記録する
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = pkgPane.colName
                                }

                                // ドラッグ量に応じて列幅を変更する (縮小下限を下回らない)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        pkgPane.colName = Math.max(pkgPane.minColName, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // ヘッダの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // 概要列ヘッダ (リサイズ可能)
                    Item {
                        width: pkgPane.colSummary  // 概要列の幅
                        height: parent.height      // ヘッダの高さに合わせる
                        clip: true                 // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.centerIn: parent  // 列ヘッダの中央に表示する
                            text: qsTr("Summary")     // 列名の文字列
                            font.bold: true           // 太字で表示する
                        }

                        // リサイズハンドル (ホバー中またはドラッグ中は選択色で表示する)
                        Rectangle {
                            anchors.right: parent.right  // 列ヘッダの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // ヘッダの高さに合わせる
                            // 操作中のみ見える色
                            color: phResize2.hovered || phResize2.pressed ? palette.highlight : "transparent"

                            // ドラッグで概要列の幅を変更する
                            MouseArea {
                                id: phResize2  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時の画面X座標
                                property real startW: 0        // ドラッグ開始時の概要列幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー開始
                                onEntered: {
                                    hovered = true
                                }

                                // ホバー終了
                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の画面座標と列幅を記録する
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = pkgPane.colSummary
                                }

                                // ドラッグ量に応じて列幅を変更する (縮小下限を下回らない)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        pkgPane.colSummary = Math.max(pkgPane.minColSummary, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // ヘッダの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // インストール済み列ヘッダ (リサイズ可能)
                    Item {
                        width: pkgPane.colInstalled  // 導入済み列の幅
                        height: parent.height        // ヘッダの高さに合わせる
                        clip: true                   // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.centerIn: parent                           // 列ヘッダの中央に表示する
                            width: Math.min(implicitWidth, parent.width - 10)  // 列幅に収まる幅 (左右に余白を残す)
                            text: qsTr("Installed")                            // 列名の文字列
                            font.bold: true                                    // 太字で表示する
                            horizontalAlignment: Text.AlignHCenter             // 文字列を中央揃えにする
                            elide: Text.ElideRight                             // 幅に収まらない場合は末尾を省略する
                        }

                        // リサイズハンドル (ホバー中またはドラッグ中は選択色で表示する)
                        Rectangle {
                            anchors.right: parent.right  // 列ヘッダの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // ヘッダの高さに合わせる
                            // 操作中のみ見える色
                            color: phResize3.hovered || phResize3.pressed ? palette.highlight : "transparent"

                            // ドラッグで導入済み列の幅を変更する
                            MouseArea {
                                id: phResize3  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時の画面X座標
                                property real startW: 0        // ドラッグ開始時の導入済み列幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー開始
                                onEntered: {
                                    hovered = true
                                }

                                // ホバー終了
                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の画面座標と列幅を記録する
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = pkgPane.colInstalled
                                }

                                // ドラッグ量に応じて列幅を変更する (縮小下限を下回らない)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        pkgPane.colInstalled = Math.max(pkgPane.minColInstalled, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // ヘッダの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // 利用可能列ヘッダ (リサイズ可能)
                    Item {
                        width: pkgPane.colAvailable  // 利用可能列の幅
                        height: parent.height        // ヘッダの高さに合わせる
                        clip: true                   // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.centerIn: parent  // 列ヘッダの中央に表示する
                            text: qsTr("Available")   // 列名の文字列
                            font.bold: true           // 太字で表示する
                        }

                        // リサイズハンドル (ホバー中またはドラッグ中は選択色で表示する)
                        Rectangle {
                            anchors.right: parent.right  // 列ヘッダの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // ヘッダの高さに合わせる
                            // 操作中のみ見える色
                            color: phResize4.hovered || phResize4.pressed ? palette.highlight : "transparent"

                            // ドラッグで利用可能列の幅を変更する
                            MouseArea {
                                id: phResize4  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時の画面X座標
                                property real startW: 0        // ドラッグ開始時の利用可能列幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー開始
                                onEntered: {
                                    hovered = true
                                }

                                // ホバー終了
                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の画面座標と列幅を記録する
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = pkgPane.colAvailable
                                }

                                // ドラッグ量に応じて列幅を変更する (縮小下限を下回らない)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        pkgPane.colAvailable = Math.max(pkgPane.minColAvailable, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // ヘッダの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // サイズ列ヘッダ (固定幅)
                    Item {
                        width: 88              // サイズ列の幅 (固定)
                        height: parent.height  // ヘッダの高さに合わせる

                        // 列名
                        Label {
                            anchors.centerIn: parent  // 列ヘッダの中央に表示する
                            text: qsTr("Size")        // 列名の文字列
                            font.bold: true           // 太字で表示する
                        }
                    }
                }
            }
        }

        // 表示中の選択行数
        // 選択が無い場合は非表示にし、レイアウト上の領域も確保しない
        Label {
            Layout.fillWidth: true                                                              // 親の幅いっぱいに広げる
            Layout.leftMargin: 8                                                                // 左側の余白
            visible: PackageController.packageProxy.selectedVisibleCount > 0                    // 1行以上選択している場合のみ表示する
            text: qsTr("%n selected", "", PackageController.packageProxy.selectedVisibleCount)  // 選択行数
            color: palette.placeholderText                                                      // 補足情報として控えめな文字色
        }

        // パッケージリスト (横スクロール対応)
        Flickable {
            id: packageFlickable  // 横スクロール領域の参照名

            Layout.fillWidth: true                                          // 親の幅いっぱいに広げる
            Layout.fillHeight: true                                         // 残りの高さをすべて使う
            contentWidth: Math.max(width, pkgPane.pkgMinRowWidth)           // 表示幅と全列の合計幅の大きい方
            contentHeight: height                                           // 縦方向はListView側でスクロールする
            clip: true                                                      // 領域外にはみ出した部分を描画しない
            flickableDirection: Flickable.HorizontalFlick                   // 横方向のみスクロールする
            boundsBehavior: Flickable.StopAtBounds                          // 端を越えてスクロールさせない
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ横スクロールバーを表示する

            // パッケージの一覧 (キーボード操作とtypeahead検索に対応する)
            ListView {
                id: packageListView  // パッケージリストの参照名

                // 詳細ペインに表示中のパッケージ名
                property string detailsName: ""  // 詳細ペインに表示中のパッケージ名

                /**
                 * @brief 選択行の名前に応じてC++側の詳細データを再読込する
                 * @param force trueなら同じパッケージ名でも再読込する
                 * @note 戻り値は無く、再読込不要時は早期に戻る
                 * @note PackageController::loadPackageDetailsまたはclearPackageDetailsを呼ぶ
                 * @note 選択行のパッケージが変わった時だけ詳細を読み込む
                 *       (一覧の差分更新で同じ行番号に別パッケージが来た場合も追従する)
                 */
                function syncDetails(force) {
                    var name = PackageController.packageProxy.nameAt(currentIndex)

                    // 表示中と同じパッケージで、強制再読込でもない場合は何もしない
                    if (name === detailsName && !force) {
                        return
                    }

                    detailsName = name

                    // パッケージ名がある場合は詳細を読み込み、空の場合は詳細を消去する
                    if (name.length > 0) {
                        PackageController.loadPackageDetails(name)
                    }
                    else {
                        PackageController.clearPackageDetails()
                    }
                }

                width: packageFlickable.contentWidth   // 横スクロール領域の内容幅に合わせる
                height: packageFlickable.height        // 横スクロール領域の高さに合わせる
                clip: true                             // 領域外にはみ出した部分を描画しない
                focus: true                            // 初期状態でキーボード入力を受け取る
                activeFocusOnTab: true                 // [Tab]キーでフォーカスを移動できるようにする
                model: PackageController.packageProxy  // PackageControllerのフィルタ・ソート済みモデル
                keyNavigationWraps: false              // 先頭と末尾で選択を折り返さない
                highlightMoveDuration: 0               // 選択移動のアニメーションを行わない
                highlightMoveVelocity: -1              // 移動速度による制御を無効にする
                highlightResizeDuration: 0             // 選択サイズ変更のアニメーションを行わない
                ScrollBar.vertical: ScrollBar {}       // 縦スクロールバー

                // 選択行が変わったので詳細を同期する
                onCurrentIndexChanged: {
                    syncDetails()
                }

                // 上下・先頭・末尾・ページ移動キー、全選択、状態変更キー、typeahead検索の文字入力を処理する
                // 移動キーは、[Shift]キー併用なら起点からの範囲選択を広げ、単独なら移動先の行だけを選択する
                Keys.onPressed: function(event) {
                    var extend = (event.modifiers & Qt.ShiftModifier) !== 0

                    // [Ctrl]+[A]キー : 表示中の全行を選択する (typeahead検索の文字としては扱わない)
                    if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier) !== 0) {
                        PackageController.packageProxy.selectAllVisible()
                        event.accepted = true
                        return
                    }

                    switch (event.key) {
                        // [↑]キー : 1行上へ移動する
                        case Qt.Key_Up:
                            if (currentIndex > 0) {
                                currentIndex--
                            }
                            pkgPane.selectAfterMove(currentIndex, extend)
                            event.accepted = true
                            break
                        // [↓]キー : 1行下へ移動する
                        case Qt.Key_Down:
                            if (currentIndex < count - 1) {
                                currentIndex++
                            }
                            pkgPane.selectAfterMove(currentIndex, extend)
                            event.accepted = true
                            break
                        // [Home]キー : 先頭行へ移動する
                        case Qt.Key_Home:
                            if (count > 0) {
                                currentIndex = 0
                                positionViewAtIndex(0, ListView.Beginning)
                                pkgPane.selectAfterMove(currentIndex, extend)
                            }
                            event.accepted = true
                            break
                        // [End]キー : 末尾行へ移動する
                        case Qt.Key_End:
                            if (count > 0) {
                                currentIndex = count - 1
                                positionViewAtIndex(count - 1, ListView.End)
                                pkgPane.selectAfterMove(currentIndex, extend)
                            }
                            event.accepted = true
                            break
                        // [PageUp]キー : 表示行数 (行の高さ28px換算) だけ上へ移動する
                        case Qt.Key_PageUp:
                            currentIndex = Math.max(0, currentIndex - Math.floor(height / 28))
                            positionViewAtIndex(currentIndex, ListView.Contain)
                            pkgPane.selectAfterMove(currentIndex, extend)
                            event.accepted = true
                            break
                        // [PageDown]キー : 表示行数 (行の高さ28px換算) だけ下へ移動する
                        case Qt.Key_PageDown:
                            currentIndex = Math.min(count - 1, currentIndex + Math.floor(height / 28))
                            positionViewAtIndex(currentIndex, ListView.Contain)
                            pkgPane.selectAfterMove(currentIndex, extend)
                            event.accepted = true
                            break
                        case Qt.Key_Escape:
                            // 通常はウィンドウ側のShortcut ([Esc]キー) が処理する
                            // キーがここへ届いた場合のフォールバック
                            pkgSearchBuffer = ""
                            pkgSearchClearTimer.stop()
                            event.accepted = true
                            break
                        // [Backspace]キー : 検索バッファの末尾1文字を削除する
                        case Qt.Key_Backspace:
                            if (pkgSearchBuffer.length > 0) {
                                pkgSearchBuffer = pkgSearchBuffer.slice(0, -1)
                                // 文字が残っている場合は再検索し、空になった場合は消去タイマを止める
                                if (pkgSearchBuffer.length > 0) {
                                    performPkgSearch()
                                    pkgSearchClearTimer.restart()
                                }
                                else {
                                    pkgSearchClearTimer.stop()
                                }
                            }
                            event.accepted = true
                            break
                        // その他のキー : 状態変更キーであれば対象パッケージの状態を一括変更する
                        // 状態変更キーとして扱うのは検索バッファが空の場合だけで、入力途中の[-]などは検索文字列として扱う
                        // それ以外の表示可能な文字であれば検索バッファへ追加して検索する
                        default:
                            var noCommandModifier = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier)) === 0
                            var keyAction = ""

                            if (pkgPane.pkgSearchBuffer.length === 0 && noCommandModifier) {
                                keyAction = pkgPane.statusKeyAction(event.text)
                            }

                            if (keyAction.length > 0) {
                                pkgPane.runPackagesAction(pkgPane.actionTargetNames(), keyAction)
                                event.accepted = true
                            }
                            else if (event.text.length > 0 && event.text.charCodeAt(0) >= 0x20) {
                                pkgSearchBuffer += event.text
                                performPkgSearch()
                                pkgSearchClearTimer.restart()
                                event.accepted = true
                            }
                            break
                    }
                }

                // パッケージの1行
                delegate: ItemDelegate {
                    id: pkgDelegate  // 行の参照名

                    required property int index                // PackageProxy内の行番号
                    required property string name              // パッケージ名
                    required property string summary           // パッケージ概要
                    required property string version           // 利用可能な候補バージョン
                    required property string installedVersion  // 導入済みバージョン
                    required property string arch              // パッケージアーキテクチャ
                    required property string repoAlias         // 提供リポジトリの別名
                    required property string repoName          // 提供リポジトリの表示名
                    required property real size                // 導入サイズ
                    required property int status               // C++側PackageStatus値
                    required property bool selected            // 複数選択に含まれているか
                    required property bool updatable           // 導入済みより新しい更新候補があるか

                    property bool isChecked: PackageStatusInfo.isChecked(pkgDelegate.status)  // チェック表示する状態か
                    // 削除予定の状態か
                    property bool isMarkedForRemoval: PackageStatusInfo.isMarkedForRemoval(pkgDelegate.status)

                    width: packageListView.width  // リストの幅に合わせる
                    height: 28                    // 行の高さ
                    // 選択行を強調表示する
                    // 表示中の選択行が無い場合は、状態変更キーの対象になる現在行を強調表示する
                    highlighted: pkgDelegate.selected
                                 || (pkgDelegate.ListView.isCurrentItem && PackageController.packageProxy.selectedVisibleCount === 0)

                    // 行の背景 (選択行は選択色、選択されていない現在行は選択色の枠線)
                    background: Rectangle {
                        color: pkgDelegate.highlighted ? palette.highlight : "transparent"  // 選択行のみ選択色で塗りつぶす
                        // 選択されていない現在行は枠線だけで示す
                        border.width: pkgDelegate.ListView.isCurrentItem && !pkgDelegate.highlighted ? 1 : 0
                        border.color: palette.highlight                                     // 現在行の枠線の色
                    }

                    // 左クリックで行を選択する
                    // 修飾キーなしはこの行だけ、[Ctrl]キー併用は選択の反転、[Shift]キー併用は起点からの範囲を選択する
                    TapHandler {
                        id: rowTapHandler  // 左クリック判定の参照名

                        acceptedButtons: Qt.LeftButton  // 左ボタンのみ受け付ける

                        // 修飾キーに応じて選択と起点を更新し、この行を現在行にしてキーボード操作を受け付けるようにする
                        // 修飾キーはクリック時の状態を保持しているpoint.modifiersから取得する
                        onTapped: function(eventPoint, button) {
                            var proxy = PackageController.packageProxy
                            var modifiers = rowTapHandler.point.modifiers

                            if ((modifiers & Qt.ShiftModifier) !== 0) {
                                proxy.selectRange(pkgDelegate.index)
                            }
                            else if ((modifiers & Qt.ControlModifier) !== 0) {
                                proxy.toggleSelected(pkgDelegate.index)
                                proxy.setAnchorRow(pkgDelegate.index)
                            }
                            else {
                                proxy.selectOnly(pkgDelegate.index)
                                proxy.setAnchorRow(pkgDelegate.index)
                            }

                            packageListView.currentIndex = pkgDelegate.index
                            packageListView.forceActiveFocus()
                        }
                    }

                    // 右クリックでこの行を選択し、コンテキストメニューを開く
                    TapHandler {
                        acceptedButtons: Qt.RightButton  // 右ボタンのみ受け付ける

                        // 未選択の行ならこの行だけを選択し、選択済みの行なら複数選択を維持する
                        // 表示中の選択行が2行以上なら一括操作メニュー、それ以外は対象パッケージの情報を渡して単一行のメニューを開く
                        onTapped: {
                            var proxy = PackageController.packageProxy

                            if (!pkgDelegate.selected) {
                                proxy.selectOnly(pkgDelegate.index)
                                proxy.setAnchorRow(pkgDelegate.index)
                            }

                            packageListView.currentIndex = pkgDelegate.index
                            packageListView.forceActiveFocus()

                            if (proxy.selectedVisibleCount > 1) {
                                pkgBulkMenu.popup()
                            }
                            else {
                                pkgContextMenu.targetPkg = ({ name: pkgDelegate.name,
                                                              installedVersion: pkgDelegate.installedVersion,
                                                              status: pkgDelegate.status,
                                                              updatable: pkgDelegate.updatable,
                                                              hasCandidate: pkgDelegate.repoAlias !== "" })
                                pkgContextMenu.popup()
                            }
                        }
                    }

                    // 各列のセルを横に並べる
                    Row {
                        anchors.fill: parent   // 行全体を使う
                        anchors.leftMargin: 4  // 左側の余白
                        spacing: 0             // 列の間隔は区切り線で表現する

                        // チェックボックス列 (ステータスアイコン)
                        Item {
                            width: 36              // チェックボックス列の幅
                            height: parent.height  // 行の高さに合わせる
                            z: 10                  // 行のクリック判定より手前で受け付ける

                            // ステータスアイコン
                            StatusIcon {
                                anchors.centerIn: parent         // セルの中央に配置する
                                status: pkgDelegate.status || 0  // 表示するステータス値 (未設定時は0)
                            }

                            // アイコンのクリックでステータスを切り替える
                            MouseArea {
                                anchors.fill: parent                // チェックボックス列全体で受け付ける
                                cursorShape: Qt.PointingHandCursor  // クリックできることを示す指差しカーソル

                                // 現在のステータスに応じて、削除取り消し・インストール・解除のいずれかを要求する
                                onClicked: function(mouse) {
                                    mouse.accepted = true
                                    var pkg = ({ name: pkgDelegate.name, installedVersion: pkgDelegate.installedVersion, status: pkgDelegate.status })
                                    var s = pkg.status

                                    if (pkgDelegate.isMarkedForRemoval) {
                                        // Del/AutoDel → KeepInstalled (削除取り消し)
                                        setStatus(pkg.name, 3)
                                    }
                                    else if (!pkgDelegate.isChecked) {
                                        // 未チェック → インストール
                                        if (s === 0) {
                                            setStatus(pkg.name, 1)
                                        }
                                        else if (s === 8) {
                                            // Taboo → Install
                                            setStatus(pkg.name, 1)
                                        }
                                    }
                                    else {
                                        // チェック済み → 解除
                                        if (s === 3 || s === 4 || s === 5 || s === 9) {
                                            setStatus(pkg.name, 6)
                                        }
                                        else if (s === 1) {
                                            setStatus(pkg.name, 0)
                                        }
                                        else if (s === 2) {
                                            // AutoInstall → Taboo
                                            setStatus(pkg.name, 8)
                                        }
                                    }
                                }
                            }
                        }

                        // 区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.5           // ヘッダより控えめに表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // パッケージ名
                        Item {
                            width: pkgPane.colName  // パッケージ名列の幅
                            height: parent.height   // 行の高さに合わせる
                            clip: true              // 列幅を狭めた場合に文字列がはみ出さないようにする

                            // パッケージ名
                            Label {
                                anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                anchors.left: parent.left                      // セルの左端を基準にする
                                anchors.right: parent.right                    // セルの右端を基準にする
                                anchors.leftMargin: 4                          // 左側の余白
                                anchors.rightMargin: 4                         // 右側の余白
                                text: pkgDelegate.name || ""                   // パッケージ名
                                textFormat: Text.PlainText                     // HTMLとして解釈させない
                                // 選択行では選択色の上で読みやすい文字色
                                color: pkgDelegate.highlighted ? palette.highlightedText : palette.text
                                elide: Text.ElideRight                         // 幅に収まらない場合は末尾を省略する
                            }
                        }

                        // 区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.5           // ヘッダより控えめに表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // 概要
                        Item {
                            width: pkgPane.colSummary  // 概要列の幅
                            height: parent.height      // 行の高さに合わせる
                            clip: true                 // 列幅を狭めた場合に文字列がはみ出さないようにする

                            // パッケージの概要
                            Label {
                                anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                anchors.left: parent.left                      // セルの左端を基準にする
                                anchors.right: parent.right                    // セルの右端を基準にする
                                anchors.leftMargin: 4                          // 左側の余白
                                anchors.rightMargin: 4                         // 右側の余白
                                text: pkgDelegate.summary || ""                // パッケージの概要
                                textFormat: Text.PlainText                     // HTMLとして解釈させない
                                // 選択行では選択色の上で読みやすい文字色
                                color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                elide: Text.ElideRight                         // 幅に収まらない場合は末尾を省略する
                            }
                        }

                        // 区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.5           // ヘッダより控えめに表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // インストール済み
                        Item {
                            width: pkgPane.colInstalled  // インストール済み列の幅
                            height: parent.height        // 行の高さに合わせる
                            clip: true                   // 列幅を狭めた場合に文字列がはみ出さないようにする

                            // インストール済みのバージョン
                            Label {
                                anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                anchors.left: parent.left                      // セルの左端を基準にする
                                anchors.right: parent.right                    // セルの右端を基準にする
                                anchors.leftMargin: 4                          // 左側の余白
                                anchors.rightMargin: 4                         // 右側の余白
                                text: pkgDelegate.installedVersion || ""       // インストール済みのバージョン
                                textFormat: Text.PlainText                     // HTMLとして解釈させない
                                // 選択行では選択色の上で読みやすい文字色
                                color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                elide: Text.ElideRight                         // 幅に収まらない場合は末尾を省略する
                            }
                        }

                        // 区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.5           // ヘッダより控えめに表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // 利用可能
                        Item {
                            width: pkgPane.colAvailable  // 利用可能列の幅
                            height: parent.height        // 行の高さに合わせる
                            clip: true                   // 列幅を狭めた場合に文字列がはみ出さないようにする

                            // 利用可能な候補バージョン
                            Label {
                                anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                anchors.left: parent.left                      // セルの左端を基準にする
                                anchors.right: parent.right                    // セルの右端を基準にする
                                anchors.leftMargin: 4                          // 左側の余白
                                anchors.rightMargin: 4                         // 右側の余白
                                text: pkgDelegate.version || ""                // 利用可能な候補バージョン
                                textFormat: Text.PlainText                     // HTMLとして解釈させない
                                // 選択行では選択色の上で読みやすい文字色
                                color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                elide: Text.ElideRight                         // 幅に収まらない場合は末尾を省略する
                            }
                        }

                        // 区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.5           // ヘッダより控えめに表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // サイズ
                        Item {
                            width: 88              // サイズ列の幅 (固定)
                            height: parent.height  // 行の高さに合わせる

                            // インストールサイズ (Util.formatSize()で単位付きに整形する)
                            Label {
                                anchors.verticalCenter: parent.verticalCenter                    // 上下中央に配置する
                                anchors.left: parent.left                                        // セルの左端を基準にする
                                anchors.right: parent.right                                      // セルの右端を基準にする
                                anchors.rightMargin: 8                                           // 右側の余白
                                text: pkgDelegate.size ? Util.formatSize(pkgDelegate.size) : ""  // 単位付きのサイズ (0の場合は空)
                                // 選択行では選択色の上で読みやすい文字色
                                color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                horizontalAlignment: Text.AlignRight                             // 数値なので右揃えにする
                            }
                        }
                    }
                }

                // パッケージモデルの変更通知を受けて、選択行の詳細を同期する
                Connections {
                    target: PackageController.packageProxy  // PackageControllerのフィルタ・ソート済みモデル

                    /** @brief モデル全体の再構築後に選択行の詳細を同期する */
                    function onModelReset() {
                        packageListView.syncDetails()
                    }

                    /** @brief 行配置変更後に選択行の詳細を同期する */
                    function onLayoutChanged() {
                        packageListView.syncDetails()
                    }

                    /** @brief 行追加後に選択行の詳細を同期する */
                    function onRowsInserted() {
                        packageListView.syncDetails()
                    }

                    /** @brief 行削除後に選択行の詳細を同期する */
                    function onRowsRemoved() {
                        packageListView.syncDetails()
                    }

                    /**
                     * @brief 選択行のデータ変更時だけ詳細を強制再読込する
                     * @param topLeft 変更範囲の左上 (rowプロパティを持つ)
                     * @param bottomRight 変更範囲の右下 (rowプロパティを持つ)
                     * @note 変更範囲に選択行が含まれるときsyncDetailsを強制実行する
                     * @note 選択中パッケージの状態・候補版が変わったら詳細 (バージョンタブ等) も更新する
                     */
                    function onDataChanged(topLeft, bottomRight) {
                        var row = packageListView.currentIndex
                        if (row >= topLeft.row && row <= bottomRight.row) {
                            packageListView.syncDetails(true)
                        }
                    }
                }
            }

            // typeahead検索オーバーレイ
            Rectangle {
                id: pkgSearchPopup  // 検索オーバーレイの参照名

                anchors.top: parent.top                                       // リストの上端を基準にする
                anchors.right: parent.right                                   // リストの右端を基準にする
                anchors.topMargin: 8                                          // 上側の間隔
                anchors.rightMargin: 16                                       // 右側の間隔
                width: Math.max(220, pkgSearchPopupLabel.implicitWidth + 24)  // 検索文字列の幅に余白を加えた幅 (最小220px)
                height: 36                                                    // オーバーレイの高さ
                z: 1000                                                       // リストより手前に表示する
                visible: pkgSearchBuffer.length > 0                           // 検索文字列を入力中の場合のみ表示する
                color: pkgPane.darkMode ? "#1B5E20" : "#2E7D32"               // 背景色 (ダークモードでは暗い緑)
                radius: 5                                                     // 角の丸み
                border.color: pkgPane.darkMode ? "#0E3A12" : "#1B5E20"        // 枠線の色 (ダークモードでは暗い緑)
                border.width: 1                                               // 枠線の太さ

                // 入力中の検索文字列
                Label {
                    id: pkgSearchPopupLabel  // 検索文字列の参照名

                    anchors.centerIn: parent                  // オーバーレイの中央に表示する
                    text: qsTr("Search: ") + pkgSearchBuffer  // 入力中の検索文字列
                    textFormat: Text.PlainText                // HTMLとして解釈させない
                    color: "#ffffff"                          // 緑の背景上で読みやすい白
                    font.bold: true                           // 太字で表示する
                }
            }
        }

        // パッケージコンテキストメニュー
        Menu {
            id: pkgContextMenu  // コンテキストメニューの参照名

            property var targetPkg: ({})  // 右クリックで選択したパッケージ情報

            // [Install]メニュー
            MenuItem {
                text: qsTr("Install")                           // メニューの文字列
                enabled: pkgContextMenu.targetPkg.status === 0  // 未インストール (NoInst) の場合のみ選択できる

                // インストール (Install) を要求する
                onTriggered: {
                    setStatus(pkgContextMenu.targetPkg.name, 1)
                }
            }

            // [Delete]メニュー
            MenuItem {
                text: qsTr("Delete")  // メニューの文字列
                // 導入済み (KeepInstalled) または更新予定 (Update) の場合のみ選択できる
                enabled: pkgContextMenu.targetPkg.status === 3
                        || pkgContextMenu.targetPkg.status === 4

                // 削除 (Del) を要求する
                onTriggered: {
                    setStatus(pkgContextMenu.targetPkg.name, 6)
                }
            }

            // [Update]メニュー
            MenuItem {
                text: qsTr("Update")  // メニューの文字列
                // 導入済み (KeepInstalled) で、libzyppが導入済みより新しい更新候補を見つけた場合のみ選択できる
                enabled: pkgContextMenu.targetPkg.status === 3 && pkgContextMenu.targetPkg.updatable === true

                // 更新 (Update) を要求する
                // 候補の版が導入済みより古い場合もあるため、状態を直接変えずに一括操作の[update]で更新候補を選んでから更新する
                // PackageController::setPackagesActionを呼び出し、結果はpackagesActionFinishedシグナルで受け取る
                onTriggered: {
                    pkgPane.runPackagesAction([pkgContextMenu.targetPkg.name], "update")
                }
            }

            // [Update Unconditionally]メニュー
            // 版の新旧を比較せず、現在の候補の版で入れ直す (同じ版なら再インストール、古い版ならダウングレード)
            MenuItem {
                text: qsTr("Update Unconditionally")  // メニューの文字列 (一括操作メニューと同じ訳語を使う)
                // 導入済み (KeepInstalled) で、リポジトリに候補の版がある場合のみ選択できる
                // repoAliasは候補の版がある場合だけ設定されるため、空なら導入済みのみ (リポジトリに無い) と判断する
                // 保護 (Protected) のパッケージは変更させないため、[Update]メニューと同じく対象外にする
                enabled: pkgContextMenu.targetPkg.status === 3 && pkgContextMenu.targetPkg.hasCandidate === true

                // 無条件の更新を要求する
                // PackageController::setPackagesActionを呼び出し、結果はpackagesActionFinishedシグナルで受け取る
                onTriggered: {
                    pkgPane.runPackagesAction([pkgContextMenu.targetPkg.name], "updateForce")
                }
            }

            // 区切り線
            MenuSeparator {}

            // [Taboo (Prevent Install)]メニュー
            MenuItem {
                text: qsTr("Taboo (Prevent Install)")           // メニューの文字列
                enabled: pkgContextMenu.targetPkg.status === 0  // 未インストール (NoInst) の場合のみ選択できる

                // インストール禁止 (Taboo) を要求する
                onTriggered: {
                    setStatus(pkgContextMenu.targetPkg.name, 8)
                }
            }

            // [Protected (Prevent Removal)]メニュー
            MenuItem {
                text: qsTr("Protected (Prevent Removal)")       // メニューの文字列
                enabled: pkgContextMenu.targetPkg.status === 3  // 導入済み (KeepInstalled) の場合のみ選択できる

                // 削除禁止 (Protected) を要求する
                onTriggered: {
                    setStatus(pkgContextMenu.targetPkg.name, 9)
                }
            }

            // 区切り線
            MenuSeparator {}

            // [Undo Changes]メニュー
            MenuItem {
                text: qsTr("Undo Changes")  // メニューの文字列
                // Install (1)、Update (4)、Del (6)、Taboo (8)、Protected (9) の変更予定がある場合のみ選択できる
                enabled: pkgContextMenu.targetPkg.status === 1
                        || pkgContextMenu.targetPkg.status === 4
                        || pkgContextMenu.targetPkg.status === 6
                        || pkgContextMenu.targetPkg.status === 8
                        || pkgContextMenu.targetPkg.status === 9

                // 変更予定を取り消し、元の状態に戻す
                onTriggered: {
                    var pkg = pkgContextMenu.targetPkg
                    // インストール系 → NoInst、削除/更新系 → KeepInstalled
                    var revert = (pkg.installedVersion && pkg.installedVersion !== "") ? 3 : 0
                    setStatus(pkg.name, revert)
                }
            }

            // 区切り線
            MenuSeparator {}

            // [Lock (Persistent)]メニュー
            MenuItem {
                text: qsTr("Lock (Persistent)")                                   // メニューの文字列
                Accessible.name: qsTr("Create persistent lock for this package")  // スクリーンリーダー向けの名前

                // PackageController::addLock()で、このパッケージ名に完全一致する永続ロックを作成する
                onTriggered: {
                    PackageController.addLock({ name: pkgContextMenu.targetPkg.name,
                                                kind: "package", match: "exact" })
                }
            }

            // [Unlock]メニュー
            MenuItem {
                text: qsTr("Unlock")                                              // メニューの文字列
                Accessible.name: qsTr("Remove persistent lock for this package")  // スクリーンリーダー向けの名前

                // PackageController::removeLock()で、このパッケージ名に完全一致する永続ロックを削除する
                onTriggered: {
                    PackageController.removeLock({ name: pkgContextMenu.targetPkg.name,
                                                    kind: "package", match: "exact" })
                }
            }

            // 区切り線
            MenuSeparator {}

            // [All in This List]サブメニュー (一覧に表示中の全パッケージを一括変更する)
            Menu {
                id: listActionsMenu  // サブメニューの参照名

                title: qsTr("All in This List")  // サブメニューの文字列

                // 一括操作の各項目を生成し、サブメニューへ順に追加する
                // Qt 6.5のMenuはRepeaterに対応しないため、Instantiatorで生成した項目をinsertItemで挿入する
                Instantiator {
                    model: pkgPane.bulkActions  // 一括操作の一覧

                    // 一覧の表示中全件を対象にする一括操作の項目
                    delegate: BulkActionMenuItem {
                        wholeList: true  // 一覧の表示中全件を対象にする
                    }

                    // 生成した項目をサブメニューの同じ位置へ挿入する
                    onObjectAdded: function(index, object) {
                        listActionsMenu.insertItem(index, object)
                    }

                    // 破棄される項目をサブメニューから外す
                    onObjectRemoved: function(index, object) {
                        listActionsMenu.removeItem(object)
                    }
                }
            }
        }

        // 複数選択時のコンテキストメニュー
        // 表示中の選択行が2行以上の場合に、単一行のメニューの代わりに開く
        Menu {
            id: pkgBulkMenu  // 複数選択時のコンテキストメニューの参照名

            // 選択中のパッケージ数の見出し (選択できない)
            MenuItem {
                // 見出しの文字列
                text: qsTr("%n selected package(s)", "", PackageController.packageProxy.selectedVisibleCount)
                enabled: false  // 見出しなので選択できない
            }

            // 区切り線
            MenuSeparator {}

            // 選択行を対象にする一括操作の項目を生成し、見出しと区切り線の後へ順に追加する
            // Qt 6.5のMenuはRepeaterに対応しないため、Instantiatorで生成した項目をinsertItemで挿入する
            Instantiator {
                model: pkgPane.bulkActions  // 一括操作の一覧

                // 表示中の選択行を対象にする一括操作の項目
                delegate: BulkActionMenuItem {}

                // 見出しと区切り線 (2項目) の後ろへ挿入する
                onObjectAdded: function(index, object) {
                    pkgBulkMenu.insertItem(index + 2, object)
                }

                // 破棄される項目をメニューから外す
                onObjectRemoved: function(index, object) {
                    pkgBulkMenu.removeItem(object)
                }
            }

            // 区切り線
            MenuSeparator {}

            // [All in This List]サブメニュー (一覧に表示中の全パッケージを一括変更する)
            Menu {
                id: bulkListActionsMenu  // サブメニューの参照名

                title: qsTr("All in This List")  // サブメニューの文字列

                // 一括操作の各項目を生成し、サブメニューへ順に追加する
                Instantiator {
                    model: pkgPane.bulkActions  // 一括操作の一覧

                    // 一覧の表示中全件を対象にする一括操作の項目
                    delegate: BulkActionMenuItem {
                        wholeList: true  // 一覧の表示中全件を対象にする
                    }

                    // 生成した項目をサブメニューの同じ位置へ挿入する
                    onObjectAdded: function(index, object) {
                        bulkListActionsMenu.insertItem(index, object)
                    }

                    // 破棄される項目をサブメニューから外す
                    onObjectRemoved: function(index, object) {
                        bulkListActionsMenu.removeItem(object)
                    }
                }
            }
        }
    }

    // 空一覧時のメッセージ (全体が0件か、ステータスフィルタで0件か)
    Label {
        anchors.centerIn: parent                                         // ペインの中央に表示する
        visible: packageListView.count === 0 && !PackageController.busy  // 一覧が0件で、処理中でない場合のみ表示する
        // パッケージが1件もない場合と、ステータスフィルタで全件除外された場合でメッセージを切り替える
        text: PackageController.packages.length === 0 ? qsTr("No packages to display")
                                            : qsTr("No packages match the status filter")
        color: palette.placeholderText                                   // 補足情報として控えめな文字色
    }
}
