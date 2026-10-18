import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

/*
 * libzypp インストール履歴ビューア (GetHistory)。
 * offset/limit (500件) でページング取得し、テキスト/アクション/日付範囲で絞り込む。
 * バックエンドフィルタキー: since, until (epoch ms), limit, offset, actions (as)。
 */
Dialog {
    id: historyDialog
    title: qsTr("Installation History")
    anchors.centerIn: parent
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(parent.width * 0.92, 1040)
    height: Math.min(parent.height * 0.85, 700)

    // ページング状態
    property int pageSize: 500
    property var entries: []        // サーバから取得した全件 (未フィルタ)
    property bool appending: false  // true: 追記モード
    property int lastBatch: 0
    property var actionNames: ["All"]

    onOpened: {
        entries = []
        lastBatch = 0
        actionNames = ["All"]
        runQuery(false)
    }

    /*
     * @brief フィルタを適用した表示用行配列を返す (クライアント側絞り込み)。
     */
    function displayRows() {
        var out = []
        var needle = filterField.text.trim().toLowerCase()
        var act = actionCombo.currentIndex > 0 ? actionNames[actionCombo.currentIndex] : ""
        for (var i = 0; i < entries.length; ++i) {
            var e = entries[i]
            if (act && String(e.action || "") !== act)
                continue
            if (needle !== "") {
                var hay = (String(e.name || "") + " " + String(e.action || "") + " "
                           + String(e.edition || "") + " " + String(e.repoAlias || "") + " "
                           + String(e.requestedBy || "") + " " + String(e.command || "")).toLowerCase()
                if (hay.indexOf(needle) < 0)
                    continue
            }
            out.push(e)
        }
        return out
    }

    /*
     * @brief GetHistory を発行する。append=true のとき offset を進めて追記取得する。
     */
    function runQuery(append) {
        var filter = { limit: pageSize, offset: append ? entries.length : 0 }
        var s = sinceField.text.trim()
        var u = untilField.text.trim()
        if (s.length >= 10) {
            var ms = new Date(s + "T00:00:00").getTime()
            if (!isNaN(ms)) filter.since = ms
        }
        if (u.length >= 10) {
            var mu = new Date(u + "T23:59:59").getTime()
            if (!isNaN(mu)) filter.until = mu
        }
        if (actionCombo.currentIndex > 0)
            filter.actions = [actionNames[actionCombo.currentIndex]]
        appending = append
        PackageController.loadHistory(filter)
    }

    // 履歴取得完了の受け口 (Paging 完了時に distinct action を再収集する)
    Connections {
        target: PackageController
        function onHistoryResultReady(result) {
            if (historyDialog.appending)
                historyDialog.entries = historyDialog.entries.concat(result)
            else
                historyDialog.entries = result
            historyDialog.lastBatch = result.length

            var seen = {}
            for (var i = 0; i < historyDialog.entries.length; ++i) {
                var a = String(historyDialog.entries[i].action || "")
                if (a !== "") seen[a] = true
            }
            var names = ["All"]
            var keys = Object.keys(seen).sort()
            for (var k = 0; k < keys.length; ++k) names.push(keys[k])
            var keep = actionCombo.currentIndex
            actionNames = names
            actionCombo.currentIndex = Math.min(keep, names.length - 1)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ===== フィルタ行 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Filter:")
            }

            TextField {
                id: filterField
                Layout.preferredWidth: 180
                placeholderText: qsTr("Text (name, repo, user...)")
                activeFocusOnTab: true
                Accessible.name: qsTr("History text filter")
                onTextChanged: historyTable.model = historyDialog.displayRows()
            }

            Label {
                text: qsTr("Action:")
            }

            ComboBox {
                id: actionCombo
                Layout.preferredWidth: 140
                model: historyDialog.actionNames
                activeFocusOnTab: true
                Accessible.name: qsTr("History action filter")
                onActivated: historyTable.model = historyDialog.displayRows()

                // バックエンド履歴の action 文字列は外部由来。Fusion ComboBox の既定 delegate
                // (MenuItem の内部 IconLabel) は AutoText で描画するため、contentItem を
                // PlainText で上書きする。色・フォント・ハイライトは Fusion/MenuItem.qml
                // の既定ロジック (palette ベース) を再現。
                delegate: MenuItem {
                    id: actionItem
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    text: modelData
                    font.weight: actionCombo.currentIndex === index ? Font.DemiBold : Font.Normal
                    highlighted: actionCombo.highlightedIndex === index
                    hoverEnabled: actionCombo.hoverEnabled
                    contentItem: Text {
                        text: actionItem.text
                        textFormat: Text.PlainText
                        font: actionItem.font
                        color: actionItem.down || actionItem.highlighted
                               ? actionItem.palette.highlightedText : actionItem.palette.text
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Label {
                text: qsTr("Since:")
            }

            TextField {
                id: sinceField
                Layout.preferredWidth: 100
                placeholderText: qsTr("YYYY-MM-DD")
                activeFocusOnTab: true
                Accessible.name: qsTr("History start date")
                Accessible.description: qsTr("Only show entries at or after this date")
            }

            Label {
                text: qsTr("Until:")
            }

            TextField {
                id: untilField
                Layout.preferredWidth: 100
                placeholderText: qsTr("YYYY-MM-DD")
                activeFocusOnTab: true
                Accessible.name: qsTr("History end date")
                Accessible.description: qsTr("Only show entries at or before this date")
            }

            Button {
                text: qsTr("Query")
                enabled: !PackageController.busy
                activeFocusOnTab: true
                Accessible.name: qsTr("Run history query")
                onClicked: historyDialog.runQuery(false)
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Load More")
                visible: historyDialog.lastBatch >= historyDialog.pageSize
                enabled: !PackageController.busy
                activeFocusOnTab: true
                Accessible.name: qsTr("Load more history entries")
                onClicked: historyDialog.runQuery(true)
            }
        }

        // ===== 見出し行 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label { text: qsTr("Timestamp"); Layout.preferredWidth: 150; font.bold: true }
            Label { text: qsTr("Action"); Layout.preferredWidth: 130; font.bold: true }
            Label { text: qsTr("Name"); Layout.fillWidth: true; font.bold: true }
            Label { text: qsTr("Edition"); Layout.preferredWidth: 120; font.bold: true }
            Label { text: qsTr("Arch"); Layout.preferredWidth: 60; font.bold: true }
            Label { text: qsTr("Repo"); Layout.preferredWidth: 100; font.bold: true }
            Label { text: qsTr("Requested By"); Layout.preferredWidth: 90; font.bold: true }
            Label { text: qsTr("Command"); Layout.preferredWidth: 120; font.bold: true }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth

                ListView {
                    id: historyTable
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: historyDialog.displayRows()
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: RowLayout {
                        width: historyTable.width - 4
                        required property var modelData

                        function fmtTime(e) {
                            var t = Number(e.timestamp !== undefined ? e.timestamp
                                              : (e.time !== undefined ? e.time : e.date))
                            if (isNaN(t) || t === 0) return ""
                            if (t < 1e11) t *= 1000
                            return Qt.formatDateTime(new Date(t), "yyyy-MM-dd hh:mm:ss")
                        }

                        Label { text: fmtTime(modelData); Layout.preferredWidth: 150; elide: Text.ElideRight }
                        Label { text: String(modelData.action || ""); textFormat: Text.PlainText; Layout.preferredWidth: 130; elide: Text.ElideRight }
                        Label { text: String(modelData.name || ""); textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: String(modelData.edition || ""); textFormat: Text.PlainText; Layout.preferredWidth: 120; elide: Text.ElideRight }
                        Label { text: String(modelData.arch || ""); textFormat: Text.PlainText; Layout.preferredWidth: 60; elide: Text.ElideRight }
                        Label { text: String(modelData.repoAlias || ""); textFormat: Text.PlainText; Layout.preferredWidth: 100; elide: Text.ElideRight }
                        Label { text: String(modelData.requestedBy || ""); textFormat: Text.PlainText; Layout.preferredWidth: 90; elide: Text.ElideRight }
                        Label { text: String(modelData.command || ""); textFormat: Text.PlainText; Layout.preferredWidth: 120; elide: Text.ElideRight }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: historyTable.count === 0 && !PackageController.busy
                text: qsTr("No history entries found")
                color: palette.placeholderText
            }
        }

        ProgressBar {
            indeterminate: true
            visible: PackageController.busy
            Layout.fillWidth: true
            Accessible.name: qsTr("Loading history")
        }
    }
}
