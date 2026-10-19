/**
 * @file NavDrawer.qml
 * @brief アプリの主要画面へ移動するナビゲーションドロワー
 *
 * Main.qmlで使用され、画面選択と各種管理ダイアログを開く
 *
 * 入力 : 現在の画面状態と操作コールバック
 * 出力 : 画面選択と管理操作の要求
 * 依存 : PackageController (パッケージ操作と状態)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// ハンバーガーメニュー (ナビゲーションDrawer)
Drawer {
    id: navDrawer  // ドロワー自身の参照名

    // 各操作はMain側で処理するためシグナルで通知する
    signal updateAllRequested()         // 全パッケージ更新の要求
    signal applyChangesRequested()      // 保留中変更の適用要求
    signal manageReposRequested()       // リポジトリ管理表示の要求
    signal refreshReposRequested()      // リポジトリ更新の要求
    signal aboutqZypperRequested()      // アプリ情報表示の要求
    signal aboutQtRequested()           // Qt情報表示の要求
    signal endSessionRequested()        // バックエンドセッション終了の要求
    signal distUpgradeRequested()       // ディストリビューション更新の要求
    signal packageLocksRequested()      // パッケージロック管理の要求
    signal historyRequested()           // 操作履歴表示の要求
    signal verifySystemRequested()      // システム整合性確認の要求
    signal installLocalRpmsRequested()  // ローカルRPMインストールの要求

    width: 300                            // ドロワーの幅
    height: parent ? parent.height : 600  // 親の高さいっぱい (親が未確定のときのフォールバック)
    edge: Qt.LeftEdge                     // 画面の左端から開く

    // メニュー全体 (ヘッダと各セクションを縦に並べる)
    ColumnLayout {
        anchors.fill: parent  // ドロワー全体を使う
        spacing: 0            // 各要素の間隔は各要素の余白で調整する

        // ヘッダ
        Pane {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            padding: 16             // ヘッダ内側の余白

            // ヘッダの背景 (選択色で塗りつぶす)
            background: Rectangle {
                color: palette.highlight  // 選択色
            }

            // アプリ名
            Label {
                text: "qZypper"                 // アプリ名の文字列
                color: palette.highlightedText  // 選択色の上で読みやすい文字色
                font.pixelSize: 18              // 文字サイズ
                font.bold: true                 // 太字で表示する
            }
        }

        // --- パッケージセクション ---
        Label {
            Layout.fillWidth: true          // 親の幅いっぱいに広げる
            Layout.topMargin: 12            // 上側の間隔
            Layout.leftMargin: 16           // 左側の間隔
            Layout.bottomMargin: 4          // 下側の間隔
            text: qsTr("Packages")          // セクション見出しの文字列
            color: palette.placeholderText  // 見出しとして控えめな文字色
            font.bold: true                 // 太字で表示する
            font.pixelSize: 11              // 文字サイズ
        }

        // [Update All Packages]メニュー
        ItemDelegate {
            Layout.fillWidth: true               // 親の幅いっぱいに広げる
            enabled: !PackageController.busy     // 実行中の選択状態に重ねて更新要求を送らない
            text: qsTr("Update All Packages")    // メニューの文字列
            icon.name: "system-software-update"  // テーマのアイコン名
            icon.color: palette.text             // アイコンを文字色で着色する

            // ドロワーを閉じて、全パッケージ更新をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.updateAllRequested()
            }
        }

        // [Distribution Upgrade]メニュー
        ItemDelegate {
            Layout.fillWidth: true                         // 親の幅いっぱいに広げる
            enabled: !PackageController.busy               // 依存解決やコミット中の更新競合を防ぐ
            text: qsTr("Distribution Upgrade")             // メニューの文字列
            icon.name: "package-upgrade"                   // テーマのアイコン名
            icon.color: palette.text                       // アイコンを文字色で着色する
            Accessible.name: qsTr("Distribution Upgrade")  // スクリーンリーダー向けの名前
            // スクリーンリーダー向けの説明
            Accessible.description: qsTr("Upgrade the whole distribution, optionally allowing vendor changes")

            // ドロワーを閉じて、ディストリビューション更新をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.distUpgradeRequested()
            }
        }

        // [Apply Changes]メニュー
        ItemDelegate {
            Layout.fillWidth: true                                                   // 親の幅いっぱいに広げる
            enabled: !PackageController.busy && PackageController.hasPendingChanges  // 実行中と変更なしでは適用できない
            text: qsTr("Apply Changes")                                              // メニューの文字列
            icon.name: "dialog-ok-apply"                                             // テーマのアイコン名
            icon.color: palette.text                                                 // アイコンを文字色で着色する

            // ドロワーを閉じて、保留中変更の適用をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.applyChangesRequested()
            }
        }

        // 区切り線
        MenuSeparator {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
        }

        // --- 設定セクション ---
        Label {
            Layout.fillWidth: true          // 親の幅いっぱいに広げる
            Layout.topMargin: 8             // 上側の間隔
            Layout.leftMargin: 16           // 左側の間隔
            Layout.bottomMargin: 4          // 下側の間隔
            text: qsTr("Settings")          // セクション見出しの文字列
            color: palette.placeholderText  // 見出しとして控えめな文字色
            font.bold: true                 // 太字で表示する
            font.pixelSize: 11              // 文字サイズ
        }

        // [Manage Repositories]メニュー
        ItemDelegate {
            Layout.fillWidth: true             // 親の幅いっぱいに広げる
            text: qsTr("Manage Repositories")  // メニューの文字列
            icon.name: "repository"            // テーマのアイコン名
            icon.color: palette.text           // アイコンを文字色で着色する

            // ドロワーを閉じて、リポジトリ管理の表示をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.manageReposRequested()
            }
        }

        // [Refresh Repositories]メニュー
        ItemDelegate {
            Layout.fillWidth: true              // 親の幅いっぱいに広げる
            enabled: !PackageController.busy    // 既存のリフレッシュ完了通知を取り違えない
            text: qsTr("Refresh Repositories")  // メニューの文字列
            icon.name: "view-refresh"           // テーマのアイコン名
            icon.color: palette.text            // アイコンを文字色で着色する

            // ドロワーを閉じて、リポジトリ更新をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.refreshReposRequested()
            }
        }

        // [Install Local RPM Files...]メニュー
        ItemDelegate {
            Layout.fillWidth: true                            // 親の幅いっぱいに広げる
            enabled: !PackageController.busy                  // パッケージ操作中に新しい依存解決を開始しない
            text: qsTr("Install Local RPM Files...")          // メニューの文字列
            icon.name: "application-x-rpm"                    // テーマのアイコン名
            icon.color: palette.text                          // アイコンを文字色で着色する
            Accessible.name: qsTr("Install Local RPM Files")  // スクリーンリーダー向けの名前
            // スクリーンリーダー向けの説明
            Accessible.description: qsTr("Select RPM files on this computer and install them with dependency resolution")

            // ドロワーを閉じて、ローカルRPMのインストールをMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.installLocalRpmsRequested()
            }
        }

        // [Package Locks...]メニュー
        ItemDelegate {
            Layout.fillWidth: true                                                    // 親の幅いっぱいに広げる
            text: qsTr("Package Locks...")                                            // メニューの文字列
            icon.name: "changes-prevent"                                              // テーマのアイコン名
            icon.color: palette.text                                                  // アイコンを文字色で着色する
            Accessible.name: qsTr("Package Locks")                                    // スクリーンリーダー向けの名前
            Accessible.description: qsTr("View and manage persistent package locks")  // スクリーンリーダー向けの説明

            // ドロワーを閉じて、パッケージロック管理をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.packageLocksRequested()
            }
        }

        // [Installation History...]メニュー
        ItemDelegate {
            Layout.fillWidth: true                                                              // 親の幅いっぱいに広げる
            text: qsTr("Installation History...")                                               // メニューの文字列
            icon.name: "document-open-recent"                                                   // テーマのアイコン名
            icon.color: palette.text                                                            // アイコンを文字色で着色する
            Accessible.name: qsTr("Installation History")                                       // スクリーンリーダー向けの名前
            Accessible.description: qsTr("Browse the libzypp history of package transactions")  // スクリーンリーダー向けの説明

            // ドロワーを閉じて、操作履歴の表示をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.historyRequested()
            }
        }

        // [Verify System]メニュー
        ItemDelegate {
            Layout.fillWidth: true                  // 親の幅いっぱいに広げる
            enabled: !PackageController.busy        // 実行中のトランザクションと検証を同時に走らせない
            text: qsTr("Verify System")             // メニューの文字列
            icon.name: "dialog-ok"                  // テーマのアイコン名
            icon.color: palette.text                // アイコンを文字色で着色する
            Accessible.name: qsTr("Verify System")  // スクリーンリーダー向けの名前
            // スクリーンリーダー向けの説明
            Accessible.description: qsTr("Verify system integrity and repair changed or missing files")

            // ドロワーを閉じて、システム整合性確認をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.verifySystemRequested()
            }
        }

        // 区切り線
        MenuSeparator {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
        }

        // --- ヘルプセクション ---
        Label {
            Layout.fillWidth: true          // 親の幅いっぱいに広げる
            Layout.topMargin: 8             // 上側の間隔
            Layout.leftMargin: 16           // 左側の間隔
            Layout.bottomMargin: 4          // 下側の間隔
            text: qsTr("Help")              // セクション見出しの文字列
            color: palette.placeholderText  // 見出しとして控えめな文字色
            font.bold: true                 // 太字で表示する
            font.pixelSize: 11              // 文字サイズ
        }

        // [About qZypper]メニュー
        ItemDelegate {
            Layout.fillWidth: true       // 親の幅いっぱいに広げる
            text: qsTr("About qZypper")  // メニューの文字列
            icon.name: "help-about"      // テーマのアイコン名
            icon.color: palette.text     // アイコンを文字色で着色する

            // ドロワーを閉じて、アプリ情報の表示をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.aboutqZypperRequested()
            }
        }

        // [About Qt]メニュー
        ItemDelegate {
            Layout.fillWidth: true    // 親の幅いっぱいに広げる
            text: qsTr("About Qt")    // メニューの文字列
            icon.name: "qt"           // テーマのアイコン名
            icon.color: palette.text  // アイコンを文字色で着色する

            // ドロワーを閉じて、Qt情報の表示をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.aboutQtRequested()
            }
        }

        // 終了セクションをドロワーの下端へ寄せるための空白
        Item {
            Layout.fillHeight: true  // 残りの高さをすべて使う
        }

        // --- 終了 ---
        // 区切り線
        MenuSeparator {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
        }

        // [End Editing Session]メニュー
        ItemDelegate {
            Layout.fillWidth: true                  // 親の幅いっぱいに広げる
            enabled: PackageController.initialized  // 未開始のセッションには解放要求を送らない
            text: qsTr("End Editing Session")       // メニューの文字列
            icon.name: "process-stop"               // テーマのアイコン名
            icon.color: palette.text                // アイコンを文字色で着色する

            // ドロワーを閉じて、バックエンドセッションの終了をMain側へ要求する
            onClicked: {
                navDrawer.close()
                navDrawer.endSessionRequested()
            }
        }

        // [Quit]メニュー
        ItemDelegate {
            Layout.fillWidth: true         // 親の幅いっぱいに広げる
            text: qsTr("Quit")             // メニューの文字列
            icon.name: "application-exit"  // テーマのアイコン名
            icon.color: palette.text       // アイコンを文字色で着色する

            // アプリケーションを終了する
            onClicked: {
                Qt.quit()
            }
        }
    }
}
