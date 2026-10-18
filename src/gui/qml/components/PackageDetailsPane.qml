import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

            Pane {
                id: detailPane
                readonly property real minPaneHeight: 200
                readonly property real defaultPaneHeight: 300
                // 上部のパッケージ一覧の最小高 (200) + SplitView ハンドル (2) を残す
                readonly property real reservedTopHeight: 202

                // 過去に最小値付近の値が保存されている場合は既定値で補正する
                SplitView.preferredHeight: splitSettings.detailPaneHeight >= minPaneHeight
                                           ? splitSettings.detailPaneHeight : defaultPaneHeight
                SplitView.minimumHeight: minPaneHeight
                SplitView.maximumHeight: {
                    const view = detailPane.SplitView.view
                    return view ? Math.max(minPaneHeight, view.height - reservedTopHeight) : Infinity
                }
                padding: 0
                // 起動時のレイアウト確定前やウィンドウリサイズ時の高さを保存しないよう、
                // ユーザがハンドルをドラッグした時のみ保存する
                onHeightChanged: {
                    const view = detailPane.SplitView.view
                    if (view && view.resizing)
                        splitSettings.detailPaneHeight = height
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    TabBar {
                        id: detailsTabBar
                        Layout.fillWidth: true

                        TabButton { text: qsTr("Description") }
                        TabButton { text: qsTr("Technical Data") }
                        TabButton { text: qsTr("Dependencies") }
                        TabButton { text: qsTr("Version") }
                        TabButton { text: qsTr("File List") }
                        TabButton { text: qsTr("Changelog") }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: detailsTabBar.currentIndex

                        // 説明タブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                textFormat: TextArea.RichText
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    if (!d || !d.name) return ""
                                    return "<h3>" + Util.escapeHtml(d.name) + "</h3>"
                                         + "<p>" + Util.escapeHtml(d.summary) + "</p>"
                                         + "<hr/>"
                                         + "<p>" + Util.escapeHtml(d.description).replace(/\n/g, "<br/>") + "</p>"
                                }
                            }
                        }

                        // 技術データタブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                textFormat: TextArea.RichText
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    if (!d || !d.name) return ""
                                    return Util.formatTechData(d)
                                }
                                onLinkActivated: function(link) {
                                    Util.openSafeUrl(link)
                                }
                                HoverHandler {
                                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor
                                }
                            }
                        }

                        // 依存関係タブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                textFormat: TextArea.RichText
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    if (!d || !d.name) return ""
                                    return Util.formatDependencies(d)
                                }
                            }
                        }

                        // バージョンタブ
                        Pane {

                    Settings {
                        id: splitSettings
                        category: "splitView"
                        property real detailPaneHeight: 300
                    }
                            id: versionPane
                            padding: 0

                            // カラム幅のプロパティ (ドラッグでリサイズ可能)
                            readonly property real minColRadio: 40
                            readonly property real minColVersion: 220
                            readonly property real minColArch: 120
                            readonly property real minColRepo: 200
                            readonly property real minColStatus: 100
                            property real colRadio: minColRadio
                            property real colVersion: minColVersion
                            property real colArch: minColArch
                            property real colStatus: minColStatus

                            // 全列の合計最小幅
                            property real verMinRowWidth: colRadio + colVersion + colArch + minColRepo + colStatus

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                // カラムヘッダ (横スクロール同期)
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 28
                                    color: palette.button
                                    clip: true

                                    Flickable {
                                        id: verHeaderFlickable
                                        anchors.fill: parent
                                        contentX: versionFlickable.contentX
                                        contentWidth: versionFlickable.contentWidth
                                        interactive: false

                                        Row {
                                            width: Math.max(verHeaderFlickable.width, versionPane.verMinRowWidth)
                                            height: 28
                                            spacing: 0

                                            // ラジオ列ヘッダ
                                            Item {
                                                width: versionPane.colRadio
                                                height: parent.height
                                            }
                                            // 区切り線
                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // バージョン列ヘッダ + リサイズハンドル
                                            Item {
                                                width: versionPane.colVersion - 1
                                                height: parent.height
                                                Label {
                                                    anchors.centerIn: parent
                                                    text: qsTr("Version")
                                                    font.bold: true
                                                }
                                                Rectangle {
                                                    width: 4; height: parent.height
                                                    anchors.right: parent.right
                                                    color: vhResize1.hovered || vhResize1.pressed ? palette.highlight : "transparent"

                                                    MouseArea {
                                                        id: vhResize1
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SplitHCursor
                                                        property bool hovered: false
                                                        hoverEnabled: true
                                                        onEntered: hovered = true
                                                        onExited: hovered = false
                                                        property real globalStartX: 0
                                                        property real startW: 0
                                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = versionPane.colVersion }
                                                        onPositionChanged: function(mouse) {
                                                            if (pressed) {
                                                                var p = mapToGlobal(mouse.x, 0)
                                                                versionPane.colVersion = Math.max(versionPane.minColVersion, startW + p.x - globalStartX)
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // アーキテクチャ列ヘッダ + リサイズハンドル
                                            Item {
                                                width: versionPane.colArch - 1
                                                height: parent.height
                                                clip: true

                                                Label {
                                                    anchors.centerIn: parent
                                                    width: Math.min(implicitWidth, parent.width - 10)
                                                    text: qsTr("Architecture")
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    horizontalAlignment: Text.AlignHCenter
                                                }
                                                Rectangle {
                                                    width: 4; height: parent.height
                                                    anchors.right: parent.right
                                                    color: vhResize2.hovered || vhResize2.pressed ? palette.highlight : "transparent"
                                                    MouseArea {
                                                        id: vhResize2
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SplitHCursor
                                                        property bool hovered: false
                                                        hoverEnabled: true
                                                        onEntered: hovered = true
                                                        onExited: hovered = false
                                                        property real globalStartX: 0
                                                        property real startW: 0
                                                        onPressed: function(mouse) { var p = mapToGlobal(mouse.x, 0); globalStartX = p.x; startW = versionPane.colArch }
                                                        onPositionChanged: function(mouse) {
                                                            if (pressed) {
                                                                var p = mapToGlobal(mouse.x, 0)
                                                                versionPane.colArch = Math.max(versionPane.minColArch, startW + p.x - globalStartX)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // リポジトリ列ヘッダ
                                            Item {
                                                width: Math.max(versionPane.minColRepo, parent.width - versionPane.colRadio - versionPane.colVersion - versionPane.colArch - versionPane.colStatus - 3)
                                                height: parent.height
                                                Label {
                                                    anchors.centerIn: parent
                                                    text: qsTr("Repository")
                                                    font.bold: true
                                                }
                                            }
                                            Rectangle { width: 1; height: parent.height; color: palette.mid }

                                            // ステータス列ヘッダ
                                            Item {
                                                width: versionPane.colStatus - 1
                                                height: parent.height
                                                Label {
                                                    anchors.centerIn: parent
                                                    text: qsTr("Status")
                                                    font.bold: true
                                                }
                                            }
                                        }
                                    }
                                }

                                // バージョンリスト (横スクロール対応)
                                Flickable {
                                    id: versionFlickable
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    contentWidth: Math.max(width, versionPane.verMinRowWidth)
                                    contentHeight: height
                                    flickableDirection: Flickable.HorizontalFlick
                                    boundsBehavior: Flickable.StopAtBounds

                                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                                    ListView {
                                        id: versionListView
                                        width: versionFlickable.contentWidth
                                        height: versionFlickable.height
                                        clip: true
                                        // 表示中パッケージの詳細 (変更時に選択を同期する)
                                        property var details: PackageController.currentPackageDetails
                                        model: details.availableVersions || []

                                        property int selectedIndex: -1

                                        // モデル差し替えと同じ通知で変わるため、モデル更新後に同期する
                                        onDetailsChanged: Qt.callLater(syncSelection)

                                        function selectVersion(idx, versionData) {
                                            if (selectedIndex === idx) return
                                            selectedIndex = idx
                                            var d = details
                                            if (d && d.name) {
                                                PackageController.setPackageVersion(
                                                    d.name,
                                                    versionData.version,
                                                    versionData.arch,
                                                    versionData.repoAlias
                                                )
                                            }
                                            // 変更に失敗した場合も含め、実際の状態に選択を合わせる
                                            Qt.callLater(syncSelection)
                                        }

                                        // 選択 (ラジオボタン) を詳細の状態に合わせる
                                        // インストール/更新対象なら候補バージョン、それ以外はインストール済みバージョンを選択する
                                        function syncSelection() {
                                            var d = details
                                            var versions = (d && d.availableVersions) || []
                                            var idx = -1
                                            var i
                                            if (d && d.name) {
                                                var s = d.status
                                                var toInstall = s === PackageStatusInfo.Install || s === PackageStatusInfo.AutoInstall
                                                             || s === PackageStatusInfo.Update || s === PackageStatusInfo.AutoUpdate
                                                if (toInstall) {
                                                    for (i = 0; i < versions.length; i++) {
                                                        var v = versions[i]
                                                        if (v.version === d.version && v.arch === d.arch && v.repoAlias === d.repoAlias) {
                                                            idx = i
                                                            break
                                                        }
                                                    }
                                                }
                                                else if (d.installedVersion) {
                                                    // installed: いずれかのインストール済みとバージョン・アーキテクチャが一致する (バックエンドで判定)
                                                    // 複数版インストール時は代表版 (installedVersion) を選ぶ。優先順:
                                                    //   0. 候補と一致するインストール済みの行 (インストール済みの版を選んで「変更なし」にした場合)
                                                    //   1. installedVersion と一致し、候補と同じリポジトリの行
                                                    //   2. installedVersion と一致する行
                                                    //   3. インストール済みの行
                                                    var sameVerIdx = -1
                                                    var anyInstIdx = -1
                                                    for (i = 0; i < versions.length; i++) {
                                                        var c = versions[i]
                                                        if (c.installed === true && c.version === d.version
                                                                && c.arch === d.arch && c.repoAlias === d.repoAlias) {
                                                            idx = i
                                                            break
                                                        }
                                                    }
                                                    for (i = 0; idx < 0 && i < versions.length; i++) {
                                                        if (versions[i].installed !== true) continue
                                                        if (anyInstIdx < 0) anyInstIdx = i
                                                        if (versions[i].version !== d.installedVersion) continue
                                                        if (sameVerIdx < 0) sameVerIdx = i
                                                        if (versions[i].repoAlias === d.repoAlias) {
                                                            idx = i
                                                            break
                                                        }
                                                    }
                                                    if (idx < 0) idx = sameVerIdx >= 0 ? sameVerIdx : anyInstIdx
                                                }
                                            }
                                            selectedIndex = idx
                                            // 選択行が見えている場合はスクロール位置を動かさない
                                            if (idx >= 0) positionViewAtIndex(idx, ListView.Contain)
                                        }

                                        delegate: ItemDelegate {
                                            width: versionListView.width
                                            height: 32

                                            required property int index
                                            required property var modelData

                                            property bool isInstalled: modelData.installed === true
                                            property bool isSelected: versionListView.selectedIndex === index

                                            onClicked: versionListView.selectVersion(index, modelData)

                                            Row {
                                                anchors.fill: parent
                                                spacing: 0

                                                // ラジオボタン (カスタム描画)
                                                Item {
                                                    width: versionPane.colRadio
                                                    height: parent.height

                                                    Rectangle {
                                                        width: 16; height: 16
                                                        anchors.centerIn: parent
                                                        radius: 8
                                                        color: "transparent"
                                                        border.color: isSelected ? palette.highlight : palette.text
                                                        border.width: 1.5

                                                        Rectangle {
                                                            width: 8; height: 8
                                                            anchors.centerIn: parent
                                                            radius: 4
                                                            color: palette.highlight
                                                            visible: isSelected
                                                        }
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: versionListView.selectVersion(index, modelData)
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // バージョン
                                                Item {
                                                    width: versionPane.colVersion - 1
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        anchors.right: parent.right
                                                        anchors.rightMargin: 4
                                                        text: modelData.version || ""
                                                        textFormat: Text.PlainText
                                                        font.bold: isInstalled
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // アーキテクチャ
                                                Item {
                                                    width: versionPane.colArch - 1
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        text: modelData.arch || ""
                                                        textFormat: Text.PlainText
                                                        color: palette.placeholderText
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // リポジトリ
                                                Item {
                                                    width: Math.max(versionPane.minColRepo, parent.width - versionPane.colRadio - versionPane.colVersion - versionPane.colArch - versionPane.colStatus - 3)
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        anchors.right: parent.right
                                                        anchors.rightMargin: 4
                                                        text: modelData.repoName || ""
                                                        textFormat: Text.PlainText
                                                        elide: Text.ElideRight
                                                        color: palette.placeholderText
                                                    }
                                                }
                                                Rectangle { width: 1; height: parent.height; color: palette.mid; opacity: 0.5 }

                                                // ステータス
                                                Item {
                                                    width: versionPane.colStatus - 1
                                                    height: parent.height
                                                    Label {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: 4
                                                        text: isInstalled ? qsTr("Installed") : ""
                                                        color: "#4caf50"
                                                        font.pixelSize: 12
                                                    }
                                                }
                                            }
                                        }

                                        ScrollBar.vertical: ScrollBar {}
                                    }
                                }
                            }
                        }

                        // ファイル一覧タブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.NoWrap
                                textFormat: TextArea.PlainText
                                font.family: "monospace"
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    return (d && d.fileList) ? d.fileList.join("\n") : ""
                                }
                            }
                        }

                        // 変更ログタブ
                        ScrollView {
                            TextArea {
                                readOnly: true
                                wrapMode: TextArea.WordWrap
                                textFormat: TextArea.PlainText
                                font.family: "monospace"
                                text: {
                                    var d = PackageController.currentPackageDetails
                                    return (d && d.changeLog) ? d.changeLog.join("\n\n") : ""
                                }
                            }
                        }
                    }
                }
            }
