pragma ComponentBehavior: Bound
/**
 * @file StatusFilterPanel.qml
 * @brief パッケージステータスで一覧を絞り込むフィルタパネル
 *
 * Main.qmlで使用され、PackageTableに表示する状態のビットマスクを操作する
 *
 * 入力 : statusMask (現在のステータスマスク)
 * 出力 : statusMaskChangedシグナル
 * 依存 : PackageStatusInfo
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// [Show Packages with Status]フィルタパネル (MyrlynのSecondary Filter → Status相当)
// statusMaskのビットnが1の時、ステータスnのパッケージを表示する
Pane {
    id: root  // パネル自身の参照名

    property int statusMask: PackageStatusInfo.defaultMask  // 表示対象を表すビットマスク

    // ユーザ操作でstatusMaskが変更された場合
    signal maskEdited()        // 親へフィルタ再適用を知らせる
    // [Refresh List]が押下された場合 (現在のマスクで表示集合を作り直す)
    signal refreshRequested()  // 親へ明示的な再読み込みを要求する

    /**
     * @brief マスクを変更し、実際に変わったときだけmaskEditedを発行する
     * @param mask 設定するステータスビットマスク
     * @note 戻り値は無く、同じ値では早期に戻る
     * @note 同じ値ではモデル更新を起こさない
     */
    function setMask(mask) {
        // 同じ値の場合は、モデル更新を起こさないように何もしない
        if (mask === statusMask) {
            return
        }

        statusMask = mask
        maskEdited()
    }

    /**
     * @brief 指定ステータスだけを表示するマスクに切り替える
     * @param status PackageStatusの数値
     * @note setMaskを通じて親へ変更を通知する
     */
    function showOnly(status) {
        setMask(1 << status)
    }

    padding: 6  // パネル内側の余白

    // [Show Packages with Status]グループ枠
    GroupBox {
        anchors.fill: parent                      // パネル全体を使う
        title: qsTr("Show Packages with Status")  // グループ枠の見出し

        // ステータス一覧、操作説明、ボタン行を縦に並べる
        ColumnLayout {
            anchors.fill: parent  // グループ枠の内側全体を使う
            spacing: 6            // 各要素の縦方向の間隔

            // ステータスのチェックボックス一覧 (縦方向のみスクロールする)
            ScrollView {
                id: rowScroll  // スクロール領域の参照名

                Layout.fillWidth: true                            // 親の幅いっぱいに広げる
                Layout.fillHeight: true                           // 残りの高さをすべて使う
                Layout.minimumHeight: 48                          // 最低でも確保する高さ
                clip: true                                        // 領域外にはみ出した部分を描画しない
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff  // 横スクロールバーは表示しない

                // グループごとのチェックボックスを縦に並べる
                ColumnLayout {
                    width: rowScroll.availableWidth  // スクロール領域の表示幅に合わせる
                    spacing: 0                       // グループ間の間隔はLayout.topMarginで調整する

                    // ステータスのグループ (PackageStatusInfo.filterGroupsの要素ごとに生成する)
                    Repeater {
                        model: PackageStatusInfo.filterGroups  // PackageStatusInfoが定義するステータスのグループ一覧

                        // 1つのグループ (グループ内のステータスを縦に並べる)
                        delegate: ColumnLayout {
                            id: groupItem  // グループの参照名

                            required property var modelData  // filterGroups内のステータス配列
                            required property int index      // filterGroups内のグループ番号

                            Layout.fillWidth: true               // 親の幅いっぱいに広げる
                            Layout.topMargin: index > 0 ? 8 : 0  // 2番目以降のグループは上に間隔を空ける
                            spacing: 0                           // チェックボックス間の間隔

                            // グループ内の各ステータス
                            Repeater {
                                model: groupItem.modelData  // グループ内のステータス値の配列

                                // ステータスのチェックボックス (アイコン付き)
                                delegate: CheckBox {
                                    id: statusCheck  // チェックボックスの参照名

                                    required property int modelData          // グループ内のPackageStatus値
                                    readonly property int status: modelData  // チェック状態に使うステータス値

                                    Layout.fillWidth: true                                       // 親の幅いっぱいに広げる
                                    topPadding: 2                                                // 上側の余白
                                    bottomPadding: 2                                             // 下側の余白
                                    text: PackageStatusInfo.label(status)                        // ステータスの表示名
                                    checked: PackageStatusInfo.maskHas(root.statusMask, status)  // マスクにこのステータスが含まれるか
                                    Accessible.name: text                                        // スクリーンリーダー向けの名前
                                    // スクリーンリーダー向けの操作説明
                                    Accessible.description: qsTr("Middle-click on a status to show only this one")

                                    // チェック状態が変わったので、マスクのビットを更新する
                                    onToggled: {
                                        var bit = 1 << status
                                        var mask = checked ? (root.statusMask | bit) : (root.statusMask & ~bit)
                                        // ユーザ操作で切れたcheckedのバインディングを戻す
                                        checked = Qt.binding(function() {
                                            return PackageStatusInfo.maskHas(root.statusMask, statusCheck.status)
                                        })
                                        root.setMask(mask)
                                    }

                                    // キーボード : [Menu]キー / [Shift]+[F10]キーでメニューを開く
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Menu
                                                || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                            statusMenu.popup(statusCheck, 0, statusCheck.height)
                                            event.accepted = true
                                        }
                                    }

                                    // チェックボックスの表示内容 (ステータスアイコンと表示名)
                                    contentItem: Row {
                                        // チェック欄の右側から表示する
                                        leftPadding: statusCheck.indicator.width + statusCheck.spacing
                                        spacing: 6  // アイコンと表示名の間隔

                                        // ステータスアイコン (ツールチップはチェックボックス側で不要なため表示しない)
                                        StatusIcon {
                                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                            status: statusCheck.status                     // 表示するステータス値
                                            showToolTip: false                             // ツールチップを表示しない
                                        }

                                        // ステータスの表示名
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                            // チェック欄、アイコン(16px)、間隔を除いた残りの幅
                                            width: statusCheck.availableWidth - statusCheck.indicator.width
                                                   - statusCheck.spacing - 16 - parent.spacing
                                            text: statusCheck.text                         // チェックボックスの文字列
                                            elide: Text.ElideRight                         // 幅に収まらない場合は末尾を省略する
                                        }
                                    }

                                    // 中クリック : このステータスのみ表示 / 右クリック : メニュー (中クリックの代替)
                                    // CheckBoxは中ボタンでもトグルするため、左ボタン以外はCheckBoxより手前のMouseAreaで受け取る
                                    MouseArea {
                                        anchors.fill: parent                               // チェックボックス全体で受け付ける
                                        acceptedButtons: Qt.MiddleButton | Qt.RightButton  // 中ボタンと右ボタンのみ受け付ける

                                        // 中クリックはこのステータスのみ表示し、右クリックはメニューを開く
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.MiddleButton) {
                                                root.showOnly(statusCheck.status)
                                            }
                                            else {
                                                statusMenu.popup()
                                            }
                                        }
                                    }

                                    // ステータスのコンテキストメニュー
                                    Menu {
                                        id: statusMenu  // コンテキストメニューの参照名

                                        // [Show Only This Status]メニュー
                                        MenuItem {
                                            text: qsTr("Show Only This Status")  // メニューの文字列

                                            // このステータスのみ表示する
                                            onTriggered: {
                                                root.showOnly(statusCheck.status)
                                            }
                                        }

                                        // [Show All Statuses]メニュー
                                        MenuItem {
                                            text: qsTr("Show All Statuses")  // メニューの文字列

                                            // 既定のマスクに戻す
                                            onTriggered: {
                                                root.setMask(PackageStatusInfo.defaultMask)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 中クリック操作の説明
            Label {
                Layout.fillWidth: true                                        // 親の幅いっぱいに広げる
                text: qsTr("Middle-click on a status to show only this one")  // 操作説明の文字列
                wrapMode: Text.WordWrap                                       // 幅に収まらない場合は単語単位で折り返す
                color: palette.placeholderText                                // 補足情報として控えめな文字色
            }

            // [Defaults]ボタンと[Refresh List]ボタンの行
            RowLayout {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる
                spacing: 6              // ボタンの間隔

                // [Defaults]ボタン
                Button {
                    Layout.fillWidth: true  // 行の幅を等分する
                    text: qsTr("Defaults")  // ボタンの文字列

                    // 既にマスクが既定値の場合も表示集合を作り直す (stickyで残った行・隠れた行を反映)
                    onClicked: {
                        if (root.statusMask === PackageStatusInfo.defaultMask) {
                            root.refreshRequested()
                        }
                        else {
                            root.setMask(PackageStatusInfo.defaultMask)
                        }
                    }
                }

                // [Refresh List]ボタン
                Button {
                    Layout.fillWidth: true      // 行の幅を等分する
                    text: qsTr("Refresh List")  // ボタンの文字列

                    // 現在のマスクで表示集合を作り直すよう親へ要求する
                    onClicked: {
                        root.refreshRequested()
                    }
                }
            }
        }
    }
}
