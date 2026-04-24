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

    property bool showDetails: commitSettings.showDetails
    property bool showSummaryPage: commitSettings.showSummaryPage
    property string currentPackage: ""
    property int overallPercent: 0
    property string currentStage: "downloading"

    // PackageStateChanged 未対応の旧バックエンド用:
    // CommitProgressChanged の stage/packageName 変化から状態遷移を推定
    property string _prevPkg: ""
    property string _prevStage: ""
    property bool _hasStateSignal: false

    ListModel { id: todoModel }
    ListModel { id: downloadsModel }
    ListModel { id: doingModel }
    ListModel { id: doneModel }

    Settings {
        id: commitSettings
        category: "commitProgress"
        property bool showDetails: true
        property bool showSummaryPage: true
    }

    function initFromPendingChanges(pendingList) {
        todoModel.clear()
        downloadsModel.clear()
        doingModel.clear()
        doneModel.clear()
        overallPercent = 0
        currentPackage = ""
        currentStage = "downloading"
        _prevPkg = ""
        _prevStage = ""
        _hasStateSignal = false

        var sorted = pendingList.slice().sort(function(a, b) {
            return a.name.localeCompare(b.name)
        })
        for (var i = 0; i < sorted.length; i++) {
            var statusVal = sorted[i].status || 0
            var isRemoval = (statusVal === 6 || statusVal === 7)
            todoModel.append({
                name: sorted[i].name,
                downloadDone: false,
                isRemoval: isRemoval
            })
        }
    }

    function moveItem(fromModel, toModel, packageName) {
        for (var i = 0; i < fromModel.count; i++) {
            if (fromModel.get(i).name === packageName) {
                var item = fromModel.get(i)
                toModel.append({
                    name: item.name,
                    downloadDone: item.downloadDone,
                    isRemoval: item.isRemoval
                })
                fromModel.remove(i)
                return true
            }
        }
        return false
    }

    function setDownloadDone(model, packageName, done) {
        for (var i = 0; i < model.count; i++) {
            if (model.get(i).name === packageName) {
                model.setProperty(i, "downloadDone", done)
                return
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // Header
        RowLayout {
            Layout.fillWidth: true

            Label {
                text: commitProgressDialog.currentPackage !== ""
                    ? commitProgressDialog.currentPackage
                    : qsTr("Preparing...")
                font.bold: true
                font.pixelSize: 15
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Button {
                id: detailsToggleBtn
                text: commitProgressDialog.showDetails
                    ? qsTr("Hide Details")
                    : qsTr("Show Details")
                onClicked: {
                    commitProgressDialog.showDetails = !commitProgressDialog.showDetails
                    commitSettings.showDetails = commitProgressDialog.showDetails
                }
            }
        }

        // Details frame
        Frame {
            id: detailsFrame
            visible: commitProgressDialog.showDetails
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                spacing: 10

                // To Do column
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1

                    Label {
                        text: todoModel.count > 0
                            ? qsTr("To Do (%1)").arg(todoModel.count)
                            : qsTr("To Do")
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    ListView {
                        id: todoListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: todoModel
                        clip: true
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: ItemDelegate {
                            width: todoListView.width
                            height: 24
                            contentItem: Label {
                                text: model.isRemoval ? ("- " + model.name) : model.name
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                            }
                            background: null
                        }
                    }
                }

                // Separator
                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: palette.mid
                }

                // Downloads + Doing column
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1

                    Label {
                        text: downloadsModel.count > 0
                            ? qsTr("Downloads (%1)").arg(downloadsModel.count)
                            : qsTr("Downloads")
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    ListView {
                        id: downloadsListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: downloadsModel
                        clip: true
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: ItemDelegate {
                            width: downloadsListView.width
                            height: 24
                            contentItem: RowLayout {
                                spacing: 4
                                Label {
                                    text: model.downloadDone ? "⬇✓" : "⬇"
                                    color: model.downloadDone ? "#4CAF50" : "#2196F3"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 28
                                }
                                Label {
                                    text: model.name
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                }
                            }
                            background: null
                        }
                    }

                    Label {
                        text: qsTr("Doing")
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    ListView {
                        id: doingListView
                        Layout.fillWidth: true
                        Layout.maximumHeight: 70
                        Layout.preferredHeight: 70
                        model: doingModel
                        clip: true
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: ItemDelegate {
                            width: doingListView.width
                            height: 24
                            contentItem: Label {
                                text: model.name
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                            }
                            background: null
                        }
                    }
                }

                // Separator
                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: palette.mid
                }

                // Done column
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
                            contentItem: Label {
                                text: model.name
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                            }
                            background: null
                        }
                    }
                }
            }
        }

        // Spacer (fills space when details hidden)
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: !commitProgressDialog.showDetails
            visible: !commitProgressDialog.showDetails
        }

        // Progress bar
        ProgressBar {
            id: totalProgressBar
            Layout.fillWidth: true
            from: 0
            to: 100
            value: commitProgressDialog.overallPercent
            indeterminate: commitProgressDialog.overallPercent === 0
        }

        // Percentage label
        Label {
            text: commitProgressDialog.overallPercent + "%"
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            font.pixelSize: 13
        }

        // Cancel button
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                onClicked: PackageController.cancelOperation()
            }
            Item { Layout.fillWidth: true }
        }

        // Show Summary Page checkbox
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
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
                moveItem(todoModel, downloadsModel, packageName)
                setDownloadDone(downloadsModel, packageName, false)
                break
            case "download_end":
                setDownloadDone(downloadsModel, packageName, true)
                break
            case "cached":
                moveItem(todoModel, downloadsModel, packageName)
                setDownloadDone(downloadsModel, packageName, true)
                break
            case "install_start":
                if (!moveItem(downloadsModel, doingModel, packageName))
                    if (!moveItem(todoModel, doingModel, packageName))
                        doingModel.append({ name: packageName, downloadDone: true, isRemoval: false })
                break
            case "install_end":
                if (!moveItem(doingModel, doneModel, packageName))
                    doneModel.append({ name: packageName, downloadDone: true, isRemoval: false })
                break
            case "remove_start":
                if (!moveItem(todoModel, doingModel, packageName))
                    doingModel.append({ name: packageName, downloadDone: false, isRemoval: true })
                break
            case "remove_end":
                if (!moveItem(doingModel, doneModel, packageName))
                    doneModel.append({ name: packageName, downloadDone: false, isRemoval: true })
                break
            }
        }

        function onCommitProgressChanged(packageName, percentage, stage,
                                          totalSteps, completedSteps,
                                          overallPercentage) {
            commitProgressDialog.currentPackage = packageName
            commitProgressDialog.overallPercent = overallPercentage
            commitProgressDialog.currentStage = stage
            totalProgressBar.indeterminate = false

            if (commitProgressDialog._hasStateSignal || packageName === "")
                return

            // PackageStateChanged 未対応バックエンド用フォールバック:
            // stage と packageName の変化から状態遷移を推定
            var prev = commitProgressDialog._prevPkg
            var prevStage = commitProgressDialog._prevStage

            if (stage === "downloading") {
                moveItem(todoModel, downloadsModel, packageName)
            } else if (stage === "installing") {
                if (prev !== "" && prev !== packageName)
                    moveItem(doingModel, doneModel, prev)
                setDownloadDone(downloadsModel, packageName, true)
                if (!moveItem(downloadsModel, doingModel, packageName))
                    if (!moveItem(todoModel, doingModel, packageName))
                        doingModel.append({ name: packageName, downloadDone: true, isRemoval: false })
            } else if (stage === "removing") {
                if (prev !== "" && prev !== packageName)
                    moveItem(doingModel, doneModel, prev)
                if (!moveItem(todoModel, doingModel, packageName))
                    doingModel.append({ name: packageName, downloadDone: false, isRemoval: true })
            }

            commitProgressDialog._prevPkg = packageName
            commitProgressDialog._prevStage = stage
        }

        function onCommitResultChanged() {
            // 残りの Doing アイテムを Done に移動
            while (doingModel.count > 0) {
                var item = doingModel.get(0)
                doneModel.append({
                    name: item.name,
                    downloadDone: item.downloadDone,
                    isRemoval: item.isRemoval
                })
                doingModel.remove(0)
            }

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
