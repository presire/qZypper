import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui
import "dialogs" as Dialogs

ApplicationWindow {
    id: root
    width: 1200
    height: 800
    minimumWidth: 800
    minimumHeight: 600
    visible: true
    title: qsTr("qZypper - Software Management")

    // -- テーマ切り替え --
    // 初回起動時はシステム設定に従い、以降はユーザ選択を永続化
    property bool darkMode: themeSettings.darkMode

    Settings {
        id: themeSettings
        category: "theme"
        property bool darkMode: systemDarkMode
    }

    // テーマカラーは PackageController.applyDarkMode() で C++ 側から
    // QGuiApplication::setPalette() で適用する。
    // Qt 6.5 では QML の palette グループ化プロパティ構文での動的バインディング更新が
    // 子コントロールに正しく伝播しないため、C++ 側で一括適用する。
    onDarkModeChanged: PackageController.applyDarkMode(darkMode)

    // -- 設定の永続化 --
    Settings {
        category: "window"
        property alias width: root.width
        property alias height: root.height
    }

    Settings {
        id: splitSettings
        category: "splitView"
        property real leftPaneWidth: 280
        property real detailPaneHeight: 250
    }

    Settings {
        category: "packageTable"
        property alias colName: pkgPane.colName
        property alias colSummary: pkgPane.colSummary
        property alias colInstalled: pkgPane.colInstalled
        property alias colAvailable: pkgPane.colAvailable
    }

    // -- グローバルショートカット --
    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: Qt.quit()
    }

    Shortcut {
        sequence: "Ctrl+Return"
        enabled: !PackageController.busy && PackageController.hasPendingChanges
        onActivated: applyChanges()
    }

    // -- ナビゲーションDrawer (ハンバーガーメニュー) --
    Drawer {
        id: navDrawer
        width: 300
        height: root.height
        edge: Qt.LeftEdge

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ヘッダ
            Pane {
                Layout.fillWidth: true
                padding: 16
                background: Rectangle { color: palette.highlight }

                Label {
                    text: "qZypper"
                    font.pixelSize: 18
                    font.bold: true
                    color: palette.highlightedText
                }
            }

            // -- パッケージセクション --
            Label {
                text: qsTr("Packages")
                font.bold: true
                font.pixelSize: 11
                color: palette.placeholderText
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 16
                Layout.bottomMargin: 4
            }

            ItemDelegate {
                text: qsTr("Update All Packages")
                icon.name: "system-software-update"
                icon.color: palette.text
                Layout.fillWidth: true
                enabled: !PackageController.busy
                onClicked: {
                    navDrawer.close()
                    if (!PackageController.checkAuth("org.presire.qzypper.install-packages"))
                        return
                    updateAllPackages()
                }
            }
            ItemDelegate {
                text: qsTr("Apply Changes")
                icon.name: "dialog-ok-apply"
                icon.color: palette.text
                Layout.fillWidth: true
                enabled: !PackageController.busy && PackageController.hasPendingChanges
                onClicked: {
                    navDrawer.close()
                    applyChanges()
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // -- 設定セクション --
            Label {
                text: qsTr("Settings")
                font.bold: true
                font.pixelSize: 11
                color: palette.placeholderText
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.bottomMargin: 4
            }

            ItemDelegate {
                text: qsTr("Manage Repositories")
                icon.name: "repository"
                icon.color: palette.text
                Layout.fillWidth: true
                onClicked: {
                    navDrawer.close()
                    repoDrawer.open()
                    if (repoDrawerLoader.item)
                        repoDrawerLoader.item.loadData()
                }
            }

            ItemDelegate {
                text: qsTr("Refresh Repositories")
                icon.name: "view-refresh"
                icon.color: palette.text
                Layout.fillWidth: true
                enabled: !PackageController.busy
                onClicked: {
                    navDrawer.close()
                    if (!PackageController.checkAuth("org.presire.qzypper.refresh-repos"))
                        return
                    refreshOverlay.open()
                    PackageController.refreshReposAsync()
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // -- ヘルプセクション --
            Label {
                text: qsTr("Help")
                font.bold: true
                font.pixelSize: 11
                color: palette.placeholderText
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.bottomMargin: 4
            }

            ItemDelegate {
                text: qsTr("About qZypper")
                icon.name: "help-about"
                icon.color: palette.text
                Layout.fillWidth: true
                onClicked: {
                    navDrawer.close()
                    aboutqZypperDialog.open()
                }
            }

            ItemDelegate {
                text: qsTr("About Qt")
                icon.name: "qt"
                icon.color: palette.text
                Layout.fillWidth: true
                onClicked: {
                    navDrawer.close()
                    aboutQtDialog.open()
                }
            }

            Item { Layout.fillHeight: true }

            // -- 終了 --
            MenuSeparator { Layout.fillWidth: true }

            ItemDelegate {
                text: qsTr("Quit")
                icon.name: "application-exit"
                icon.color: palette.text
                Layout.fillWidth: true
                onClicked: Qt.quit()
            }
        }
    }

    // -- リポジトリ管理ドロワー (全画面) --
    Drawer {
        id: repoDrawer
        width: root.width
        height: root.height
        edge: Qt.RightEdge
        interactive: false  // スワイプで閉じない (Escまたはボタンで閉じる)

        Loader {
            id: repoDrawerLoader
            anchors.fill: parent
            active: repoDrawer.visible
            sourceComponent: Component {
                Dialogs.RepoManagerDrawerContent {
                    onCloseRequested: repoDrawer.close()
                    onRefreshAllRequested: {
                        refreshOverlay.open()
                        PackageController.refreshReposAsync()
                    }
                }
            }
        }

        onOpened: {
            if (repoDrawerLoader.item)
                repoDrawerLoader.item.loadData()
        }
    }

    // Escキーハンドリング
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (refreshOverlay.visible) {
                PackageController.cancelRefresh()
                refreshOverlay.close()
            } else if (repoDrawer.visible) {
                repoDrawer.close()
            } else if (navDrawer.visible) {
                navDrawer.close()
            }
        }
    }

    // -- リフレッシュ進捗オーバーレイ --
    Popup {
        id: refreshOverlay
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        modal: true
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: palette.window
            opacity: 0.85
        }

        property bool cancelling: false

        // Popup内のShortcutはモーダル中でもウィンドウレベルで動作する
        Shortcut {
            sequence: "Escape"
            enabled: refreshOverlay.visible && !refreshOverlay.cancelling
            onActivated: refreshOverlay.doCancel()
        }

        contentItem: Item {
            id: refreshOverlayRoot

            Column {
                anchors.centerIn: parent
                spacing: 16

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: refreshOverlay.visible
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: PackageController.statusMessage
                    font.pixelSize: 16
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: refreshOverlay.cancelling
                          ? qsTr("Will be cancelled after the current repository finishes")
                          : qsTr("Press Esc or button to cancel")
                    opacity: 0.6
                    font.pixelSize: 13
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Cancel")
                    visible: !refreshOverlay.cancelling
                    onClicked: refreshOverlay.doCancel()
                }
            }
        }

        function doCancel() {
            cancelling = true
            PackageController.cancelRefresh()
        }

        onClosed: cancelling = false

        Connections {
            target: PackageController
            function onRefreshFinished(success) {
                refreshOverlay.close()
            }
        }
    }

    // -- ヘッダツールバー --
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 8

            ToolButton {
                text: "☰"
                font.pixelSize: 20
                onClicked: navDrawer.open()

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Menu")
            }

            Label {
                text: qsTr("View:")
                Layout.alignment: Qt.AlignVCenter
            }

            TabBar {
                id: filterTabBar
                Layout.fillWidth: false

                TabButton { text: qsTr("Search"); width: Math.max(70, implicitWidth) }
                TabButton { text: qsTr("Installation Summary"); width: Math.max(140, implicitWidth) }
                TabButton { text: qsTr("Repository"); width: Math.max(90, implicitWidth) }
                TabButton { text: qsTr("Patterns"); width: Math.max(80, implicitWidth) }

                onCurrentIndexChanged: {
                    if (currentIndex === 3) {
                        PackageController.loadPatterns()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // ダークモード/ライトモード切り替え
            RowLayout {
                spacing: 6
                Layout.alignment: Qt.AlignVCenter

                Label {
                    text: root.darkMode ? qsTr("Dark Mode") : qsTr("Light Mode")
                    Layout.alignment: Qt.AlignVCenter
                }

                Switch {
                    id: themeSwitch
                    checked: root.darkMode
                    onToggled: {
                        themeSettings.darkMode = checked
                        root.darkMode = checked
                    }
                }
            }
        }
    }

    // -- ソート状態 --
    property int sortColumn: -1       // -1=なし, 0=チェックボックス, 1=パッケージ名
    property bool sortAscending: true

    function sortedPackages() {
        var pkgs = PackageController.packages
        if (sortColumn < 0) return pkgs

        var arr = []
        for (var i = 0; i < pkgs.length; i++) arr.push(pkgs[i])

        if (sortColumn === 0) {
            arr.sort(function(a, b) {
                var aChecked = (a.status === 3 || a.status === 1 || a.status === 2 || a.status === 4 || a.status === 5 || a.status === 9) ? 1 : 0
                var bChecked = (b.status === 3 || b.status === 1 || b.status === 2 || b.status === 4 || b.status === 5 || b.status === 9) ? 1 : 0
                return sortAscending ? (aChecked - bChecked) : (bChecked - aChecked)
            })
        }
        else if (sortColumn === 1) {
            arr.sort(function(a, b) {
                var na = (a.name || "").toLowerCase()
                var nb = (b.name || "").toLowerCase()
                var cmp = na < nb ? -1 : (na > nb ? 1 : 0)
                return sortAscending ? cmp : -cmp
            })
        }
        return arr
    }

    function toggleSort(col) {
        if (sortColumn === col) {
            sortAscending = !sortAscending
        }
        else {
            sortColumn = col
            sortAscending = true
        }
        packageListView.model = sortedPackages()
    }

    // パッケージリスト更新時にソートを再適用
    Connections {
        target: PackageController
        function onPackagesChanged() {
            packageListView.model = sortedPackages()
        }
    }

    // -- メインコンテンツ --
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // 左ペイン: フィルタ
        Pane {
            id: leftPane
            SplitView.preferredWidth: splitSettings.leftPaneWidth
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 400
            padding: 0
            onWidthChanged: splitSettings.leftPaneWidth = width

            StackLayout {
                anchors.fill: parent
                currentIndex: filterTabBar.currentIndex

                // 検索フィルタ
                Pane {
                    padding: 12

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        // 検索入力
                        RowLayout {
                            Layout.fillWidth: true

                            TextField {
                                id: searchField
                                Layout.fillWidth: true
                                placeholderText: qsTr("Search packages...")
                                onAccepted: performSearch()
                            }

                            Button {
                                text: qsTr("Search")
                                onClicked: performSearch()
                            }
                        }

                        // 検索対象チェックボックス
                        GroupBox {
                            title: qsTr("Search Criteria")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent

                                component ElidedCheckBox : CheckBox {
                                    Layout.fillWidth: true
                                    contentItem: Label {
                                        text: parent.text
                                        leftPadding: parent.indicator.width + parent.spacing
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                ElidedCheckBox { id: cbName;     text: qsTr("Name (&E)");                    checked: true }
                                ElidedCheckBox { id: cbKeyword;  text: qsTr("Keywords (&K)");                 checked: true }
                                ElidedCheckBox { id: cbSummary;  text: qsTr("Summary (&M)");                  checked: true }
                                ElidedCheckBox { id: cbDesc;     text: qsTr("Description (&I)") }
                                ElidedCheckBox { id: cbProvides; text: qsTr("RPM \"Provides\" (&R)") }
                                ElidedCheckBox { id: cbRequires; text: qsTr("RPM \"Requires\" (&Q)") }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // インストールの概要
                Pane {
                    padding: 0

                    ListView {
                        id: summaryListView
                        anchors.fill: parent
                        clip: true
                        model: buildSummaryModel()

                        section.property: "category"
                        section.delegate: Rectangle {
                            width: summaryListView.width
                            height: 28
                            color: section === "install" ? "#4caf50"
                                 : section === "update"  ? "#2196f3"
                                 : section === "delete"  ? "#f44336" : "#9e9e9e"

                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                font.bold: true
                                color: "white"
                                text: section === "install" ? qsTr("Install")
                                    : section === "update"  ? qsTr("Update")
                                    : section === "delete"  ? qsTr("Delete") : section
                            }
                        }

                        delegate: ItemDelegate {
                            width: summaryListView.width
                            height: 24

                            required property var modelData

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                spacing: 8

                                Label {
                                    text: modelData.name || ""
                                    Layout.preferredWidth: 200
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: modelData.version || ""
                                    Layout.fillWidth: true
                                    color: palette.placeholderText
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("No pending package changes")
                            color: palette.placeholderText
                            visible: summaryListView.count === 0
                        }
                    }
                }

                // リポジトリフィルタ
                Pane {
                    padding: 0

                    ListView {
                        id: repoListView
                        anchors.fill: parent
                        clip: true
                        model: PackageController.repos
                        delegate: ItemDelegate {
                            width: repoListView.width
                            text: modelData.name || modelData.alias || ""
                            highlighted: ListView.isCurrentItem
                            onClicked: {
                                repoListView.currentIndex = index
                                PackageController.loadPackagesByRepo(modelData.alias)
                            }
                        }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                }

                // パターン
                Pane {
                    padding: 0

                    // スクロール位置保存用
                    property real savedContentY: 0
                    property int  savedIndex: -1
                    property bool restorePending: false

                    ListView {
                        id: patternListView
                        anchors.fill: parent
                        model: PackageController.patterns
                        clip: true

                        onModelChanged: {
                            if (parent.restorePending) {
                                parent.restorePending = false
                                patternListView.currentIndex = parent.savedIndex
                                patternListView.contentY = parent.savedContentY
                            }
                        }

                        // パターンのチェック状態をトグル (スクロール位置を保持)
                        function togglePattern(name) {
                            parent.savedContentY = patternListView.contentY
                            parent.savedIndex    = patternListView.currentIndex
                            parent.restorePending = true
                            PackageController.togglePatternStatus(name)
                        }

                        section.property: "category"
                        section.delegate: Rectangle {
                            width: patternListView.width
                            height: 28
                            color: palette.alternateBase

                            Label {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: section
                                font.bold: true
                                font.pixelSize: 13
                            }
                        }

                        delegate: ItemDelegate {
                            id: patternDelegate
                            width: patternListView.width
                            highlighted: ListView.isCurrentItem

                            readonly property int pStatus: modelData.status || 0
                            readonly property bool pChecked:
                                pStatus === 1 || pStatus === 2 || pStatus === 3 ||
                                pStatus === 4 || pStatus === 5 || pStatus === 9
                            readonly property color statusColor: {
                                if (pStatus === 3 || pStatus === 9) return "#4CAF50"  // KeepInstalled/Protected
                                if (pStatus === 1 || pStatus === 2) return "#2196F3"  // Install/AutoInstall
                                if (pStatus === 4 || pStatus === 5) return "#FF9800"  // Update/AutoUpdate
                                if (pStatus === 6 || pStatus === 7) return "#F44336"  // Del/AutoDel
                                return palette.mid
                            }

                            contentItem: RowLayout {
                                spacing: 6

                                // カスタムチェックボックス
                                Rectangle {
                                    width: 18; height: 18
                                    radius: 3
                                    color: patternDelegate.pChecked ? patternDelegate.statusColor : "transparent"
                                    border.color: patternDelegate.pChecked ? patternDelegate.statusColor : palette.mid
                                    border.width: patternDelegate.pChecked ? 0 : 1.5

                                    // チェックマーク
                                    Label {
                                        anchors.centerIn: parent
                                        text: "\u2713"
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: "white"
                                        visible: patternDelegate.pChecked
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            patternListView.togglePattern(modelData.name)
                                        }
                                    }
                                }

                                Label {
                                    text: modelData.summary || modelData.name || ""
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: patternDelegate.highlighted ? palette.highlightedText : palette.text
                                }
                            }

                            onClicked: {
                                patternListView.currentIndex = index
                                PackageController.loadPackagesByPattern(modelData.name)
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("Loading patterns...")
                            color: palette.placeholderText
                            visible: patternListView.count === 0
                        }
                    }
                }
            }
        }

        // 右ペイン: パッケージリスト + 詳細
        SplitView {
            SplitView.fillWidth: true
            orientation: Qt.Vertical

            // パッケージリスト (上部)
            Pane {
                id: pkgPane
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                padding: 0

                // リサイズ可能な列幅プロパティ
                readonly property real minColName: 120
                readonly property real minColSummary: 120
                readonly property real minColInstalled: 100
                readonly property real minColAvailable: 100
                property real colName: 200
                property real colSummary: 200
                property real colInstalled: 140
                property real colAvailable: 120

                // 全列の合計幅 (動的計算)
                property real pkgMinRowWidth: 4 + 36 + 1 + colName + 1 + colSummary + 1 + colInstalled + 1 + colAvailable + 1 + 88

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // テーブルヘッダ (横スクロール同期)
                    Rectangle {
                        Layout.fillWidth: true
                        height: 32
                        color: palette.button
                        clip: true

                        Flickable {
                            id: headerFlickable
                            anchors.fill: parent
                            contentX: packageFlickable.contentX
                            contentWidth: packageFlickable.contentWidth
                            interactive: false

                            Row {
                                x: 4
                                width: Math.max(headerFlickable.width, pkgPane.pkgMinRowWidth) - 4
                                height: 32
                                spacing: 0

                                // チェックボックス列ヘッダ (ソート可能)
                                Item {
                                    width: 36
                                    height: parent.height

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: toggleSort(0)
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        text: sortColumn === 0 ? (sortAscending ? "▲" : "▼") : ""
                                        font.pixelSize: 10
                                    }
                                }

                                Rectangle { width: 1; height: parent.height; color: palette.mid }

                                // パッケージ列ヘッダ (ソート可能・リサイズ可能)
                                Item {
                                    width: pkgPane.colName
                                    height: parent.height
                                    clip: true

                                    MouseArea {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: parent.width - 4
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: toggleSort(1)
                                    }

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 4
                                        Label { text: qsTr("Packages"); font.bold: true }
                                        Label { text: sortColumn === 1 ? (sortAscending ? "▲" : "▼") : ""; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                                    }

                                    Rectangle {
                                        width: 4; height: parent.height; anchors.right: parent.right
                                        color: phResize1.hovered || phResize1.pressed ? palette.highlight : "transparent"
                                        MouseArea {
                                            id: phResize1; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                            property bool hovered: false; hoverEnabled: true
                                            onEntered: hovered = true; onExited: hovered = false
                                            property real globalStartX: 0; property real startW: 0
                                            onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = pkgPane.colName }
                                            onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); pkgPane.colName = Math.max(pkgPane.minColName, startW + p.x - globalStartX) } }
                                        }
                                    }
                                }

                                Rectangle { width: 1; height: parent.height; color: palette.mid }

                                // 概要列ヘッダ (リサイズ可能)
                                Item {
                                    width: pkgPane.colSummary
                                    height: parent.height
                                    clip: true
                                    Label {
                                        anchors.centerIn: parent
                                        text: qsTr("Summary"); font.bold: true
                                    }

                                    Rectangle {
                                        width: 4; height: parent.height; anchors.right: parent.right
                                        color: phResize2.hovered || phResize2.pressed ? palette.highlight : "transparent"
                                        MouseArea {
                                            id: phResize2; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                            property bool hovered: false; hoverEnabled: true
                                            onEntered: hovered = true; onExited: hovered = false
                                            property real globalStartX: 0; property real startW: 0
                                            onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = pkgPane.colSummary }
                                            onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); pkgPane.colSummary = Math.max(pkgPane.minColSummary, startW + p.x - globalStartX) } }
                                        }
                                    }
                                }

                                Rectangle { width: 1; height: parent.height; color: palette.mid }

                                // インストール済み列ヘッダ (リサイズ可能)
                                Item {
                                    width: pkgPane.colInstalled
                                    height: parent.height
                                    clip: true

                                    Label {
                                        anchors.centerIn: parent
                                        text: qsTr("Installed"); font.bold: true
                                        width: Math.min(implicitWidth, parent.width - 10)
                                        elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                                    }

                                    Rectangle {
                                        width: 4; height: parent.height; anchors.right: parent.right
                                        color: phResize3.hovered || phResize3.pressed ? palette.highlight : "transparent"

                                        MouseArea {
                                            id: phResize3; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                            property bool hovered: false; hoverEnabled: true
                                            onEntered: hovered = true; onExited: hovered = false
                                            property real globalStartX: 0; property real startW: 0
                                            onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = pkgPane.colInstalled }
                                            onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); pkgPane.colInstalled = Math.max(pkgPane.minColInstalled, startW + p.x - globalStartX) } }
                                        }
                                    }
                                }

                                Rectangle { width: 1; height: parent.height; color: palette.mid }

                                // 利用可能列ヘッダ (リサイズ可能)
                                Item {
                                    width: pkgPane.colAvailable
                                    height: parent.height
                                    clip: true

                                    Label {
                                        anchors.centerIn: parent
                                        text: qsTr("Available"); font.bold: true
                                    }

                                    Rectangle {
                                        width: 4; height: parent.height; anchors.right: parent.right
                                        color: phResize4.hovered || phResize4.pressed ? palette.highlight : "transparent"

                                        MouseArea {
                                            id: phResize4;
                                            anchors.fill: parent;
                                            cursorShape: Qt.SplitHCursor

                                            property bool hovered: false;
                                            property real globalStartX: 0;
                                            property real startW: 0

                                            hoverEnabled: true
                                            onEntered: hovered = true;
                                            onExited: hovered = false
                                            onPressed: function(mouse) {
                                                var p = mapToGlobal(mouse.x, 0);
                                                globalStartX = p.x;
                                                startW = pkgPane.colAvailable
                                            }
                                            onPositionChanged: function(mouse) {
                                                if (pressed) {
                                                    var p = mapToGlobal(mouse.x, 0);
                                                    pkgPane.colAvailable = Math.max(pkgPane.minColAvailable, startW + p.x - globalStartX)
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle { width: 1; height: parent.height; color: palette.mid }

                                // サイズ列ヘッダ (固定幅)
                                Item {
                                    width: 88
                                    height: parent.height
                                    Label {
                                        anchors.centerIn: parent
                                        text: qsTr("Size"); font.bold: true
                                    }
                                }
                            }
                        }
                    }

                    // パッケージリスト (横スクロール対応)
                    Flickable {
                        id: packageFlickable
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: Math.max(width, pkgPane.pkgMinRowWidth)
                        contentHeight: height
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds

                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                        ListView {
                            id: packageListView
                            width: packageFlickable.contentWidth
                            height: packageFlickable.height
                            clip: true
                            model: sortedPackages()

                            delegate: ItemDelegate {
                                id: pkgDelegate
                                width: packageListView.width
                                height: 28
                                highlighted: ListView.isCurrentItem

                                required property int index
                                required property var modelData

                                background: Rectangle {
                                    color: pkgDelegate.highlighted ? palette.highlight : "transparent"
                                }

                                property bool isChecked: {
                                    var s = pkgDelegate.modelData.status
                                    return s === 3 || s === 1 || s === 2 || s === 4 || s === 5 || s === 9
                                }

                                property bool isMarkedForRemoval: {
                                    var s = pkgDelegate.modelData.status
                                    return s === 6 || s === 7
                                }

                                property color statusColor: {
                                    var s = pkgDelegate.modelData.status
                                    if (s === 3 || s === 9) return "#4CAF50"  // KeepInstalled/Protected
                                    if (s === 1 || s === 2) return "#2196F3"  // Install/AutoInstall
                                    if (s === 4 || s === 5) return "#FF9800"  // Update/AutoUpdate
                                    if (s === 6 || s === 7) return "#F44336"  // Del/AutoDel
                                    return palette.mid
                                }

                                onClicked: {
                                    packageListView.currentIndex = pkgDelegate.index
                                    PackageController.loadPackageDetails(pkgDelegate.modelData.name)
                                }

                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: {
                                        packageListView.currentIndex = pkgDelegate.index
                                        pkgContextMenu.targetPkg = pkgDelegate.modelData
                                        pkgContextMenu.popup()
                                    }
                                }

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    spacing: 0

                                    Item {
                                        width: 36
                                        height: parent.height
                                        z: 10

                                        Rectangle {
                                            width: 18; height: 18
                                            anchors.centerIn: parent
                                            radius: 3
                                            color: (pkgDelegate.isChecked || pkgDelegate.isMarkedForRemoval) ? pkgDelegate.statusColor : "transparent"
                                            border.color: (pkgDelegate.isChecked || pkgDelegate.isMarkedForRemoval) ? pkgDelegate.statusColor : palette.mid
                                            border.width: (pkgDelegate.isChecked || pkgDelegate.isMarkedForRemoval) ? 0 : 1.5

                                            Label {
                                                anchors.centerIn: parent
                                                text: pkgDelegate.isMarkedForRemoval ? "\u2715" : "\u2713"
                                                font.pixelSize: 13
                                                font.bold: true
                                                color: "white"
                                                visible: pkgDelegate.isChecked || pkgDelegate.isMarkedForRemoval
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: function(mouse) {
                                                mouse.accepted = true
                                                var pkg = pkgDelegate.modelData
                                                var s = pkg.status
                                                if (pkgDelegate.isMarkedForRemoval) {
                                                    // Del/AutoDel → KeepInstalled (削除取り消し)
                                                    setStatus(pkg.name, 3)
                                                } else if (!pkgDelegate.isChecked) {
                                                    // 未チェック → インストール
                                                    if (s === 0) setStatus(pkg.name, 1)
                                                    else if (s === 8) setStatus(pkg.name, 1)  // Taboo → Install
                                                } else {
                                                    // チェック済み → 解除
                                                    if (s === 3 || s === 4 || s === 5 || s === 9) setStatus(pkg.name, 6)
                                                    else if (s === 1) setStatus(pkg.name, 0)
                                                    else if (s === 2) setStatus(pkg.name, 8)  // AutoInstall → Taboo
                                                }
                                            }
                                        }
                                    }

                                    Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                    // パッケージ名
                                    Item {
                                        width: pkgPane.colName
                                        height: parent.height
                                        clip: true
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.leftMargin: 4; anchors.rightMargin: 4
                                            text: pkgDelegate.modelData.name || ""
                                            elide: Text.ElideRight
                                            color: pkgDelegate.highlighted ? palette.highlightedText : palette.text
                                        }
                                    }

                                    Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                    // 概要
                                    Item {
                                        width: pkgPane.colSummary
                                        height: parent.height
                                        clip: true
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.leftMargin: 4; anchors.rightMargin: 4
                                            text: pkgDelegate.modelData.summary || ""
                                            elide: Text.ElideRight
                                            color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                        }
                                    }

                                    Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                    // インストール済み
                                    Item {
                                        width: pkgPane.colInstalled
                                        height: parent.height
                                        clip: true
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.leftMargin: 4; anchors.rightMargin: 4
                                            text: pkgDelegate.modelData.installedVersion || ""
                                            elide: Text.ElideRight
                                            color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                        }
                                    }

                                    Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                    // 利用可能
                                    Item {
                                        width: pkgPane.colAvailable
                                        height: parent.height
                                        clip: true

                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.leftMargin: 4; anchors.rightMargin: 4
                                            text: pkgDelegate.modelData.version || ""
                                            elide: Text.ElideRight
                                            color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                        }
                                    }

                                    Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                    // サイズ
                                    Item {
                                        width: 88
                                        height: parent.height
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.rightMargin: 8
                                            text: pkgDelegate.modelData.size ? formatSize(pkgDelegate.modelData.size) : ""
                                            horizontalAlignment: Text.AlignRight
                                            color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                        }
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {}
                        }
                    }

                    // パッケージコンテキストメニュー
                    Menu {
                        id: pkgContextMenu
                        property var targetPkg: ({})

                        MenuItem {
                            text: qsTr("Install")
                            enabled: pkgContextMenu.targetPkg.status === 0  // NoInst
                            onTriggered: setStatus(pkgContextMenu.targetPkg.name, 1)
                        }

                        MenuItem {
                            text: qsTr("Delete")
                            enabled: pkgContextMenu.targetPkg.status === 3  // KeepInstalled
                                  || pkgContextMenu.targetPkg.status === 4  // Update
                            onTriggered: setStatus(pkgContextMenu.targetPkg.name, 6)
                        }

                        MenuItem {
                            text: qsTr("Update")
                            enabled: (pkgContextMenu.targetPkg.status === 3)  // KeepInstalled
                                  && (pkgContextMenu.targetPkg.version || "") !== ""
                                  && (pkgContextMenu.targetPkg.version || "") !== (pkgContextMenu.targetPkg.installedVersion || "")
                            onTriggered: setStatus(pkgContextMenu.targetPkg.name, 4)
                        }

                        MenuSeparator {}

                        MenuItem {
                            text: qsTr("Taboo (Prevent Install)")
                            enabled: pkgContextMenu.targetPkg.status === 0
                            onTriggered: setStatus(pkgContextMenu.targetPkg.name, 8)
                        }

                        MenuItem {
                            text: qsTr("Protected (Prevent Removal)")
                            enabled: pkgContextMenu.targetPkg.status === 3
                            onTriggered: setStatus(pkgContextMenu.targetPkg.name, 9)
                        }

                        MenuSeparator {}

                        MenuItem {
                            text: qsTr("Undo Changes")
                            enabled: pkgContextMenu.targetPkg.status === 1
                                  || pkgContextMenu.targetPkg.status === 4
                                  || pkgContextMenu.targetPkg.status === 6
                                  || pkgContextMenu.targetPkg.status === 8
                                  || pkgContextMenu.targetPkg.status === 9
                            onTriggered: {
                                var pkg = pkgContextMenu.targetPkg
                                // インストール系 → NoInst、削除/更新系 → KeepInstalled
                                var revert = (pkg.installedVersion && pkg.installedVersion !== "") ? 3 : 0
                                setStatus(pkg.name, revert)
                            }
                        }
                    }
                }
            }

            // 詳細パネル (下部)
            Pane {
                id: detailPane
                SplitView.preferredHeight: splitSettings.detailPaneHeight
                SplitView.minimumHeight: 120
                padding: 0
                onHeightChanged: splitSettings.detailPaneHeight = height

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    TabBar {
                        id: detailsTabBar
                        Layout.fillWidth: true

                        TabButton { text: qsTr("Description") }
                        TabButton { text: qsTr("Technical Data") }
                        TabButton { text: qsTr("Dependencies") }
                        TabButton { text: qsTr("Version") }
                        TabButton { text: qsTr("File List") }
                        TabButton { text: qsTr("Changelog") }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: detailsTabBar.currentIndex

                        // 説明タブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                textFormat: TextArea.RichText
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    if (!d || !d.name) return ""
                                    return "<h3>" + (d.name || "") + "</h3>"
                                         + "<p>" + (d.summary || "") + "</p>"
                                         + "<hr/>"
                                         + "<p>" + (d.description || "").replace(/\n/g, "<br/>") + "</p>"
                                }
                            }
                        }

                        // 技術データタブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                textFormat: TextArea.RichText
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    if (!d || !d.name) return ""
                                    return formatTechData(d)
                                }
                                onLinkActivated: function(link) {
                                    Qt.openUrlExternally(link)
                                }
                                HoverHandler {
                                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor
                                }
                            }
                        }

                        // 依存関係タブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                textFormat: TextArea.RichText
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    if (!d || !d.name) return ""
                                    return formatDependencies(d)
                                }
                            }
                        }

                        // バージョンタブ
                        Pane {
                            id: versionPane
                            padding: 0

                            // カラム幅のプロパティ (ドラッグでリサイズ可能)
                            readonly property real minColRadio: 40
                            readonly property real minColVersion: 220
                            readonly property real minColArch: 120
                            readonly property real minColRepo: 200
                            readonly property real minColStatus: 100
                            property real colRadio: minColRadio
                            property real colVersion: minColVersion
                            property real colArch: minColArch
                            property real colStatus: minColStatus

                            // 全列の合計最小幅
                            property real verMinRowWidth: colRadio + colVersion + colArch + minColRepo + colStatus

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                // カラムヘッダ (横スクロール同期)
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 28
                                    color: palette.button
                                    clip: true

                                    Flickable {
                                        id: verHeaderFlickable
                                        anchors.fill: parent
                                        contentX: versionFlickable.contentX
                                        contentWidth: versionFlickable.contentWidth
                                        interactive: false

                                        Row {
                                            width: Math.max(verHeaderFlickable.width, versionPane.verMinRowWidth)
                                            height: 28
                                            spacing: 0

                                            // ラジオ列ヘッダ
                                            Item {
                                                width: versionPane.colRadio
                                                height: parent.height
                                            }
                                            // 区切り線
                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // バージョン列ヘッダ + リサイズハンドル
                                            Item {
                                                width: versionPane.colVersion - 1
                                                height: parent.height
                                                Label {
                                                    anchors.centerIn: parent
                                                    text: qsTr("Version")
                                                    font.bold: true
                                                }
                                                Rectangle {
                                                    width: 4; height: parent.height
                                                    anchors.right: parent.right
                                                    color: vhResize1.hovered || vhResize1.pressed ? palette.highlight : "transparent"

                                                    MouseArea {
                                                        id: vhResize1
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SplitHCursor
                                                        property bool hovered: false
                                                        hoverEnabled: true
                                                        onEntered: hovered = true
                                                        onExited: hovered = false
                                                        property real globalStartX: 0
                                                        property real startW: 0
                                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = versionPane.colVersion }
                                                        onPositionChanged: function(mouse) {
                                                            if (pressed) {
                                                                var p = mapToGlobal(mouse.x, 0)
                                                                versionPane.colVersion = Math.max(versionPane.minColVersion, startW + p.x - globalStartX)
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // アーキテクチャ列ヘッダ + リサイズハンドル
                                            Item {
                                                width: versionPane.colArch - 1
                                                height: parent.height
                                                clip: true

                                                Label {
                                                    anchors.centerIn: parent
                                                    width: Math.min(implicitWidth, parent.width - 10)
                                                    text: qsTr("Architecture")
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    horizontalAlignment: Text.AlignHCenter
                                                }
                                                Rectangle {
                                                    width: 4; height: parent.height
                                                    anchors.right: parent.right
                                                    color: vhResize2.hovered || vhResize2.pressed ? palette.highlight : "transparent"
                                                    MouseArea {
                                                        id: vhResize2
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SplitHCursor
                                                        property bool hovered: false
                                                        hoverEnabled: true
                                                        onEntered: hovered = true
                                                        onExited: hovered = false
                                                        property real globalStartX: 0
                                                        property real startW: 0
                                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = versionPane.colArch }
                                                        onPositionChanged: function(mouse) {
                                                            if (pressed) {
                                                                var p = mapToGlobal(mouse.x, 0)
                                                                versionPane.colArch = Math.max(versionPane.minColArch, startW + p.x - globalStartX)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // リポジトリ列ヘッダ
                                            Item {
                                                width: Math.max(versionPane.minColRepo, parent.width - versionPane.colRadio - versionPane.colVersion - versionPane.colArch - versionPane.colStatus - 3)
                                                height: parent.height
                                                Label {
                                                    anchors.centerIn: parent
                                                    text: qsTr("Repository")
                                                    font.bold: true
                                                }
                                            }
                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // ステータス列ヘッダ
                                            Item {
                                                width: versionPane.colStatus - 1
                                                height: parent.height
                                                Label {
                                                    anchors.centerIn: parent
                                                    text: qsTr("Status")
                                                    font.bold: true
                                                }
                                            }
                                        }
                                    }
                                }

                                // バージョンリスト (横スクロール対応)
                                Flickable {
                                    id: versionFlickable
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    contentWidth: Math.max(width, versionPane.verMinRowWidth)
                                    contentHeight: height
                                    flickableDirection: Flickable.HorizontalFlick
                                    boundsBehavior: Flickable.StopAtBounds

                                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                                    ListView {
                                        id: versionListView
                                        width: versionFlickable.contentWidth
                                        height: versionFlickable.height
                                        clip: true
                                        model: PackageController.currentPackageDetails.availableVersions || []

                                        property int selectedIndex: -1

                                        function selectVersion(idx, versionData) {
                                            if (selectedIndex === idx) return
                                            selectedIndex = idx
                                            var d = PackageController.currentPackageDetails
                                            if (d && d.name) {
                                                PackageController.setPackageVersion(
                                                    d.name,
                                                    versionData.version,
                                                    versionData.arch,
                                                    versionData.repoAlias
                                                )
                                            }
                                        }

                                        onModelChanged: {
                                            selectedIndex = -1
                                            var d = PackageController.currentPackageDetails
                                            if (!d || !d.installedVersion) return
                                            var versions = d.availableVersions || []
                                            for (var i = 0; i < versions.length; i++) {
                                                if (versions[i].version === d.installedVersion) {
                                                    selectedIndex = i
                                                    break
                                                }
                                            }
                                        }

                                        delegate: ItemDelegate {
                                            width: versionListView.width
                                            height: 32

                                            required property int index
                                            required property var modelData

                                            property bool isInstalled: {
                                                var d = PackageController.currentPackageDetails
                                                return d && d.installedVersion && d.installedVersion === modelData.version
                                            }
                                            property bool isSelected: versionListView.selectedIndex === index

                                            onClicked: versionListView.selectVersion(index, modelData)

                                            Row {
                                                anchors.fill: parent
                                                spacing: 0

                                                // ラジオボタン (カスタム描画)
                                                Item {
                                                    width: versionPane.colRadio
                                                    height: parent.height

                                                    Rectangle {
                                                        width: 16; height: 16
                                                        anchors.centerIn: parent
                                                        radius: 8
                                                        color: "transparent"
                                                        border.color: isSelected ? palette.highlight : palette.text
                                                        border.width: 1.5

                                                        Rectangle {
                                                            width: 8; height: 8
                                                            anchors.centerIn: parent
                                                            radius: 4
                                                            color: palette.highlight
                                                            visible: isSelected
                                                        }
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: versionListView.selectVersion(index, modelData)
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // バージョン
                                                Item {
                                                    width: versionPane.colVersion - 1
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        anchors.right: parent.right
                                                        anchors.rightMargin: 4
                                                        text: modelData.version || ""
                                                        font.bold: isInstalled
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // アーキテクチャ
                                                Item {
                                                    width: versionPane.colArch - 1
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        text: modelData.arch || ""
                                                        color: palette.placeholderText
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // リポジトリ
                                                Item {
                                                    width: Math.max(versionPane.minColRepo, parent.width - versionPane.colRadio - versionPane.colVersion - versionPane.colArch - versionPane.colStatus - 3)
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        anchors.right: parent.right
                                                        anchors.rightMargin: 4
                                                        text: modelData.repoName || ""
                                                        elide: Text.ElideRight
                                                        color: palette.placeholderText
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // ステータス
                                                Item {
                                                    width: versionPane.colStatus - 1
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        text: isInstalled ? qsTr("Installed") : ""
                                                        color: "#4caf50"
                                                        font.pixelSize: 12
                                                    }
                                                }
                                            }
                                        }

                                        ScrollBar.vertical: ScrollBar {}
                                    }
                                }
                            }
                        }

                        // ファイル一覧タブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.NoWrap
                                font.family: "monospace"
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    return (d && d.fileList) ? d.fileList.join("\n") : ""
                                }
                            }
                        }

                        // 変更ログタブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                font.family: "monospace"
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    return (d && d.changeLog) ? d.changeLog.join("\n\n") : ""
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // -- フッタ --
    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            BusyIndicator {
                running: PackageController.busy
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                visible: PackageController.busy
            }

            Label {
                text: PackageController.statusMessage
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Cancel")
                onClicked: {
                    PackageController.restoreState()
                    Qt.quit()
                }
            }
            Button {
                text: qsTr("Apply")
                highlighted: true
                enabled: !PackageController.busy && PackageController.hasPendingChanges
                onClicked: applyChanges()
            }
        }
    }

    // -- ダイアログ --

    // ソルバー実行中ウェイトダイアログ
    Dialog {
        id: solverWaitDialog
        title: qsTr("Resolving dependencies...")
        anchors.centerIn: parent
        modal: true
        closePolicy: Dialog.NoAutoClose
        width: 320
        standardButtons: Dialog.NoButton

        RowLayout {
            anchors.fill: parent
            spacing: 12

            BusyIndicator {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                running: solverWaitDialog.visible
            }

            Label {
                text: qsTr("Resolving dependencies...\nPlease wait.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // PackageController.busyに連動して自動開閉
        // ただし、commitProgressDialog / conflictDialog / refreshOverlay表示中は開かない
        Connections {
            target: PackageController
            function onBusyChanged() {
                if (PackageController.busy) {
                    if (!commitProgressDialog.visible && !conflictDialog.visible && !refreshOverlay.visible)
                        solverWaitDialog.open()
                } else {
                    solverWaitDialog.close()
                }
            }
        }
    }

    // コミット進捗ダイアログ (myrlyn風 To Do / Downloads / Doing / Done)
    Dialogs.CommitProgressDialog {
        id: commitProgressDialog
    }

    // トランザクション結果ダイアログ
    Dialog {
        id: resultDialog
        title: qsTr("Installation Report")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        width: 520
        // 縦サイズは親ウィンドウの60%を上限 (大量パッケージ時もコンパクトに)
        height: Math.min(root.height * 0.85, 700)

        background: Rectangle {
            color: palette.window
            border.color: palette.highlight
            border.width: 2
        }
        header: Label {
            text: resultDialog.title
            font.bold: true
            padding: 10
        }

        property bool resultSuccess: true
        property var resultData: ({})

        Flickable {
            id: resultFlickable
            anchors.fill: parent
            contentHeight: resultColumn.implicitHeight
            clip: true
            boundsMovement: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: resultColumn
                width: resultFlickable.width
                spacing: 12

            // ヘッダ
            Label {
                text: resultDialog.resultSuccess
                    ? qsTr("Installation completed successfully.")
                    : qsTr("Installation completed with errors.")
                font.bold: true
                font.pixelSize: 15
                color: resultDialog.resultSuccess ? "#4CAF50" : "#F44336"
                Layout.fillWidth: true
            }

            // エラーメッセージ
            Label {
                visible: !resultDialog.resultSuccess && (resultDialog.resultData.errorMessage || "") !== ""
                text: resultDialog.resultData.errorMessage || ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: "#FF9800"
                font.pixelSize: 12
            }

            // 区切り線
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
            }

            // パッケージ セクション
            Label {
                text: qsTr("Packages")
                font.bold: true
                font.pixelSize: 13
                color: palette.highlight
            }

            // インストール済み
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: (resultDialog.resultData.installed || 0) > 0

                Label {
                    text: "\u2022 " + qsTr("Installed packages: %1").arg(
                        resultDialog.resultData.installed || 0)
                    Layout.fillWidth: true
                }
                TextEdit {
                    text: (resultDialog.resultData.installedPackages || []).join("\n")
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    wrapMode: TextEdit.NoWrap
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    color: palette.text
                    font.pixelSize: 13
                    font.family: "monospace"
                    visible: (resultDialog.resultData.installedPackages || []).length > 0
                }
            }

            // 更新済み
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: (resultDialog.resultData.updated || 0) > 0

                Label {
                    text: "\u2022 " + qsTr("Updated packages: %1").arg(
                        resultDialog.resultData.updated || 0)
                    Layout.fillWidth: true
                }
                TextEdit {
                    text: (resultDialog.resultData.updatedPackages || []).join("\n")
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    wrapMode: TextEdit.NoWrap
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    color: palette.text
                    font.pixelSize: 13
                    font.family: "monospace"
                    visible: (resultDialog.resultData.updatedPackages || []).length > 0
                }
            }

            // 削除済み
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: (resultDialog.resultData.removed || 0) > 0

                Label {
                    text: "\u2022 " + qsTr("Removed packages: %1").arg(
                        resultDialog.resultData.removed || 0)
                    Layout.fillWidth: true
                }
                TextEdit {
                    text: (resultDialog.resultData.removedPackages || []).join("\n")
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    wrapMode: TextEdit.NoWrap
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    color: palette.text
                    font.pixelSize: 13
                    font.family: "monospace"
                    visible: (resultDialog.resultData.removedPackages || []).length > 0
                }
            }

            // 失敗パッケージ
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: (resultDialog.resultData.failedPackages || []).length > 0

                Label {
                    text: "\u2022 " + qsTr("Failed packages: %1").arg(
                        (resultDialog.resultData.failedPackages || []).length)
                    color: "#F44336"
                    Layout.fillWidth: true
                }
                TextEdit {
                    text: (resultDialog.resultData.failedPackages || []).join("\n")
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    wrapMode: TextEdit.NoWrap
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    color: "#F44336"
                    font.pixelSize: 13
                    font.family: "monospace"
                }
            }

            // 区切り線
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
            }

            // 統計 セクション
            Label {
                text: qsTr("Statistics")
                font.bold: true
                font.pixelSize: 13
                color: palette.highlight
            }

            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 12
                rowSpacing: 4

                Label { text: "\u2022 " + qsTr("Elapsed time:") }
                Label {
                    text: {
                        var secs = resultDialog.resultData.elapsedSeconds || 0
                        var min = Math.floor(secs / 60)
                        var sec = Math.floor(secs % 60)
                        return String(min).padStart(2, '0') + ":" + String(sec).padStart(2, '0')
                    }
                }

                Label { text: "\u2022 " + qsTr("Total installed size:") }
                Label {
                    text: formatBytes(resultDialog.resultData.totalInstalledSize || 0)
                }

                Label { text: "\u2022 " + qsTr("Total download size:") }
                Label {
                    text: formatBytes(resultDialog.resultData.totalDownloadSize || 0)
                }
            }
            }   // ColumnLayout
        }   // Flickable

        onAccepted: {
            // パッケージリストを再検索して更新
            performSearch()
        }
    }

    // コンフリクト解決ダイアログ
    Dialog {
        id: conflictDialog
        title: qsTr("Warning")
        anchors.centerIn: parent
        modal: true
        width: Math.min(root.width * 0.85, 750)
        height: Math.min(root.height * 0.75, 550)
        standardButtons: Dialog.NoButton

        onRejected: {
            PackageController.restoreState()
        }

        property var problems: []
        // 各問題で選択された解決策のインデックスを保持
        property var selectedSolutions: []
        // applyChanges()から呼ばれたかどうか
        property bool fromApply: false

        onProblemsChanged: {
            // デフォルト: 各問題の最初の解決策を選択
            var sel = []
            for (var i = 0; i < problems.length; i++)
                sel.push(0)
            selectedSolutions = sel
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            ListView {
                id: conflictListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: conflictDialog.problems
                spacing: 12

                delegate: ColumnLayout {
                    id: conflictDelegate
                    width: conflictListView.width - 12

                    required property int index
                    required property var modelData

                    // 問題の説明 (⚠アイコン付き)
                    Label {
                        text: "\u26A0 " + (modelData.description || "")
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // 詳細
                    Label {
                        text: modelData.details || ""
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        color: palette.placeholderText
                        visible: (modelData.details || "") !== ""
                    }

                    // [競合の解決方法:]ラベル
                    Label {
                        text: qsTr("Conflict resolution:")
                        Layout.topMargin: 4
                    }

                    // 解決策リスト (RadioButton + details)
                    Repeater {
                        model: modelData.solutions || []

                        ColumnLayout {
                            required property int index
                            required property var modelData

                            property int problemIdx: conflictDelegate.index

                            Layout.fillWidth: true
                            spacing: 2

                            RadioButton {
                                text: (parent.index + 1) + ": " + (parent.modelData.description || "")
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                checked: {
                                    var sel = conflictDialog.selectedSolutions
                                    var pi = parent.problemIdx
                                    return sel && pi < sel.length && sel[pi] === parent.index
                                }
                                onClicked: {
                                    var sel = conflictDialog.selectedSolutions.slice()
                                    sel[parent.problemIdx] = parent.index
                                    conflictDialog.selectedSolutions = sel
                                }
                            }

                            // 解決策の詳細 (具体的なアクション内容)
                            Label {
                                text: parent.modelData.details || ""
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                Layout.leftMargin: 48
                                color: palette.placeholderText
                                font.pixelSize: 13
                                visible: (parent.modelData.details || "") !== ""
                            }
                        }
                    }

                    // 問題間の区切り線
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: palette.mid
                        opacity: 0.5
                        Layout.topMargin: 4
                    }
                }
            }

            // ボタン行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("OK -- Try Again (O)")
                    onClicked: {
                        // 選択された解決策を適用
                        var sel = conflictDialog.selectedSolutions
                        for (var i = 0; i < conflictDialog.problems.length; i++) {
                            var solIdx = (sel && i < sel.length) ? sel[i] : 0
                            PackageController.applySolution(i, solIdx)
                        }
                        conflictDialog.close()

                        if (conflictDialog.fromApply) {
                            // 「適用」からの呼び出し — 再度依存関係解決
                            applyChanges()
                        } else {
                            // チェックボックスからの呼び出し — ソルバー再実行
                            var result = PackageController.resolveDependencies()
                            if (result && !result.success && result.problems && result.problems.length > 0) {
                                conflictDialog.problems = result.problems
                                conflictDialog.open()
                            }
                        }
                    }
                }

                Button {
                    text: qsTr("Cancel (C)")
                    onClicked: {
                        // ステータス変更を元に戻す (内部でパッケージリスト再取得も実行)
                        PackageController.restoreState()
                        conflictDialog.close()
                    }
                }
            }
        }
    }

    // 全パッケージ更新 結果通知ダイアログ
    Dialog {
        id: updateInfoDialog
        title: qsTr("Update All Packages")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        width: 400

        property string message: ""

        Label {
            text: updateInfoDialog.message
            wrapMode: Text.WordWrap
            anchors.left: parent.left
            anchors.right: parent.right
        }
    }

    Dialogs.CommitConfirmDialog {
        id: commitConfirmDialog

        onAccepted: {
            // Polkit認証
            if (!PackageController.checkAuth("org.presire.qzypper.install-packages"))
                return

            // コミット実行
            var pending = PackageController.getPendingChanges()
            commitProgressDialog.initFromPendingChanges(pending)
            commitProgressDialog.open()
            PackageController.commitAsync()
        }
    }

    AboutqZypperDialog {
        id: aboutqZypperDialog
    }

    AboutQtDialog {
        id: aboutQtDialog
    }

    // リポジトリ管理は repoDrawer (上部で定義) に移行済み

    // -- 起動時ウェイトダイアログ --
    Dialog {
        id: startupDialog
        anchors.centerIn: parent
        modal: true
        closePolicy: Dialog.NoAutoClose
        standardButtons: Dialog.NoButton
        title: qsTr("Starting")

        ColumnLayout {
            spacing: 16

            Label {
                text: qsTr("Initializing. Please wait...")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            BusyIndicator {
                running: true
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
            }
        }
    }

    // -- ヘルパー関数 --

    // チェックボックス操作時のコンフリクト自動表示
    Connections {
        target: PackageController
        function onConflictsDetected(problems) {
            conflictDialog.fromApply = false
            conflictDialog.problems = problems
            conflictDialog.open()
        }
    }

    Component.onCompleted: {
        Theme.darkMode = Qt.binding(() => root.darkMode)
        PackageController.applyDarkMode(root.darkMode)
        startupDialog.open()
        initTimer.start()
    }

    Timer {
        id: initTimer
        interval: 50
        onTriggered: {
            PackageController.initialize()
            PackageController.loadRepos()
            PackageController.saveState()
            startupDialog.close()
        }
    }

    function performSearch() {
        var flags = 0
        if (cbName.checked)     flags |= 0x01
        if (cbKeyword.checked)  flags |= 0x02
        if (cbSummary.checked)  flags |= 0x04
        if (cbDesc.checked)     flags |= 0x08
        if (cbRequires.checked) flags |= 0x10
        if (cbProvides.checked) flags |= 0x20
        PackageController.searchPackages(searchField.text, flags)
    }

    function setStatus(packageName, status) {
        // C++側でステータス変更 → ソルバー実行 → パッケージリスト再読み込みを一括処理
        PackageController.setPackageStatus(packageName, status)
    }

    function updateAllPackages() {
        var result = PackageController.updateAllPackages()
        if (!result) return

        if (result.backendCrashed) {
            return
        }

        if (result.success) {
            var pending = PackageController.getPendingChanges()
            if (pending.length === 0) {
                // 更新可能なパッケージなし — 情報ダイアログ表示
                updateInfoDialog.message = qsTr("All packages are up to date.")
                updateInfoDialog.open()
                return
            }
            // パッケージがマークされた — 確認ダイアログ表示
            updateInfoDialog.message = qsTr("%1 package(s) will be updated.\nClick \"Apply Changes\" to proceed.").arg(pending.length)
            updateInfoDialog.open()
        } else {
            // コンフリクト解決ダイアログを表示
            conflictDialog.fromApply = true
            conflictDialog.problems = result.problems || []
            conflictDialog.open()
        }
    }

    function applyChanges() {
        var result = PackageController.resolveDependencies()
        if (!result) return

        if (result.backendCrashed) {
            return
        }

        if (result.success) {
            // コミット確認ダイアログを表示
            commitConfirmDialog.summaryModel = buildSummaryModel()
            commitConfirmDialog.diskUsageModel = PackageController.getDiskUsage()
            commitConfirmDialog.open()
        } else {
            // コンフリクト解決ダイアログを表示
            conflictDialog.fromApply = true
            conflictDialog.problems = result.problems || []
            conflictDialog.open()
        }
    }

    function formatBytes(bytes) {
        if (bytes === 0) return "0 B"
        var units = ["B", "KiB", "MiB", "GiB"]
        var i = 0
        var value = bytes
        while (value >= 1024 && i < units.length - 1) {
            value /= 1024
            i++
        }
        return value.toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    function buildSummaryModel() {
        // PackageController.packages を参照してバインディング再評価をトリガー
        var _trigger = PackageController.packages
        var installs = [], updates = [], deletes = []
        // 全プール横断で変更予定パッケージを取得
        var pkgs = PackageController.getPendingChanges()
        for (var i = 0; i < pkgs.length; i++) {
            var pkg = pkgs[i]
            var s = pkg.status
            if (s === 1 || s === 2) {  // Install, AutoInstall
                installs.push({ category: "install", name: pkg.name, version: pkg.version })
            } else if (s === 4 || s === 5) {  // Update, AutoUpdate
                updates.push({ category: "update", name: pkg.name, version: pkg.version })
            } else if (s === 6 || s === 7) {  // Del, AutoDel
                deletes.push({ category: "delete", name: pkg.name, version: pkg.installedVersion })
            }
        }
        // カテゴリ順: インストール --> 更新 --> 削除
        return installs.concat(updates, deletes)
    }

    function statusLabel(status) {
        switch (status) {
            case 1: return "+"     // Install
            case 2: return "a+"    // AutoInstall
            case 4: return "↑"     // Update
            case 5: return "a↑"    // AutoUpdate
            case 6: return "-"     // Del
            case 7: return "a-"    // AutoDel
            case 8: return "T"     // Taboo
            case 9: return "P"     // Protected
            default: return ""
        }
    }

    function statusColor(status) {
        switch (status) {
            case 1: return "#4caf50"   // Install
            case 2: return "#81c784"   // AutoInstall
            case 4: return "#2196f3"   // Update
            case 5: return "#64b5f6"   // AutoUpdate
            case 6: return "#f44336"   // Del
            case 7: return "#e57373"   // AutoDel
            case 8: return "#ff9800"   // Taboo
            case 9: return "#9c27b0"   // Protected
            default: return "transparent"
        }
    }

    function formatSize(bytes) {
        if (bytes < 1024)            return bytes + " B"
        if (bytes < 1024 * 1024)     return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function formatTechData(d) {
        var html = "<table>"
        html += "<tr><td><b>" + qsTr("Version") + ":</b></td><td>" + (d.version || "") + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Architecture") + ":</b></td><td>" + (d.arch || "") + "</td></tr>"
        html += "<tr><td><b>" + qsTr("License") + ":</b></td><td>" + (d.license || "") + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Group") + ":</b></td><td>" + (d.group || "") + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Vendor") + ":</b></td><td>" + (d.vendor || "") + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Build Host") + ":</b></td><td>" + (d.buildHost || "") + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Source RPM") + ":</b></td><td>" + (d.sourceRpm || "") + "</td></tr>"
        var urlVal = d.url || ""
        if (urlVal)
            html += "<tr><td><b>" + qsTr("URL") + ":</b></td><td><a href=\"" + urlVal + "\">" + urlVal + "</a></td></tr>"
        else
            html += "<tr><td><b>" + qsTr("URL") + ":</b></td><td></td></tr>"
        html += "</table>"
        return html
    }

    function formatDependencies(d) {
        var html = ""
        if (d.provides && d.provides.length > 0)
            html += "<h4>" + qsTr("Provides") + "</h4><p>" + d.provides.join("<br/>") + "</p>"
        if (d.requires && d.requires.length > 0)
            html += "<h4>" + qsTr("Requires") + "</h4><p>" + d.requires.join("<br/>") + "</p>"
        if (d.conflicts && d.conflicts.length > 0)
            html += "<h4>" + qsTr("Conflicts") + "</h4><p>" + d.conflicts.join("<br/>") + "</p>"
        if (d.obsoletes && d.obsoletes.length > 0)
            html += "<h4>" + qsTr("Obsoletes") + "</h4><p>" + d.obsoletes.join("<br/>") + "</p>"
        if (d.recommends && d.recommends.length > 0)
            html += "<h4>" + qsTr("Recommends") + "</h4><p>" + d.recommends.join("<br/>") + "</p>"
        if (d.suggests && d.suggests.length > 0)
            html += "<h4>" + qsTr("Suggests") + "</h4><p>" + d.suggests.join("<br/>") + "</p>"
        return html || qsTr("<p>No dependency information</p>")
    }
}
