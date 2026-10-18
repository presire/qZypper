import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import QtQuick.Dialogs
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
    property bool applyPendingCommit: false   // Apply Changes 起点の resolve かを追跡 (非同期チェーン用)
    property bool quitting: false             // 終了時に releaseSession の応答を待っているか
    readonly property int patchesTabIndex: 4

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
        property real statusFilterHeight: 330
    }

    Settings {
        category: "statusFilter"
        property alias mask: statusFilterPanel.statusMask
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
    NavDrawer {
        id: navDrawer
        height: root.height
        onUpdateAllRequested: root.updateAllPackages()
        onApplyChangesRequested: root.applyChanges()
        onManageReposRequested: repoDrawer.open()
        onRefreshReposRequested: {
            refreshOverlay.open()
            PackageController.refreshReposAsync()
        }
        onAboutqZypperRequested: aboutqZypperDialog.open()
        onAboutQtRequested: aboutQtDialog.open()
        onEndSessionRequested: PackageController.releaseSession()
        onDistUpgradeRequested: distUpgradeDialog.open()
        onPackageLocksRequested: locksDialog.open()
        onHistoryRequested: historyDialog.open()
        onVerifySystemRequested: PackageController.verifySystem()
        onInstallLocalRpmsRequested: localRpmFileDialog.open()
    }

    // -- リポジトリ管理ドロワー (全画面) --
    Drawer {
        id: repoDrawer
        width: root.width
        height: root.height
        edge: Qt.RightEdge
        interactive: false  // スワイプで閉じない (Escまたはボタンで閉じる)

        // モーダルなドロワー表示中はウィンドウ直下の Shortcut がブロックされるため、
        // Esc はドロワー内の Shortcut で扱う (子ダイアログ表示中はそちらが優先される)
        Shortcut {
            sequence: "Escape"
            enabled: repoDrawer.visible && !refreshOverlay.visible
            onActivated: repoDrawer.close()
        }

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
    // リフレッシュ中は refreshOverlay 内の Shortcut が取消しを扱い、
    // オーバーレイは refreshFinished まで閉じない
    // ウィンドウ全体の Shortcut が Esc を先に受け取るため、パッケージ一覧の
    // typeahead 検索文字列のクリアもここで処理する (ListView の Keys.onPressed には Esc が届かない)
    Shortcut {
        sequence: "Escape"
        enabled: !refreshOverlay.visible
        onActivated: {
            if (repoDrawer.visible) {
                repoDrawer.close()
            } else if (navDrawer.visible) {
                navDrawer.close()
            } else if (packageTablePane.pkgSearchBuffer.length > 0) {
                packageTablePane.pkgSearchBuffer = ""
                packageTablePane.pkgSearchClearTimer.stop()
            }
        }
    }

    // -- リフレッシュ進捗オーバーレイ --
    RefreshOverlay {
        id: refreshOverlay
    }

    // 終了時に releaseSession の応答を待つためのタイマ (応答が無ければ強制終了)
    Timer {
        id: quitTimer
        interval: 3000
        repeat: false
        onTriggered: Qt.quit()
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
                TabButton {
                    text: qsTr("Patches")
                    width: Math.max(80, implicitWidth)
                    Accessible.name: qsTr("Patches view")
                }

                onCurrentIndexChanged: {
                    if (currentIndex === 3) {
                        PackageController.loadPatterns()
                    } else if (currentIndex === root.patchesTabIndex) {
                        patchView.reload()
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

            SplitView {
                id: leftSplit
                anchors.fill: parent
                orientation: Qt.Vertical

                StackLayout {
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 180   // ウィンドウ最小高 600 でもリポジトリ一覧が数行表示できる高さ
                    currentIndex: filterTabBar.currentIndex

                    SearchView {
                        id: searchView
                    }

                    SummaryView {
                        id: summaryView
                    }

                    RepositoryView {
                        id: repositoryView
                    }

                    PatternView {
                        id: patternView
                    }

                    // パッチタブ: 左ペインには必要パッチの件数概要を表示 (一覧は右ペイン)
                    Pane {
                        id: patchSummaryPane
                        padding: 12

                        readonly property var counts: {
                            var c = { needed: 0, security: 0, selected: 0, reboot: 0 }
                            var list = PackageController.patches
                            for (var i = 0; i < list.length; ++i) {
                                var p = list[i]
                                if (p.isNeeded) {
                                    c.needed++
                                    if (String(p.category).toLowerCase() === "security")
                                        c.security++
                                    if (p.rebootSuggested)
                                        c.reboot++
                                }
                                if (p.selected)
                                    c.selected++
                            }
                            return c
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6

                            Label { text: qsTr("Patch Overview"); font.bold: true }
                            Label { text: qsTr("Needed patches: %1").arg(patchSummaryPane.counts.needed) }
                            Label { text: qsTr("Needed security patches: %1").arg(patchSummaryPane.counts.security) }
                            Label { text: qsTr("Selected for installation: %1").arg(patchSummaryPane.counts.selected) }
                            Label {
                                visible: patchSummaryPane.counts.reboot > 0
                                text: qsTr("Needed patches suggesting a reboot: %1").arg(patchSummaryPane.counts.reboot)
                                color: "#F44336"
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                // ステータスフィルタ (右側のパッケージ一覧にのみ適用。インストールの概要には適用しない)
                StatusFilterPanel {
                    id: statusFilterPanel
                    SplitView.preferredHeight: splitSettings.statusFilterHeight
                    SplitView.minimumHeight: 150
                    SplitView.maximumHeight: Math.max(150, leftSplit.height - 180)
                    // 起動時のレイアウト確定前の高さを保存しないよう、ユーザがハンドルをドラッグした時のみ保存する
                    onHeightChanged: if (leftSplit.resizing) splitSettings.statusFilterHeight = height
                    onMaskEdited: PackageController.packageProxy.statusMask = statusFilterPanel.statusMask
                    onRefreshRequested: PackageController.packageProxy.statusMask = statusFilterPanel.statusMask
                }
            }
        }

        // 右ペイン: パッケージリスト + 詳細
        SplitView {
            SplitView.fillWidth: true
            orientation: Qt.Vertical

            // パッケージリスト (上部)。パッチタブ選択時はパッチ一覧に切り替える
            StackLayout {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                currentIndex: filterTabBar.currentIndex === root.patchesTabIndex ? 1 : 0

                PackageTable {
                    id: packageTablePane
                    darkMode: root.darkMode
                }

                PatchView {
                    id: patchView
                }
            }

            // パッケージ詳細 (下部)
            PackageDetailsPane {
                id: detailPane
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
                textFormat: Text.PlainText
                Layout.fillWidth: true
            }

            // バックエンドの操作フェーズ表示 (operationState)
            Label {
                text: root.operationStageText(PackageController.operationState)
                visible: text.length > 0
                opacity: 0.7
                Layout.alignment: Qt.AlignVCenter
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
    Dialogs.SolverWaitDialog {
        id: solverWaitDialog
        blocked: startupDialog.visible || commitProgressDialog.visible
                 || conflictDialog.visible || refreshOverlay.visible
    }

    // コミット進捗ダイアログ (myrlyn風 To Do / Downloads / Doing / Done)
    Dialogs.CommitProgressDialog {
        id: commitProgressDialog
    }

    // トランザクション結果ダイアログ
    Dialogs.InstallationResultDialog {
        id: resultDialog
        onRefreshRequested: searchView.performSearch()
    }

    // コンフリクト解決ダイアログ
    Dialogs.ConflictDialog {
        id: conflictDialog
        onApplyChangesRequested: root.applyChanges()
        onApplyPendingCommitCleared: root.applyPendingCommit = false
    }

    // 更新情報ダイアログ
    Dialogs.UpdateInfoDialog {
        id: updateInfoDialog
    }

    Dialogs.CommitConfirmDialog {
        id: commitConfirmDialog
        diskUsageModel: PackageController.diskUsage
        transactionSummary: PackageController.transactionSummary

        onAccepted: {
            // コミット実行 (非同期: 進捗はシグナルで通知される)
            var pending = PackageController.pendingChanges
            commitProgressDialog.initFromPendingChanges(pending)
            commitProgressDialog.open()
            PackageController.commitAsync()
        }
    }

    // 未信頼リポジトリ署名鍵の確認ダイアログ
    Dialogs.KeyTrustDialog {
        id: keyTrustDialog
    }

    AboutqZypperDialog {
        id: aboutqZypperDialog
    }

    AboutQtDialog {
        id: aboutQtDialog
    }

    // リポジトリ管理はrepoDrawer (上部で定義) に移行済み

    // -- 起動時ウェイトダイアログ --
    Dialogs.StartupDialog {
        id: startupDialog
    }

    // -- パッケージ操作の対話プロンプト (Wave1) --
    Dialogs.PromptDialog {
        id: promptDialog
    }

    // -- Zypp ロック検出ダイアログ (Wave1) --
    Dialogs.PackageLockDialog {
        id: lockDialog
        onRetryRequested: PackageController.retryOperation()
    }

    // -- セッション終了後の再開始ダイアログ (Wave1) --
    Dialogs.SessionEndedDialog {
        id: sessionEndedDialog
        onStartSessionRequested: {
            startupDialog.open()
            PackageController.initialize()
        }
        onQuitRequested: Qt.quit()
    }

    // -- 情報トースト (PoolReloaded 等の通知) --
    InfoToast {
        id: infoToast
    }

    Dialogs.DistUpgradeDialog {
        id: distUpgradeDialog
        onConfirmed: function(allowVendorChange) {
            PackageController.distUpgrade(allowVendorChange)
        }
    }

    Dialogs.LocksDialog {
        id: locksDialog
    }

    Dialogs.HistoryDialog {
        id: historyDialog
    }

    Dialogs.SolverTestcaseDialog {
        id: testcaseDialog
    }

    // バックエンド/コントローラのエラー通知
    Dialog {
        id: errorDialog
        title: qsTr("Error")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        width: Math.min(root.width * 0.7, 520)

        property string message: ""

        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            text: errorDialog.message
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
    }

    // -- ヘルパー関数 --

    // 操作フェーズ (operationState.stage) を表示用テキストへ変換する。
    function operationStageText(s) {
        if (!s || !s.state || s.state === "idle")
            return ""
        switch (s.stage) {
        case "initializing": return qsTr("Initializing...")
        case "reading":      return qsTr("Reading package database...")
        case "resolving":    return qsTr("Resolving dependencies...")
        case "refreshing":   return qsTr("Refreshing repositories...")
        case "downloading":  return qsTr("Downloading packages...")
        case "installing":   return qsTr("Installing packages...")
        case "repo-change":  return qsTr("Updating repositories...")
        case "releasing":    return qsTr("Releasing session...")
        default:             return qsTr("Working...")
        }
    }

    function updateAllPackages() {
        PackageController.updateAllPackages()
    }

    function applyChanges() {
        // 依存解決を開始できない状態ではフラグを立てない (resolveFinished が届かず残るため)
        if (!PackageController.connected || !PackageController.initialized || PackageController.busy)
            return
        root.applyPendingCommit = true
        PackageController.resolveDependencies()
    }

    function summaryHasChanges(s) {
        // 概要が取得できない場合は変更の有無を判断できないため、確認ダイアログへ進める (fail-closed)
        if (!s || s.installCount === undefined)
            return true
        return (s.installCount || 0) + (s.updateCount || 0) + (s.removeCount || 0)
                + (s.downgradeCount || 0) > 0
    }

    function showInfo(title, message) {
        updateInfoDialog.title = title
        updateInfoDialog.message = message
        updateInfoDialog.open()
    }

    // ソルバー結果 (dup / パッチ一括選択 / 検証) を共通フローで処理する:
    // 問題あり → コンフリクトダイアログ、変更あり → 確認ダイアログ (危険変更の承認込み)、変更なし → 情報表示
    function handleSolverResult(result, title, noChangeMessage) {
        if (!result || result.backendCrashed)
            return
        if (!result.success) {
            conflictDialog.fromApply = true
            conflictDialog.problems = result.problems || []
            conflictDialog.open()
            return
        }
        if (summaryHasChanges(result.summary)) {
            root.applyChanges()
            return
        }
        showInfo(title, noChangeMessage)
    }

    // ウィンドウ終了時に非同期で releaseSession を発行 (応答 or 3秒で終了)
    onClosing: function(close) {
        if (PackageController.initialized && !root.quitting) {
            close.accepted = false
            root.quitting = true
            quitTimer.start()
            PackageController.releaseSession()
        }
    }

    // ローカル RPM ファイル選択 → バックエンドで一時リポジトリ経由の依存解決
    FileDialog {
        id: localRpmFileDialog
        title: qsTr("Select RPM Files to Install")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("RPM packages (*.rpm)"), qsTr("All files (*)")]
        onAccepted: PackageController.installLocalRpms(selectedFiles)
    }

    Connections {
        target: PackageController
        function onLocalRpmsFinished(result) {
            if (!result || result.backendCrashed)
                return
            if (!result.success && (!result.problems || result.problems.length === 0)) {
                errorDialog.message = result.errorMessage || qsTr("The RPM files could not be prepared for installation.")
                errorDialog.open()
                return
            }
            var unsigned = result.unsignedPackages || []
            if (result.success && unsigned.length > 0)
                infoToast.show(qsTr("Warning: unsigned packages: %1").arg(unsigned.join(", ")))
            root.handleSolverResult(result, qsTr("Install Local RPM Files"),
                                    qsTr("The selected packages are already installed."))
        }
    }

    // チェックボックス操作時のコンフリクト自動表示
    Connections {
        target: PackageController
        function onConflictsDetected(problems) {
            conflictDialog.fromApply = false
            conflictDialog.problems = problems
            conflictDialog.open()
        }
    }

    // バックエンドが未信頼の署名鍵を検出 → 確認ダイアログへキュー投入
    Connections {
        target: PackageController
        function onUntrustedKeyDetected(keyInfo) {
            keyTrustDialog.enqueue(keyInfo)
        }
    }

    // Zypp ロック検出 → フレンドリーダイアログ
    Connections {
        target: PackageController
        function onPackageLocked(info) {
            lockDialog.lockerName = info.name || ""
            lockDialog.lockerPid = info.pid || 0
            lockDialog.lockerMessage = info.message || ""
            lockDialog.open()
        }
    }

    // プール再読込 (外部変更) → 再取得 + 情報トースト
    Connections {
        target: PackageController
        function onPoolReloaded() {
            infoToast.show(qsTr("Package database was changed externally and reloaded"))
        }
    }

    // 編集セッション解放 → 再開始状態へ (終了中なら終了)
    Connections {
        target: PackageController
        function onSessionReleased(ok) {
            if (root.quitting) {
                quitTimer.stop()
                Qt.quit()
                return
            }
            sessionEndedDialog.open()
        }
    }
    Component.onCompleted: {
        Theme.darkMode = Qt.binding(() => root.darkMode)
        PackageController.applyDarkMode(root.darkMode)
        PackageController.packageProxy.statusMask = statusFilterPanel.statusMask
        startupDialog.open()
        initTimer.start()
    }

    Timer {
        id: initTimer
        interval: 50
        onTriggered: PackageController.initialize()
    }

    // 非同期D-Bus呼び出しの結果ハンドリング
    Connections {
        target: PackageController

        function onInitializedChanged() {
            if (PackageController.initialized) {
                // 初期化完了 — 起動ウェイトを解除して一覧を読み込む
                startupDialog.close()
                PackageController.loadRepos()
                PackageController.saveState()
            }
        }

        function onConnectedChanged() {
            // 切断時は依存解決の応答が破棄され resolveFinished が届かないため、保留中の適用フラグを解除する
            if (!PackageController.connected)
                root.applyPendingCommit = false
        }

        function onBusyChanged() {
            // 初期化に失敗した場合 (busy=false かつ 未初期化) はウェイトを解除してエラー表示に戻す
            if (startupDialog.opened && !PackageController.busy && !PackageController.initialized)
                startupDialog.close()
        }

        function onUpdateAllFinished(result) {
            if (!result || result.backendCrashed || result.busError)
                return
            if (result.success) {
                var pending = PackageController.pendingChanges
                if (pending.length === 0) {
                    // 更新可能なパッケージなし — 情報ダイアログ表示
                    root.showInfo(qsTr("Update All Packages"), qsTr("All packages are up to date."))
                    return
                }
                // パッケージがマークされた — 確認ダイアログ表示
                root.showInfo(qsTr("Update All Packages"),
                              qsTr("%1 package(s) will be updated.\nClick \"Apply Changes\" to proceed.").arg(pending.length))
            } else {
                // コンフリクト解決ダイアログを表示
                conflictDialog.fromApply = true
                conflictDialog.problems = result.problems || []
                conflictDialog.open()
            }
        }

        function onResolveFinished(result) {
            if (!result || result.backendCrashed || result.busError || !PackageController.connected) {
                // 依存解決が進行せず終了した場合は保留中の適用フラグを解除する
                root.applyPendingCommit = false
                return
            }
            if (result.success) {
                if (root.applyPendingCommit) {
                    root.applyPendingCommit = false
                    PackageController.prepareCommit()
                }
            } else {
                // コンフリクト解決ダイアログを表示
                conflictDialog.fromApply = root.applyPendingCommit
                root.applyPendingCommit = false
                conflictDialog.problems = result.problems || []
                conflictDialog.open()
            }
        }

        function onCommitPrepared(ok) {
            if (!ok)
                return
            // コミット確認ダイアログを表示 (ディスク使用量とリスク概要は非同期取得しバインディングで反映)
            commitConfirmDialog.summaryModel = Util.buildSummaryModel()
            commitConfirmDialog.summaryPending = true
            PackageController.requestDiskUsage()
            PackageController.requestTransactionSummary()
            commitConfirmDialog.open()
        }

        function onTransactionSummaryChanged() {
            commitConfirmDialog.summaryPending = false
        }

        function onDistUpgradeFinished(result) {
            root.handleSolverResult(result, qsTr("Distribution Upgrade"),
                                    qsTr("The system is already up to date."))
        }

        function onSelectPatchesFinished(result) {
            // パッチ一括選択は確認画面へ直行せず、一覧で選択内容を確認できるようにする
            if (result && result.success && !result.backendCrashed && root.summaryHasChanges(result.summary)) {
                root.showInfo(qsTr("Patches"),
                              qsTr("Needed patches have been selected. Review the selection and click \"Apply\" to install them."))
                return
            }
            root.handleSolverResult(result, qsTr("Patches"),
                                    qsTr("No needed patches were found."))
        }

        function onVerifyFinished(result) {
            root.handleSolverResult(result, qsTr("Verify System"),
                                    qsTr("The system is consistent. No changes are needed."))
        }

        function onSolverTestcaseCreated(path) {
            testcaseDialog.testcasePath = path
            testcaseDialog.open()
        }

        function onErrorOccurred(errorMessage) {
            if (!errorMessage || errorMessage.length === 0)
                return
            errorDialog.message = errorMessage
            errorDialog.open()
        }

        function onAddRepoFinished(success) {
            if (success && repoDrawerLoader.item)
                repoDrawerLoader.item.loadData()
        }

        function onAddServiceFinished(success) {
            if (success && repoDrawerLoader.item)
                repoDrawerLoader.item.loadData()
        }
    }
}
