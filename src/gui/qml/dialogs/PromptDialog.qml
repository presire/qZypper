import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// バックエンドの対話プロンプト (PromptRequested) を表示する汎用ダイアログ。
// PackageController.currentPrompt / pendingPrompts に連動し、複数プロンプトを
// 順番に表示する。回答は action キーのみを送信する。
//
// data.buttons ([{action, label}]) をバックエンドが許可したアクションとして扱い、
// 表示ラベルは type/action からローカライズ済み文言へ変換する。
Dialog {
    id: promptDialog
    anchors.centerIn: parent
    modal: true
    closePolicy: Dialog.NoAutoClose
    width: Math.min((parent ? parent.width : 640) * 0.9, isLongText ? 680 : 520)
    title: titleForType(promptType)

    // PackageController が保持する現在のプロンプト ({promptId, type, data})。
    readonly property var prompt: PackageController.currentPrompt
    readonly property var promptData: (prompt && prompt.data) ? prompt.data : ({})
    readonly property string promptType: (prompt && prompt.type) ? String(prompt.type) : ""
    readonly property bool isLongText: promptType === "license" || promptType === "file-conflicts"
    // 未署名/署名検証失敗は危険操作扱いとし、拒否側を強調する
    readonly property bool isInsecure: promptType === "gpg-unsigned" || promptType === "gpg-verify-failed"

    // 許可された action の一覧。data.buttons が無ければ安全側の既定値を使う。
    function allowedActions() {
        var buttons = promptData ? promptData.buttons : undefined
        var out = []
        if (buttons && buttons.length > 0) {
            for (var i = 0; i < buttons.length; ++i) {
                var b = buttons[i]
                var action = (typeof b === "string") ? b : (b && b.action ? String(b.action) : "")
                if (action.length > 0)
                    out.push(action)
            }
        }
        if (out.length === 0)
            out.push(promptType.indexOf("gpg") === 0 || promptType === "license" ? "reject" : "abort")
        return out
    }

    function safeAction() {
        var actions = allowedActions()
        var safe = ["reject", "abort", "cancel", "no"]
        for (var i = 0; i < safe.length; ++i) {
            if (actions.indexOf(safe[i]) >= 0)
                return safe[i]
        }
        return actions[actions.length - 1]
    }

    function titleForType(type) {
        switch (type) {
        case "license":           return qsTr("License Agreement")
        case "media-change":      return qsTr("Media Change Required")
        case "file-conflicts":    return qsTr("File Conflicts")
        case "gpg-key":           return qsTr("Unknown GPG Key")
        case "gpg-unsigned":      return qsTr("Unsigned File")
        case "gpg-verify-failed": return qsTr("Signature Verification Failed")
        default:                  return qsTr("Package Manager")
        }
    }

    function messageForPrompt() {
        var d = promptData || {}
        switch (promptType) {
        case "license":
            return qsTr("Package \"%1\" %2 requires you to accept the following license agreement.")
                   .arg(d.package || "").arg(d.version || "")
        case "media-change":
            return qsTr("Please insert medium %1 (%2) or make the repository URL reachable:\n%3")
                   .arg(d.mediaNr !== undefined ? d.mediaNr : "?").arg(d.label || "").arg(d.url || "")
        case "file-conflicts":
            return qsTr("%1 file conflict(s) were detected. Continuing will overwrite files owned by other packages.")
                   .arg(d.count || 0)
        case "gpg-key":
            return qsTr("The repository \"%1\" is signed with an unknown GPG key. Check the fingerprint before trusting it.")
                   .arg(d.repoName || d.repoAlias || "")
        case "gpg-unsigned":
            return qsTr("\"%1\" is not signed. Accepting unsigned data is insecure.").arg(d.file || "")
        case "gpg-verify-failed":
            return qsTr("The signature of \"%1\" could not be verified. The data may have been tampered with.")
                   .arg(d.file || "")
        default:
            return d.message ? String(d.message) : qsTr("The package manager needs your input.")
        }
    }

    // フィンガープリントを 4 文字ごとに区切って目視確認しやすくする
    function formatFingerprint(fp) {
        var s = String(fp || "").replace(/\s+/g, "").toUpperCase()
        return s.replace(/(.{4})/g, "$1 ").trim()
    }

    function formatEpoch(secs) {
        if (!secs)
            return ""
        return Qt.formatDateTime(new Date(Number(secs) * 1000), Locale.ShortFormat)
    }

    function detailsForPrompt() {
        var d = promptData || {}
        if (promptType === "gpg-key") {
            return qsTr("Key ID: %1\nFingerprint: %2\nOwner: %3\nCreated: %4\nExpires: %5")
                   .arg(d.id || "").arg(formatFingerprint(d.fingerprint))
                   .arg(d.name || "").arg(formatEpoch(d.created))
                   .arg(d.expires ? formatEpoch(d.expires) : qsTr("Never"))
        }
        return d.details ? String(d.details) : ""
    }

    function actionLabel(action) {
        switch (action) {
        case "yes":               return qsTr("Yes")
        case "no":                return qsTr("No")
        case "ok":                return qsTr("OK")
        case "retry":             return qsTr("Retry")
        case "skip":              return qsTr("Skip")
        case "cancel":            return qsTr("Cancel")
        case "continue":          return qsTr("Continue")
        case "trust":             return qsTr("Trust")
        case "abort":             return qsTr("Abort")
        case "accept":            return qsTr("Accept License")
        case "reject":            return promptType === "license" ? qsTr("Reject License") : qsTr("Reject")
        case "trust-temporary":   return qsTr("Trust Temporarily")
        case "trust-permanent":   return qsTr("Trust Permanently")
        case "accept-once":       return qsTr("Accept Once (Insecure)")
        default:                  return action
        }
    }

    function actionDescription(action) {
        switch (action) {
        case "trust-temporary": return qsTr("Trust this key for the current operation only (requires authentication)")
        case "trust-permanent": return qsTr("Import this key into the trusted keyring (requires authentication)")
        case "accept-once":     return qsTr("Use the unverified data this time only")
        case "skip":            return qsTr("Skip the packages on this medium")
        default:                return ""
        }
    }

    function respond(action) {
        if (prompt && prompt.promptId !== undefined)
            PackageController.answerPrompt(prompt.promptId, action)
    }

    // プロンプトキューの変化に追従して自動開閉
    Connections {
        target: PackageController
        function onPendingPromptsChanged() {
            var current = PackageController.currentPrompt
            if (current && current.promptId !== undefined)
                promptDialog.open()
            else
                promptDialog.close()
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.bold: promptDialog.isInsecure
            color: promptDialog.isInsecure ? "#F44336" : palette.text
            text: promptDialog.messageForPrompt()
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: promptDialog.isLongText ? 300 : 120
            visible: detailsArea.text.length > 0

            TextArea {
                id: detailsArea
                readOnly: true
                wrapMode: TextArea.WordWrap
                textFormat: TextEdit.PlainText
                selectByMouse: true
                font.family: promptDialog.promptType === "gpg-key" ? "monospace" : Qt.application.font.family
                text: promptDialog.detailsForPrompt()
                activeFocusOnTab: true
                Accessible.name: promptDialog.promptType === "license" ? qsTr("License text") : qsTr("Details")
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: promptDialog.promptType.indexOf("gpg") === 0
            opacity: 0.7
            font.pixelSize: 11
            text: qsTr("If you do not answer within 60 seconds, the request is rejected automatically.")
        }
    }

    footer: Item {
        implicitHeight: 52

        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Repeater {
                model: promptDialog.allowedActions()

                delegate: Button {
                    required property var modelData
                    text: promptDialog.actionLabel(modelData)
                    highlighted: promptDialog.isInsecure
                                 ? modelData === promptDialog.safeAction()
                                 : modelData === promptDialog.allowedActions()[0]
                    activeFocusOnTab: true
                    Accessible.name: text
                    Accessible.description: promptDialog.actionDescription(modelData)
                    ToolTip.visible: hovered && ToolTip.text.length > 0
                    ToolTip.text: promptDialog.actionDescription(modelData)
                    onClicked: promptDialog.respond(modelData)
                }
            }
        }
    }
}
