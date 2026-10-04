// 未信頼リポジトリ署名鍵の確認ダイアログ
// 未知の GPG 署名鍵の情報 (リポジトリ/鍵名/ID/指紋/有効期限) を表示し、
// ユーザが信頼してインポートするか否かを明示的に判断できるようにする。
// 鍵名・リポジトリ名等は信頼できないリモートメタデータ由来のため必ず PlainText で表示する。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

Dialog {
    id: root
    title: qsTr("Untrusted Repository Signing Key")
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.NoAutoClose
    standardButtons: Dialog.NoButton

    width: 520

    // 現在表示中の鍵 (keyInfo: {fingerprint, id, name, created, expires, repoAlias, repoName})
    property var currentKey: null
    // 確認待ちの鍵キュー
    property var keyQueue: []
    // 信頼インポート成功後に表示するメッセージ
    property string trustedMessage: ""
    // 信頼インポート失敗時に表示するメッセージ (バックエンドがビジー等の再試行可能な失敗)
    property string trustError: ""

    ColumnLayout {
        // 折返しラベルの幅を確定させるため、ダイアログの利用可能幅に合わせる
        width: root.availableWidth
        spacing: 12

        // ===== 鍵情報 =====
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 6

            Label {
                text: qsTr("Repository:")
                color: palette.placeholderText
            }
            Label {
                Layout.fillWidth: true
                text: root.repoText(root.currentKey)
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }

            Label {
                text: qsTr("Key name:")
                color: palette.placeholderText
            }
            Label {
                Layout.fillWidth: true
                text: root.currentKey ? (root.currentKey.name || "") : ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }

            Label {
                text: qsTr("Key ID:")
                color: palette.placeholderText
            }
            Label {
                Layout.fillWidth: true
                text: root.currentKey ? (root.currentKey.id || "") : ""
                textFormat: Text.PlainText
                font.family: "monospace"
            }

            Label {
                text: qsTr("Fingerprint:")
                color: palette.placeholderText
            }
            // 信頼できる情報源 (公式Webサイト等) と1文字ずつ比較できるよう選択可能にする
            TextEdit {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                text: root.currentKey ? root.formatFingerprint(root.currentKey.fingerprint) : ""
                textFormat: Text.PlainText
                font.family: "monospace"
                color: palette.text
                wrapMode: Text.WrapAnywhere
            }

            Label {
                text: qsTr("Created:")
                color: palette.placeholderText
            }
            Label {
                Layout.fillWidth: true
                text: root.formatCreated(root.currentKey)
                textFormat: Text.PlainText
            }

            Label {
                text: qsTr("Expires:")
                color: palette.placeholderText
            }
            Label {
                Layout.fillWidth: true
                text: root.formatExpires(root.currentKey)
                textFormat: Text.PlainText
            }
        }

        // ===== 区切り線 =====
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: palette.mid
            opacity: 0.5
        }

        // ===== 警告文 =====
        Label {
            Layout.fillWidth: true
            text: qsTr("Only trust this key if you have verified its fingerprint from a trustworthy source (for example the repository provider's official website). Packages signed with a trusted key can be installed with administrator (root) privileges.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.bold: true
            color: palette.text
        }

        // ===== 信頼インポート成功メッセージ =====
        Label {
            Layout.fillWidth: true
            visible: root.trustedMessage !== ""
            text: root.trustedMessage
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.bold: true
            color: palette.text
        }

        // ===== 信頼インポート失敗メッセージ =====
        Label {
            Layout.fillWidth: true
            visible: root.trustError !== ""
            text: root.trustError
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.bold: true
            color: palette.brightText
        }

        // ===== ボタン行 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                id: rejectButton
                // 信頼成功後は「閉じる」に切り替わる
                text: root.trustedMessage !== "" ? qsTr("Close") : qsTr("Reject")
                onClicked: root.close()
            }

            Button {
                text: qsTr("Trust and Import")
                highlighted: true
                visible: root.trustedMessage === ""
                enabled: root.currentKey !== null && !PackageController.busy
                onClicked: {
                    root.trustError = ""
                    var fp = root.currentKey ? String(root.currentKey.fingerprint) : ""
                    // ポルキット認証入力中は呼び出しがブロックされることがある
                    if (PackageController.trustKey(fp)) {
                        root.trustedMessage = qsTr("The key has been trusted. Please retry the previous operation (refresh or add repository).")
                    } else {
                        // 失敗時はダイアログと鍵表示を維持し、ビジー解除後に再試行できるようにする
                        root.trustError = qsTr("The key could not be trusted. Wait until the current operation has finished and try again, or reject the key.")
                    }
                }
            }
        }
    }

    onOpened: rejectButton.forceActiveFocus()

    onClosed: {
        root.currentKey = null
        root.trustedMessage = ""
        root.trustError = ""
        // キューに残る次の鍵を順に表示する
        if (root.keyQueue.length > 0)
            Qt.callLater(root.showNext)
    }

    // 未信頼鍵をキューに追加。可視状態でなければすぐに表示に移る。
    // 指紋が既存キュー/表示中と一致するものは無視する。
    function enqueue(keyInfo) {
        if (!keyInfo) return
        var fp = root.normalizeFingerprint(keyInfo.fingerprint)
        if (fp === "") return
        if (root.currentKey && root.normalizeFingerprint(root.currentKey.fingerprint) === fp) return
        for (var i = 0; i < root.keyQueue.length; i++) {
            if (root.normalizeFingerprint(root.keyQueue[i].fingerprint) === fp) return
        }
        root.keyQueue.push(keyInfo)
        if (!root.visible)
            root.showNext()
    }

    // キューの先頭を取り出して表示
    function showNext() {
        if (root.keyQueue.length === 0) {
            root.currentKey = null
            return
        }
        root.currentKey = root.keyQueue.shift()
        root.trustedMessage = ""
        root.trustError = ""
        root.open()
    }

    // 空白除去 + 大文字化 (比較用正規形)
    function normalizeFingerprint(fp) {
        if (!fp) return ""
        return String(fp).replace(/\s+/g, "").toUpperCase()
    }

    // 16進4文字区切りに整形
    function formatFingerprint(fp) {
        var s = root.normalizeFingerprint(fp)
        var out = ""
        for (var i = 0; i < s.length; i += 4) {
            if (i > 0) out += " "
            out += s.substring(i, i + 4)
        }
        return out
    }

    // repoName と repoAlias が異なる/alias非空なら "(alias)" を付記
    function repoText(k) {
        if (!k) return ""
        var name = k.repoName || ""
        var alias = k.repoAlias || ""
        if (alias !== "" && alias !== name)
            return name + " (" + alias + ")"
        return name
    }

    // epoch秒 → 日付表示
    function formatCreated(k) {
        if (!k || !k.created || k.created <= 0) return qsTr("Unknown")
        return new Date(k.created * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    function formatExpires(k) {
        if (!k) return ""
        if (!k.expires || k.expires <= 0) return qsTr("Never")
        return new Date(k.expires * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }
}
