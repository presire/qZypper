/**
 * @file UpdatesView.qml
 * @brief 更新可能なパッケージの件数と更新操作をまとめて表示するビュー
 *
 * Main.qmlの左ペインのStackLayout内で生成され、[Updates]タブの選択時に表示される
 * 更新可能数と最後のリフレッシュ日時を表示し、一覧表示・全更新・ディストリビューション更新・リフレッシュをMain側へ要求する
 * 全更新の後は、自動では更新できなかったパッケージの件数を表示する
 *
 * 入力 : active (このビューが表示中か)、listReady (右ペインの一覧へ更新可能パッケージを読み込めたか)
 * 出力 : showUpdatesRequested()、updateAllRequested()、distUpgradeRequested()、refreshRequested() シグナル
 * 依存 : PackageController (updateCount、repos、busy、connected、packageModel、updateAllFinished)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// [Updates]タブで表示するビュー本体
Pane {
    id: updatesView  // 更新ビュー自身の参照名

    property bool active: false         // [Updates]タブを表示中か (Main側が設定する)
    property bool listReady: false      // 右ペインの一覧へ更新可能パッケージを読み込めたか (Main側が設定する)
    property bool updateAllDone: false  // 表示中に全更新の選択が完了したか (未更新件数の表示判定に使う)
    property int modelRevision: 0       // 一覧モデルの変更回数 (未更新件数の再計算に使う)

    // 最後のリフレッシュ日時 (有効なリポジトリのうち最も古い日時、epoch秒、0 : 不明)
    // PackageController::reposのlastRefreshから求め、reposChangedで再計算される
    readonly property real oldestRefresh: {
        var list = PackageController.repos
        var oldest = -1

        for (var i = 0; i < list.length; ++i) {
            if (!list[i].enabled) {
                continue
            }

            var t = Number(list[i].lastRefresh || 0)

            if (oldest < 0 || t < oldest) {
                oldest = t
            }
        }

        return oldest < 0 ? 0 : oldest
    }

    // 全更新の後も自動では更新されなかったパッケージ (状態がKeepInstalledのまま) の件数
    // countWithStatusはQ_INVOKABLEのため、一覧と変更予定の更新通知を参照して再計算させる
    readonly property int notUpdatedCount: {
        var revision = updatesView.modelRevision
        var generation = PackageController.listGeneration
        var pending = PackageController.pendingChanges

        // 一覧が更新可能パッケージでない場合は、無関係なパッケージを数えないよう0件とする
        if (!updatesView.updateAllDone || !updatesView.listReady) {
            return 0
        }

        return PackageController.packageModel.countWithStatus(3)
    }

    signal showUpdatesRequested()  // 更新可能パッケージの一覧表示の要求
    signal updateAllRequested()    // 全パッケージ更新の要求
    signal distUpgradeRequested()  // ディストリビューション更新の要求
    signal refreshRequested()      // リポジトリのリフレッシュの要求

    padding: 12  // 外周の余白

    // 非表示になったら未更新件数の表示を終える (再表示時は一覧を読み直すため)
    // 他のタブでは一覧が別の取得元に置き換わるため、読込済みの記録も消す
    onActiveChanged: {
        if (!active) {
            updateAllDone = false
            listReady = false
        }
    }

    // 件数・日時と操作ボタンを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ビュー全体に広げる
        spacing: 8            // 各行の間隔

        // 更新可能なパッケージの件数 (PackageController::updateCount、-1は未取得)
        Label {
            Layout.fillWidth: true  // ビューの幅に合わせる
            // 未取得の場合は件数の代わりに不明と表示する
            text: PackageController.updateCount < 0 ? qsTr("Number of updates: unknown")
                                                    : qsTr("%n update(s) available", "", PackageController.updateCount)
            font.bold: true         // 見出しとして太字にする
            wrapMode: Text.Wrap     // 幅が狭い場合は折り返す
        }

        // 最後にリポジトリをリフレッシュした日時 (有効なリポジトリのうち最も古いもの)
        Label {
            Layout.fillWidth: true  // ビューの幅に合わせる
            // 日時が不明 (0) の場合はunknownと表示する
            text: qsTr("Last refresh: %1").arg(updatesView.oldestRefresh > 0
                                               ? Qt.formatDateTime(new Date(updatesView.oldestRefresh * 1000), Locale.ShortFormat)
                                               : qsTr("unknown"))
            opacity: 0.8            // 補足情報として控えめに表示する
            wrapMode: Text.Wrap     // 幅が狭い場合は折り返す
        }

        // 全更新で自動的に更新されなかったパッケージの件数 (表示中に全更新した後で、1件以上ある場合のみ表示する)
        Label {
            Layout.fillWidth: true                                          // ビューの幅に合わせる
            visible: updatesView.active && updatesView.notUpdatedCount > 0  // 全更新後に未更新が残る場合のみ
            // 未更新のパッケージ件数
            text: qsTr("%n package(s) could not be updated automatically", "", updatesView.notUpdatedCount)
            color: "#F44336"                                                // 注意を促す赤色
            wrapMode: Text.Wrap                                             // 幅が狭い場合は折り返す
        }

        // [Show Updates]ボタン : 更新可能なパッケージを右ペインの一覧へ表示する
        Button {
            Layout.fillWidth: true                                           // ビューの幅に合わせる
            enabled: PackageController.connected && !PackageController.busy  // 接続中かつ非実行中のみ読み込める
            text: qsTr("Show Updates")                                       // ボタンの文字列

            // Main側へ一覧の読み込みを要求する
            onClicked: {
                updatesView.showUpdatesRequested()
            }
        }

        // [Update All]ボタン : 全パッケージの更新を選択する
        Button {
            Layout.fillWidth: true                                           // ビューの幅に合わせる
            enabled: PackageController.connected && !PackageController.busy  // 接続中かつ非実行中のみ選択できる
            text: qsTr("Update All")                                         // ボタンの文字列

            // Main側へ全更新を要求する (結果はPackageController::updateAllFinishedで受け取る)
            onClicked: {
                updatesView.updateAllRequested()
            }
        }

        // [Distribution Upgrade...]ボタン : ディストリビューション更新の確認ダイアログを開く
        Button {
            Layout.fillWidth: true                                           // ビューの幅に合わせる
            enabled: PackageController.connected && !PackageController.busy  // 接続中かつ非実行中のみ開始できる
            text: qsTr("Distribution Upgrade...")                            // ボタンの文字列

            // Main側へ確認ダイアログの表示を要求する
            onClicked: {
                updatesView.distUpgradeRequested()
            }
        }

        // [Refresh Repositories]ボタン : 全リポジトリをリフレッシュする
        Button {
            Layout.fillWidth: true                                           // ビューの幅に合わせる
            enabled: PackageController.connected && !PackageController.busy  // 接続中かつ非実行中のみ開始できる
            text: qsTr("Refresh Repositories")                               // ボタンの文字列

            // Main側へリフレッシュを要求する
            onClicked: {
                updatesView.refreshRequested()
            }
        }

        // 各行を上詰めにするための余白
        Item {
            Layout.fillHeight: true  // 残りの高さをすべて使う
        }
    }

    // 全更新の完了を受け取り、表示中であれば未更新件数の表示を始める
    Connections {
        target: PackageController  // シグナルの送信元 (PackageController)

        /**
         * @brief 更新可能パッケージの一覧を表示中に全更新の選択が成功したとき、未更新件数の表示を有効にする
         * @param result 全更新の選択結果
         * @note updateAllDoneを変更する
         */
        function onUpdateAllFinished(result) {
            if (updatesView.active && updatesView.listReady && result && result.success) {
                updatesView.updateAllDone = true
            }
        }
    }

    // 一覧モデルの変更を数え、未更新件数を再計算させる
    // (全更新後の状態の再取得は行の更新として届くため、変更予定の通知だけでは足りない)
    Connections {
        target: PackageController.packageModel  // PackageListModel

        /**
         * @brief 行データの変更で再計算を促す
         * @note modelRevisionを変更する
         */
        function onDataChanged() {
            updatesView.modelRevision++
        }

        /**
         * @brief 一覧の再構築で再計算を促す
         * @note modelRevisionを変更する
         */
        function onModelReset() {
            updatesView.modelRevision++
        }
    }
}
