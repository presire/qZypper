import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

Dialog {
    id: commitProgressDialog
    title: qsTr("Installing / Removing Packages")
    anchors.centerIn: parent
    modal: true
    closePolicy: Dialog.NoAutoClose
    width: Math.min(root.width * 0.85, 800)
    height: Math.min(root.height * 0.9, 800)
    standardButtons: Dialog.NoButton

    background: Rectangle {
        color: palette.window
        border.color: palette.highlight
        border.width: 2
    }
    header: Label {
        text: commitProgressDialog.title
        font.bold: true
        padding: 10
    }
    footer: Item { implicitHeight: 0 }

    property bool showSummaryPage: commitSettings.showSummaryPage
    property int overallPercent: 0

    // 現在実行中の単一アクティビティ (カード表示用)
    property string currentPackage: ""
    property string currentStage: ""
    property int currentPercent: 0
    property bool currentRemoval: false

    // PackageStateChanged 未対応の旧バックエンド用:
    // CommitProgressChanged の stage/packageName 変化から状態遷移を推定
    property string _prevPkg: ""
    property string _prevStage: ""
    property bool _hasStateSignal: false

    ListModel { id: pendingModel }
    ListModel { id: doneModel }

    Settings {
        id: commitSettings
        category: "commitProgress"
        property bool showSummaryPage: true
    }

    // -- モデル操作ヘルパー (重複なし / 現在カードを持つ堅牢な検索・移動) --

    function indexOf(model, name) {
        for (var i = 0; i < model.count; i++) {
            if (model.get(i).name === name)
                return i
        }
        return -1
    }

    function removeFromPending(name) {
        var idx = indexOf(pendingModel, name)
        if (idx >= 0)
            pendingModel.remove(idx)
    }

    function setPendingDownloadDone(name, done) {
        var idx = indexOf(pendingModel, name)
        if (idx >= 0)
            pendingModel.setProperty(idx, "downloadDone", done)
    }

    // ダウンロード完了の記録。現在カードが同一パッケージなら Pending へ戻し、
    // Pending に無ければダウンロード済みとして追加 (重複防止)。
    function markDownloaded(name) {
        if (currentPackage === name && currentStage === "downloading") {
            appendPending(name, false, true)
            clearCurrent()
            return
        }
        if (indexOf(pendingModel, name) >= 0) {
            setPendingDownloadDone(name, true)
        } else if (indexOf(doneModel, name) < 0) {
            appendPending(name, false, true)
        }
    }

    function appendPending(name, isRemoval, downloadDone) {
        if (indexOf(pendingModel, name) < 0) {
            pendingModel.append({
                name: name,
                isRemoval: isRemoval,
                downloadDone: downloadDone
            })
        }
    }

    function appendDone(name, isRemoval) {
        if (indexOf(doneModel, name) < 0)
            doneModel.append({ name: name, isRemoval: isRemoval })
    }

    function clearCurrent() {
        currentPackage = ""
        currentStage = ""
        currentPercent = 0
        currentRemoval = false
    }

    // 現在カードのアクティビティを完了させる。ダウンロード途中なら Pending へ返す。
    function finalizeCurrent() {
        if (currentPackage === "")
            return
        if (currentStage === "downloading") {
            // ダウンロードのみ完了 → ダウンロード済みとして Pending へ戻す
            appendPending(currentPackage, false, true)
        } else {
            // install / remove 完了 → Done へ
            appendDone(currentPackage, currentRemoval)
        }
        clearCurrent()
    }

    // 新規アクティビティを現在カードに表示する。カードが別パッケージで占有中なら先に完了させる。
    function startCurrent(name, stage, isRemoval) {
        if (currentPackage !== "" && currentPackage !== name)
            finalizeCurrent()
        removeFromPending(name)
        currentPackage = name
        currentStage = stage
        currentRemoval = isRemoval
    }

    function phaseText(stage) {
        switch (stage) {
        case "downloading": return qsTr("Downloading")
        case "installing": return qsTr("Installing")
        case "removing": return qsTr("Removing")
        default: return qsTr("Working")
        }
    }

    function tagText(isRemoval, downloadDone) {
        if (isRemoval)
            return qsTr("to remove")
        if (downloadDone)
            return qsTr("downloaded")
        return qsTr("to download")
    }

    function initFromPendingChanges(pendingList) {
        pendingModel.clear()
        doneModel.clear()
        overallPercent = 0
        currentPackage = ""
        currentStage = ""
        currentPercent = 0
        currentRemoval = false
        _prevPkg = ""
        _prevStage = ""
        _hasStateSignal = false

        var sorted = pendingList.slice().sort(function(a, b) {
            return a.name.localeCompare(b.name)
        })
        for (var i = 0; i < sorted.length; i++) {
            var statusVal = sorted[i].status || 0
            var isRemoval = (statusVal === 6 || statusVal === 7)
            pendingModel.append({
                name: sorted[i].name,
                isRemoval: isRemoval,
                downloadDone: false
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // 現在の操作カード (常時表示)
        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 100

            background: Rectangle {
                color: Qt.rgba(palette.highlight.r, palette.highlight.g,
                               palette.highlight.b, 0.12)
                border.color: palette.highlight
                border.width: 1
                radius: 4
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: commitProgressDialog.currentPackage !== ""
                            ? (commitProgressDialog.currentRemoval
                               ? ("- " + commitProgressDialog.currentPackage)
                               : commitProgressDialog.currentPackage)
                            : qsTr("Preparing...")
                        font.bold: true
                        font.pixelSize: 15
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        color: palette.text
                    }
                    Label {
                        text: commitProgressDialog.currentPackage !== ""
                            ? commitProgressDialog.phaseText(commitProgressDialog.currentStage)
                            : qsTr("Idle")
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 84
                        color: commitProgressDialog.currentPackage !== ""
                            ? palette.highlight
                            : palette.placeholderText
                    }
                }

                ProgressBar {
                    id: packageProgressBar
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: commitProgressDialog.currentPercent
                    indeterminate: commitProgressDialog.currentPackage === ""
                        || commitProgressDialog.currentPercent === 0
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Label {
                        text: commitProgressDialog.currentPackage !== ""
                            ? (commitProgressDialog.currentPercent + "%")
                            : ""
                        font.pixelSize: 12
                        color: palette.text
                    }
                }
            }
        }

        // Pending と Done の2列 (下部を占有)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // Pending 列
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1

                Label {
                    text: pendingModel.count > 0
                        ? qsTr("Pending (%1)").arg(pendingModel.count)
                        : qsTr("Pending")
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    color: palette.text
                }

                ListView {
                    id: pendingListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: pendingModel
                    clip: true
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: ItemDelegate {
                        width: pendingListView.width
                        height: 24
                        contentItem: RowLayout {
                            spacing: 6
                            Label {
                                text: commitProgressDialog.tagText(model.isRemoval, model.downloadDone)
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.preferredWidth: 88
                                color: (model.downloadDone || model.isRemoval)
                                    ? palette.highlight
                                    : palette.text
                            }
                            Label {
                                text: model.isRemoval ? ("- " + model.name) : model.name
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                                color: palette.text
                                Layout.fillWidth: true
                            }
                        }
                        background: null
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 1
                color: palette.mid
            }

            // Done 列
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1

                Label {
                    text: doneModel.count > 0
                        ? qsTr("Done (%1)").arg(doneModel.count)
                        : qsTr("Done")
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    color: palette.placeholderText
                }

                ListView {
                    id: doneListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: doneModel
                    clip: true
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: ItemDelegate {
                        width: doneListView.width
                        height: 24
                        contentItem: RowLayout {
                            spacing: 6
                            Label {
                                text: qsTr("Done")
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.preferredWidth: 88
                                color: palette.placeholderText
                            }
                            Label {
                                text: model.isRemoval ? ("- " + model.name) : model.name
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                                color: palette.placeholderText
                                Layout.fillWidth: true
                            }
                        }
                        background: null
                    }
                }
            }
        }

        // コンパクトなフッター行: 全体進捗 + パーセント + キャンセル + サマリー表示
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ProgressBar {
                id: totalProgressBar
                Layout.fillWidth: true
                from: 0
                to: 100
                value: commitProgressDialog.overallPercent
                indeterminate: commitProgressDialog.overallPercent === 0
            }

            Label {
                text: commitProgressDialog.overallPercent + "%"
                Layout.preferredWidth: 44
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 13
            }

            Button {
                text: qsTr("Cancel")
                onClicked: PackageController.cancelOperation()
            }

            CheckBox {
                text: qsTr("Show Summary Page")
                checked: commitProgressDialog.showSummaryPage
                onToggled: {
                    commitProgressDialog.showSummaryPage = checked
                    commitSettings.showSummaryPage = checked
                }
            }
        }
    }

    Connections {
        target: PackageController

        function onPackageStateChanged(packageName, event) {
            commitProgressDialog._hasStateSignal = true
            switch (event) {
            case "download_start":
                commitProgressDialog.currentPercent = 0
                commitProgressDialog.startCurrent(packageName, "downloading", false)
                break
            case "download_end":
                commitProgressDialog.markDownloaded(packageName)
                break
            case "cached":
                commitProgressDialog.markDownloaded(packageName)
                break
            case "install_start":
                commitProgressDialog.currentPercent = 0
                commitProgressDialog.startCurrent(packageName, "installing", false)
                break
            case "install_end":
                commitProgressDialog.appendDone(packageName, false)
                commitProgressDialog.removeFromPending(packageName)
                if (commitProgressDialog.currentPackage === packageName)
                    commitProgressDialog.clearCurrent()
                break
            case "remove_start":
                commitProgressDialog.currentPercent = 0
                commitProgressDialog.startCurrent(packageName, "removing", true)
                break
            case "remove_end":
                commitProgressDialog.appendDone(packageName, true)
                commitProgressDialog.removeFromPending(packageName)
                if (commitProgressDialog.currentPackage === packageName)
                    commitProgressDialog.clearCurrent()
                break
            }
        }

        function onCommitProgressChanged(packageName, percentage, stage,
                                          totalSteps, completedSteps,
                                          overallPercentage) {
            commitProgressDialog.overallPercent = overallPercentage
            commitProgressDialog.currentPercent = percentage
            totalProgressBar.indeterminate = false

            if (commitProgressDialog._hasStateSignal || packageName === "")
                return

            // PackageStateChanged 未対応バックエンド用フォールバック:
            // stage と packageName の変化から状態遷移を推定
            var prev = commitProgressDialog._prevPkg
            var prevStage = commitProgressDialog._prevStage

            if (stage === "downloading") {
                commitProgressDialog.startCurrent(packageName, "downloading", false)
            } else if (stage === "installing") {
                if (prev !== "" && prev !== packageName)
                    commitProgressDialog.finalizeCurrent()
                commitProgressDialog.startCurrent(packageName, "installing", false)
            } else if (stage === "removing") {
                if (prev !== "" && prev !== packageName)
                    commitProgressDialog.finalizeCurrent()
                commitProgressDialog.startCurrent(packageName, "removing", true)
            }

            commitProgressDialog._prevPkg = packageName
            commitProgressDialog._prevStage = stage
        }

        function onCommitResultChanged() {
            // 残りの現在アクティビティを完了させる
            commitProgressDialog.finalizeCurrent()

            commitProgressDialog.close()
            var cr = PackageController.commitResult
            resultDialog.resultSuccess = cr.success || false
            resultDialog.resultData = cr
            if (commitProgressDialog.showSummaryPage)
                resultDialog.open()
            else
                performSearch()
        }
    }
}
