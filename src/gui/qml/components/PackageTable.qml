import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

            Pane {
                id: pkgPane
                // ダークモード連動 (typeahead ポップアップ配色用)
                property bool darkMode: false

                Settings {
                    id: pkgSettings
                    category: "packageTable"
                    property alias colName: pkgPane.colName
                    property alias colSummary: pkgPane.colSummary
                    property alias colInstalled: pkgPane.colInstalled
                    property alias colAvailable: pkgPane.colAvailable
                }

                // -- パッケージ一覧の typeahead 検索バッファ --
                property string pkgSearchBuffer: ""

                Timer {
                    id: pkgSearchClearTimer
                    interval: 5000
                    repeat: false
                    onTriggered: pkgSearchBuffer = ""
                }

                function performPkgSearch() {
                    var q = pkgSearchBuffer.toLowerCase()
                    if (q.length === 0) return
                    var proxy = PackageController.packageProxy
                    var idx = proxy.indexOfNamePrefix(q)
                    if (idx >= 0) {
                        packageListView.currentIndex = idx
                        packageListView.positionViewAtIndex(idx, ListView.Center)
                    }
                }

                function toggleSort(col) {
                    var proxy = PackageController.packageProxy
                    if (proxy.sortColumn === col) {
                        proxy.sortAscending = !proxy.sortAscending
                    } else {
                        proxy.sortColumn = col
                        proxy.sortAscending = true
                    }
                }

                function setStatus(packageName, status) {
                    PackageController.setPackageStatus(packageName, status)
                }
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
                                        text: PackageController.packageProxy.sortColumn === 0 ? (PackageController.packageProxy.sortAscending ? "▲" : "▼") : ""
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
                                        Label { text: PackageController.packageProxy.sortColumn === 1 ? (PackageController.packageProxy.sortAscending ? "▲" : "▼") : ""; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
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

                            // 詳細ペインに表示中のパッケージ名
                            property string detailsName: ""

                            width: packageFlickable.contentWidth
                            height: packageFlickable.height
                            clip: true
                            model: PackageController.packageProxy
                            focus: true
                            activeFocusOnTab: true
                            keyNavigationWraps: false
                            highlightMoveDuration: 0
                            highlightMoveVelocity: -1
                            highlightResizeDuration: 0

                            // 選択行のパッケージが変わった時だけ詳細を読み込む
                            // (一覧の差分更新で同じ行番号に別パッケージが来た場合も追従する)
                            function syncDetails(force) {
                                var name = PackageController.packageProxy.nameAt(currentIndex)
                                if (name === detailsName && !force) return
                                detailsName = name
                                if (name.length > 0)
                                    PackageController.loadPackageDetails(name)
                                else
                                    PackageController.clearPackageDetails()
                            }

                            onCurrentIndexChanged: syncDetails()

                            Connections {
                                target: PackageController.packageProxy
                                function onModelReset() { packageListView.syncDetails() }
                                function onLayoutChanged() { packageListView.syncDetails() }
                                function onRowsInserted() { packageListView.syncDetails() }
                                function onRowsRemoved() { packageListView.syncDetails() }
                                // 選択中パッケージの状態・候補版が変わったら詳細 (バージョンタブ等) も更新する
                                function onDataChanged(topLeft, bottomRight) {
                                    var row = packageListView.currentIndex
                                    if (row >= topLeft.row && row <= bottomRight.row)
                                        packageListView.syncDetails(true)
                                }
                            }

                            Keys.onPressed: function(event) {
                                switch (event.key) {
                                    case Qt.Key_Up:
                                        if (currentIndex > 0) currentIndex--
                                        event.accepted = true
                                        break
                                    case Qt.Key_Down:
                                        if (currentIndex < count - 1) currentIndex++
                                        event.accepted = true
                                        break
                                    case Qt.Key_Home:
                                        if (count > 0) {
                                            currentIndex = 0
                                            positionViewAtIndex(0, ListView.Beginning)
                                        }
                                        event.accepted = true
                                        break
                                    case Qt.Key_End:
                                        if (count > 0) {
                                            currentIndex = count - 1
                                            positionViewAtIndex(count - 1, ListView.End)
                                        }
                                        event.accepted = true
                                        break
                                    case Qt.Key_PageUp:
                                        currentIndex = Math.max(0, currentIndex - Math.floor(height / 28))
                                        positionViewAtIndex(currentIndex, ListView.Contain)
                                        event.accepted = true
                                        break
                                    case Qt.Key_PageDown:
                                        currentIndex = Math.min(count - 1, currentIndex + Math.floor(height / 28))
                                        positionViewAtIndex(currentIndex, ListView.Contain)
                                        event.accepted = true
                                        break
                                    case Qt.Key_Escape:
                                        // 通常はウィンドウ側の Shortcut(Escape) が処理する。キーがここへ届いた場合のフォールバック
                                        pkgSearchBuffer = ""
                                        pkgSearchClearTimer.stop()
                                        event.accepted = true
                                        break
                                    case Qt.Key_Backspace:
                                        if (pkgSearchBuffer.length > 0) {
                                            pkgSearchBuffer = pkgSearchBuffer.slice(0, -1)
                                            if (pkgSearchBuffer.length > 0) {
                                                performPkgSearch()
                                                pkgSearchClearTimer.restart()
                                            }
                                            else {
                                                pkgSearchClearTimer.stop()
                                            }
                                        }
                                        event.accepted = true
                                        break
                                    default:
                                        if (event.text.length > 0 && event.text.charCodeAt(0) >= 0x20) {
                                            pkgSearchBuffer += event.text
                                            performPkgSearch()
                                            pkgSearchClearTimer.restart()
                                            event.accepted = true
                                        }
                                        break
                                }
                            }

                            delegate: ItemDelegate {
                                id: pkgDelegate
                                width: packageListView.width
                                height: 28
                                highlighted: ListView.isCurrentItem

                                required property int index
                                required property string name
                                required property string summary
                                required property string version
                                required property string installedVersion
                                required property string arch
                                required property string repoAlias
                                required property string repoName
                                required property real size
                                required property int status

                                background: Rectangle {
                                    color: pkgDelegate.highlighted ? palette.highlight : "transparent"
                                }

                                property bool isChecked: PackageStatusInfo.isChecked(pkgDelegate.status)
                                property bool isMarkedForRemoval: PackageStatusInfo.isMarkedForRemoval(pkgDelegate.status)

                                onClicked: {
                                    packageListView.currentIndex = pkgDelegate.index
                                    packageListView.forceActiveFocus()
                                }

                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: {
                                        packageListView.currentIndex = pkgDelegate.index
                                        packageListView.forceActiveFocus()
                                        pkgContextMenu.targetPkg = ({ name: pkgDelegate.name, installedVersion: pkgDelegate.installedVersion, status: pkgDelegate.status })
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

                                        StatusIcon {
                                            anchors.centerIn: parent
                                            status: pkgDelegate.status || 0
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: function(mouse) {
                                                mouse.accepted = true
                                                var pkg = ({ name: pkgDelegate.name, installedVersion: pkgDelegate.installedVersion, status: pkgDelegate.status })
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
                                            text: pkgDelegate.name || ""
                                            textFormat: Text.PlainText
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
                                            text: pkgDelegate.summary || ""
                                            textFormat: Text.PlainText
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
                                            text: pkgDelegate.installedVersion || ""
                                            textFormat: Text.PlainText
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
                                            text: pkgDelegate.version || ""
                                            textFormat: Text.PlainText
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
                                            text: pkgDelegate.size ? Util.formatSize(pkgDelegate.size) : ""
                                            horizontalAlignment: Text.AlignRight
                                            color: pkgDelegate.highlighted ? palette.highlightedText : palette.placeholderText
                                        }
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {}
                        }

                        // typeahead 検索オーバーレイ
                        Rectangle {
                            id: pkgSearchPopup
                            z: 1000
                            visible: pkgSearchBuffer.length > 0
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.topMargin: 8
                            anchors.rightMargin: 16
                            radius: 5
                            color: pkgPane.darkMode ? "#1B5E20" : "#2E7D32"
                            border.color: pkgPane.darkMode ? "#0E3A12" : "#1B5E20"
                            border.width: 1
                            width: Math.max(220, pkgSearchPopupLabel.implicitWidth + 24)
                            height: 36

                            Label {
                                id: pkgSearchPopupLabel
                                anchors.centerIn: parent
                                text: qsTr("Search: ") + pkgSearchBuffer
                                color: "#ffffff"
                                font.bold: true
                            }
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

                        MenuSeparator {}

                        MenuItem {
                            text: qsTr("Lock (Persistent)")
                            Accessible.name: qsTr("Create persistent lock for this package")
                            onTriggered: PackageController.addLock({ name: pkgContextMenu.targetPkg.name,
                                                                    kind: "package", match: "exact" })
                        }

                        MenuItem {
                            text: qsTr("Unlock")
                            Accessible.name: qsTr("Remove persistent lock for this package")
                            onTriggered: PackageController.removeLock({ name: pkgContextMenu.targetPkg.name,
                                                                       kind: "package", match: "exact" })
                        }
                    }
                }

                // 空一覧時のメッセージ (全体が 0 件か、ステータスフィルタで 0 件か)
                Label {
                    anchors.centerIn: parent
                    visible: packageListView.count === 0 && !PackageController.busy
                    text: PackageController.packages.length === 0 ? qsTr("No packages to display")
                                                     : qsTr("No packages match the status filter")
                    color: palette.placeholderText
                }
            }
