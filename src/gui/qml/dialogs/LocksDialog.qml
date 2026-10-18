import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// パッケージロック管理ダイアログ。
// GetLocks 結果の一覧表示と、恒久ロックの追加/削除を行う。
// Polkit 認証はバックエンド側で実施され、拒否時は errorOccurred で通知される。
Dialog {
    id: locksDialog

    title: qsTr("Package Locks")
    anchors.centerIn: parent
    modal: true
    standardButtons: Dialog.Close
    width: Math.min((parent ? parent.width : 800) * 0.92, 920)
    height: Math.min((parent ? parent.height : 600) * 0.85, 640)

    // コンボの表示値とバックエンド値の対応
    readonly property var kindValues: ["package", "pattern", "patch", "product"]
    readonly property var matchValues: ["exact", "glob", "regex"]

    onOpened: PackageController.loadLocks()

    Connections {
        target: PackageController
        // 追加成功時は入力フォームをクリア
        function onLockFinished(success) {
            if (success) {
                lockNameField.clear()
                lockRepoField.clear()
                lockArchField.clear()
                lockVersionField.clear()
                lockCommentField.clear()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            text: qsTr("Persistent locks keep packages from being installed, updated, or removed by the solver.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            color: palette.placeholderText
            font.pixelSize: 11
        }

        // ---- ヘッダ行 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label { text: qsTr("Name"); Layout.preferredWidth: 150; font.bold: true }
            Label { text: qsTr("Type"); Layout.preferredWidth: 70; font.bold: true }
            Label { text: qsTr("Match"); Layout.preferredWidth: 55; font.bold: true }
            Label { text: qsTr("Repository"); Layout.preferredWidth: 100; font.bold: true }
            Label { text: qsTr("Arch"); Layout.preferredWidth: 60; font.bold: true }
            Label { text: qsTr("Version"); Layout.preferredWidth: 85; font.bold: true }
            Label { text: qsTr("Comment"); Layout.fillWidth: true; font.bold: true }
            Label {
                text: qsTr("Pkg")
                Layout.preferredWidth: 36
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Item { Layout.preferredWidth: 80 }
        }

        MenuSeparator { Layout.fillWidth: true }

        // ---- ロック一覧 ----
        ListView {
            id: locksList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 100
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: PackageController.locks
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: RowLayout {
                width: locksList.width - (locksList.ScrollBar.vertical.visible ? locksList.ScrollBar.vertical.width + 4 : 12)
                spacing: 6

                required property var modelData

                Label { text: modelData.name || ""; textFormat: Text.PlainText; Layout.preferredWidth: 150; elide: Text.ElideRight }
                Label { text: modelData.kind || ""; Layout.preferredWidth: 70; elide: Text.ElideRight }
                Label { text: modelData.match || ""; Layout.preferredWidth: 55; elide: Text.ElideRight }
                Label { text: modelData.repoAlias || ""; textFormat: Text.PlainText; Layout.preferredWidth: 100; elide: Text.ElideRight }
                Label { text: modelData.arch || ""; Layout.preferredWidth: 60; elide: Text.ElideRight }
                Label { text: modelData.version || ""; textFormat: Text.PlainText; Layout.preferredWidth: 85; elide: Text.ElideRight }
                Label { text: modelData.comment || ""; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight; color: palette.placeholderText }
                Label {
                    text: modelData.matchCount !== undefined ? String(modelData.matchCount) : ""
                    Layout.preferredWidth: 36
                    horizontalAlignment: Text.AlignHCenter
                }
                Button {
                    text: qsTr("Remove")
                    enabled: !PackageController.busy
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Remove lock %1").arg(modelData.name || "")
                    onClicked: PackageController.removeLock(modelData)
                }
            }
        }

        // 空状態
        Item {
            Layout.fillWidth: true
            visible: locksList.count === 0

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("No package locks defined")
                color: palette.placeholderText
                visible: !PackageController.busy
            }
        }

        // ---- 追加フォーム ----
        GroupBox {
            title: qsTr("Add Lock")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 4
                columnSpacing: 8
                rowSpacing: 6

                Label { text: qsTr("Name*") }
                TextField {
                    id: lockNameField
                    Layout.fillWidth: true
                    Layout.columnSpan: 3
                    placeholderText: qsTr("package / pattern / patch name")
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Lock name")
                }

                Label { text: qsTr("Type") }
                ComboBox {
                    id: lockKindCombo
                    model: [qsTr("package"), qsTr("pattern"), qsTr("patch"), qsTr("product")]
                    Layout.columnSpan: 3
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Lock entry type")
                }

                Label { text: qsTr("Match") }
                ComboBox {
                    id: lockMatchCombo
                    model: [qsTr("exact"), qsTr("glob"), qsTr("regex")]
                    Layout.columnSpan: 3
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Lock match mode")
                }

                Label { text: qsTr("Repository") }
                TextField {
                    id: lockRepoField
                    placeholderText: qsTr("optional")
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Repository alias")
                }
                Label { text: qsTr("Architecture") }
                TextField {
                    id: lockArchField
                    placeholderText: qsTr("optional")
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Architecture")
                }

                Label { text: qsTr("Version") }
                TextField {
                    id: lockVersionField
                    placeholderText: qsTr("optional")
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Version constraint")
                }
                Label { text: qsTr("Comment") }
                TextField {
                    id: lockCommentField
                    Layout.fillWidth: true
                    placeholderText: qsTr("optional")
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Lock comment")
                }

                Item { Layout.fillWidth: true; Layout.columnSpan: 3 }

                Button {
                    text: qsTr("Add Lock")
                    highlighted: true
                    enabled: lockNameField.text.length > 0 && !PackageController.busy
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Add package lock")
                    Accessible.description: qsTr("Create a persistent lock for the given name")
                    onClicked: PackageController.addLock({
                        name: lockNameField.text,
                        kind: locksDialog.kindValues[lockKindCombo.currentIndex],
                        match: locksDialog.matchValues[lockMatchCombo.currentIndex],
                        repoAlias: lockRepoField.text,
                        arch: lockArchField.text,
                        version: lockVersionField.text,
                        comment: lockCommentField.text
                    })
                }
            }
        }
    }
}
