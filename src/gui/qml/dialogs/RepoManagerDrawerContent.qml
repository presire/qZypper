/**
 * @file RepoManagerDrawerContent.qml
 * @brief リポジトリとサービスを管理するドロワーの内容
 *
 * Main.qmlのリポジトリ管理ドロワーで使用される
 *
 * 入力 : PackageControllerのリポジトリ・サービス・更新状態
 * 出力 : 追加、編集、削除、更新の要求
 * 依存 : PackageController, RepoTable, RepoPropertiesPanel, AddRepoDialog, AddServiceDialog
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import QtQuick.Dialogs
import org.presire.qzypper.gui
import "../components" as Components

// リポジトリ管理ドロワーの内容 ([Repositories]タブと[Services]タブ、フッタボタン、各種確認ダイアログ)
Pane {
    id: root  // ドロワー内容自身の参照名

    property var originalRepos: []                          // 変更検出用に保持する読み込み時点のリポジトリ一覧
    property bool repoTabActive: tabBar.currentIndex === 0  // リポジトリタブが選択されているか

    signal closeRequested()                      // ドロワーを閉じる要求
    signal refreshAllRequested()                 // 有効な全リポジトリの更新要求
    signal refreshRepoRequested(string alias)    // 指定リポジトリの更新要求
    signal refreshServiceRequested(string alias) // 指定サービスの更新要求

    /**
     * @brief リポジトリとサービスを読み込む
     * @note PackageController::loadRepos / loadServicesを呼び出す
     * @note 比較用に現在のリポジトリ一覧の複製をoriginalReposへ退避する
     */
    function loadData() {
        PackageController.loadRepos()
        PackageController.loadServices()
        originalRepos = JSON.parse(JSON.stringify(PackageController.repos))
    }

    /**
     * @brief リポジトリ一覧に変更があるかを判定する
     * @return 現在の一覧と読み込み時のoriginalReposが異なればtrue
     */
    function hasRepoChanges() {
        return JSON.stringify(PackageController.repos) !== JSON.stringify(originalRepos)
    }

    padding: 0  // 内容をドロワーの端まで広げる

    // ヘッダ、タブの内容、フッタボタンを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ドロワー全体を使う
        spacing: 0            // 項目の間隔は空けない

        // --- ヘッダ ---
        ColumnLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 0              // 項目の間隔は空けない

            // タイトルと閉じるボタンの行
            RowLayout {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる
                Layout.margins: 16      // 周囲の余白
                Layout.bottomMargin: 8  // 下側の余白 (タブとの間隔)

                // ドロワーのタイトル
                Label {
                    Layout.fillWidth: true               // 残りの幅をすべて使う
                    text: qsTr("Repository Management")  // タイトルの文字列
                    font.pixelSize: 18                   // 文字サイズ
                    font.bold: true                      // 太字で表示する
                }

                // ドロワーを閉じるボタン
                ToolButton {
                    text: "✕"                          // 閉じる記号
                    font.pixelSize: 16                 // 記号のサイズ
                    ToolTip.visible: hovered           // ホバー中のみツールチップを表示する
                    ToolTip.text: qsTr("Close (Esc)")  // ツールチップの文字列

                    // ドロワーを閉じるよう要求する
                    onClicked: {
                        root.closeRequested()
                    }
                }
            }

            // [Repositories]タブと[Services]タブの切り替え
            TabBar {
                id: tabBar  // タブバーの参照名

                Layout.fillWidth: true  // 親の幅いっぱいに広げる

                // [Repositories]タブ
                TabButton {
                    text: qsTr("Repositories")  // タブの文字列
                }

                // [Services]タブ
                TabButton {
                    text: qsTr("Services")  // タブの文字列
                }
            }
        }

        // --- コンテンツ ---
        StackLayout {
            Layout.fillWidth: true             // 親の幅いっぱいに広げる
            Layout.fillHeight: true            // 残りの高さをすべて使う
            Layout.margins: 12                 // 周囲の余白
            // タブ選択に追従して表示内容を切り替える
            currentIndex: tabBar.currentIndex  // 選択中のタブに対応する内容を表示する

            // --- リポジトリタブ ---
            ColumnLayout {
                spacing: 0  // 項目の間隔は空けない

                // リポジトリ一覧の表
                Components.RepoTable {
                    id: repoTable  // リポジトリ一覧の参照名

                    Layout.fillWidth: true          // 親の幅いっぱいに広げる
                    Layout.fillHeight: true         // 残りの高さをすべて使う
                    model: PackageController.repos  // PackageControllerのリポジトリ一覧

                    // [Enabled]チェックの切り替えをバックエンドへ反映する
                    onEnabledToggled: function(alias, enabled) {
                        PackageController.setRepoEnabled(alias, enabled)
                    }

                    // ダブルクリック時は何もしない
                    onRepoDoubleClicked: function(repo) {
                        // プロパティパネルで編集
                    }
                }

                // ツールバー
                RowLayout {
                    Layout.fillWidth: true  // 親の幅いっぱいに広げる
                    Layout.topMargin: 8     // 表との間隔
                    spacing: 8              // ボタンの間隔

                    // [Add]ボタン
                    Button {
                        text: qsTr("Add")      // ボタンの文字列
                        icon.name: "list-add"  // 追加アイコン

                        // リポジトリ追加ダイアログを開く
                        onClicked: {
                            addRepoDialog.open()
                        }
                    }

                    // [Import .repo File...]ボタン
                    Button {
                        // 処理中は取り込みを開始できない
                        enabled: !PackageController.busy                                // 処理中でない場合のみ押せる
                        text: qsTr("Import .repo File...")                              // ボタンの文字列
                        icon.name: "document-import"                                    // 取り込みアイコン
                        Accessible.name: qsTr("Import repositories from a .repo file")  // スクリーンリーダー向けの名前

                        // .repoファイルの選択ダイアログを開く
                        onClicked: {
                            repoFileDialog.open()
                        }
                    }

                    // [Remove]ボタン
                    Button {
                        // 選択中のリポジトリがあるときだけ削除できる
                        enabled: repoTable.selectedRepo !== null  // リポジトリ選択中のみ押せる
                        text: qsTr("Remove")                      // ボタンの文字列
                        icon.name: "list-remove"                  // 削除アイコン

                        // 削除の確認ダイアログを開く
                        onClicked: {
                            confirmDeleteDialog.open()
                        }
                    }

                    // [Refresh]ボタンを右寄せにするためのスペーサー
                    Item {
                        Layout.fillWidth: true  // 残りの幅をすべて使う
                    }

                    // [Refresh]ボタン (押すと更新対象を選ぶメニューを開く)
                    Button {
                        // 処理中は更新を開始できない
                        enabled: !PackageController.busy  // 処理中でない場合のみ押せる
                        text: qsTr("Refresh")             // ボタンの文字列
                        icon.name: "view-refresh"         // 更新アイコン

                        // 更新対象を選ぶメニューを開く
                        onClicked: {
                            refreshMenu.open()
                        }

                        // 更新対象の選択メニュー
                        Menu {
                            id: refreshMenu  // 更新メニューの参照名

                            y: parent.height  // ボタンの直下に表示する

                            // [Selected Repository]項目
                            Action {
                                // 選択中のリポジトリがあるときだけ単体更新できる
                                enabled: repoTable.selectedRepo !== null  // リポジトリ選択中のみ選べる
                                text: qsTr("Selected Repository")         // 項目の文字列

                                // 選択中のリポジトリだけの更新を要求する
                                onTriggered: {
                                    root.refreshRepoRequested(repoTable.selectedRepo.alias)
                                }
                            }

                            // [All Enabled Repositories]項目
                            Action {
                                text: qsTr("All Enabled Repositories")  // 項目の文字列

                                // 有効な全リポジトリの更新を要求する
                                onTriggered: {
                                    root.refreshAllRequested()
                                }
                            }
                        }
                    }
                }

                // プロパティパネル
                Components.RepoPropertiesPanel {
                    id: propsPanel  // プロパティパネルの参照名

                    Layout.fillWidth: true        // 親の幅いっぱいに広げる
                    Layout.preferredHeight: 200   // パネルの高さ
                    repo: repoTable.selectedRepo  // 編集対象のリポジトリ (選択中の行)

                    // 変更内容をバックエンドへ反映し、一覧を再読み込みしても選択行を維持する
                    onApplyRequested: function(changes) {
                        PackageController.modifyRepo(changes.alias, changes)
                        var idx = repoTable.selectedIndex
                        PackageController.loadRepos()
                        repoTable.selectedIndex = idx
                    }
                }
            }

            // --- サービスタブ ---
            ColumnLayout {
                spacing: 8  // 項目の間隔

                // サービス一覧 (列幅をドラッグで変更できる表)
                ListView {
                    id: serviceListView  // サービス一覧の参照名

                    property real colEnabled: 50  // Enabled列の幅
                    property real colName: 150    // Name列の幅
                    property real colType: 80     // Type列の幅
                    // URL列は残り幅を使用

                    Layout.fillWidth: true             // 親の幅いっぱいに広げる
                    Layout.fillHeight: true            // 残りの高さをすべて使う
                    clip: true                         // 領域外にはみ出した部分を描画しない
                    model: PackageController.services  // PackageControllerのサービス一覧
                    currentIndex: -1                   // 初期状態は未選択
                    ScrollBar.vertical: ScrollBar {}   // 縦スクロールバー

                    // 列幅を永続化する
                    Settings {
                        property alias colEnabled: serviceListView.colEnabled  // Enabled列幅を永続化
                        property alias colName: serviceListView.colName        // Name列幅を永続化
                        property alias colType: serviceListView.colType        // Type列幅を永続化

                        category: "serviceTable"  // 設定ファイル内のグループ名
                    }

                    // 列見出しの行
                    header: Rectangle {
                        width: serviceListView.width  // 一覧の幅に合わせる
                        height: 32                    // 見出し行の高さ
                        color: palette.button         // ボタンと同じ背景色

                        // 列見出しを横に並べる
                        Row {
                            anchors.fill: parent  // 見出し行全体を使う
                            spacing: 0            // 列の間隔は空けない

                            // [Enabled]列
                            Item {
                                width: serviceListView.colEnabled  // [Enabled]列の幅
                                height: parent.height              // 見出し行の高さに合わせる
                                clip: true                         // 列幅より長い見出しをはみ出させない

                                // [Enabled]列の見出し文字列
                                Label {
                                    anchors.centerIn: parent                          // 列の中央に表示する
                                    width: Math.min(implicitWidth, parent.width - 8)  // 列幅に収まる幅
                                    text: qsTr("Enabled")                             // 見出しの文字列
                                    font.bold: true                                   // 太字で表示する
                                    font.pixelSize: 13                                // 文字サイズ
                                    horizontalAlignment: Text.AlignHCenter            // 中央に表示する
                                    elide: Text.ElideRight                            // 収まらない場合は末尾を省略する
                                }

                                // 列幅をドラッグで変更するハンドル (ホバーで強調表示)
                                Rectangle {
                                    anchors.right: parent.right  // 列の右端に配置する
                                    width: 4                     // ハンドルの幅
                                    height: parent.height        // 見出し行の高さに合わせる
                                    // ホバー中・ドラッグ中のみ強調色
                                    color: svcEnResize.hovered || svcEnResize.pressed ? palette.highlight : "transparent"

                                    // ドラッグ操作を受け付ける領域
                                    MouseArea {
                                        id: svcEnResize  // リサイズハンドルの参照名

                                        property bool hovered: false   // リサイズハンドルのホバー状態
                                        property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                        property real startW: 0        // ドラッグ開始時の列幅

                                        anchors.fill: parent          // ハンドル全体で操作を受け付ける
                                        hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                        cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                        // マウスが入ったらホバー状態にする
                                        onEntered: {
                                            hovered = true
                                        }

                                        // マウスが出たらホバー状態を解除する
                                        onExited: {
                                            hovered = false
                                        }

                                        // ドラッグ開始時のグローバル座標と列幅を記録する
                                        onPressed: function(mouse) {
                                            var p = mapToGlobal(mouse.x, 0)
                                            globalStartX = p.x
                                            startW = serviceListView.colEnabled
                                        }

                                        // ドラッグ中は移動量に応じて列幅を変更する (最小幅あり)
                                        onPositionChanged: function(mouse) {
                                            // ボタンを押している間のみ処理する
                                            if (pressed) {
                                                var p = mapToGlobal(mouse.x, 0)
                                                serviceListView.colEnabled = Math.max(40, startW + p.x - globalStartX)
                                            }
                                        }
                                    }
                                }
                            }

                            // 列の区切り線
                            Rectangle {
                                width: 1               // 線の太さ
                                height: parent.height  // 見出し行の高さに合わせる
                                color: palette.mid     // 線の色
                            }

                            // [Name]列
                            Item {
                                width: serviceListView.colName  // [Name]列の幅
                                height: parent.height           // 見出し行の高さに合わせる
                                clip: true                      // 列幅より長い見出しをはみ出させない

                                // [Name]列の見出し文字列
                                Label {
                                    anchors.centerIn: parent                          // 列の中央に表示する
                                    width: Math.min(implicitWidth, parent.width - 8)  // 列幅に収まる幅
                                    text: qsTr("Name")                                // 見出しの文字列
                                    font.bold: true                                   // 太字で表示する
                                    font.pixelSize: 13                                // 文字サイズ
                                    horizontalAlignment: Text.AlignHCenter            // 中央に表示する
                                    elide: Text.ElideRight                            // 収まらない場合は末尾を省略する
                                }

                                // 列幅をドラッグで変更するハンドル (ホバーで強調表示)
                                Rectangle {
                                    anchors.right: parent.right  // 列の右端に配置する
                                    width: 4                     // ハンドルの幅
                                    height: parent.height        // 見出し行の高さに合わせる
                                    // ホバー中・ドラッグ中のみ強調色
                                    color: svcNameResize.hovered || svcNameResize.pressed ? palette.highlight : "transparent"

                                    // ドラッグ操作を受け付ける領域
                                    MouseArea {
                                        id: svcNameResize  // リサイズハンドルの参照名

                                        property bool hovered: false   // リサイズハンドルのホバー状態
                                        property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                        property real startW: 0        // ドラッグ開始時の列幅

                                        anchors.fill: parent          // ハンドル全体で操作を受け付ける
                                        hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                        cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                        // マウスが入ったらホバー状態にする
                                        onEntered: {
                                            hovered = true
                                        }

                                        // マウスが出たらホバー状態を解除する
                                        onExited: {
                                            hovered = false
                                        }

                                        // ドラッグ開始時のグローバル座標と列幅を記録する
                                        onPressed: function(mouse) {
                                            var p = mapToGlobal(mouse.x, 0)
                                            globalStartX = p.x
                                            startW = serviceListView.colName
                                        }

                                        // ドラッグ中は移動量に応じて列幅を変更する (最小幅あり)
                                        onPositionChanged: function(mouse) {
                                            // ボタンを押している間のみ処理する
                                            if (pressed) {
                                                var p = mapToGlobal(mouse.x, 0)
                                                serviceListView.colName = Math.max(80, startW + p.x - globalStartX)
                                            }
                                        }
                                    }
                                }
                            }

                            // 列の区切り線
                            Rectangle {
                                width: 1               // 線の太さ
                                height: parent.height  // 見出し行の高さに合わせる
                                color: palette.mid     // 線の色
                            }

                            // [URL]列
                            Item {
                                // 他の列を除いた残りの幅 (最小150px)
                                width: Math.max(150, serviceListView.width - serviceListView.colEnabled - serviceListView.colName - serviceListView.colType - 3)
                                height: parent.height  // 見出し行の高さに合わせる
                                clip: true             // 列幅より長い見出しをはみ出させない

                                // [URL]列の見出し文字列
                                Label {
                                    anchors.centerIn: parent                          // 列の中央に表示する
                                    width: Math.min(implicitWidth, parent.width - 8)  // 列幅に収まる幅
                                    text: qsTr("URL")                                 // 見出しの文字列
                                    font.bold: true                                   // 太字で表示する
                                    font.pixelSize: 13                                // 文字サイズ
                                    horizontalAlignment: Text.AlignHCenter            // 中央に表示する
                                    elide: Text.ElideRight                            // 収まらない場合は末尾を省略する
                                }

                                // 列幅をドラッグで変更するハンドル (ホバーで強調表示、[Type]列の幅を逆向きに変更する)
                                Rectangle {
                                    anchors.right: parent.right  // 列の右端に配置する
                                    width: 4                     // ハンドルの幅
                                    height: parent.height        // 見出し行の高さに合わせる
                                    // ホバー中・ドラッグ中のみ強調色
                                    color: svcUrlResize.hovered || svcUrlResize.pressed ? palette.highlight : "transparent"

                                    // ドラッグ操作を受け付ける領域
                                    MouseArea {
                                        id: svcUrlResize  // リサイズハンドルの参照名

                                        property bool hovered: false   // リサイズハンドルのホバー状態
                                        property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                        property real startW: 0        // ドラッグ開始時の列幅

                                        anchors.fill: parent          // ハンドル全体で操作を受け付ける
                                        hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                        cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                        // マウスが入ったらホバー状態にする
                                        onEntered: {
                                            hovered = true
                                        }

                                        // マウスが出たらホバー状態を解除する
                                        onExited: {
                                            hovered = false
                                        }

                                        // ドラッグ開始時のグローバル座標と列幅を記録する
                                        onPressed: function(mouse) {
                                            var p = mapToGlobal(mouse.x, 0)
                                            globalStartX = p.x
                                            startW = serviceListView.colType
                                        }

                                        // ドラッグ中は移動量に応じて列幅を変更する (最小幅あり)
                                        onPositionChanged: function(mouse) {
                                            // ボタンを押している間のみ処理する
                                            if (pressed) {
                                                var p = mapToGlobal(mouse.x, 0)
                                                serviceListView.colType = Math.max(50, startW - (p.x - globalStartX))
                                            }
                                        }
                                    }
                                }
                            }

                            // 列の区切り線
                            Rectangle {
                                width: 1               // 線の太さ
                                height: parent.height  // 見出し行の高さに合わせる
                                color: palette.mid     // 線の色
                            }

                            // [Type]列
                            Item {
                                width: serviceListView.colType  // [Type]列の幅
                                height: parent.height           // 見出し行の高さに合わせる
                                clip: true                      // 列幅より長い見出しをはみ出させない

                                // [Type]列の見出し文字列
                                Label {
                                    anchors.centerIn: parent                          // 列の中央に表示する
                                    width: Math.min(implicitWidth, parent.width - 8)  // 列幅に収まる幅
                                    text: qsTr("Type")                                // 見出しの文字列
                                    font.bold: true                                   // 太字で表示する
                                    font.pixelSize: 13                                // 文字サイズ
                                    horizontalAlignment: Text.AlignHCenter            // 中央に表示する
                                    elide: Text.ElideRight                            // 収まらない場合は末尾を省略する
                                }

                                // 列幅をドラッグで変更するハンドル (ホバーで強調表示)
                                Rectangle {
                                    anchors.right: parent.right  // 列の右端に配置する
                                    width: 4                     // ハンドルの幅
                                    height: parent.height        // 見出し行の高さに合わせる
                                    // ホバー中・ドラッグ中のみ強調色
                                    color: svcTypeResize.hovered || svcTypeResize.pressed ? palette.highlight : "transparent"

                                    // ドラッグ操作を受け付ける領域
                                    MouseArea {
                                        id: svcTypeResize  // リサイズハンドルの参照名

                                        property bool hovered: false   // リサイズハンドルのホバー状態
                                        property real globalStartX: 0  // ドラッグ開始時のグローバルX座標
                                        property real startW: 0        // ドラッグ開始時の列幅

                                        anchors.fill: parent          // ハンドル全体で操作を受け付ける
                                        hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
                                        cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

                                        // マウスが入ったらホバー状態にする
                                        onEntered: {
                                            hovered = true
                                        }

                                        // マウスが出たらホバー状態を解除する
                                        onExited: {
                                            hovered = false
                                        }

                                        // ドラッグ開始時のグローバル座標と列幅を記録する
                                        onPressed: function(mouse) {
                                            var p = mapToGlobal(mouse.x, 0)
                                            globalStartX = p.x
                                            startW = serviceListView.colType
                                        }

                                        // ドラッグ中は移動量に応じて列幅を変更する (最小幅あり)
                                        onPositionChanged: function(mouse) {
                                            // ボタンを押している間のみ処理する
                                            if (pressed) {
                                                var p = mapToGlobal(mouse.x, 0)
                                                serviceListView.colType = Math.max(50, startW + p.x - globalStartX)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // サービス1件分の行
                    delegate: ItemDelegate {
                        id: svcDel  // 行自身の参照名

                        required property int index      // 行番号 (縞模様と選択に使用)
                        required property var modelData  // サービス1件分のデータ

                        width: serviceListView.width         // 一覧の幅に合わせる
                        height: 30                           // 行の高さ
                        padding: 0                           // 内側の余白は空けない
                        highlighted: ListView.isCurrentItem  // 選択行を強調表示する

                        // クリックした行を選択する
                        onClicked: {
                            serviceListView.currentIndex = svcDel.index
                        }

                        // 選択行は強調し、奇数行は淡色の縞模様で区別しやすくする
                        background: Rectangle {
                            // 選択行は強調色、偶数行は透明、奇数行は淡いグレー
                            color: svcDel.highlighted ? palette.highlight
                                 : svcDel.index % 2 === 0 ? "transparent"
                                 : Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.15)
                        }

                        // 各列のセルを横に並べる
                        contentItem: Row {
                            spacing: 0  // 列の間隔は空けない

                            // [Enabled]列のセル
                            Item {
                                width: serviceListView.colEnabled  // [Enabled]列の幅
                                height: parent.height              // 行の高さに合わせる

                                // サービスの有効/無効を切り替えるチェックボックス
                                CheckBox {
                                    anchors.centerIn: parent  // セルの中央に表示する

                                    // プラグインサービスはインストール済みパッケージが提供するため変更しない
                                    enabled: svcDel.modelData.type !== "plugin"
                                    checked: svcDel.modelData.enabled || false  // サービスが有効かどうか

                                    // 変更できない理由をツールチップで示す
                                    ToolTip.visible: hovered && !enabled
                                    ToolTip.text: qsTr("Plugin services are provided by installed packages")

                                    // 有効/無効の切り替えをPackageController::modifyServiceでバックエンドへ反映する
                                    onToggled: {
                                        PackageController.modifyService(svcDel.modelData.alias, {
                                            "alias": svcDel.modelData.alias,
                                            "name": svcDel.modelData.name,
                                            "url": svcDel.modelData.url,
                                            "enabled": checked,
                                            "autoRefresh": svcDel.modelData.autoRefresh
                                        })
                                    }
                                }
                            }

                            // 列の区切り線
                            Rectangle {
                                width: 1               // 線の太さ
                                height: parent.height  // 行の高さに合わせる
                                opacity: 0.3           // 控えめに表示する
                                color: palette.mid     // 線の色
                            }

                            // [Name]列のセル (サービス名、無ければ別名)
                            Label {
                                width: serviceListView.colName                                      // [Name]列の幅
                                height: parent.height                                               // 行の高さに合わせる
                                leftPadding: 6                                                      // 左側の余白
                                opacity: svcDel.modelData.enabled ? 1.0 : 0.5                       // 無効なサービスは薄く表示する
                                text: svcDel.modelData.name || svcDel.modelData.alias || ""         // サービス名
                                textFormat: Text.PlainText                                          // 外部由来の文字列をHTMLとして解釈させない
                                color: svcDel.highlighted ? palette.highlightedText : palette.text  // 選択行では強調時の文字色
                                font.pixelSize: 14                                                  // 文字サイズ
                                verticalAlignment: Text.AlignVCenter                                // 上下中央に表示する
                                elide: Text.ElideRight                                              // 収まらない場合は末尾を省略する
                            }

                            // 列の区切り線
                            Rectangle {
                                width: 1               // 線の太さ
                                height: parent.height  // 行の高さに合わせる
                                opacity: 0.3           // 控えめに表示する
                                color: palette.mid     // 線の色
                            }

                            // [URL]列のセル
                            Label {
                                // 他の列を除いた残りの幅 (最小150px)
                                width: Math.max(150, serviceListView.width - serviceListView.colEnabled - serviceListView.colName - serviceListView.colType - 3)
                                height: parent.height                                               // 行の高さに合わせる
                                leftPadding: 6                                                      // 左側の余白
                                opacity: svcDel.modelData.enabled ? 0.9 : 0.4                       // 無効なサービスは薄く表示する
                                text: svcDel.modelData.url || ""                                    // サービスのURL
                                textFormat: Text.PlainText                                          // 外部由来の文字列をHTMLとして解釈させない
                                color: svcDel.highlighted ? palette.highlightedText : palette.link  // 選択行では強調時の文字色
                                font.pixelSize: 14                                                  // 文字サイズ
                                verticalAlignment: Text.AlignVCenter                                // 上下中央に表示する
                                elide: Text.ElideMiddle                                             // 収まらない場合は中央を省略する
                            }

                            // 列の区切り線
                            Rectangle {
                                width: 1               // 線の太さ
                                height: parent.height  // 行の高さに合わせる
                                opacity: 0.3           // 控えめに表示する
                                color: palette.mid     // 線の色
                            }

                            // [Type]列のセル
                            Label {
                                width: serviceListView.colType          // [Type]列の幅
                                height: parent.height                   // 行の高さに合わせる
                                text: svcDel.modelData.type || ""       // サービスの種類
                                textFormat: Text.PlainText              // 外部由来の文字列をHTMLとして解釈させない
                                // 選択行では強調時の文字色
                                color: svcDel.highlighted ? palette.highlightedText : palette.placeholderText
                                font.pixelSize: 14                      // 文字サイズ
                                horizontalAlignment: Text.AlignHCenter  // 中央に表示する
                                verticalAlignment: Text.AlignVCenter    // 上下中央に表示する
                            }
                        }
                    }
                }

                // サービス操作のボタン行
                RowLayout {
                    Layout.fillWidth: true  // 親の幅いっぱいに広げる
                    spacing: 8              // ボタンの間隔

                    // [Add]ボタン
                    Button {
                        text: qsTr("Add")      // ボタンの文字列
                        icon.name: "list-add"  // 追加アイコン

                        // サービス追加ダイアログを開く
                        onClicked: {
                            addServiceDialog.open()
                        }
                    }

                    // [Remove]ボタン
                    Button {
                        id: removeServiceButton  // サービス削除ボタンの参照名

                        // 選択中のサービスがプラグインサービスの場合は削除できない
                        readonly property bool pluginSelected: {
                            if (serviceListView.currentIndex < 0
                                    || serviceListView.currentIndex >= PackageController.services.length) {
                                return false
                            }

                            // PackageController::servicesから選択中のサービスを取り出す
                            var svc = PackageController.services[serviceListView.currentIndex]
                            return !!svc && svc.type === "plugin"
                        }

                        // プラグインサービス以外を選択しているときだけ削除できる
                        enabled: serviceListView.currentIndex >= 0 && !pluginSelected
                        text: qsTr("Remove")      // ボタンの文字列
                        icon.name: "list-remove"  // 削除アイコン

                        // 削除できない理由をツールチップで示す
                        ToolTip.visible: hovered && pluginSelected
                        ToolTip.text: qsTr("Plugin services are provided by installed packages")

                        // 削除の確認ダイアログを開く
                        onClicked: {
                            confirmDeleteServiceDialog.open()
                        }
                    }

                    // [Refresh]ボタン
                    Button {
                        // サービス選択中かつ処理中でないときだけ更新できる
                        enabled: serviceListView.currentIndex >= 0
                                 && serviceListView.currentIndex < PackageController.services.length
                                 && !PackageController.busy
                        text: qsTr("Refresh")      // ボタンの文字列
                        icon.name: "view-refresh"  // 更新アイコン

                        // 選択中のサービスの更新を要求する
                        onClicked: {
                            // 選択位置が一覧の範囲外の場合は何もしない
                            if (serviceListView.currentIndex < 0
                                    || serviceListView.currentIndex >= PackageController.services.length) {
                                return
                            }

                            var svc = PackageController.services[serviceListView.currentIndex]

                            // サービス情報が取得できない場合は何もしない
                            if (!svc) {
                                return
                            }

                            root.refreshServiceRequested(svc.alias)
                        }
                    }
                }
            }
        }

        // --- フッタボタン ---
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            Layout.margins: 12      // 周囲の余白
            Layout.topMargin: 0     // 上側の余白は空けない

            // [Apply Changes]ボタン
            Button {
                // リポジトリタブかつ未適用の変更があるときだけ適用できる
                enabled: repoTabActive && propsPanel.hasChanges  // 未適用の変更がある場合のみ押せる
                text: qsTr("Apply Changes")                      // ボタンの文字列

                // プロパティパネルの変更内容の適用を要求する
                onClicked: {
                    propsPanel.applyRequested(propsPanel.getChanges())
                }
            }

            // [OK]・[Close]ボタンを右寄せにするためのスペーサー
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [OK]ボタン
            Button {
                text: qsTr("OK")   // ボタンの文字列
                highlighted: true  // 主操作として強調表示する

                // リポジトリ一覧を再読み込みしてからドロワーを閉じる
                onClicked: {
                    PackageController.loadRepos()
                    root.closeRequested()
                }
            }

            // [Close]ボタン
            Button {
                text: qsTr("Close")  // ボタンの文字列

                // ドロワーを閉じるよう要求する
                onClicked: {
                    root.closeRequested()
                }
            }
        }
    }

    // ダイアログ類
    // 取り込む.repoファイルの選択ダイアログ
    FileDialog {
        id: repoFileDialog  // ファイル選択ダイアログの参照名

        title: qsTr("Select a .repo File")                                       // ダイアログのタイトル
        fileMode: FileDialog.OpenFile                                            // 既存のファイルを1つ選ぶ
        nameFilters: [qsTr("Repository files (*.repo)"), qsTr("All files (*)")]  // ファイルの種類の絞り込み

        // 選択したファイルの内容をバックエンドでプレビューする
        onAccepted: {
            PackageController.previewRepoFile(selectedFile)
        }
    }

    // .repoファイルから取り込むリポジトリの確認ダイアログ
    Dialog {
        id: repoImportDialog  // 取り込み確認ダイアログの参照名

        property var repos: []         // プレビューしたリポジトリ一覧
        property string content: ""    // 取り込み時に送る .repo ファイルの内容
        property string errorText: ""  // プレビュー失敗時のエラー文言
        property bool awaiting: false  // 取り込み応答待ちか

        anchors.centerIn: parent                                  // 親の中央に表示する
        width: Math.min(640, (parent ? parent.width : 640) - 40)  // 親の幅から40px引いた幅 (最大640px)
        title: qsTr("Import Repositories")                        // ダイアログのタイトル
        modal: true                                               // 表示中は背後の画面を操作できないようにする
        // 待機中は外部クリックや[Esc]キーで閉じないようにする
        closePolicy: awaiting ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

        // 説明文またはエラー、リポジトリ一覧、待機表示を縦に並べる
        ColumnLayout {
            anchors.fill: parent  // ダイアログ内側全体を使う
            spacing: 8            // 項目の間隔

            // 取り込みの説明文
            Label {
                Layout.fillWidth: true                      // 親の幅いっぱいに広げる
                // プレビュー成功時のみ説明を表示する
                visible: repoImportDialog.errorText === ""  // エラーが無い場合のみ表示する
                // GPGチェック無効や既存の別名はスキップされる旨の説明文
                text: qsTr("The following repositories will be added. Repositories with GPG check disabled or already existing aliases are skipped.")
                wrapMode: Text.WordWrap                     // 幅に収まらない場合は単語単位で折り返す
            }

            // プレビュー失敗時のエラー文言
            Label {
                Layout.fillWidth: true                      // 親の幅いっぱいに広げる
                // プレビュー失敗時のみエラーを表示する
                visible: repoImportDialog.errorText !== ""  // エラーがある場合のみ表示する
                text: repoImportDialog.errorText            // エラー文言
                textFormat: Text.PlainText                  // 外部由来の文字列をHTMLとして解釈させない
                color: "#F44336"                            // エラー色 (赤)
                wrapMode: Text.WordWrap                     // 幅に収まらない場合は単語単位で折り返す
            }

            // ファイル内のリポジトリ一覧
            ListView {
                Layout.fillWidth: true                                // 親の幅いっぱいに広げる
                Layout.preferredHeight: Math.min(260, contentHeight)  // 内容に合わせた高さ (最大260px)
                clip: true                                            // 領域外にはみ出した部分を描画しない
                model: repoImportDialog.repos                         // プレビューしたリポジトリ一覧
                Accessible.role: Accessible.List                      // スクリーンリーダー向けの役割 (一覧)
                Accessible.name: qsTr("Repositories in file")         // スクリーンリーダー向けの名前

                // リポジトリ1件分の行
                delegate: ItemDelegate {
                    required property var modelData  // プレビュー中のリポジトリ1件分

                    width: ListView.view.width                                              // 一覧の幅に合わせる
                    Accessible.name: (modelData.alias || "") + " " + (modelData.url || "")  // スクリーンリーダー向けの名前 (別名とURL)

                    // 名前、URL、GPG警告を縦に並べる
                    contentItem: ColumnLayout {
                        spacing: 2  // 項目の間隔

                        // リポジトリ名と別名
                        Label {
                            Layout.fillWidth: true      // 親の幅いっぱいに広げる
                            // "名前 (別名)"形式の文字列 (名前が無ければ別名を使う)
                            text: (modelData.name || modelData.alias || "") + "  (" + (modelData.alias || "") + ")"
                            textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                            font.bold: true             // 太字で表示する
                            elide: Text.ElideRight      // 収まらない場合は末尾を省略する
                        }

                        // リポジトリのURL
                        Label {
                            Layout.fillWidth: true      // 親の幅いっぱいに広げる
                            text: modelData.url || ""   // リポジトリのURL
                            textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                            font.pixelSize: 11          // 文字サイズ
                            elide: Text.ElideMiddle     // 収まらない場合は中央を省略する
                        }

                        // GPGチェック無効の警告
                        Label {
                            // GPGチェック無効のエントリは取り込み対象外だと知らせる
                            visible: modelData.gpgCheck === false               // GPGチェックが無効の場合のみ表示する
                            text: qsTr("GPG check disabled — will be skipped")  // 警告文
                            color: "#F44336"                                    // 警告色 (赤)
                            font.pixelSize: 11                                  // 文字サイズ
                        }
                    }
                }
            }

            // 取り込み応答待ちのスピナー
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter   // 中央に配置する
                visible: running                    // 動作中のみ表示する
                // バックエンドからの応答待ちの間スピナーを表示する
                running: repoImportDialog.awaiting  // 応答待ちの間だけ回転させる
            }
        }

        // [Import]・[Cancel]ボタンの行
        footer: DialogButtonBox {
            // 待機状態にしてからバックエンドへ取り込みを要求する
            onAccepted: {
                repoImportDialog.awaiting = true
                PackageController.importRepoFile(repoImportDialog.content)
            }

            // 取り込みを行わずにダイアログを閉じる
            onRejected: {
                repoImportDialog.close()
            }

            // [Import]ボタン
            Button {
                // プレビュー成功かつ対象があり、待機中でないときだけ取り込める
                enabled: repoImportDialog.errorText === "" && repoImportDialog.repos.length > 0
                         && !repoImportDialog.awaiting
                text: qsTr("Import")                                     // ボタンの文字列
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole   // 承諾の役割 (onAcceptedを発生させる)
                Accessible.name: qsTr("Import the listed repositories")  // スクリーンリーダー向けの名前
            }

            // [Cancel]ボタン
            Button {
                // 応答待ちの間は閉じられないため無効化する
                enabled: !repoImportDialog.awaiting                     // 応答待ちでない場合のみ押せる
                text: qsTr("Cancel")                                    // ボタンの文字列
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole  // 拒否の役割 (onRejectedを発生させる)
            }
        }
    }

    // .repoファイル取り込み結果の通知ダイアログ
    Dialog {
        id: repoImportResultDialog  // 取り込み結果ダイアログの参照名

        property string message: ""  // 取り込み結果のメッセージ

        anchors.centerIn: parent                                  // 親の中央に表示する
        width: Math.min(560, (parent ? parent.width : 560) - 40)  // 親の幅から40px引いた幅 (最大560px)
        title: qsTr("Import Result")                              // ダイアログのタイトル
        standardButtons: Dialog.Ok                                // [OK]ボタンのみ表示する
        modal: true                                               // 表示中は背後の画面を操作できないようにする

        // 取り込み結果のメッセージ
        Label {
            anchors.fill: parent                  // ダイアログ内側全体を使う
            text: repoImportResultDialog.message  // 取り込み結果のメッセージ
            textFormat: Text.PlainText            // 外部由来の文字列をHTMLとして解釈させない
            wrapMode: Text.WordWrap               // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // リポジトリ追加ダイアログ
    AddRepoDialog {
        id: addRepoDialog  // リポジトリ追加ダイアログの参照名

        // 追加後はリポジトリ一覧を再読み込みする
        onRepoAdded: {
            PackageController.loadRepos()
        }
    }

    // サービス追加ダイアログ
    AddServiceDialog {
        id: addServiceDialog  // サービス追加ダイアログの参照名

        // 追加後はサービスとリポジトリの一覧を再読み込みする
        onServiceAdded: {
            PackageController.loadServices()
            PackageController.loadRepos()
        }
    }

    // リポジトリ削除は取り消せないため確認ダイアログを挟む
    Dialog {
        id: confirmDeleteDialog  // リポジトリ削除確認ダイアログの参照名

        anchors.centerIn: parent                 // 親の中央に表示する
        width: 400                               // ダイアログの幅
        title: qsTr("Remove Repository")         // ダイアログのタイトル
        standardButtons: Dialog.Yes | Dialog.No  // [Yes]・[No]ボタンを表示する
        modal: true                              // 表示中は背後の画面を操作できないようにする

        // はい選択時のみ削除を実行し、選択状態を解除する
        onAccepted: {
            // リポジトリを選択している場合のみ削除する
            if (repoTable.selectedRepo) {
                PackageController.removeRepo(repoTable.selectedRepo.alias)
                repoTable.selectedIndex = -1
            }
        }

        // 削除の確認文
        Label {
            // 選択中のリポジトリ名 (無ければ別名) を埋め込んだ確認文
            text: repoTable.selectedRepo
                ? qsTr("Remove repository \"%1\"?").arg(repoTable.selectedRepo.name || repoTable.selectedRepo.alias)
                : ""
            textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
            wrapMode: Text.WordWrap     // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // 依存リポジトリもまとめて消えるため確認ダイアログを挟む
    Dialog {
        id: confirmDeleteServiceDialog  // サービス削除確認ダイアログの参照名

        anchors.centerIn: parent                 // 親の中央に表示する
        width: 400                               // ダイアログの幅
        title: qsTr("Remove Service")            // ダイアログのタイトル
        standardButtons: Dialog.Yes | Dialog.No  // [Yes]・[No]ボタンを表示する
        modal: true                              // 表示中は背後の画面を操作できないようにする

        // はい選択時のみ削除を実行し、選択状態を解除する
        onAccepted: {
            // 選択位置が一覧の範囲外の場合は何もしない
            if (serviceListView.currentIndex < 0
                    || serviceListView.currentIndex >= PackageController.services.length) {
                return
            }

            var svc = PackageController.services[serviceListView.currentIndex]

            // サービス情報が取得できない場合は何もしない
            if (!svc) {
                return
            }

            PackageController.removeService(svc.alias)
            serviceListView.currentIndex = -1
        }

        // 削除の確認文
        Label {
            // 選択中のサービス名 (無ければ別名) を埋め込んだ確認文
            text: {
                // 選択位置が一覧の範囲外の場合は空文字にする
                if (serviceListView.currentIndex < 0
                        || serviceListView.currentIndex >= PackageController.services.length) {
                    return ""
                }

                var svc = PackageController.services[serviceListView.currentIndex]
                return qsTr("Remove service \"%1\" and its repositories?").arg(svc ? (svc.name || svc.alias) : "")
            }
            textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
            wrapMode: Text.WordWrap     // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // .repoファイルのプレビュー結果と取り込み結果をバックエンドから受け取る
    Connections {
        target: PackageController  // 結果を通知するPackageController

        /**
         * @brief .repoファイルのプレビュー結果を受け取る
         * @param ok ファイル全体の解析に成功したか
         * @param entries 解析したエントリ一覧
         * @param content 取り込み時に送るファイル内容
         * @param error 解析失敗時のエラー文言
         * @note 解析不能な節を除いてreposを組み立て、取り込みダイアログを開く
         */
        function onRepoFilePreviewReady(ok, entries, content, error) {
            // 解析不能な節は {alias, error}で返る
            // 単独の {error}はファイル全体の解析失敗
            var repos = []
            for (var i = 0; i < entries.length; ++i) {
                var e = entries[i]

                // エラーのみでURLを持たないエントリは一覧に含めない
                if (e.error !== undefined && e.url === undefined) {
                    // 別名も無い場合はファイル全体の解析失敗として扱う
                    if (e.alias === undefined) {
                        ok = false
                        error = e.error
                    }
                    continue
                }

                repos.push(e)
            }
            repoImportDialog.repos = repos
            repoImportDialog.content = content
            // 成功時は対象が0件なら案内文を、1件以上なら空文字を、失敗時はエラー文言を設定する
            repoImportDialog.errorText = ok ? (repos.length === 0 ? qsTr("No repositories were found in the file.") : "")
                                            : error
            repoImportDialog.awaiting = false
            repoImportDialog.open()
        }

        /**
         * @brief .repoファイル取り込みの結果を受け取る
         * @param result 取り込み結果 (success, errorMessage, added, skipped)
         * @note 追加・スキップ・エラーの内容をまとめた通知ダイアログを開く
         */
        function onRepoFileImported(result) {
            repoImportDialog.awaiting = false
            repoImportDialog.close()
            var lines = []
            var added = result.added || []
            var skipped = result.skipped || []

            // 追加したリポジトリがある場合は別名を列挙する
            if (added.length > 0) {
                lines.push(qsTr("Added: %1").arg(added.join(", ")))
            }

            // スキップしたリポジトリごとに別名と理由を列挙する
            for (var i = 0; i < skipped.length; ++i) {
                lines.push(qsTr("Skipped %1: %2").arg(skipped[i].alias || "?").arg(skipped[i].reason || ""))
            }

            // 失敗した場合はエラーメッセージを加える
            if (!result.success && result.errorMessage) {
                lines.push(result.errorMessage)
            }

            // 何も報告することが無い場合は、追加が無かった旨を表示する
            if (lines.length === 0) {
                lines.push(qsTr("No repositories were added."))
            }

            repoImportResultDialog.message = lines.join("\n")
            repoImportResultDialog.open()
        }
    }
}
