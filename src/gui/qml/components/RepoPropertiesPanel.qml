import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    property var repo: null
    property bool hasChanges: false

    signal applyRequested(var changes)

    // 現在の編集値を取得
    function getChanges() {
        if (!repo) return {}
        return {
            "alias":        repo.alias,
            "name":         nameField.text,
            "url":          urlField.text,
            "enabled":      enabledCheck.checked,
            "autoRefresh":  autoRefreshCheck.checked,
            "keepPackages": keepPkgCheck.checked,
            "priority":     prioritySpin.value,
            "type":         repo.type || ""
        }
    }

    function checkChanges() {
        if (!repo) { hasChanges = false; return }
        hasChanges = nameField.text !== (repo.name || "") ||
                     urlField.text !== (repo.rawUrl || repo.url || "") ||
                     enabledCheck.checked !== (repo.enabled || false) ||
                     autoRefreshCheck.checked !== (repo.autoRefresh || false) ||
                     keepPkgCheck.checked !== (repo.keepPackages || false) ||
                     prioritySpin.value !== (repo.priority ?? 99)
    }

    padding: 12

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        visible: root.repo !== null

        Label {
            text: qsTr("Properties")
            font.bold: true
            font.pixelSize: 14
        }

        GridLayout {
            columns: 4
            columnSpacing: 16
            rowSpacing: 6
            Layout.fillWidth: true

            // 名前
            Label { text: qsTr("Name:") }
            TextField {
                id: nameField
                Layout.fillWidth: true
                Layout.columnSpan: 3
                text: root.repo ? (root.repo.name || "") : ""
                onTextChanged: root.checkChanges()
                enabled: !(root.repo && root.repo.service)
            }

            // URL
            Label { text: qsTr("URL:") }
            TextField {
                id: urlField
                Layout.fillWidth: true
                Layout.columnSpan: 3
                text: root.repo ? (root.repo.rawUrl || root.repo.url || "") : ""
                onTextChanged: root.checkChanges()
                enabled: !(root.repo && root.repo.service)
            }

            // チェックボックス行
            CheckBox {
                id: enabledCheck
                text: qsTr("Enabled")
                checked: root.repo ? (root.repo.enabled || false) : false
                onToggled: root.checkChanges()
            }
            CheckBox {
                id: autoRefreshCheck
                text: qsTr("Auto-refresh")
                checked: root.repo ? (root.repo.autoRefresh || false) : false
                onToggled: root.checkChanges()
            }
            CheckBox {
                id: keepPkgCheck
                text: qsTr("Keep Packages")
                checked: root.repo ? (root.repo.keepPackages || false) : false
                onToggled: root.checkChanges()
            }
            Item { Layout.fillWidth: true }

            // 優先度
            Label { text: qsTr("Priority:") }
            SpinBox {
                id: prioritySpin
                from: 0; to: 200
                value: root.repo ? (root.repo.priority ?? 99) : 99
                onValueChanged: root.checkChanges()
            }

            // タイプ (読取専用) 
            Label { text: qsTr("Type:") }
            Label {
                text: root.repo ? (root.repo.type || qsTr("Unknown")) : ""
                color: palette.placeholderText
            }
        }

        // サービス警告
        Label {
            visible: root.repo !== null && (root.repo.service || "") !== ""
            text: qsTr("This repository is managed by service \"%1\". Some properties may be overwritten during service refresh.")
                .arg(root.repo ? (root.repo.service || "") : "")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            color: "#d35400"
            font.pixelSize: 13
        }

    }

    // リポ選択変更時にフィールドをリセット
    onRepoChanged: {
        if (repo) {
            nameField.text = repo.name || ""
            urlField.text = repo.rawUrl || repo.url || ""
            enabledCheck.checked = repo.enabled || false
            autoRefreshCheck.checked = repo.autoRefresh || false
            keepPkgCheck.checked = repo.keepPackages || false
            prioritySpin.value = repo.priority ?? 99
        }
        hasChanges = false
    }

    // リポ未選択時のプレースホルダー
    Label {
        anchors.centerIn: parent
        text: qsTr("Select a repository")
        color: palette.placeholderText
        visible: root.repo === null
    }
}
