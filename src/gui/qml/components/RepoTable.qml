import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Item {
    id: root

    property var model: []
    property int selectedIndex: -1
    property var selectedRepo: selectedIndex >= 0 && selectedIndex < sortedModel.length
                                ? sortedModel[selectedIndex] : null

    // ソート状態
    property int sortColumn: 3  // デフォルト: 名前列
    property bool sortAscending: true

    // 列幅
    property real colPriority: 60
    property real colEnabled: 50
    property real colAutoRefresh: 70
    property real colName: 200
    property real colService: 120
    // URL列は残り幅を使用

    signal repoDoubleClicked(var repo)
    signal enabledToggled(string alias, bool enabled)

    Settings {
        category: "repoTable"
        property alias colPriority: root.colPriority
        property alias colEnabled: root.colEnabled
        property alias colAutoRefresh: root.colAutoRefresh
        property alias colName: root.colName
        property alias colService: root.colService
    }

    function toggleSort(col) {
        if (sortColumn === col)
            sortAscending = !sortAscending
        else {
            sortColumn = col
            sortAscending = true
        }
        selectedIndex = -1
    }

    function sortedData() {
        var data = Array.from(model)
        data.sort(function(a, b) {
            var va, vb
            switch (sortColumn) {
            case 0: va = a.priority || 99; vb = b.priority || 99; break
            case 1: va = a.enabled ? 1 : 0; vb = b.enabled ? 1 : 0; break
            case 2: va = a.autoRefresh ? 1 : 0; vb = b.autoRefresh ? 1 : 0; break
            case 3: va = (a.name || a.alias || "").toLowerCase(); vb = (b.name || b.alias || "").toLowerCase(); break
            case 4: va = (a.service || "").toLowerCase(); vb = (b.service || "").toLowerCase(); break
            case 5: va = (a.url || "").toLowerCase(); vb = (b.url || "").toLowerCase(); break
            default: return 0
            }
            if (va < vb) return sortAscending ? -1 : 1
            if (va > vb) return sortAscending ? 1 : -1
            return 0
        })
        return data
    }

    property var sortedModel: sortedData()
    onModelChanged: sortedModel = sortedData()
    onSortColumnChanged: sortedModel = sortedData()
    onSortAscendingChanged: sortedModel = sortedData()

    // 最小行幅
    readonly property real minRowWidth: colPriority + 1 + colEnabled + 1 + colAutoRefresh + 1 + colName + 1 + colService + 1 + 150

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ─── ヘッダ ───────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: palette.button

            Flickable {
                id: headerFlick
                anchors.fill: parent
                contentWidth: Math.max(width, root.minRowWidth)
                flickableDirection: Flickable.HorizontalFlick
                interactive: false
                contentX: listFlick.contentX

                Row {
                    height: parent.height
                    spacing: 0

                    // 優先度
                    Item {
                        width: root.colPriority; height: parent.height
                        clip: true
                        MouseArea {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width - 4; cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleSort(0)
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 2
                            Label {
                                text: qsTr("Priority"); font.bold: true; font.pixelSize: 13
                                width: Math.min(implicitWidth, root.colPriority - 14)
                                elide: Text.ElideRight
                            }
                            Label { text: root.sortColumn === 0 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""; font.pixelSize: 9 }
                        }
                        Rectangle {
                            width: 4; height: parent.height; anchors.right: parent.right
                            color: prioResize.hovered || prioResize.pressed ? palette.highlight : "transparent"
                            MouseArea {
                                id: prioResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                property bool hovered: false; hoverEnabled: true
                                onEntered: hovered = true; onExited: hovered = false
                                property real globalStartX: 0; property real startW: 0
                                onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = root.colPriority }
                                onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); root.colPriority = Math.max(40, startW + p.x - globalStartX) } }
                            }
                        }
                    }
                    Rectangle { width: 1; height: parent.height; color: palette.mid }

                    // 有効
                    Item {
                        width: root.colEnabled; height: parent.height
                        clip: true
                        MouseArea {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width - 4; cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleSort(1)
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 2
                            Label {
                                text: qsTr("Enabled"); font.bold: true; font.pixelSize: 13
                                width: Math.min(implicitWidth, root.colEnabled - 14)
                                elide: Text.ElideRight
                            }
                            Label { text: root.sortColumn === 1 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""; font.pixelSize: 9 }
                        }
                        Rectangle {
                            width: 4; height: parent.height; anchors.right: parent.right
                            color: enabledResize.hovered || enabledResize.pressed ? palette.highlight : "transparent"
                            MouseArea {
                                id: enabledResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                property bool hovered: false; hoverEnabled: true
                                onEntered: hovered = true; onExited: hovered = false
                                property real globalStartX: 0; property real startW: 0
                                onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = root.colEnabled }
                                onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); root.colEnabled = Math.max(40, startW + p.x - globalStartX) } }
                            }
                        }
                    }
                    Rectangle { width: 1; height: parent.height; color: palette.mid }

                    // 自動更新
                    Item {
                        width: root.colAutoRefresh; height: parent.height
                        clip: true
                        MouseArea {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width - 4; cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleSort(2)
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 2
                            Label {
                                text: qsTr("Auto-refresh"); font.bold: true; font.pixelSize: 13
                                width: Math.min(implicitWidth, root.colAutoRefresh - 14)
                                elide: Text.ElideRight
                            }
                            Label { text: root.sortColumn === 2 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""; font.pixelSize: 9 }
                        }
                        Rectangle {
                            width: 4; height: parent.height; anchors.right: parent.right
                            color: arResize.hovered || arResize.pressed ? palette.highlight : "transparent"
                            MouseArea {
                                id: arResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                property bool hovered: false; hoverEnabled: true
                                onEntered: hovered = true; onExited: hovered = false
                                property real globalStartX: 0; property real startW: 0
                                onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = root.colAutoRefresh }
                                onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); root.colAutoRefresh = Math.max(50, startW + p.x - globalStartX) } }
                            }
                        }
                    }
                    Rectangle { width: 1; height: parent.height; color: palette.mid }

                    // 名前
                    Item {
                        width: root.colName; height: parent.height
                        clip: true
                        MouseArea {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width - 4; cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleSort(3)
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 2
                            Label { text: qsTr("Name"); font.bold: true; font.pixelSize: 13 }
                            Label { text: root.sortColumn === 3 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""; font.pixelSize: 9 }
                        }
                        Rectangle {
                            width: 4; height: parent.height; anchors.right: parent.right
                            color: nameResize.hovered || nameResize.pressed ? palette.highlight : "transparent"
                            MouseArea {
                                id: nameResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                property bool hovered: false; hoverEnabled: true
                                onEntered: hovered = true; onExited: hovered = false
                                property real globalStartX: 0; property real startW: 0
                                onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = root.colName }
                                onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); root.colName = Math.max(100, startW + p.x - globalStartX) } }
                            }
                        }
                    }
                    Rectangle { width: 1; height: parent.height; color: palette.mid }

                    // サービス
                    Item {
                        width: root.colService; height: parent.height
                        clip: true
                        MouseArea {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width - 4; cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleSort(4)
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 2
                            Label { text: qsTr("Service"); font.bold: true; font.pixelSize: 13 }
                            Label { text: root.sortColumn === 4 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""; font.pixelSize: 9 }
                        }
                        Rectangle {
                            width: 4; height: parent.height; anchors.right: parent.right
                            color: svcResize.hovered || svcResize.pressed ? palette.highlight : "transparent"
                            MouseArea {
                                id: svcResize; anchors.fill: parent; cursorShape: Qt.SplitHCursor
                                property bool hovered: false; hoverEnabled: true
                                onEntered: hovered = true; onExited: hovered = false
                                property real globalStartX: 0; property real startW: 0
                                onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = root.colService }
                                onPositionChanged: function(mouse) { if (pressed) { var p = mapToGlobal(mouse.x, 0); root.colService = Math.max(60, startW + p.x - globalStartX) } }
                            }
                        }
                    }
                    Rectangle { width: 1; height: parent.height; color: palette.mid }

                    // URL
                    Item {
                        width: Math.max(150, headerFlick.width - root.colPriority - root.colEnabled - root.colAutoRefresh - root.colName - root.colService - 5)
                        height: parent.height
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleSort(5)
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 2
                            Label { text: qsTr("URL"); font.bold: true; font.pixelSize: 13 }
                            Label { text: root.sortColumn === 5 ? (root.sortAscending ? "\u25B2" : "\u25BC") : ""; font.pixelSize: 9 }
                        }
                    }
                }
            }
        }

        // ─── リスト本体 ─────────────────────────────
        Flickable {
            id: listFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, root.minRowWidth)
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            ListView {
                id: repoListView
                width: listFlick.contentWidth
                height: listFlick.height
                clip: true
                model: root.sortedModel
                currentIndex: root.selectedIndex

                delegate: ItemDelegate {
                    id: del
                    width: repoListView.width
                    height: 30
                    padding: 0
                    highlighted: root.selectedIndex === del.index

                    required property int index
                    required property var modelData

                    onClicked: root.selectedIndex = del.index
                    onDoubleClicked: root.repoDoubleClicked(del.modelData)

                    background: Rectangle {
                        color: del.highlighted ? palette.highlight
                             : del.index % 2 === 0 ? "transparent"
                             : Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.15)
                    }

                    contentItem: Row {
                        spacing: 0

                        // 優先度
                        Label {
                            width: root.colPriority; height: parent.height
                            text: String(del.modelData.priority ?? 99)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 14
                            color: del.highlighted ? palette.highlightedText : palette.text
                            opacity: del.modelData.enabled ? 1.0 : 0.5
                        }
                        Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }

                        // 有効チェック
                        Item {
                            width: root.colEnabled; height: parent.height
                            CheckBox {
                                anchors.centerIn: parent
                                checked: del.modelData.enabled || false
                                onToggled: root.enabledToggled(del.modelData.alias, checked)
                            }
                        }
                        Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }

                        // 自動更新チェック
                        Item {
                            width: root.colAutoRefresh; height: parent.height
                            Label {
                                anchors.centerIn: parent
                                text: del.modelData.autoRefresh ? "\u2713" : ""
                                font.pixelSize: 14
                                color: del.highlighted ? palette.highlightedText : palette.text
                            }
                        }
                        Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }

                        // 名前
                        Label {
                            width: root.colName; height: parent.height
                            text: del.modelData.name || del.modelData.alias || ""
                            elide: Text.ElideRight; leftPadding: 6
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 14
                            color: del.highlighted ? palette.highlightedText : palette.text
                            opacity: del.modelData.enabled ? 1.0 : 0.5
                        }
                        Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }

                        // サービス
                        Label {
                            width: root.colService; height: parent.height
                            text: del.modelData.service || "-"
                            elide: Text.ElideRight; leftPadding: 6
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 14
                            color: del.highlighted ? palette.highlightedText : palette.placeholderText
                            opacity: del.modelData.enabled ? 1.0 : 0.5
                        }
                        Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.3 }

                        // URL
                        Label {
                            width: Math.max(150, listFlick.width - root.colPriority - root.colEnabled - root.colAutoRefresh - root.colName - root.colService - 5)
                            height: parent.height
                            text: del.modelData.url || ""
                            elide: Text.ElideMiddle; leftPadding: 6
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 14
                            color: del.highlighted ? palette.highlightedText : palette.link
                            opacity: del.modelData.enabled ? 0.9 : 0.4
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {}
            }
        }
    }
}
