/**
 * @file PromptDialog.qml
 * @brief バックエンドが要求する選択式のユーザー応答を表示するダイアログ
 *
 * Main.qmlで生成され、PackageControllerの保留プロンプトに応じて自動開閉する
 *
 * 入力 : プロンプト本文と選択肢
 * 出力 : 選択した応答
 * 依存 : PackageController (libzyppコールバックのプロンプト情報)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// バックエンドの対話プロンプト (PromptRequested) を表示する汎用ダイアログ
// PackageController.currentPrompt / pendingPromptsに連動し、複数プロンプトを順番に表示する
// 回答はactionキーのみを送信する
//
// data.buttons ([{action, label}]) をバックエンドが許可したアクションとして扱い、
// 表示ラベルはtype/actionからローカライズ済み文言へ変換する
Dialog {
    id: promptDialog  // ダイアログ自身の参照名

    // PackageControllerが保持する現在のプロンプト ({promptId, type, data})
    readonly property var prompt: PackageController.currentPrompt                                   // 現在のプロンプト情報
    // 現在のプロンプトのdata辞書 (無ければ空)
    readonly property var promptData: (prompt && prompt.data) ? prompt.data : ({})                  // プロンプトのdata辞書
    // data.typeを文字列化したプロンプト種別 (無ければ空)
    readonly property string promptType: (prompt && prompt.type) ? String(prompt.type) : ""         // プロンプト種別の文字列
    // 長文レイアウトが必要な種別 (ライセンス/ファイル競合) か
    readonly property bool isLongText: promptType === "license" || promptType === "file-conflicts"  // 長文レイアウトが必要か

    /**
     * @brief バックエンドが許可したactionの一覧を返す
     * @return actionキーの配列
     * @note data.buttonsが無ければ安全側の既定値 (gpg/licenseはreject、その他はabort) を使う
     */
    function allowedActions() {
        var buttons = promptData ? promptData.buttons : undefined
        var out = []

        // data.buttonsの各要素 (文字列または{action, label}) からactionキーを取り出す
        if (buttons && buttons.length > 0) {
            for (var i = 0; i < buttons.length; ++i) {
                var b = buttons[i]
                var action = (typeof b === "string") ? b : (b && b.action ? String(b.action) : "")

                // 空のactionは除外する
                if (action.length > 0) {
                    out.push(action)
                }
            }
        }

        // 許可されたactionが無い場合は、安全側の既定値を1つだけ使う
        if (out.length === 0) {
            out.push(promptType.indexOf("gpg") === 0 || promptType === "license" ? "reject" : "abort")
        }

        return out
    }

    /**
     * @brief プロンプト種別に対応するダイアログタイトルを返す
     * @param type プロンプト種別
     * @return 翻訳済みのタイトル、未知の種別はPackage Manager
     */
    function titleForType(type) {
        switch (type) {
        case "license":           return qsTr("License Agreement")
        case "media-change":      return qsTr("Media Change Required")
        case "file-conflicts":    return qsTr("File Conflicts")
        case "gpg-key":           return qsTr("Unknown GPG Key")
        default:                  return qsTr("Package Manager")
        }
    }

    /**
     * @brief プロンプト種別ごとの本文メッセージを組み立てる
     * @return 翻訳済みの本文、種別ごとのdata値で整形する
     */
    function messageForPrompt() {
        var d = promptData || {}
        switch (promptType) {
        // ライセンス : パッケージ名とバージョンを埋め込む
        case "license":
            return qsTr("Package \"%1\" %2 requires you to accept the following license agreement.")
                   .arg(d.package || "").arg(d.version || "")
        // メディア交換 : メディア番号、ラベル、URLを埋め込む
        case "media-change":
            return qsTr("Please insert medium %1 (%2) or make the repository URL reachable:\n%3")
                   .arg(d.mediaNr !== undefined ? d.mediaNr : "?").arg(d.label || "").arg(d.url || "")
        // ファイル競合 : 競合件数を埋め込む
        case "file-conflicts":
            return qsTr("%1 file conflict(s) were detected. Continuing will overwrite files owned by other packages.")
                   .arg(d.count || 0)
        // 未知のGPG鍵 : リポジトリ名 (無ければエイリアス) を埋め込む
        case "gpg-key":
            return qsTr("The repository \"%1\" is signed with an unknown GPG key. Check the fingerprint before trusting it.")
                   .arg(d.repoName || d.repoAlias || "")
        // その他 : バックエンドのメッセージ (無ければ汎用文言) を使う
        default:
            return d.message ? String(d.message) : qsTr("The package manager needs your input.")
        }
    }

    /**
     * @brief フィンガープリントを4文字ごとに区切って目視確認しやすくする
     * @param fp フィンガープリント文字列
     * @return 空白を除去して大文字化し4文字区切りにした文字列
     */
    function formatFingerprint(fp) {
        var s = String(fp || "").replace(/\s+/g, "").toUpperCase()
        return s.replace(/(.{4})/g, "$1 ").trim()
    }

    /**
     * @brief epoch秒を現在のロケールの日時文字列へ変換する
     * @param secs epoch秒
     * @return ローカルの日時文字列、未設定時は空文字
     */
    function formatEpoch(secs) {
        // 未設定 (0やundefined) の場合は空文字を返す
        if (!secs) {
            return ""
        }

        return Qt.formatDateTime(new Date(Number(secs) * 1000), Locale.ShortFormat)
    }

    /**
     * @brief プロンプト種別ごとの詳細情報を組み立てる
     * @return gpg-keyは鍵の詳細、その他はdata.details、無ければ空文字
     */
    function detailsForPrompt() {
        var d = promptData || {}

        // GPG鍵の場合は、鍵ID・フィンガープリント・所有者・作成日・有効期限を並べる
        if (promptType === "gpg-key") {
            return qsTr("Key ID: %1\nFingerprint: %2\nOwner: %3\nCreated: %4\nExpires: %5")
                   .arg(d.id || "").arg(formatFingerprint(d.fingerprint))
                   .arg(d.name || "").arg(formatEpoch(d.created))
                   .arg(d.expires ? formatEpoch(d.expires) : qsTr("Never"))
        }

        return d.details ? String(d.details) : ""
    }

    /**
     * @brief actionキーをボタンの表示ラベルへ変換する
     * @param action バックエンドが返すactionキー
     * @return 翻訳済みの表示ラベル
     * @note 未知のactionは外部文字列のためHTML解釈を無効化する
     */
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
        // 未知のactionはバックエンド (data.buttons) 由来の外部文字列
        // Fusion Buttonの内部IconLabelはAutoTextで描画し、'&'はニーモニックとして消費されるため、
        // escapeHtmlではなく'<' '>' '&'を除去してHTML解釈を無効化する
        default:                  return String(action).replace(/[<>&]/g, "")
        }
    }

    /**
     * @brief actionキーの補足説明 (ツールチップ) を返す
     * @param action バックエンドが返すactionキー
     * @return 翻訳済みの説明、説明が無いactionは空文字
     */
    function actionDescription(action) {
        switch (action) {
        case "trust-temporary": return qsTr("Trust this key for the current operation only (requires authentication)")
        case "trust-permanent": return qsTr("Import this key into the trusted keyring (requires authentication)")
        case "skip":            return qsTr("Skip the packages on this medium")
        default:                return ""
        }
    }

    /**
     * @brief 選択したactionをバックエンドへ回答として送る
     * @param action 送信するactionキー
     * @note PackageController::answerPromptを呼び出す
     */
    function respond(action) {
        // 回答先のプロンプトIDがある場合のみ送信する
        if (prompt && prompt.promptId !== undefined) {
            PackageController.answerPrompt(prompt.promptId, action)
        }
    }

    anchors.centerIn: parent                                                      // 親ウィンドウの中央に表示する
    width: Math.min((parent ? parent.width : 640) * 0.9, isLongText ? 680 : 520)  // 親幅の90% (最大は長文時680px、通常520px)
    title: titleForType(promptType)                                               // プロンプト種別に応じたタイトル
    modal: true                                                                   // 表示中は背後の画面を操作できないようにする
    closePolicy: Dialog.NoAutoClose                                               // 回答するまで[Esc]キーや外側のクリックでは閉じない

    // 本文、詳細、注意書きを縦に並べる
    contentItem: ColumnLayout {
        spacing: 12  // 項目の間隔

        // プロンプトの本文
        Label {
            Layout.fillWidth: true                  // 親の幅いっぱいに広げる
            text: promptDialog.messageForPrompt()   // 種別ごとに組み立てた本文
            textFormat: Text.PlainText              // 外部文字列を含むためHTMLとして解釈させない
            wrapMode: Text.WordWrap                 // 幅に収まらない場合は単語単位で折り返す
            Accessible.role: Accessible.StaticText  // スクリーンリーダー向けの役割 (静的テキスト)
            Accessible.name: text                   // スクリーンリーダー向けの名前 (本文と同じ)
        }

        // 詳細情報 (ライセンス本文や鍵情報) のスクロール領域
        ScrollView {
            Layout.fillWidth: true                                       // 親の幅いっぱいに広げる
            Layout.preferredHeight: promptDialog.isLongText ? 300 : 120  // 長文の種別では高くする
            visible: detailsArea.text.length > 0                         // 詳細がある場合のみ表示する

            // 詳細情報の表示欄 (選択してコピーできる)
            TextArea {
                id: detailsArea  // 詳細表示欄の参照名

                activeFocusOnTab: true                 // [Tab]キーでフォーカスを移動できるようにする
                text: promptDialog.detailsForPrompt()  // 種別ごとに組み立てた詳細
                textFormat: TextEdit.PlainText         // 外部文字列のためHTMLとして解釈させない
                // GPG鍵は等幅フォントで表示する
                font.family: promptDialog.promptType === "gpg-key" ? "monospace" : Qt.application.font.family
                wrapMode: TextArea.WordWrap            // 幅に収まらない場合は単語単位で折り返す
                readOnly: true                         // 表示専用で編集させない
                selectByMouse: true                    // マウスで文字列を選択できるようにする
                // スクリーンリーダー向けの名前
                Accessible.name: promptDialog.promptType === "license" ? qsTr("License text") : qsTr("Details")
            }
        }

        // GPG鍵の確認時のみ表示する自動拒否の注意書き
        Label {
            Layout.fillWidth: true                                 // 親の幅いっぱいに広げる
            visible: promptDialog.promptType.indexOf("gpg") === 0  // GPG関連のプロンプトのみ表示する
            opacity: 0.7                                           // 補足情報として控えめに表示する
            // 注意書きの文字列
            text: qsTr("If you do not answer within 60 seconds, the request is rejected automatically.")
            font.pixelSize: 11                                     // 文字サイズ
            wrapMode: Text.WordWrap                                // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // 応答ボタンを並べるフッタ
    footer: Item {
        implicitHeight: 52  // フッタの高さ

        // 応答ボタンを右寄せで横に並べる
        RowLayout {
            anchors.right: parent.right                    // フッタの右端に配置する
            anchors.rightMargin: 12                        // 右端からの余白
            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
            spacing: 8                                     // ボタンの間隔

            // バックエンドが許可したactionごとにボタンを生成する
            Repeater {
                model: promptDialog.allowedActions()  // 許可されたactionの一覧

                // action1件分の応答ボタン
                delegate: Button {
                    required property var modelData  // 応答候補のactionキー

                    activeFocusOnTab: true                                             // [Tab]キーでフォーカスを移動できるようにする
                    text: promptDialog.actionLabel(modelData)                          // actionに対応する表示ラベル
                    highlighted: modelData === promptDialog.allowedActions()[0]        // 先頭のactionを推奨操作として強調表示する
                    Accessible.name: text                                              // スクリーンリーダー向けの名前 (ボタンの文字列と同じ)
                    Accessible.description: promptDialog.actionDescription(modelData)  // スクリーンリーダー向けの説明
                    ToolTip.visible: hovered && ToolTip.text.length > 0                // 説明がある場合のみホバー時に表示する
                    ToolTip.text: promptDialog.actionDescription(modelData)            // ツールチップの説明

                    // 選択したactionをバックエンドへ回答する
                    onClicked: {
                        promptDialog.respond(modelData)
                    }
                }
            }
        }
    }

    // プロンプトキューの変化に追従して自動開閉
    Connections {
        target: PackageController  // 保留プロンプトを通知するPackageController

        /**
         * @brief 保留プロンプトの変化に追従して開閉する
         * @note 現在のプロンプトがあれば開き、無ければ閉じる
         */
        function onPendingPromptsChanged() {
            var current = PackageController.currentPrompt
            if (current && current.promptId !== undefined) {
                promptDialog.open()
            }
            else {
                promptDialog.close()
            }
        }
    }
}
