import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui
import "../components" as Components

Pane {
    id: root
    padding: 0

    signal closeRequested()
    signal refreshAllRequested()

    property var originalRepos: []

    function loadData() {
        PackageController.loadRepos()
        PackageController.loadServices()
        originalRepos = JSON.parse(JSON.stringify(PackageController.repos))
    }

    function hasRepoChanges() {
        return JSON.stringify(PackageController.repos) !== JSON.stringify(originalRepos)
    }

    property bool repoTabActive: tabBar.currentIndex === 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ─── ヘッダ ────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.bottomMargin: 8

                Label {
                    text: qsTr("Repository Management")
                    font.pixelSize: 18
                    font.bold: true
                    Layout.fillWidth: true
                }

                ToolButton {
                    text: "✕"
                    font.pixelSize: 16
                    onClicked: root.closeRequested()

                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Close (Esc)")
                }
            }

            TabBar {
                id: tabBar
                Layout.fillWidth: true

                TabButton { text: qsTr("Repositories") }
                TabButton { text: qsTr("Services") }
            }
        }

        // ─── コンテンツ ──────────────────────────
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            currentIndex: tabBar.currentIndex

            // ─── リポジトリタブ ──────────────────
            ColumnLayout {
                spacing: 0

                Components.RepoTable {
                    id: repoTable
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: PackageController.repos

                    onEnabledToggled: function(alias, enabled) {
                        PackageController.setRepoEnabled(alias, enabled)
                    }

                    onRepoDoubleClicked: function(repo) {
                        // プロパティパネルで編集
                    }
                }

                // ツールバー
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8

                    Button {
                        text: qsTr("Add")
                        icon.name: "list-add"
                        onClicked: addRepoDialog.open()
                    }
                    Button {
                        text: qsTr("Remove")
                        icon.name: "list-remove"
                        enabled: repoTable.selectedRepo !== null
                        onClicked: confirmDeleteDialog.open()
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("Refresh")
                        icon.name: "view-refresh"
                        enabled: !PackageController.busy

                        onClicked: refreshMenu.open()

                        Menu {
                            id: refreshMenu
                            y: parent.height

                            Action {
                                text: qsTr("Selected Repository")
                                enabled: repoTable.selectedRepo !== null
                                onTriggered: {
                                    if (!PackageController.checkAuth("org.presire.qzypper.refresh-repos"))
                                        return
                                    PackageController.refreshSingleRepo(repoTable.selectedRepo.alias)
                                }
                            }
                            Action {
                                text: qsTr("All Enabled Repositories")
                                onTriggered: {
                                    if (!PackageController.checkAuth("org.presire.qzypper.refresh-repos"))
                                        return
                                    root.refreshAllRequested()
                                }
                            }
                        }
                    }
                }

                // プロパティパネル
                Components.RepoPropertiesPanel {
                    id: propsPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    repo: repoTable.selectedRepo

                    onApplyRequested: function(changes) {
                        PackageController.modifyRepo(changes.alias, changes)
                        var idx = repoTable.selectedIndex
                        PackageController.loadRepos()
                        repoTable.selectedIndex = idx
                    }
                }
            }

            // ─── サービスタブ ────────────────────
            ColumnLayout {
                spacing: 8

                ListView {
                    id: serviceListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: PackageController.services
                    currentIndex: -1

                    property real colEnabled: 50
                    property real colName: 150
                    property real colType: 80
                    // URL列は残り幅を使用

                    Settings {
                        category: "serviceTable"
                        property alias colEnabled: serviceListView.colEnabled
                        property alias colName: serviceListView.colName
                        property alias colType: serviceListView.colType
                    }

                    header: Rectangle {
                        width: serviceListView.width
                        height: 32
                        color: palette.button

                        Row {
                            anchors.fill: parent
                            spacing: 0

                            // Enabled
                            Item {
                                width: serviceListView.colEnabled; height: parent.height
                                clip: true
                                Label {
                                    anchors.centerIn: parent
                                    width: Math.min(implicitWidth, parent.width - 8)
                                    text: qsTr("Enabled"); font.bold: true; font.pixelSize: 13
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Rectangle {
                                    width: 4; height: parent.height; anchors.right: parent.right
                                    color: svcEnResize.hovered || svcEnResize.pressed ? palette.highlight : "transparent"
                                    MouseArea {
                                        id: svcEnResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                        property bool hovered: false; hoverEnabled: true
                                        onEntered: hovered = true; onExited: hovered = false
                                        property real globalStartX: 0; property real startW: 0
                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = serviceListView.colEnabled }
                                        onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); serviceListView.colEnabled = Math.max(40, startW + p.x - globalStartX) } }
                                    }
                                }
                            }
                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                            // Name
                            Item {
                                width: serviceListView.colName; height: parent.height
                                clip: true
                                Label {
                                    anchors.centerIn: parent
                                    width: Math.min(implicitWidth, parent.width - 8)
                                    text: qsTr("Name"); font.bold: true; font.pixelSize: 13
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Rectangle {
                                    width: 4; height: parent.height; anchors.right: parent.right
                                    color: svcNameResize.hovered || svcNameResize.pressed ? palette.highlight : "transparent"
                                    MouseArea {
                                        id: svcNameResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                        property bool hovered: false; hoverEnabled: true
                                        onEntered: hovered = true; onExited: hovered = false
                                        property real globalStartX: 0; property real startW: 0
                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = serviceListView.colName }
                                        onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); serviceListView.colName = Math.max(80, startW + p.x - globalStartX) } }
                                    }
                                }
                            }
                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                            // URL
                            Item {
                                width: Math.max(150, serviceListView.width - serviceListView.colEnabled - serviceListView.colName - serviceListView.colType - 3)
                                height: parent.height
                                clip: true
                                Label {
                                    anchors.centerIn: parent
                                    width: Math.min(implicitWidth, parent.width - 8)
                                    text: qsTr("URL"); font.bold: true; font.pixelSize: 13
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Rectangle {
                                    width: 4; height: parent.height; anchors.right: parent.right
                                    color: svcUrlResize.hovered || svcUrlResize.pressed ? palette.highlight : "transparent"
                                    MouseArea {
                                        id: svcUrlResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                        property bool hovered: false; hoverEnabled: true
                                        onEntered: hovered = true; onExited: hovered = false
                                        property real globalStartX: 0; property real startW: 0
                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = serviceListView.colType }
                                        onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); serviceListView.colType = Math.max(50, startW - (p.x - globalStartX)) } }
                                    }
                                }
                            }
                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                            // Type
                            Item {
                                width: serviceListView.colType; height: parent.height
                                clip: true
                                Label {
                                    anchors.centerIn: parent
                                    width: Math.min(implicitWidth, parent.width - 8)
                                    text: qsTr("Type"); font.bold: true; font.pixelSize: 13
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Rectangle {
                                    width: 4; height: parent.height; anchors.right: parent.right
                                    color: svcTypeResize.hovered || svcTypeResize.pressed ? palette.highlight : "transparent"
                                    MouseArea {
                                        id: svcTypeResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                        property bool hovered: false; hoverEnabled: true
                                        onEntered: hovered = true; onExited: hovered = false
                                        property real globalStartX: 0; property real startW: 0
                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = serviceListView.colType }
                                        onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); serviceListView.colType = Math.max(50, startW + p.x - globalStartX) } }
                                    }
                                }
                            }
                        }
                    }

                    delegate: ItemDelegate {
                        id: svcDel
                        width: serviceListView.width
                        height: 30
                        padding: 0
                        highlighted: ListView.isCurrentItem

                        required property int index
                        required property var modelData

                        onClicked: serviceListView.currentIndex = svcDel.index

                        background: Rectangle {
                            color: svcDel.highlighted ? palette.highlight
                                 : svcDel.index % 2 === 0 ? "transparent"
                                 : Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.15)
                        }

                        contentItem: Row {
                            spacing: 0

                            Item {
                                width: serviceListView.colEnabled; height: parent.height
                                CheckBox {
                                    anchors.centerIn: parent
                                    checked: svcDel.modelData.enabled || false
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
                            Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }
                            Label {
                                width: serviceListView.colName; height: parent.height
                                text: svcDel.modelData.name || svcDel.modelData.alias || ""
                                elide: Text.ElideRight; leftPadding: 6
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 14
                                color: svcDel.highlighted ? palette.highlightedText : palette.text
                                opacity: svcDel.modelData.enabled ? 1.0 : 0.5
                            }
                            Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }
                            Label {
                                width: Math.max(150, serviceListView.width - serviceListView.colEnabled - serviceListView.colName - serviceListView.colType - 3)
                                height: parent.height
                                text: svcDel.modelData.url || ""
                                elide: Text.ElideMiddle; leftPadding: 6
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 14
                                color: svcDel.highlighted ? palette.highlightedText : palette.link
                                opacity: svcDel.modelData.enabled ? 0.9 : 0.4
                            }
                            Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }
                            Label {
                                width: serviceListView.colType; height: parent.height
                                text: svcDel.modelData.type || ""
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 14
                                color: svcDel.highlighted ? palette.highlightedText : palette.placeholderText
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {}
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: qsTr("Add")
                        icon.name: "list-add"
                        onClicked: addServiceDialog.open()
                    }
                    Button {
                        text: qsTr("Remove")
                        icon.name: "list-remove"
                        enabled: serviceListView.currentIndex >= 0
                        onClicked: confirmDeleteServiceDialog.open()
                    }
                    Button {
                        text: qsTr("Refresh")
                        icon.name: "view-refresh"
                        enabled: serviceListView.currentIndex >= 0 && !PackageController.busy
                        onClicked: {
                            if (!PackageController.checkAuth("org.presire.qzypper.manage-repos"))
                                return
                            var svc = PackageController.services[serviceListView.currentIndex]
                            PackageController.refreshService(svc.alias)
                        }
                    }
                }
            }
        }

        // ─── フッタボタン ──────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0

            Button {
                text: qsTr("Apply Changes")
                enabled: repoTabActive && propsPanel.hasChanges
                onClicked: {
                    if (!PackageController.checkAuth("org.presire.qzypper.manage-repos"))
                        return
                    propsPanel.applyRequested(propsPanel.getChanges())
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("OK")
                highlighted: true
                onClicked: {
                    if (root.hasRepoChanges()) {
                        if (!PackageController.checkAuth("org.presire.qzypper.manage-repos"))
                            return
                    }
                    PackageController.loadRepos()
                    root.closeRequested()
                }
            }
            Button {
                text: qsTr("Close")
                onClicked: root.closeRequested()
            }
        }
    }

    // ダイアログ類
    AddRepoDialog {
        id: addRepoDialog
        onRepoAdded: PackageController.loadRepos()
    }

    AddServiceDialog {
        id: addServiceDialog
        onServiceAdded: {
            PackageController.loadServices()
            PackageController.loadRepos()
        }
    }

    Dialog {
        id: confirmDeleteDialog
        title: qsTr("Remove Repository")
        modal: true
        width: 400
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No

        Label {
            text: repoTable.selectedRepo
                ? qsTr("Remove repository \"%1\"?").arg(repoTable.selectedRepo.name || repoTable.selectedRepo.alias)
                : ""
            wrapMode: Text.WordWrap
        }

        onAccepted: {
            if (repoTable.selectedRepo) {
                PackageController.removeRepo(repoTable.selectedRepo.alias)
                repoTable.selectedIndex = -1
            }
        }
    }

    Dialog {
        id: confirmDeleteServiceDialog
        title: qsTr("Remove Service")
        modal: true
        width: 400
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No

        Label {
            text: {
                if (serviceListView.currentIndex < 0) return ""
                var svc = PackageController.services[serviceListView.currentIndex]
                return qsTr("Remove service \"%1\" and its repositories?").arg(svc ? (svc.name || svc.alias) : "")
            }
            wrapMode: Text.WordWrap
        }

        onAccepted: {
            if (serviceListView.currentIndex >= 0) {
                var svc = PackageController.services[serviceListView.currentIndex]
                PackageController.removeService(svc.alias)
                serviceListView.currentIndex = -1
            }
        }
    }
}
