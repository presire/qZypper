/**
 * @file RepoTable.qml
 * @brief リポジトリを表形式で選択・有効化する一覧
 *
 * RepoManagerDrawerContentで使用される
 *
 * 入力 : PackageControllerのreposモデル
 * 出力 : 選択リポジトリと有効状態変更の要求
 * 依存 : PackageController (D-Busバックエンドのリポジトリモデル)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

// リポジトリ一覧表のルート要素
// 列見出し(ソート・列幅変更)とリポジトリの行リストを縦に並べて表示する
Item {
    id: root  // 表全体の参照名

    property var model: []                  // 親から受け取るリポジトリ一覧
    property int selectedIndex: -1          // sortedModel内の選択行番号
    // 選択行のリポジトリ情報
    // 選択行番号がsortedModelの範囲外の場合はnullとし、存在しない行を参照しないようにする
    property var selectedRepo: selectedIndex >= 0 && selectedIndex < sortedModel.length
                                 ? sortedModel[selectedIndex] : null
    property var sortedModel: sortedData()  // ListViewへ渡すソート済み一覧

    // ソート状態
    property int sortColumn: 3         // ソート対象の列番号 (デフォルト : 名前列)
    property bool sortAscending: true  // 現在のソート方向 (true : 昇順)

    // 列幅 (ただし、URL列は残り幅を使用)
    property real colPriority: 60     // 優先度列の現在幅
    property real colEnabled: 50      // 有効列の現在幅
    property real colAutoRefresh: 70  // 自動更新列の現在幅
    property real colName: 200        // 名前列の現在幅
    property real colService: 120     // サービス列の現在幅

    // 最小行幅 (全列の合計最小幅、列区切り線とURL列の最小幅150を含む)
    readonly property real minRowWidth: colPriority + 1 + colEnabled + 1 + colAutoRefresh + 1 + colName + 1 + colService + 1 + 150

    signal repoDoubleClicked(var repo)                 // 親へ選択リポジトリの詳細表示を要求する
    signal enabledToggled(string alias, bool enabled)  // 親へ有効状態の変更を通知する

    /**
     * @brief 指定列のソート方向を切り替え、選択を解除して別リポジトリを誤編集しない
     * @param col 並べ替えの基準にする列番号
     * @note sortColumnとsortAscendingを更新し、選択中の行を解除する
     */
    function toggleSort(col) {
        if (sortColumn === col) {
            sortAscending = !sortAscending
        }
        else {
            sortColumn = col
            sortAscending = true
        }
        selectedIndex = -1
    }

    /**
     * @brief 現在の列と方向で親モデルの複製をソートして表示順を作る
     * @return ソート済みのリポジトリ配列 (親モデルは変更しない)
     */
    function sortedData() {
        // 親モデルを変更しないよう、複製してからソートする
        var data = Array.from(model)

        // 2件のリポジトリを比較し、aを前にする場合は負、後ろにする場合は正、同順の場合は0を返す
        data.sort(function(a, b) {
            // 比較に使うaとbの値
            var va, vb

            // ソート対象の列に応じて、比較する値を取り出す
            // 文字列は大文字・小文字を区別せずに比較するため、小文字へ変換する
            switch (sortColumn) {
                // [優先度]列 : 未設定の場合は既定値の99として扱う
                case 0:
                    va = a.priority ?? 99
                    vb = b.priority ?? 99
                    break
                // [有効]列 : 無効を0、有効を1として比較する
                case 1:
                    va = a.enabled ? 1 : 0
                    vb = b.enabled ? 1 : 0
                    break
                // [自動更新]列 : 無効を0、有効を1として比較する
                case 2:
                    va = a.autoRefresh ? 1 : 0
                    vb = b.autoRefresh ? 1 : 0
                    break
                // [名前]列 : 名前がない場合はエイリアスで比較する
                case 3:
                    va = (a.name || a.alias || "").toLowerCase()
                    vb = (b.name || b.alias || "").toLowerCase()
                    break
                // [サービス]列 : サービスに属さない場合は空文字列として比較する
                case 4:
                    va = (a.service || "").toLowerCase()
                    vb = (b.service || "").toLowerCase()
                    break
                // [URL]列
                case 5:
                    va = (a.url || "").toLowerCase()
                    vb = (b.url || "").toLowerCase()
                    break
                // 未知の列番号の場合は、並び順を変更しない
                default:
                    return 0
            }

            // 昇順の場合は小さい値を前へ、降順の場合は大きい値を前へ並べる
            if (va < vb) {
                return sortAscending ? -1 : 1
            }

            if (va > vb) {
                return sortAscending ? 1 : -1
            }

            // 値が等しい場合は同順として扱う
            return 0
        })

        return data
    }

    // 親モデル・ソート列・ソート方向のいずれかが変わったら、ソート済み一覧を作り直して表示に反映する
    onModelChanged: {
        sortedModel = sortedData()
    }

    onSortColumnChanged: {
        sortedModel = sortedData()
    }

    onSortAscendingChanged: {
        sortedModel = sortedData()
    }

    // ユーザが変更した列幅を設定ファイルへ保存し、次回表示時に復元する
    Settings {
        property alias colPriority: root.colPriority        // 保存する優先度列幅
        property alias colEnabled: root.colEnabled          // 保存する有効列幅
        property alias colAutoRefresh: root.colAutoRefresh  // 保存する自動更新列幅
        property alias colName: root.colName                // 保存する名前列幅
        property alias colService: root.colService          // 保存するサービス列幅

        category: "repoTable"  // 設定ファイル内で列幅を保存するグループ名
    }

    // 列見出しとリスト本体を上下に並べ、表全体を構成する
    ColumnLayout {
        anchors.fill: parent  // 表全体の領域に広げる
        spacing: 0            // 列見出しとリスト本体の間に隙間を空けない

        // 列見出し
        // 見出しのクリックでその列によるソートを行い、見出し右端のドラッグで列幅を変更する
        Rectangle {
            Layout.fillWidth: true  // 表の横幅いっぱいに広げる
            height: 32              // 列見出しの高さ
            color: palette.button   // 行と区別するためボタン色を背景にする

            // 列見出しの横スクロール領域
            // リスト本体と横位置を揃えるため、スクロール位置はリスト本体に追従させ、見出し側での操作は受け付けない
            Flickable {
                id: headerFlick                                  // 列見出しのスクロール領域の参照名
                anchors.fill: parent                             // 列見出し全体に広げる
                contentWidth: Math.max(width, root.minRowWidth)  // リスト本体と同じ内容幅 (表示幅と全列の最小幅の大きい方)
                contentX: listFlick.contentX                     // リスト本体の横スクロール位置に同期する
                flickableDirection: Flickable.HorizontalFlick    // 横方向のみスクロールする
                interactive: false                               // ドラッグやフリックによるスクロールを無効にする

                // 各列の見出しを左から順に並べる
                Row {
                    height: parent.height  // 列見出しの高さに合わせる
                    spacing: 0             // 見出しの間隔は列区切り線で表すため隙間を空けない

                    // [優先度]列の見出し
                    Item {
                        width: root.colPriority  // [優先度]列の現在幅
                        height: parent.height    // 列見出しの高さに合わせる
                        clip: true               // 列幅を狭めた時に見出しがはみ出さないようにする

                        // 見出しのクリックで[優先度]列によるソートを行う領域
                        // 右端の4pxはリサイズハンドルの操作領域として空けておく
                        MouseArea {
                            anchors.left: parent.left           // 見出しの左端から
                            anchors.top: parent.top             // 見出しの上端から
                            anchors.bottom: parent.bottom       // 見出しの下端まで
                            width: parent.width - 4             // リサイズハンドル分を除いた幅
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指カーソル

                            // [優先度]列でソートする (同じ列の再クリックで昇順・降順を切り替える)
                            onClicked: {
                                root.toggleSort(0)
                            }
                        }

                        // 列名とソート方向の矢印を並べて、見出しの中央に表示する
                        Row {
                            anchors.centerIn: parent  // 見出しの中央に配置する
                            spacing: 2                // 列名と矢印の間隔

                            // [優先度]の列名
                            Label {
                                width: Math.min(implicitWidth, root.colPriority - 14)  // 列幅が狭い場合は矢印分を残して列名を縮める
                                text: qsTr("Priority")                                 // 列名 (翻訳対象)
                                font.bold: true                                        // 見出しは太字にする
                                font.pixelSize: 13                                     // 見出しの文字サイズ
                                elide: Text.ElideRight                                 // 収まらない列名は末尾を省略する
                            }

                            // ソート方向の矢印
                            Label {
                                // [優先度]列でソート中の場合のみ、昇順は▲、降順は▼を表示する
                                text: root.sortColumn === 0 ? (root.sortAscending ? "\u25B2" : "\u25BC")
                                                            : ""
                                font.pixelSize: 9  // 列名より小さい矢印の文字サイズ
                            }
                        }

                        // [優先度]列の右端にあるリサイズハンドルの表示部
                        Rectangle {
                            anchors.right: parent.right  // 見出しの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // 見出しの高さに合わせる
                            // ホバー中またはドラッグ中のみ強調色で表示し、操作できる位置を示す
                            color: prioResize.hovered || prioResize.pressed ? palette.highlight
                                                                            : "transparent"

                            // [優先度]列の右端にあるリサイズハンドル
                            // ドラッグ操作で[優先度]列の幅を変更する
                            MouseArea {
                                id: prioResize  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                property real startW: 0        // ドラッグ開始時の[優先度]列の幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー中はハンドルを強調表示するため、マウスカーソルの出入りを記録する
                                onEntered: {
                                    hovered = true
                                }

                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の座標と列幅を保存する
                                // 列幅の変更に伴いハンドル自体も移動するため、ローカル座標ではなくグローバル座標を基準にする
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = root.colPriority
                                }

                                // ドラッグ量に応じて[優先度]列の幅を更新する (最小幅は40)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        root.colPriority = Math.max(40, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // [優先度]列と[有効]列の区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 列見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [有効]列の見出し
                    Item {
                        width: root.colEnabled  // [有効]列の現在幅
                        height: parent.height   // 列見出しの高さに合わせる
                        clip: true              // 列幅を狭めた時に見出しがはみ出さないようにする

                        // 見出しのクリックで[有効]列によるソートを行う領域
                        // 右端の4pxはリサイズハンドルの操作領域として空けておく
                        MouseArea {
                            anchors.left: parent.left           // 見出しの左端から
                            anchors.top: parent.top             // 見出しの上端から
                            anchors.bottom: parent.bottom       // 見出しの下端まで
                            width: parent.width - 4             // リサイズハンドル分を除いた幅
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指カーソル

                            // [有効]列でソートする (同じ列の再クリックで昇順・降順を切り替える)
                            onClicked: {
                                root.toggleSort(1)
                            }
                        }

                        // 列名とソート方向の矢印を並べて、見出しの中央に表示する
                        Row {
                            anchors.centerIn: parent  // 見出しの中央に配置する
                            spacing: 2                // 列名と矢印の間隔

                            // [有効]の列名
                            Label {
                                width: Math.min(implicitWidth, root.colEnabled - 14)  // 列幅が狭い場合は矢印分を残して列名を縮める
                                text: qsTr("Enabled")                                 // 列名 (翻訳対象)
                                font.bold: true                                       // 見出しは太字にする
                                font.pixelSize: 13                                    // 見出しの文字サイズ
                                elide: Text.ElideRight                                // 収まらない列名は末尾を省略する
                            }

                            // ソート方向の矢印
                            Label {
                                // [有効]列でソート中の場合のみ▲(昇順)/▼(降順)を表示する
                                text: root.sortColumn === 1 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""
                                font.pixelSize: 9  // 列名より小さい矢印の文字サイズ
                            }
                        }

                        // [有効]列の右端にあるリサイズハンドルの表示部
                        Rectangle {
                            anchors.right: parent.right  // 見出しの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // 見出しの高さに合わせる
                            // ホバー中またはドラッグ中のみ強調色で表示する
                            color: enabledResize.hovered || enabledResize.pressed ? palette.highlight : "transparent"

                            // [有効]列の右端にあるリサイズハンドル
                            // ドラッグ操作で[有効]列の幅を変更する
                            MouseArea {
                                id: enabledResize  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                property real startW: 0        // ドラッグ開始時の[有効]列の幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー中はハンドルを強調表示するため、マウスカーソルの出入りを記録する
                                onEntered: {
                                    hovered = true
                                }

                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の座標と列幅を保存する
                                // 列幅の変更に伴いハンドル自体も移動するため、ローカル座標ではなくグローバル座標を基準にする
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = root.colEnabled
                                }

                                // ドラッグ量に応じて[有効]列の幅を更新する (最小幅は40)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        root.colEnabled = Math.max(40, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // [有効]列と[自動更新]列の区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 列見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [自動更新]列の見出し
                    Item {
                        width: root.colAutoRefresh  // [自動更新]列の現在幅
                        height: parent.height       // 列見出しの高さに合わせる
                        clip: true                  // 列幅を狭めた時に見出しがはみ出さないようにする

                        // 見出しのクリックで[自動更新]列によるソートを行う領域
                        // 右端の4pxはリサイズハンドルの操作領域として空けておく
                        MouseArea {
                            anchors.left: parent.left           // 見出しの左端から
                            anchors.top: parent.top             // 見出しの上端から
                            anchors.bottom: parent.bottom       // 見出しの下端まで
                            width: parent.width - 4             // リサイズハンドル分を除いた幅
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指カーソル

                            // [自動更新]列でソートする (同じ列の再クリックで昇順・降順を切り替える)
                            onClicked: {
                                root.toggleSort(2)
                            }
                        }

                        // 列名とソート方向の矢印を並べて、見出しの中央に表示する
                        Row {
                            anchors.centerIn: parent  // 見出しの中央に配置する
                            spacing: 2                // 列名と矢印の間隔

                            // [自動更新]の列名
                            Label {
                                width: Math.min(implicitWidth, root.colAutoRefresh - 14)  // 列幅が狭い場合は矢印分を残して列名を縮める
                                text: qsTr("Auto-refresh")                                // 列名 (翻訳対象)
                                font.bold: true                                           // 見出しは太字にする
                                font.pixelSize: 13                                        // 見出しの文字サイズ
                                elide: Text.ElideRight                                    // 収まらない列名は末尾を省略する
                            }

                            // ソート方向の矢印
                            Label {
                                // [自動更新]列でソート中の場合のみ▲(昇順)/▼(降順)を表示する
                                text: root.sortColumn === 2 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""
                                font.pixelSize: 9  // 列名より小さい矢印の文字サイズ
                            }
                        }

                        // [自動更新]列の右端にあるリサイズハンドルの表示部
                        Rectangle {
                            anchors.right: parent.right  // 見出しの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // 見出しの高さに合わせる
                            // ホバー中またはドラッグ中のみ強調色で表示する
                            color: arResize.hovered || arResize.pressed ? palette.highlight : "transparent"

                            // [自動更新]列の右端にあるリサイズハンドル
                            // ドラッグ操作で[自動更新]列の幅を変更する
                            MouseArea {
                                id: arResize  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                property real startW: 0        // ドラッグ開始時の[自動更新]列の幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー中はハンドルを強調表示するため、マウスカーソルの出入りを記録する
                                onEntered: {
                                    hovered = true
                                }

                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の座標と列幅を保存する
                                // 列幅の変更に伴いハンドル自体も移動するため、ローカル座標ではなくグローバル座標を基準にする
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = root.colAutoRefresh
                                }

                                // ドラッグ量に応じて[自動更新]列の幅を更新する (最小幅は50)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        root.colAutoRefresh = Math.max(50, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // [自動更新]列と[名前]列の区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 列見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [名前]列の見出し
                    Item {
                        width: root.colName    // [名前]列の現在幅
                        height: parent.height  // 列見出しの高さに合わせる
                        clip: true             // 列幅を狭めた時に見出しがはみ出さないようにする

                        // 見出しのクリックで[名前]列によるソートを行う領域
                        // 右端の4pxはリサイズハンドルの操作領域として空けておく
                        MouseArea {
                            anchors.left: parent.left           // 見出しの左端から
                            anchors.top: parent.top             // 見出しの上端から
                            anchors.bottom: parent.bottom       // 見出しの下端まで
                            width: parent.width - 4             // リサイズハンドル分を除いた幅
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指カーソル

                            // [名前]列でソートする (同じ列の再クリックで昇順・降順を切り替える)
                            onClicked: {
                                root.toggleSort(3)
                            }
                        }

                        // 列名とソート方向の矢印を並べて、見出しの中央に表示する
                        Row {
                            anchors.centerIn: parent  // 見出しの中央に配置する
                            spacing: 2                // 列名と矢印の間隔

                            // [名前]の列名
                            Label {
                                text: qsTr("Name")  // 列名 (翻訳対象)
                                font.bold: true     // 見出しは太字にする
                                font.pixelSize: 13  // 見出しの文字サイズ
                            }

                            // ソート方向の矢印
                            Label {
                                // [名前]列でソート中の場合のみ▲(昇順)/▼(降順)を表示する
                                text: root.sortColumn === 3 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""
                                font.pixelSize: 9  // 列名より小さい矢印の文字サイズ
                            }
                        }

                        // [名前]列の右端にあるリサイズハンドルの表示部
                        Rectangle {
                            anchors.right: parent.right  // 見出しの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // 見出しの高さに合わせる
                            // ホバー中またはドラッグ中のみ強調色で表示する
                            color: nameResize.hovered || nameResize.pressed ? palette.highlight : "transparent"

                            // [名前]列の右端にあるリサイズハンドル
                            // ドラッグ操作で[名前]列の幅を変更する
                            MouseArea {
                                id: nameResize  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                property real startW: 0        // ドラッグ開始時の[名前]列の幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー中はハンドルを強調表示するため、マウスカーソルの出入りを記録する
                                onEntered: {
                                    hovered = true
                                }

                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の座標と列幅を保存する
                                // 列幅の変更に伴いハンドル自体も移動するため、ローカル座標ではなくグローバル座標を基準にする
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = root.colName
                                }

                                // ドラッグ量に応じて[名前]列の幅を更新する (最小幅は100)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        root.colName = Math.max(100, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // [名前]列と[サービス]列の区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 列見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [サービス]列の見出し
                    Item {
                        width: root.colService  // [サービス]列の現在幅
                        height: parent.height   // 列見出しの高さに合わせる
                        clip: true              // 列幅を狭めた時に見出しがはみ出さないようにする

                        // 見出しのクリックで[サービス]列によるソートを行う領域
                        // 右端の4pxはリサイズハンドルの操作領域として空けておく
                        MouseArea {
                            anchors.left: parent.left           // 見出しの左端から
                            anchors.top: parent.top             // 見出しの上端から
                            anchors.bottom: parent.bottom       // 見出しの下端まで
                            width: parent.width - 4             // リサイズハンドル分を除いた幅
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指カーソル

                            // [サービス]列でソートする (同じ列の再クリックで昇順・降順を切り替える)
                            onClicked: {
                                root.toggleSort(4)
                            }
                        }

                        // 列名とソート方向の矢印を並べて、見出しの中央に表示する
                        Row {
                            anchors.centerIn: parent  // 見出しの中央に配置する
                            spacing: 2                // 列名と矢印の間隔

                            // [サービス]の列名
                            Label {
                                text: qsTr("Service")  // 列名 (翻訳対象)
                                font.bold: true        // 見出しは太字にする
                                font.pixelSize: 13     // 見出しの文字サイズ
                            }

                            // ソート方向の矢印
                            Label {
                                // [サービス]列でソート中の場合のみ、昇順は▲、降順は▼を表示する
                                text: root.sortColumn === 4 ? (root.sortAscending ? "\u25B2" : "\u25BC")
                                                            : ""
                                font.pixelSize: 9  // 列名より小さい矢印の文字サイズ
                            }
                        }

                        // [サービス]列の右端にあるリサイズハンドルの表示部
                        Rectangle {
                            anchors.right: parent.right  // 見出しの右端に配置する
                            width: 4                     // ハンドルの幅
                            height: parent.height        // 見出しの高さに合わせる
                            // ホバー中またはドラッグ中のみ強調色で表示する
                            color: svcResize.hovered || svcResize.pressed ? palette.highlight : "transparent"

                            // [サービス]列の右端にあるリサイズハンドル
                            // ドラッグ操作で[サービス]列の幅を変更する
                            MouseArea {
                                id: svcResize  // リサイズハンドルの参照名

                                property bool hovered: false   // リサイズハンドルのホバー状態
                                property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                property real startW: 0        // ドラッグ開始時の[サービス]列の幅

                                anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
                                hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                // ホバー中はハンドルを強調表示するため、マウスカーソルの出入りを記録する
                                onEntered: {
                                    hovered = true
                                }

                                onExited: {
                                    hovered = false
                                }

                                // ドラッグ開始時の座標と列幅を保存する
                                // 列幅の変更に伴いハンドル自体も移動するため、ローカル座標ではなくグローバル座標を基準にする
                                onPressed: function(mouse) {
                                    var p = mapToGlobal(mouse.x, 0)
                                    globalStartX = p.x
                                    startW = root.colService
                                }

                                // ドラッグ量に応じて[サービス]列の幅を更新する (最小幅は60)
                                onPositionChanged: function(mouse) {
                                    if (pressed) {
                                        var p = mapToGlobal(mouse.x, 0)
                                        root.colService = Math.max(60, startW + p.x - globalStartX)
                                    }
                                }
                            }
                        }
                    }

                    // [サービス]列と[URL]列の区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 列見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [URL]列の見出し
                    // 最終列のため列幅の変更はできず、他の列を除いた残りの幅を使用する
                    Item {
                        // 残りの幅 (最小150、5は列区切り線の合計幅)
                        width: Math.max(150, headerFlick.width - root.colPriority - root.colEnabled - root.colAutoRefresh - root.colName - root.colService - 5)
                        height: parent.height  // 列見出しの高さに合わせる

                        // 見出しのクリックで[URL]列によるソートを行う領域
                        MouseArea {
                            anchors.fill: parent                // 見出し全体でクリックを受け付ける
                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指カーソル

                            // [URL]列でソートする (同じ列の再クリックで昇順・降順を切り替える)
                            onClicked: {
                                root.toggleSort(5)
                            }
                        }

                        // 列名とソート方向の矢印を並べて、見出しの中央に表示する
                        Row {
                            anchors.centerIn: parent  // 見出しの中央に配置する
                            spacing: 2                // 列名と矢印の間隔

                            // [URL]の列名
                            Label {
                                text: qsTr("URL")   // 列名 (翻訳対象)
                                font.bold: true     // 見出しは太字にする
                                font.pixelSize: 13  // 見出しの文字サイズ
                            }

                            // ソート方向の矢印
                            Label {
                                // [URL]列でソート中の場合のみ、昇順は▲、降順は▼を表示する
                                text: root.sortColumn === 5 ? (root.sortAscending ? "\u25B2" : "\u25BC")
                                                            : ""
                                font.pixelSize: 9  // 列名より小さい矢印の文字サイズ
                            }
                        }
                    }
                }
            }
        }

        // リスト本体の横スクロール領域
        // 全列の合計幅が表示幅を超える場合に横スクロールでき、列見出しもこの位置に追従する
        Flickable {
            id: listFlick                                    // リスト本体のスクロール領域の参照名
            Layout.fillWidth: true                           // 表の横幅いっぱいに広げる
            Layout.fillHeight: true                          // 列見出しを除いた残りの高さに広げる
            contentWidth: Math.max(width, root.minRowWidth)  // 内容幅 (表示幅と全列の最小幅の大きい方)
            contentHeight: height                            // 縦方向はスクロールさせず、ListView側でスクロールする
            clip: true                                       // 横スクロール時に領域外の行を描画しない
            flickableDirection: Flickable.HorizontalFlick    // 横方向のみスクロールする
            boundsBehavior: Flickable.StopAtBounds           // 端を越えてスクロールさせない

            // 内容幅が表示幅を超える場合のみ表示する横スクロールバー
            ScrollBar.horizontal: ScrollBar {
                policy: ScrollBar.AsNeeded  // 必要な場合のみスクロールバーを表示する
            }

            // リポジトリを1行ずつ表示する縦方向のリスト
            ListView {
                id: repoListView                  // リポジトリ一覧のリストの参照名
                width: listFlick.contentWidth     // 横スクロール領域の内容幅に合わせる
                height: listFlick.height          // 表示領域の高さに合わせる
                clip: true                        // 縦スクロール時に領域外の行を描画しない
                model: root.sortedModel           // ソート済みのリポジトリ一覧
                currentIndex: root.selectedIndex  // 選択行をリストの現在行に同期する
                ScrollBar.vertical: ScrollBar {}  // 縦スクロールバー

                // リポジトリ1件分の行
                // クリックで行を選択し、ダブルクリックで詳細表示を要求する
                delegate: ItemDelegate {
                    id: del  // 行自身の参照名

                    required property int index      // sortedModel内の行番号
                    required property var modelData  // 現在行のリポジトリ情報

                    width: repoListView.width                      // リストの横幅に合わせる
                    height: 30                                     // 行の高さ
                    padding: 0                                     // 各列を行の端から配置するため余白を設けない
                    highlighted: root.selectedIndex === del.index  // 選択行を強調表示する

                    // クリックした行を選択する
                    onClicked: {
                        root.selectedIndex = del.index
                    }

                    // ダブルクリックした行のリポジトリの詳細表示を親へ要求する
                    onDoubleClicked: {
                        root.repoDoubleClicked(del.modelData)
                    }

                    // 行の背景
                    background: Rectangle {
                        // 選択行は強調色、それ以外は1行おきに薄い色を付けて縞模様にし、行を見分けやすくする
                        color: del.highlighted ? palette.highlight
                                               : del.index % 2 === 0 ? "transparent"
                                               : Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.15)
                    }

                    // 行の各列を左から順に並べる
                    // 各列の幅は列見出しと同じ値を使用し、見出しと列の位置を揃える
                    contentItem: Row {
                        spacing: 0  // 列の間隔は列区切り線で表すため隙間を空けない

                        // [優先度]列 (優先度の数値)
                        // 無効なリポジトリは半透明で表示する
                        Label {
                            width: root.colPriority                                          // [優先度]列の現在幅
                            height: parent.height                                            // 行の高さに合わせる
                            opacity: del.modelData.enabled ? 1.0 : 0.5                       // 無効なリポジトリは半透明にする
                            text: String(del.modelData.priority ?? 99)                       // 優先度 (未設定の場合は99)
                            font.pixelSize: 14                                               // 行の文字サイズ
                            color: del.highlighted ? palette.highlightedText : palette.text  // 選択行では強調色の背景に合わせた文字色にする
                            horizontalAlignment: Text.AlignHCenter                           // 数値を列の中央に揃える
                            verticalAlignment: Text.AlignVCenter                             // 行の上下中央に揃える
                        }

                        // [優先度]列と[有効]列の区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.3           // 列見出しより目立たないよう薄く表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // [有効]列
                        Item {
                            width: root.colEnabled  // [有効]列の現在幅
                            height: parent.height   // 行の高さに合わせる

                            // リポジトリの有効・無効を切り替えるチェックボックス
                            CheckBox {
                                anchors.centerIn: parent                 // 列の中央に配置する
                                checked: del.modelData.enabled || false  // リポジトリの有効状態 (未設定の場合は無効)

                                // ユーザ操作で切り替えた有効状態を親へ通知する
                                // 実際の変更は親がPackageController経由でバックエンドへ要求する
                                onToggled: {
                                    root.enabledToggled(del.modelData.alias, checked)
                                }
                            }
                        }

                        // [有効]列と[自動更新]列の区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.3           // 列見出しより目立たないよう薄く表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // [自動更新]列
                        Item {
                            width: root.colAutoRefresh  // [自動更新]列の現在幅
                            height: parent.height       // 行の高さに合わせる

                            // 自動更新が有効なリポジトリにチェックマークを表示する
                            Label {
                                anchors.centerIn: parent                                         // 列の中央に配置する
                                text: del.modelData.autoRefresh ? "\u2713" : ""                  // 自動更新が有効な場合は✓を表示する
                                font.pixelSize: 14                                               // 行の文字サイズ
                                color: del.highlighted ? palette.highlightedText : palette.text  // 選択行では強調色の背景に合わせた文字色にする
                            }
                        }

                        // [自動更新]列と[名前]列の区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.3           // 列見出しより目立たないよう薄く表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // [名前]列 (リポジトリ名)
                        // 無効なリポジトリは半透明で表示する
                        Label {
                            width: root.colName                                              // [名前]列の現在幅
                            height: parent.height                                            // 行の高さに合わせる
                            leftPadding: 6                                                   // 列区切り線との間の余白
                            opacity: del.modelData.enabled ? 1.0 : 0.5                       // 無効なリポジトリは半透明にする
                            text: del.modelData.name || del.modelData.alias || ""            // リポジトリ名 (名前がない場合はエイリアス)
                            textFormat: Text.PlainText                                       // リポジトリ名をリッチテキストとして解釈させない
                            font.pixelSize: 14                                               // 行の文字サイズ
                            color: del.highlighted ? palette.highlightedText : palette.text  // 選択行では強調色の背景に合わせた文字色にする
                            verticalAlignment: Text.AlignVCenter                             // 行の上下中央に揃える
                            elide: Text.ElideRight                                           // 収まらない名前は末尾を省略する
                        }

                        // [名前]列と[サービス]列の区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.3           // 列見出しより目立たないよう薄く表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // [サービス]列 (リポジトリを提供するサービス名)
                        // 無効なリポジトリは半透明で表示する
                        Label {
                            width: root.colService                      // [サービス]列の現在幅
                            height: parent.height                       // 行の高さに合わせる
                            leftPadding: 6                              // 列区切り線との間の余白
                            opacity: del.modelData.enabled ? 1.0 : 0.5  // 無効なリポジトリは半透明にする
                            text: del.modelData.service || "-"          // サービス名 (サービスに属さない場合は"-")
                            textFormat: Text.PlainText                  // サービス名をリッチテキストとして解釈させない
                            font.pixelSize: 14                          // 行の文字サイズ
                            // 補足情報のため、通常は控えめな文字色にする
                            color: del.highlighted ? palette.highlightedText : palette.placeholderText
                            verticalAlignment: Text.AlignVCenter        // 行の上下中央に揃える
                            elide: Text.ElideRight                      // 収まらないサービス名は末尾を省略する
                        }

                        // [サービス]列と[URL]列の区切り線
                        Rectangle {
                            width: 1               // 区切り線の太さ
                            height: parent.height  // 行の高さに合わせる
                            opacity: 0.3           // 列見出しより目立たないよう薄く表示する
                            color: palette.mid     // 区切り線の色
                        }

                        // [URL]列 (リポジトリのURL)
                        // 無効なリポジトリは他の列より薄く表示する
                        Label {
                            // 残りの幅 (最小150、5は列区切り線の合計幅)
                            width: Math.max(150, listFlick.width - root.colPriority - root.colEnabled - root.colAutoRefresh - root.colName - root.colService - 5)
                            height: parent.height                                            // 行の高さに合わせる
                            leftPadding: 6                                                   // 列区切り線との間の余白
                            opacity: del.modelData.enabled ? 0.9 : 0.4                       // 無効なリポジトリはさらに薄くする
                            text: del.modelData.url || ""                                    // リポジトリのURL
                            textFormat: Text.PlainText                                       // URLをリッチテキストとして解釈させない
                            font.pixelSize: 14                                               // 行の文字サイズ
                            color: del.highlighted ? palette.highlightedText : palette.link  // 通常はリンク色で表示する
                            verticalAlignment: Text.AlignVCenter                             // 行の上下中央に揃える
                            elide: Text.ElideMiddle                                          // 収まらないURLは中央を省略し、スキームと末尾のパスを残す
                        }
                    }
                }
            }
        }
    }
}
