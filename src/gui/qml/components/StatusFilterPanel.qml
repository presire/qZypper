pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// 「Show Packages with Status」フィルタパネル (Myrlyn の Secondary Filter → Status 相当)
// statusMask のビット n が 1 のとき、ステータス n のパッケージを表示する
Pane {
    id: root

    property int statusMask: PackageStatusInfo.defaultMask

    // ユーザ操作で statusMask が変更された
    signal maskEdited()
    // 「Refresh List」が押された (現在のマスクで表示集合を作り直す)
    signal refreshRequested()

    padding: 6

    // マスクを変更して maskEdited を発行する (値が変わらない場合は何もしない)
    function setMask(mask) {
        if (mask === statusMask) return
        statusMask = mask
        maskEdited()
    }

    // 指定ステータスのみを表示する
    function showOnly(status) {
        setMask(1 << status)
    }

    GroupBox {
        anchors.fill: parent
        title: qsTr("Show Packages with Status")

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            ScrollView {
                id: rowScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 48
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: rowScroll.availableWidth
                    spacing: 0

                    Repeater {
                        model: PackageStatusInfo.filterGroups

                        delegate: ColumnLayout {
                            id: groupItem

                            required property var modelData
                            required property int index

                            Layout.fillWidth: true
                            Layout.topMargin: index > 0 ? 8 : 0
                            spacing: 0

                            Repeater {
                                model: groupItem.modelData

                                delegate: CheckBox {
                                    id: statusCheck

                                    required property int modelData
                                    readonly property int status: modelData

                                    Layout.fillWidth: true
                                    topPadding: 2
                                    bottomPadding: 2
                                    text: PackageStatusInfo.label(status)
                                    checked: PackageStatusInfo.maskHas(root.statusMask, status)

                                    Accessible.name: text
                                    Accessible.description: qsTr("Middle-click on a status to show only this one")

                                    contentItem: Row {
                                        leftPadding: statusCheck.indicator.width + statusCheck.spacing
                                        spacing: 6

                                        StatusIcon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            status: statusCheck.status
                                            showToolTip: false
                                        }

                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: statusCheck.availableWidth - statusCheck.indicator.width
                                                   - statusCheck.spacing - 16 - parent.spacing
                                            text: statusCheck.text
                                            elide: Text.ElideRight
                                        }
                                    }

                                    onToggled: {
                                        var bit = 1 << status
                                        var mask = checked ? (root.statusMask | bit) : (root.statusMask & ~bit)
                                        // ユーザ操作で切れた checked のバインディングを戻す
                                        checked = Qt.binding(function() {
                                            return PackageStatusInfo.maskHas(root.statusMask, statusCheck.status)
                                        })
                                        root.setMask(mask)
                                    }

                                    // 中クリック: このステータスのみ表示 / 右クリック: メニュー (中クリックの代替)
                                    // CheckBox は中ボタンでもトグルするため、左ボタン以外は CheckBox より手前の MouseArea で受け取る
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.MiddleButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.MiddleButton)
                                                root.showOnly(statusCheck.status)
                                            else
                                                statusMenu.popup()
                                        }
                                    }

                                    // キーボード: Menu キー / Shift+F10 でメニューを開く
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Menu
                                                || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                            statusMenu.popup(statusCheck, 0, statusCheck.height)
                                            event.accepted = true
                                        }
                                    }

                                    Menu {
                                        id: statusMenu

                                        MenuItem {
                                            text: qsTr("Show Only This Status")
                                            onTriggered: root.showOnly(statusCheck.status)
                                        }

                                        MenuItem {
                                            text: qsTr("Show All Statuses")
                                            onTriggered: root.setMask(PackageStatusInfo.defaultMask)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Middle-click on a status to show only this one")
                wrapMode: Text.WordWrap
                color: palette.placeholderText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Defaults")
                    // 既にマスクが既定値の場合も表示集合を作り直す (sticky で残った行・隠れた行を反映)
                    onClicked: {
                        if (root.statusMask === PackageStatusInfo.defaultMask)
                            root.refreshRequested()
                        else
                            root.setMask(PackageStatusInfo.defaultMask)
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Refresh List")
                    onClicked: root.refreshRequested()
                }
            }
        }
    }
}
