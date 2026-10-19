/**
 * @file Main.qml
 * @brief qZypperのメインウィンドウ (ApplicationWindow)
 *
 * main.cppからロードされる最上位コンポーネントで、ヘッダのビュータブ、左ペイン (検索/概要/リポジトリ/パターン/パッチ概要とステータスフィルタ)、
 * 右ペイン (パッケージ一覧と詳細)、フッタ (進捗表示とApply/Cancel) を構成する
 *
 * 各ダイアログを生成し、PackageController経由のD-BusシグナルをConnectionsで受けて、リフレッシュ/依存解決/コミット/セッション管理の一連フローを制御する
 *
 * 入力 : なし (最上位のため外部プロパティは受けない)
 * 出力 : 自身はシグナルを発行しない (子コンポーネントのハンドラで完結する)
 * 依存 : PackageController (QMLシングルトン、D-Busバックエンドの操作とモデル保持)、
 *       Theme、Util、NavDrawer、PackageTable、PackageDetailsPane、StatusFilterPanel、RefreshOverlay、InfoToast、各views/dialogs
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import QtQuick.Dialogs
import org.presire.qzypper.gui
import "dialogs" as Dialogs

// qZypperのメインウィンドウ
ApplicationWindow {
    id: root  // メインウィンドウ自身の参照名

    // --- テーマ切り替え ---
    // 初回起動時はシステム設定に従い、以降はユーザ選択をSettingsへ永続化
    property bool darkMode: themeSettings.darkMode  // ダークモードフラグ (theme設定とバインド)
    property bool applyPendingCommit: false         // [Apply Changes]起点のresolveかを追跡 (非同期チェーン用)
    property bool sessionWasConnected: false        // 接続済みだったか (切断時の再開始ダイアログ表示判定用)
    property bool quitting: false                   // 終了時にreleaseSessionの応答を待っているか
    readonly property int patchesTabIndex: 4        // [Patches]タブのTabBar index (0始まりの4番目)

    // --- ヘルパー関数 ---

    /**
     * @brief operationStateをフッタ表示用の翻訳テキストへ変換する
     * @param s operationStateオブジェクト
     * @return ステージに対応する表示文字列 (idleまたは不正時は空文字列)
     */
    function operationStageText(s) {
        // 待機中、または状態が取得できない場合は何も表示しない
        if (!s || !s.state || s.state === "idle") {
            return ""
        }

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

    /**
     * @brief PackageControllerへ全パッケージ更新の選択を要求する
     * @note PackageController::updateAllPackagesを呼び出す
     */
    function updateAllPackages() {
        PackageController.updateAllPackages()
    }

    /**
     * @brief 接続済みかつ待機中のときだけ依存解決を開始して適用フローを記録する
     * @note 戻り値は無く、開始できない状態では早期に戻る
     * @note applyPendingCommitを立て、PackageController::resolveDependenciesを呼ぶ
     */
    function applyChanges() {
        // 依存解決を開始できない状態ではフラグを立てない (resolveFinishedが届かず残るため)
        if (!PackageController.connected || !PackageController.initialized || PackageController.busy) {
            return
        }
        root.applyPendingCommit = true
        PackageController.resolveDependencies()
    }

    /**
     * @brief トランザクション概要に適用対象の変更があるかを判定する
     * @param s PackageController.transactionSummaryの概要オブジェクト
     * @return 変更がある場合、または概要が無いかinstallCountが未定義の場合はtrue
     */
    function summaryHasChanges(s) {
        // 概要が取得できない場合は変更の有無を判断できないため、確認ダイアログへ進める (fail-closed)
        if (!s || s.installCount === undefined) {
            return true
        }
        // インストール・更新・削除・ダウングレードのいずれかが1件以上あれば変更あり
        return (s.installCount || 0) + (s.updateCount || 0) + (s.removeCount || 0)
                + (s.downgradeCount || 0) > 0
    }

    /**
     * @brief 指定した見出しと本文で情報ダイアログを表示する
     * @param title ダイアログの見出し
     * @param message ダイアログの本文
     * @note updateInfoDialogのプロパティを更新して表示する
     */
    function showInfo(title, message) {
        updateInfoDialog.title = title
        updateInfoDialog.message = message
        updateInfoDialog.open()
    }

    // ソルバー結果 (dup / パッチ一括選択 / 検証) を共通フローで処理する:
    // 問題あり → コンフリクトダイアログ、変更あり → 確認ダイアログ (危険変更の承認込み)、変更なし → 情報表示
    /**
     * @brief ソルバー結果を競合、適用、情報表示のいずれかの画面遷移へ振り分ける
     * @param result バックエンドのソルバー結果
     * @param title 情報・競合ダイアログの見出し
     * @param noChangeMessage 変更が無いときに表示する本文
     * @note 戻り値は無く、結果が無効なときは早期に戻る
     * @note 依存競合はconflictDialog、変更ありはapplyChanges、変更なしはshowInfoへ振り分ける
     */
    function handleSolverResult(result, title, noChangeMessage) {
        // 結果が無い、またはバックエンドが異常終了した場合は何もしない
        if (!result || result.backendCrashed) {
            return
        }

        // 依存関係の問題がある場合はコンフリクト解決ダイアログを表示する
        if (!result.success) {
            conflictDialog.fromApply = true
            conflictDialog.problems = result.problems || []
            conflictDialog.open()
            return
        }

        // 変更がある場合は通常の適用フロー (依存解決 → 確認ダイアログ) へ進める
        if (summaryHasChanges(result.summary)) {
            root.applyChanges()
            return
        }

        showInfo(title, noChangeMessage)
    }

    width: 1200                                   // 初期のウィンドウ幅
    height: 800                                   // 初期のウィンドウ高
    minimumWidth: 800                             // ウィンドウの最小幅
    minimumHeight: 600                            // ウィンドウの最小高
    visible: true                                 // 起動と同時にウィンドウを表示する
    title: qsTr("qZypper - Software Management")  // ウィンドウのタイトル

    // テーマ配色はC++側へ委任し、PackageController.applyDarkMode()が
    // QGuiApplication::setPalette()で全コントロールへ一括適用する
    // 理由 : Qt 6.5ではQMLのpaletteグループ化プロパティへの動的バインディングが
    // 子コントロールへ正しく伝播しないため
    onDarkModeChanged: {
        PackageController.applyDarkMode(darkMode)
    }

    /**
     * @brief セッション解放の応答を待ってから終了し、応答なしではTimerが終了する
     * @param close 終了要求イベント
     * @note 初期化済みの初回終了要求では終了を保留し、quittingを設定してquitTimerを開始し、PackageController::releaseSessionを呼ぶ
     */
    onClosing: function(close) {
        if (PackageController.initialized && !root.quitting) {
            close.accepted = false
            root.quitting = true
            quitTimer.start()
            PackageController.releaseSession()
        }
    }

    // テーマ設定の永続化 (ダークモードの選択を保存する)
    Settings {
        id: themeSettings  // テーマ設定の参照名

        property bool darkMode: systemDarkMode  // システム配色を初回値にする設定値

        category: "theme"  // 設定ファイル内のグループ名
    }

    // -- 設定の永続化 --
    // ウィンドウサイズの保存
    Settings {
        property alias width: root.width    // 保存するウィンドウ幅
        property alias height: root.height  // 保存するウィンドウ高

        category: "window"  // 設定ファイル内のグループ名
    }

    // 左ペインの幅とステータスフィルタの高さの保存
    Settings {
        id: splitSettings  // 分割位置の設定の参照名

        property real leftPaneWidth: 280       // 保存する左ペイン幅
        property real statusFilterHeight: 330  // 保存するフィルタペイン高

        category: "splitView"  // 設定ファイル内のグループ名
    }

    // ステータスフィルタの選択状態の保存
    Settings {
        property alias mask: statusFilterPanel.statusMask  // 保存するステータスフィルタ

        category: "statusFilter"  // 設定ファイル内のグループ名
    }

    // -- グローバルショートカット --
    // [Ctrl]+[Q]キーでアプリケーションを終了する
    Shortcut {
        sequence: "Ctrl+Q"  // 割り当てるキー

        // アプリケーションを終了する
        onActivated: {
            Qt.quit()
        }
    }

    // [Ctrl]+[Enter]キーで変更を適用する
    // 解決/コミット中の再入防止と、変更が無い時の無効化 ([Apply]ボタンと同一条件)
    Shortcut {
        enabled: !PackageController.busy && PackageController.hasPendingChanges  // 処理中や変更が無い場合は無効にする
        sequence: "Ctrl+Return"                                                  // 割り当てるキー

        // 変更の適用フローを開始する
        onActivated: {
            applyChanges()
        }
    }

    // --- ナビゲーションDrawer (ハンバーガーメニュー) ---
    NavDrawer {
        id: navDrawer  // ナビゲーションメニューの参照名

        height: root.height  // ウィンドウの高さに合わせる

        // [Update All Packages]メニュー : 全パッケージ更新の選択を要求する
        onUpdateAllRequested: {
            root.updateAllPackages()
        }

        // [Apply Changes]メニュー : 変更の適用フローを開始する
        onApplyChangesRequested: {
            root.applyChanges()
        }

        // [Manage Repositories]メニュー : リポジトリ管理ドロワーを開く
        onManageReposRequested: {
            repoDrawer.open()
        }

        // [Refresh Repositories]メニュー : 全リポジトリのリフレッシュを開始する
        onRefreshReposRequested: {
            // 未接続/処理中は要求が即時破棄されrefreshFinishedが届かないため、オーバーレイを開かない
            if (!PackageController.connected || PackageController.busy) {
                return
            }

            refreshOverlay.open()
            PackageController.refreshReposAsync()
        }

        // [About qZypper]メニュー : qZypperの情報ダイアログを開く
        onAboutqZypperRequested: {
            aboutqZypperDialog.open()
        }

        // [About Qt]メニュー : Qtの情報ダイアログを開く
        onAboutQtRequested: {
            aboutQtDialog.open()
        }

        // [End Session]メニュー : 編集セッションを解放する (結果はonSessionReleasedで受け取る)
        onEndSessionRequested: {
            PackageController.releaseSession()
        }

        // [Distribution Upgrade]メニュー : ディストリビューション更新の確認ダイアログを開く
        onDistUpgradeRequested: {
            distUpgradeDialog.open()
        }

        // [Package Locks]メニュー : パッケージロックの管理ダイアログを開く
        onPackageLocksRequested: {
            locksDialog.open()
        }

        // [History]メニュー : 操作履歴ダイアログを開く
        onHistoryRequested: {
            historyDialog.open()
        }

        // [Verify System]メニュー : システムの検証を開始する (結果はonVerifyFinishedで受け取る)
        onVerifySystemRequested: {
            PackageController.verifySystem()
        }

        // [Install Local RPMs]メニュー : RPMファイルの選択ダイアログを開く
        onInstallLocalRpmsRequested: {
            localRpmFileDialog.open()
        }
    }

    // --- リポジトリ管理ドロワー (全画面) ---
    Drawer {
        id: repoDrawer  // リポジトリ管理ドロワーの参照名

        width: root.width    // ウィンドウ全体を覆う幅
        height: root.height  // ウィンドウ全体を覆う高さ
        edge: Qt.RightEdge   // 右端から開く
        interactive: false   // スワイプで閉じない ([Esc]またはボタンで閉じる)

        // 開くたびにリポジトリ/サービス一覧をバックエンドから再取得させる
        onOpened: {
            if (repoDrawerLoader.item) {
                repoDrawerLoader.item.loadData()
            }
        }

        // モーダルなドロワー表示中はウィンドウ直下のShortcutがブロックされるため、
        // [Esc]はドロワー内のShortcutで扱う (子ダイアログ表示中はそちらが優先される)
        Shortcut {
            enabled: repoDrawer.visible && !refreshOverlay.visible  // リフレッシュ中は取消し操作を優先するため無効にする
            sequence: "Escape"                                      // 割り当てるキー

            // ドロワーを閉じる
            onActivated: {
                repoDrawer.close()
            }
        }

        // ドロワー表示中のみ内容を生成し、閉じたら破棄してメモリを節約する
        Loader {
            id: repoDrawerLoader  // ドロワー内容の読み込み領域の参照名

            anchors.fill: parent        // ドロワー全体に広げる
            active: repoDrawer.visible  // ドロワー表示中のみ内容を生成する

            // リポジトリ管理画面の内容
            sourceComponent: Component {
                // リポジトリ/サービス管理画面本体
                Dialogs.RepoManagerDrawerContent {
                    // [閉じる]ボタン : ドロワーを閉じる
                    onCloseRequested: {
                        repoDrawer.close()
                    }

                    // [すべてリフレッシュ]ボタン : 全リポジトリのリフレッシュを開始する
                    onRefreshAllRequested: {
                        // 未接続または処理中は要求が即時に破棄されrefreshFinishedが届かないため、オーバーレイを開かない
                        if (!PackageController.connected || PackageController.busy) {
                            return
                        }
                        refreshOverlay.open()
                        PackageController.refreshReposAsync()
                    }
                }
            }
        }
    }

    // [Esc]キーのハンドリング
    // リフレッシュ中はrefreshOverlay内のShortcutが取消しを扱い、オーバーレイはrefreshFinishedまで閉じない
    // ウィンドウ全体のShortcutが[Esc]を先に受け取るため、パッケージ一覧のtypeahead検索文字列のクリアもここで処理する (ListViewのKeys.onPressedには[Esc]が届かない)
    Shortcut {
        enabled: !refreshOverlay.visible  // リフレッシュ中は無効にする
        sequence: "Escape"                // 割り当てるキー

        // 開いているドロワーを閉じる (どちらも開いていなければtypeahead検索文字列をクリアする)
        onActivated: {
            if (repoDrawer.visible) {
                repoDrawer.close()
            }
            else if (navDrawer.visible) {
                navDrawer.close()
            }
            else if (packageTablePane.pkgSearchBuffer.length > 0) {
                packageTablePane.pkgSearchBuffer = ""
                packageTablePane.pkgSearchClearTimer.stop()
            }
        }
    }

    // --- リフレッシュ進捗オーバーレイ ---
    RefreshOverlay {
        id: refreshOverlay  // リフレッシュ進捗オーバーレイの参照名
    }

    // 終了時にreleaseSessionの応答を待つためのタイマ (応答が無ければ強制終了)
    Timer {
        id: quitTimer  // 終了待ちタイマの参照名

        interval: 3000  // 応答を待つ最大時間 (ミリ秒)
        repeat: false   // 1回だけ実行する

        // 応答が無いまま時間切れになったため強制終了する
        onTriggered: {
            Qt.quit()
        }
    }

    // --- ヘッダツールバー ---
    header: ToolBar {
        // ツールバーの部品を横に並べる
        RowLayout {
            anchors.fill: parent    // ツールバー全体に広げる
            anchors.leftMargin: 4   // 左端の余白
            anchors.rightMargin: 8  // 右端の余白

            // ナビゲーションメニューを開くボタン
            ToolButton {
                text: "☰"                   // ハンバーガーメニューの記号
                font.pixelSize: 20          // 記号の文字サイズ
                ToolTip.visible: hovered    // ホバー中にツールチップを表示する
                ToolTip.text: qsTr("Menu")  // ツールチップの文字

                // ナビゲーションメニューを開く
                onClicked: {
                    navDrawer.open()
                }
            }

            // ビュー切り替えタブの見出し
            Label {
                Layout.alignment: Qt.AlignVCenter  // ツールバーの高さ方向の中央に配置する
                text: qsTr("View:")                // 見出しの文字
            }

            // 左ペインのビュー切り替えタブ (インデックスは左ペインのStackLayoutの並びと対応する)
            TabBar {
                id: filterTabBar  // ビュー切り替えタブの参照名

                Layout.fillWidth: false  // タブの内容に合わせた幅にする

                // タブを切り替えたときに、そのビューで必要なデータを読み込む
                onCurrentIndexChanged: {
                    // パターン/パッチはタブ表示時にのみ取得する遅延読み込み (起動時に全量を読むと重くなる)
                    if (currentIndex === 3) {
                        PackageController.loadPatterns()
                    }
                    else if (currentIndex === root.patchesTabIndex) {
                        patchView.reload()
                    }
                }

                // [Search]タブ
                TabButton {
                    width: Math.max(70, implicitWidth)  // 最小70pxを確保したタブ幅
                    text: qsTr("Search")                // タブの表示名
                }

                // [Installation Summary]タブ
                TabButton {
                    width: Math.max(140, implicitWidth)  // 最小140pxを確保したタブ幅
                    text: qsTr("Installation Summary")   // タブの表示名
                }

                // [Repository]タブ
                TabButton {
                    width: Math.max(90, implicitWidth)  // 最小90pxを確保したタブ幅
                    text: qsTr("Repository")            // タブの表示名
                }

                // [Patterns]タブ
                TabButton {
                    width: Math.max(80, implicitWidth)  // 最小80pxを確保したタブ幅
                    text: qsTr("Patterns")              // タブの表示名
                }

                // [Patches]タブ
                TabButton {
                    width: Math.max(80, implicitWidth)     // 最小80pxを確保したタブ幅
                    text: qsTr("Patches")                  // タブの表示名
                    Accessible.name: qsTr("Patches view")  // スクリーンリーダー向けの名前
                }
            }

            // 残りの幅を埋めてテーマ切り替えを右寄せにするスペーサー
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // ダークモード/ライトモード切り替え
            RowLayout {
                Layout.alignment: Qt.AlignVCenter  // ツールバーの高さ方向の中央に配置する
                spacing: 6                         // ラベルとスイッチの間隔

                // 現在のテーマ名
                Label {
                    Layout.alignment: Qt.AlignVCenter                             // 高さ方向の中央に配置する
                    text: root.darkMode ? qsTr("Dark Mode") : qsTr("Light Mode")  // 現在のテーマ名
                }

                // テーマの切り替えスイッチ
                Switch {
                    id: themeSwitch  // テーマ切り替えスイッチの参照名

                    checked: root.darkMode  // ダークモードならオン

                    // 選択したテーマを設定へ保存し、画面へ反映する
                    onToggled: {
                        themeSettings.darkMode = checked
                        root.darkMode = checked
                    }
                }
            }
        }
    }

    // 左ペイン (フィルタ) と右ペイン (パッケージ一覧と詳細) の左右分割
    SplitView {
        anchors.fill: parent        // ヘッダとフッタの間の領域全体に広げる
        orientation: Qt.Horizontal  // 左右に分割する

        // 左ペイン : フィルタ
        Pane {
            id: leftPane  // 左ペインの参照名

            SplitView.preferredWidth: splitSettings.leftPaneWidth  // 保存済みの左ペイン幅
            SplitView.minimumWidth: 240                            // 左ペインの最小幅
            SplitView.maximumWidth: 400                            // 左ペインの最大幅
            padding: 0                                             // 内側の部品を枠いっぱいに広げるため余白なし

            // 幅が変わるたびに設定へ保存する
            onWidthChanged: {
                splitSettings.leftPaneWidth = width
            }

            // ビュー領域とステータスフィルタの上下分割
            SplitView {
                id: leftSplit  // 左ペインの上下分割の参照名

                anchors.fill: parent      // 左ペイン全体に広げる
                orientation: Qt.Vertical  // 上下に分割する

                // ビュー切り替えタブで選択したビューを表示する
                StackLayout {
                    SplitView.fillHeight: true               // 残りの高さをすべて使う
                    SplitView.minimumHeight: 180             // ウィンドウ最小高600でもリポジトリ一覧が数行表示できる高さ
                    currentIndex: filterTabBar.currentIndex  // 選択中のタブに対応するビュー

                    // [Search]タブのビュー
                    SearchView {
                        id: searchView  // 検索ビューの参照名
                    }

                    // [Installation Summary]タブのビュー
                    SummaryView {
                        id: summaryView  // 変更予定の概要ビューの参照名
                    }

                    // [Repository]タブのビュー
                    RepositoryView {
                        id: repositoryView  // リポジトリビューの参照名
                    }

                    // [Patterns]タブのビュー
                    PatternView {
                        id: patternView  // パターンビューの参照名
                    }

                    // パッチタブ : 左ペインには必要パッチの件数概要を表示 (一覧は右ペイン)
                    Pane {
                        id: patchSummaryPane  // パッチ概要の参照名

                        // パッチ概要に表示する集計値
                        // PackageController.patchesが更新されるたびに再集計される
                        readonly property var counts: {
                            var c = { needed: 0, security: 0, selected: 0, reboot: 0 }
                            var list = PackageController.patches
                            for (var i = 0; i < list.length; ++i) {
                                var p = list[i]
                                // ソルバが必要と判断したパッチを種類別に数える
                                if (p.isNeeded) {
                                    c.needed++
                                    if (String(p.category).toLowerCase() === "security") {
                                        c.security++
                                    }

                                    if (p.rebootSuggested) {
                                        c.reboot++
                                    }
                                }

                                // インストール対象に選ばれているパッチを数える
                                if (p.selected) {
                                    c.selected++
                                }
                            }
                            return c
                        }

                        padding: 12  // 外周の余白

                        // 集計値を縦に並べる
                        ColumnLayout {
                            anchors.fill: parent  // パッチ概要全体に広げる
                            spacing: 6            // 各行の間隔

                            // 概要の見出し
                            Label {
                                text: qsTr("Patch Overview")  // 見出しの文字
                                font.bold: true               // 見出しとして太字にする
                            }

                            // 必要なパッチの件数
                            Label {
                                text: qsTr("Needed patches: %1").arg(patchSummaryPane.counts.needed)  // 必要なパッチの件数
                            }

                            // 必要なセキュリティパッチの件数
                            Label {
                                // 必要なセキュリティパッチの件数
                                text: qsTr("Needed security patches: %1").arg(patchSummaryPane.counts.security)
                            }

                            // インストール対象に選ばれているパッチの件数
                            Label {
                                // 選択済みパッチの件数
                                text: qsTr("Selected for installation: %1").arg(patchSummaryPane.counts.selected)
                            }

                            // 再起動を推奨する必要なパッチの件数 (該当するパッチがある場合のみ注意を促す)
                            Label {
                                visible: patchSummaryPane.counts.reboot > 0  // 該当するパッチがある場合のみ表示する
                                // 再起動推奨のパッチ件数
                                text: qsTr("Needed patches suggesting a reboot: %1").arg(patchSummaryPane.counts.reboot)
                                color: "#F44336"                             // 赤色
                            }

                            // 各行を上詰めにするための余白
                            Item {
                                Layout.fillHeight: true  // 残りの高さをすべて使う
                            }
                        }
                    }
                }

                // ステータスフィルタ (右側のパッケージ一覧にのみ適用、インストールの概要には適用しない)
                StatusFilterPanel {
                    id: statusFilterPanel  // ステータスフィルタの参照名

                    SplitView.preferredHeight: splitSettings.statusFilterHeight     // 保存済みのフィルタペイン高
                    SplitView.minimumHeight: 150                                    // フィルタペインの最小高
                    SplitView.maximumHeight: Math.max(150, leftSplit.height - 180)  // ビュー領域の最小高180を残す最大高

                    // 起動時のレイアウト確定前の高さを保存しないよう、ユーザがハンドルをドラッグした時のみ保存する
                    onHeightChanged: {
                        if (leftSplit.resizing) {
                            splitSettings.statusFilterHeight = height
                        }
                    }

                    // フィルタの選択を変更したときに、パッケージ一覧の絞り込みへ反映する
                    onMaskEdited: {
                        PackageController.packageProxy.statusMask = statusFilterPanel.statusMask
                    }

                    // 再適用を要求されたときに、パッケージ一覧の絞り込みへ反映し直す
                    onRefreshRequested: {
                        PackageController.packageProxy.statusMask = statusFilterPanel.statusMask
                    }
                }
            }
        }

        // 右ペイン : パッケージリスト+詳細
        SplitView {
            SplitView.fillWidth: true  // 残りの幅をすべて使う
            orientation: Qt.Vertical   // 上下に分割する

            // パッケージリスト (上部)
            // パッチタブ選択時はパッチ一覧に切り替える
            StackLayout {
                SplitView.fillHeight: true                                                // 残りの高さをすべて使う
                SplitView.minimumHeight: 200                                              // 一覧の最小高
                currentIndex: filterTabBar.currentIndex === root.patchesTabIndex ? 1 : 0  // パッチタブ選択中のみパッチ一覧

                // パッケージ一覧
                PackageTable {
                    id: packageTablePane  // パッケージ一覧の参照名

                    darkMode: root.darkMode  // 一覧の配色に使うテーマ
                }

                // パッチ一覧
                PatchView {
                    id: patchView  // パッチ一覧の参照名
                }
            }

            // パッケージ詳細 (下部)
            PackageDetailsPane {
                id: detailPane  // パッケージ詳細の参照名
            }
        }
    }

    // --- フッタ ---
    footer: ToolBar {
        // フッタの部品を横に並べる
        RowLayout {
            anchors.fill: parent    // フッタ全体に広げる
            anchors.leftMargin: 8   // 左端の余白
            anchors.rightMargin: 8  // 右端の余白

            // 処理中を示す回転表示 (PackageController::busyの間だけ表示する)
            BusyIndicator {
                Layout.preferredWidth: 20        // 表示の幅
                Layout.preferredHeight: 20       // 表示の高さ
                visible: PackageController.busy  // 処理中のみ表示する
                running: PackageController.busy  // 処理中のみ回転させる
            }

            // バックエンドからの状態メッセージ
            Label {
                Layout.fillWidth: true                 // 残りの幅をすべて使う
                text: PackageController.statusMessage  // PackageController::statusMessageの状態メッセージ
                textFormat: Text.PlainText             // HTMLとして解釈させない
            }

            // バックエンドの操作フェーズ表示 (operationState)
            Label {
                Layout.alignment: Qt.AlignVCenter                                // フッタの高さ方向の中央に配置する
                visible: text.length > 0                                         // 表示するフェーズがある場合のみ表示する (待機中は空文字列)
                opacity: 0.7                                                     // 状態メッセージより控えめに表示する
                text: root.operationStageText(PackageController.operationState)  // 現在の操作フェーズ
            }

            // 変更を破棄して終了するボタン
            Button {
                text: qsTr("Cancel")  // ボタンの表示名

                // 起動時に保存した状態へ戻してから終了する
                onClicked: {
                    PackageController.restoreState()
                    Qt.quit()
                }
            }

            // 変更を適用するボタン
            Button {
                enabled: !PackageController.busy && PackageController.hasPendingChanges  // 処理中や変更が無い場合は押せないようにする
                text: qsTr("Apply")                                                      // ボタンの表示名
                highlighted: true                                                        // 主要な操作として強調表示する

                // 変更の適用フローを開始する
                onClicked: {
                    applyChanges()
                }
            }
        }
    }

    // --- ダイアログ ---

    // ソルバー実行中ウェイトダイアログ
    Dialogs.SolverWaitDialog {
        id: solverWaitDialog  // ソルバー待機ダイアログの参照名

        // 他のモーダル表示中は重ねて表示しない
        blocked: startupDialog.visible || commitProgressDialog.visible || conflictDialog.visible || refreshOverlay.visible
    }

    // コミット進捗ダイアログ (myrlyn風To Do / Downloads / Doing / Done)
    Dialogs.CommitProgressDialog {
        id: commitProgressDialog  // コミット進捗ダイアログの参照名
    }

    // トランザクション結果ダイアログ
    Dialogs.InstallationResultDialog {
        id: resultDialog  // トランザクション結果ダイアログの参照名

        // 結果を反映するため、検索をやり直して一覧を更新する
        onRefreshRequested: {
            searchView.performSearch()
        }
    }

    // コンフリクト解決ダイアログ
    Dialogs.ConflictDialog {
        id: conflictDialog  // コンフリクト解決ダイアログの参照名

        // 解決策の適用後に、変更の適用フローを再開する
        onApplyChangesRequested: {
            root.applyChanges()
        }

        // 適用フローを中断したため、保留中の適用フラグを解除する
        onApplyPendingCommitCleared: {
            root.applyPendingCommit = false
        }
    }

    // 更新情報ダイアログ
    Dialogs.UpdateInfoDialog {
        id: updateInfoDialog  // 情報ダイアログの参照名 (showInfo()で使用する)
    }

    // コミット前の確認ダイアログ (変更内容・ディスク使用量・危険な変更を表示する)
    Dialogs.CommitConfirmDialog {
        id: commitConfirmDialog  // コミット確認ダイアログの参照名

        diskUsageModel: PackageController.diskUsage               // PackageController::diskUsageのディスク使用量
        transactionSummary: PackageController.transactionSummary  // PackageController::transactionSummaryの変更概要

        // 確認後にコミットを開始する
        onAccepted: {
            // コミット実行 (非同期 : 進捗はシグナルで通知される)
            var pending = PackageController.pendingChanges
            commitProgressDialog.initFromPendingChanges(pending)
            commitProgressDialog.open()
            PackageController.commitAsync()
        }
    }

    // 未信頼リポジトリ署名鍵の確認ダイアログ
    Dialogs.KeyTrustDialog {
        id: keyTrustDialog  // 署名鍵確認ダイアログの参照名
    }

    // qZypperの情報ダイアログ
    AboutqZypperDialog {
        id: aboutqZypperDialog  // qZypper情報ダイアログの参照名
    }

    // Qtの情報ダイアログ
    AboutQtDialog {
        id: aboutQtDialog  // Qt情報ダイアログの参照名
    }

    // リポジトリ管理はrepoDrawer (上部で定義) に移行済み

    // --- 起動時ウェイトダイアログ ---
    Dialogs.StartupDialog {
        id: startupDialog  // 起動待機ダイアログの参照名
    }

    // --- パッケージ操作の対話プロンプト ---
    Dialogs.PromptDialog {
        id: promptDialog  // 対話プロンプトダイアログの参照名
    }

    // --- Zyppロック検出ダイアログ ---
    Dialogs.PackageLockDialog {
        id: lockDialog  // Zyppロック検出ダイアログの参照名

        // ロックが解放された後に、中断した操作をやり直す
        onRetryRequested: {
            PackageController.retryOperation()
        }
    }

    // --- セッション終了後の再開始ダイアログ ---
    Dialogs.SessionEndedDialog {
        id: sessionEndedDialog  // セッション再開始ダイアログの参照名

        // 新しいセッションを開始する
        onStartSessionRequested: {
            // 起動時と同じく、待機ダイアログの描画後にバックエンドの起動・接続を行う
            startupDialog.open()
            initTimer.start()
        }

        // アプリケーションを終了する
        onQuitRequested: {
            Qt.quit()
        }
    }

    // --- 情報トースト (PoolReloaded等の通知) ---
    InfoToast {
        id: infoToast  // 情報トーストの参照名
    }

    // ディストリビューション更新の確認ダイアログ
    Dialogs.DistUpgradeDialog {
        id: distUpgradeDialog  // ディストリビューション更新ダイアログの参照名

        // 確認後にディストリビューション更新を開始する (結果はonDistUpgradeFinishedで受け取る)
        onConfirmed: function(allowVendorChange) {
            PackageController.distUpgrade(allowVendorChange)
        }
    }

    // パッケージロックの管理ダイアログ
    Dialogs.LocksDialog {
        id: locksDialog  // ロック管理ダイアログの参照名
    }

    // 操作履歴ダイアログ
    Dialogs.HistoryDialog {
        id: historyDialog  // 操作履歴ダイアログの参照名
    }

    // ソルバーテストケースの保存先を表示するダイアログ
    Dialogs.SolverTestcaseDialog {
        id: testcaseDialog  // テストケースダイアログの参照名
    }

    // バックエンド/コントローラのエラー通知
    Dialog {
        id: errorDialog  // エラーダイアログの参照名

        property string message: ""  // エラー本文を一時保持する

        anchors.centerIn: parent                // ウィンドウの中央に表示する
        width: Math.min(root.width * 0.7, 520)  // ウィンドウ幅の70% (最大520px)
        title: qsTr("Error")                    // ダイアログのタイトル
        standardButtons: Dialog.Ok              // [OK]ボタンのみ表示する
        modal: true                             // 表示中は背後の画面を操作できないようにする

        // エラー本文 (バックエンド由来の文字列のためPlainText)
        Label {
            anchors.left: parent.left    // ダイアログの左端に合わせる
            anchors.right: parent.right  // ダイアログの右端に合わせる
            text: errorDialog.message    // エラー本文
            textFormat: Text.PlainText   // HTMLとして解釈させない
            wrapMode: Text.WordWrap      // 長い本文は単語単位で折り返す
        }
    }

    // ローカルRPMファイル選択 → バックエンドで一時リポジトリ経由の依存解決
    FileDialog {
        id: localRpmFileDialog  // RPMファイル選択ダイアログの参照名

        title: qsTr("Select RPM Files to Install")                          // ダイアログのタイトル
        fileMode: FileDialog.OpenFiles                                      // 複数のファイルを選択できるようにする
        nameFilters: [qsTr("RPM packages (*.rpm)"), qsTr("All files (*)")]  // ファイルの種類の絞り込み

        // 選択したRPMファイルの依存解決を開始する (結果はonLocalRpmsFinishedで受け取る)
        onAccepted: {
            PackageController.installLocalRpms(selectedFiles)
        }
    }

    // バックエンドの初期化を遅延させるタイマ
    // 起動待機ダイアログを先に描画させるため、初期化 (バックエンドの起動・接続) を少し遅らせて開始する
    Timer {
        id: initTimer  // 初期化開始タイマの参照名

        interval: 50  // 初期化を開始するまでの待ち時間 (ミリ秒)

        // バックエンドの起動・接続を開始する (完了はonInitializedChangedで受け取る)
        onTriggered: {
            PackageController.initialize()
        }
    }

    // ローカルRPMの依存解決結果のハンドリング
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief ローカルRPMの依存解決結果をエラー、警告、適用フローへ振り分ける
         * @param result 依存解決の結果
         * @note 戻り値は無く、結果が無効なときは早期に戻る
         * @note 失敗時はerrorDialog、未署名時はinfoToast、それ以外はhandleSolverResultを使う
         */
        function onLocalRpmsFinished(result) {
            if (!result || result.backendCrashed) {
                return
            }

            // 依存関係の問題ではない失敗 (ファイルを読めない等) はエラーとして表示する
            if (!result.success && (!result.problems || result.problems.length === 0)) {
                errorDialog.message = result.errorMessage || qsTr("The RPM files could not be prepared for installation.")
                errorDialog.open()
                return
            }

            var unsigned = result.unsignedPackages || []

            // 署名の無いパッケージが含まれる場合は警告する
            if (result.success && unsigned.length > 0) {
                infoToast.show(qsTr("Warning: unsigned packages: %1").arg(unsigned.join(", ")))
            }

            root.handleSolverResult(result, qsTr("Install Local RPM Files"), qsTr("The selected packages are already installed."))
        }
    }

    // チェックボックス操作時のコンフリクト自動表示
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief チェック操作で検出した依存競合を解決ダイアログへ渡す
         * @param problems 検出した競合の一覧
         * @note conflictDialogへ競合を設定して表示する
         */
        function onConflictsDetected(problems) {
            conflictDialog.fromApply = false
            conflictDialog.problems = problems
            conflictDialog.open()
        }
    }

    // バックエンドが未信頼の署名鍵を検出 → 確認ダイアログへキュー投入
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief 未信頼鍵の確認要求を順番待ちダイアログへ追加する
         * @param keyInfo 確認が必要な鍵の情報
         * @note keyTrustDialogへキュー投入して順に確認させる
         */
        function onUntrustedKeyDetected(keyInfo) {
            keyTrustDialog.enqueue(keyInfo)
        }
    }

    // Zyppロック検出 → フレンドリーダイアログ
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief 検出したZyppロック情報を再試行可能なダイアログへ表示する
         * @param info ロックを保持するプロセスの情報
         * @note lockDialogへ保持者を設定して表示する
         */
        function onPackageLocked(info) {
            lockDialog.lockerName = info.name || ""
            lockDialog.lockerPid = info.pid || 0
            lockDialog.lockerMessage = info.message || ""
            lockDialog.open()
        }
    }

    // プール再読込 (外部変更) → 再取得+情報トースト
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief 外部変更後に再読込したプールをトーストで知らせる
         * @note infoToastで再読込の完了を通知する
         */
        function onPoolReloaded() {
            infoToast.show(qsTr("Package database was changed externally and reloaded"))
        }
    }

    // 編集セッション解放 → 再開始状態へ (終了中なら終了)
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief 解放結果に応じて終了を続行し、通常時はセッション再開を促す
         * @param ok セッション解放が成功したか
         * @note 戻り値は無く、終了中はQt.quit後に早期に戻る
         * @note 終了中はタイマーを止めて終了し、通常時はsessionEndedDialogを開く
         */
        function onSessionReleased(ok) {
            // ウィンドウを閉じる途中の場合は、応答を受け取れたので待たずに終了する
            if (root.quitting) {
                quitTimer.stop()
                Qt.quit()
                return
            }
            sessionEndedDialog.open()
        }
    }

    // 非同期D-Bus呼び出しの結果ハンドリング
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief 初期化完了時に起動画面を閉じ、リポジトリ読込と現在状態の保存を開始する
         * @note startupDialogを閉じ、loadReposとsaveStateを呼び出す
         */
        function onInitializedChanged() {
            if (PackageController.initialized) {
                // 初期化完了 — 起動ウェイトを解除して一覧を読み込む
                startupDialog.close()
                PackageController.loadRepos()
                PackageController.saveState()
            }
        }

        /**
         * @brief バックエンド接続の復旧または切断時に進捗画面と適用状態を整合させる
         * @note 戻り値は無く、復旧時や終了中は早期に戻る
         * @note 復旧時は再開始ダイアログを閉じ、切断時は適用フラグを解除して進捗を閉じる
         */
        function onConnectedChanged() {
            if (PackageController.connected) {
                root.sessionWasConnected = true

                // 自動再接続できた場合は再開始ダイアログを閉じる
                sessionEndedDialog.close()
                return
            }

            // 切断時は依存解決の応答が破棄されresolveFinishedが届かないため、保留中の適用フラグを解除する
            root.applyPendingCommit = false

            // 一度も接続していない (起動時の接続失敗) 場合は、再開始ダイアログを出さない
            if (!root.sessionWasConnected) {
                return
            }

            root.sessionWasConnected = false

            // 終了処理中の切断は想定どおりのため何もしない
            if (root.quitting) {
                return
            }

            // 切断時は実行中のリフレッシュ/適用の完了通知が届かないため、進捗モーダルを閉じて操作を戻す
            const commitInterrupted = commitProgressDialog.visible
            refreshOverlay.close()
            commitProgressDialog.close()

            if (commitInterrupted) {
                root.showInfo(qsTr("Backend disconnected"),
                              qsTr("The connection to the backend was lost while applying changes. The result is unknown. Start a new session and check the package state."))
            }

            // アイドル終了などでバックエンドが消えた場合、ユーザー操作で新しいセッションを開始できるようにする
            if (!startupDialog.visible) {
                sessionEndedDialog.open()
            }
        }

        /**
         * @brief 初期化失敗でbusyが解除された場合に起動待機画面を閉じる
         * @note 未初期化のままbusyがfalseになったときstartupDialogを閉じる
         */
        function onBusyChanged() {
            // 初期化に失敗した場合 (busy=falseかつ未初期化) はウェイトを解除してエラー表示に戻す
            if (startupDialog.opened && !PackageController.busy && !PackageController.initialized) {
                startupDialog.close()
            }
        }

        /**
         * @brief 全更新の選択結果を情報または競合解決画面に表示する
         * @param result 全更新の選択結果
         * @note 戻り値は無く、結果が無効なときは早期に戻る
         * @note 変更なしはshowInfo、成功時はshowInfo、失敗時はconflictDialogを表示する
         */
        function onUpdateAllFinished(result) {
            if (!result || result.backendCrashed || result.busError) {
                return
            }

            if (result.success) {
                var pending = PackageController.pendingChanges

                if (pending.length === 0) {
                    // 更新可能なパッケージなし — 情報ダイアログ表示
                    root.showInfo(qsTr("Update All Packages"), qsTr("All packages are up to date."))
                    return
                }

                // パッケージがマークされた — 確認ダイアログ表示
                root.showInfo(qsTr("Update All Packages"), qsTr("%1 package(s) will be updated.\nClick \"Apply Changes\" to proceed.").arg(pending.length))
            }
            else {
                // コンフリクト解決ダイアログを表示
                conflictDialog.fromApply = true
                conflictDialog.problems = result.problems || []
                conflictDialog.open()
            }
        }

        /**
         * @brief 依存解決の成功時はコミット準備へ進み、失敗時は競合を表示する
         * @param result 依存解決の結果
         * @note 戻り値は無く、結果が無効なときは早期に戻る
         * @note 成功時はprepareCommit、失敗時はconflictDialogを呼び、保留フラグを解除する
         */
        function onResolveFinished(result) {
            if (!result || result.backendCrashed || result.busError || !PackageController.connected) {
                // 依存解決が進行せず終了した場合は保留中の適用フラグを解除する
                root.applyPendingCommit = false
                return
            }

            if (result.success) {
                // [Apply]起点の依存解決の場合のみ、コミット準備へ進む
                if (root.applyPendingCommit) {
                    root.applyPendingCommit = false
                    PackageController.prepareCommit()
                }
            }
            else {
                // コンフリクト解決ダイアログを表示
                conflictDialog.fromApply = root.applyPendingCommit
                root.applyPendingCommit = false
                conflictDialog.problems = result.problems || []
                conflictDialog.open()
            }
        }

        /**
         * @brief コミット準備完了後に危険変更と容量を取得して確認画面を開く
         * @param ok コミット準備が成功したか
         * @note 戻り値は無く、準備失敗時は早期に戻る
         * @note commitConfirmDialogを開き、ディスク使用量と概要を非同期で要求する
         */
        function onCommitPrepared(ok) {
            if (!ok) {
                return
            }

            // コミット確認ダイアログを表示 (ディスク使用量とリスク概要は非同期取得しバインディングで反映)
            commitConfirmDialog.summaryModel = Util.buildSummaryModel()
            commitConfirmDialog.summaryPending = true
            PackageController.requestDiskUsage()
            PackageController.requestTransactionSummary()
            commitConfirmDialog.open()
        }

        /**
         * @brief 非同期のトランザクション概要取得完了を確認画面へ反映する
         * @note commitConfirmDialogのsummaryPendingを解除する
         */
        function onTransactionSummaryChanged() {
            commitConfirmDialog.summaryPending = false
        }

        /**
         * @brief ディストリビューション更新のソルバー結果を共通フローで扱う
         * @param result ディストリビューション更新のソルバー結果
         * @note handleSolverResultへ振り分ける
         */
        function onDistUpgradeFinished(result) {
            root.handleSolverResult(result, qsTr("Distribution Upgrade"), qsTr("The system is already up to date."))
        }

        /**
         * @brief 必要パッチの一括選択結果を確認案内または共通フローへ渡す
         * @param result パッチ一括選択の結果
         * @note 戻り値は無く、変更があるときはshowInfo後に早期に戻る
         * @note 変更がある時はshowInfo、それ以外はhandleSolverResultを使う
         */
        function onSelectPatchesFinished(result) {
            // パッチ一括選択は確認画面へ直行せず、一覧で選択内容を確認できるようにする
            if (result && result.success && !result.backendCrashed && root.summaryHasChanges(result.summary)) {
                root.showInfo(qsTr("Patches"), qsTr("Needed patches have been selected. Review the selection and click \"Apply\" to install them."))
                return
            }

            root.handleSolverResult(result, qsTr("Patches"), qsTr("No needed patches were found."))
        }

        /**
         * @brief システム検証のソルバー結果を共通フローで扱う
         * @param result システム検証のソルバー結果
         * @note handleSolverResultへ振り分ける
         */
        function onVerifyFinished(result) {
            root.handleSolverResult(result, qsTr("Verify System"), qsTr("The system is consistent. No changes are needed."))
        }

        /**
         * @brief 作成したソルバーテストケースの保存先を表示する
         * @param path 保存したテストケースのパス
         * @note testcaseDialogへパスを渡して表示する
         */
        function onSolverTestcaseCreated(path) {
            testcaseDialog.testcasePath = path
            testcaseDialog.open()
        }

        /**
         * @brief バックエンドエラー本文がある場合だけエラーダイアログを開く
         * @param errorMessage バックエンドが通知したエラー本文
         * @note 戻り値は無く、本文が空の時は早期に戻る
         * @note errorDialogへ本文を設定して表示する
         */
        function onErrorOccurred(errorMessage) {
            if (!errorMessage || errorMessage.length === 0) {
                return
            }

            errorDialog.message = errorMessage
            errorDialog.open()
        }

        /**
         * @brief リポジトリ追加成功時だけ開いている管理一覧を再読込する
         * @param success リポジトリ追加が成功したか
         * @note repoDrawerLoaderで読込中ならloadDataを呼び直す
         */
        function onAddRepoFinished(success) {
            if (success && repoDrawerLoader.item) {
                repoDrawerLoader.item.loadData()
            }
        }

        /**
         * @brief サービス追加成功時だけ開いている管理一覧を再読込する
         * @param success サービス追加が成功したか
         * @note repoDrawerLoaderで読込中ならloadDataを呼び直す
         */
        function onAddServiceFinished(success) {
            if (success && repoDrawerLoader.item) {
                repoDrawerLoader.item.loadData()
            }
        }
    }

    // 初期レイアウト確定後に配色、保存済みフィルタ、バックエンド初期化を順に適用する
    // バックエンドの初期化は時間がかかるため、起動待機ダイアログを開いてからinitTimerで遅延開始する
    Component.onCompleted: {
        Theme.darkMode = Qt.binding(() => root.darkMode)
        PackageController.applyDarkMode(root.darkMode)
        PackageController.packageProxy.statusMask = statusFilterPanel.statusMask
        startupDialog.open()
        initTimer.start()
    }
}
