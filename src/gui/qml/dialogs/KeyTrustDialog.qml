/**
 * @file KeyTrustDialog.qml
 * @brief リポジトリ署名鍵を信頼するか確認するセキュリティダイアログ
 *
 * CommitProgressDialogまたはリポジトリ更新中に開かれる
 *
 * 入力 : 署名鍵の識別情報と信頼要求
 * 出力 : 信頼・拒否・中止の応答
 * 依存 : PackageController (libzyppの鍵確認プロンプト)
 */
// 未信頼リポジトリ署名鍵の確認ダイアログ
// 未知のGPG署名鍵の情報 (リポジトリ/鍵名/ID/指紋/有効期限) を表示し、
// ユーザが信頼してインポートするか否かを明示的に判断できるようにする
// 鍵名・リポジトリ名等は信頼できないリモートメタデータ由来のため必ずPlainTextで表示する
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// 未信頼のリポジトリ署名鍵を1件ずつ表示し、承認するか拒否するかを選ばせるダイアログ
Dialog {
    id: root  // ダイアログ自身の参照名

    // 現在表示中の鍵 (keyInfo: {fingerprint, id, name, created, expires, repoAlias, repoName})
    property var currentKey: null       // 表示中の鍵情報
    // 確認待ちの鍵キュー
    property var keyQueue: []           // 確認待ちの鍵キュー
    // 承認記録成功後に表示するメッセージ (鍵はまだこの時点ではインポートされない)
    property string trustedMessage: ""  // 承認成功時のメッセージ
    // 承認記録失敗時に表示するメッセージ (バックエンドがビジー等の再試行可能な失敗)
    property string trustError: ""      // 承認失敗時のメッセージ
    property bool awaiting: false       // 鍵承認要求の応答待ち

    /**
     * @brief 未信頼鍵をキューに追加する
     * @param keyInfo 追加する鍵情報
     * @note 可視状態でなければすぐに表示に移る
     * @note 指紋が既存キューまたは表示中の鍵と一致するものは無視する
     * @note 戻り値は無く、対象外の鍵は早期に戻る
     */
    function enqueue(keyInfo) {
        // 鍵情報が無い場合は何もしない
        if (!keyInfo) {
            return
        }

        var fp = root.normalizeFingerprint(keyInfo.fingerprint)

        // 指紋が無い鍵は比較も表示もできないため無視する
        if (fp === "") {
            return
        }

        // 表示中の鍵と同じ指紋の場合は重複として無視する
        if (root.currentKey && root.normalizeFingerprint(root.currentKey.fingerprint) === fp) {
            return
        }

        // キューに同じ指紋の鍵が既にある場合は重複として無視する
        for (var i = 0; i < root.keyQueue.length; i++) {
            if (root.normalizeFingerprint(root.keyQueue[i].fingerprint) === fp) {
                return
            }
        }

        root.keyQueue.push(keyInfo)

        // ダイアログを表示していない場合は、すぐに先頭の鍵を表示する
        if (!root.visible) {
            root.showNext()
        }
    }

    /**
     * @brief キューの先頭を取り出して表示する
     * @note キューが空ならcurrentKeyをnullにして表示を終える
     * @note 戻り値は無く、キューが空なら早期に戻る
     */
    function showNext() {
        // キューが空の場合は表示中の鍵を消して終える
        if (root.keyQueue.length === 0) {
            root.currentKey = null
            return
        }

        root.currentKey = root.keyQueue.shift()
        root.trustedMessage = ""
        root.trustError = ""
        root.open()
    }

    /**
     * @brief 指紋を比較用の正規形へ整える
     * @param fp 指紋文字列
     * @return 空白を除去して大文字化した文字列
     */
    function normalizeFingerprint(fp) {
        // 指紋が未設定の場合は空文字を返す
        if (!fp) {
            return ""
        }

        return String(fp).replace(/\s+/g, "").toUpperCase()
    }

    /**
     * @brief 指紋を16進4文字ごとに区切って整形する
     * @param fp 指紋文字列
     * @return 4文字区切りに空白を挿入した文字列
     */
    function formatFingerprint(fp) {
        var s = root.normalizeFingerprint(fp)
        var out = ""
        for (var i = 0; i < s.length; i += 4) {
            // 2つ目以降の区切りの前に空白を入れる
            if (i > 0) {
                out += " "
            }

            out += s.substring(i, i + 4)
        }
        return out
    }

    /**
     * @brief リポジトリの表示名を組み立てる
     * @param k 鍵情報
     * @return repoNameとrepoAliasが異なりaliasが非空なら"(alias)"を付記した文字列
     */
    function repoText(k) {
        // 鍵情報が無い場合は空文字を返す
        if (!k) {
            return ""
        }

        var name = k.repoName || ""
        var alias = k.repoAlias || ""

        // 別名が名前と異なる場合は、別名を括弧書きで付記する
        if (alias !== "" && alias !== name) {
            return name + " (" + alias + ")"
        }

        return name
    }

    /**
     * @brief 作成日時を現在のロケールで表示する
     * @param k 鍵情報
     * @return epoch秒をローカルの日時文字列へ変換した値、未設定時はUnknown
     */
    function formatCreated(k) {
        // 作成日時が不明な場合はUnknownを返す
        if (!k || !k.created || k.created <= 0) {
            return qsTr("Unknown")
        }

        return new Date(k.created * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    /**
     * @brief 有効期限を現在のロケールで表示する
     * @param k 鍵情報
     * @return epoch秒をローカルの日時文字列へ変換した値、無期限または未設定時はNever
     */
    function formatExpires(k) {
        // 鍵情報が無い場合は空文字を返す
        if (!k) {
            return ""
        }

        // 有効期限が無い (無期限) 場合はNeverを返す
        if (!k.expires || k.expires <= 0) {
            return qsTr("Never")
        }

        return new Date(k.expires * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    anchors.centerIn: parent                         // 親ウィンドウの中央に表示する
    width: 520                                       // ダイアログの幅
    title: qsTr("Untrusted Repository Signing Key")  // ダイアログのタイトル
    standardButtons: Dialog.NoButton                 // 標準ボタンは使わず、独自のボタン行を使う
    modal: true                                      // 表示中は背後の画面を操作できないようにする
    closePolicy: Popup.NoAutoClose                   // 判断するまで[Esc]キーや外側のクリックでは閉じない

    // 開いたときは安全側の[Reject]ボタンにフォーカスを当てる
    onOpened: {
        rejectButton.forceActiveFocus()
    }

    // 閉じたときは表示状態を初期化し、キューに残る鍵があれば続けて表示する
    onClosed: {
        root.awaiting = false
        root.currentKey = null
        root.trustedMessage = ""
        root.trustError = ""

        // キューに残る次の鍵を順に表示する
        if (root.keyQueue.length > 0) {
            Qt.callLater(root.showNext)
        }
    }

    // 鍵情報、警告文、結果メッセージ、ボタン行を縦に並べる
    ColumnLayout {
        // 折返しラベルの幅を確定させるため、ダイアログの利用可能幅に合わせる
        width: root.availableWidth  // ダイアログの利用可能幅
        spacing: 12                 // 項目の間隔

        // --- 鍵情報 ---
        GridLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            columns: 2              // 項目名と値の2列
            columnSpacing: 12       // 列の間隔
            rowSpacing: 6           // 行の間隔

            // [Repository:]の項目名
            Label {
                text: qsTr("Repository:")       // 項目名の文字列
                color: palette.placeholderText  // 値より控えめな文字色
            }

            // 鍵を要求したリポジトリの表示名
            Label {
                Layout.fillWidth: true                // 残りの幅をすべて使う
                text: root.repoText(root.currentKey)  // リポジトリ名 (別名付き)
                textFormat: Text.PlainText            // リモート由来の文字列をHTMLとして解釈させない
                wrapMode: Text.WordWrap               // 幅に収まらない場合は単語単位で折り返す
            }

            // [Key name:]の項目名
            Label {
                text: qsTr("Key name:")         // 項目名の文字列
                color: palette.placeholderText  // 値より控えめな文字色
            }

            // 鍵の名前
            Label {
                Layout.fillWidth: true                                     // 残りの幅をすべて使う
                text: root.currentKey ? (root.currentKey.name || "") : ""  // 鍵の名前 (未設定時は空)
                textFormat: Text.PlainText                                 // リモート由来の文字列をHTMLとして解釈させない
                wrapMode: Text.WordWrap                                    // 幅に収まらない場合は単語単位で折り返す
            }

            // [Key ID:]の項目名
            Label {
                text: qsTr("Key ID:")           // 項目名の文字列
                color: palette.placeholderText  // 値より控えめな文字色
            }

            // 鍵のID
            Label {
                Layout.fillWidth: true                                   // 残りの幅をすべて使う
                text: root.currentKey ? (root.currentKey.id || "") : ""  // 鍵のID (未設定時は空)
                textFormat: Text.PlainText                               // リモート由来の文字列をHTMLとして解釈させない
                font.family: "monospace"                                 // 文字を判別しやすい等幅フォント
            }

            // [Fingerprint:]の項目名
            Label {
                text: qsTr("Fingerprint:")      // 項目名の文字列
                color: palette.placeholderText  // 値より控えめな文字色
            }

            // 信頼できる情報源 (公式Webサイト等) と1文字ずつ比較できるよう選択可能にする
            TextEdit {
                Layout.fillWidth: true                                                            // 残りの幅をすべて使う
                text: root.currentKey ? root.formatFingerprint(root.currentKey.fingerprint) : ""  // 4文字区切りの指紋
                textFormat: Text.PlainText                                                        // リモート由来の文字列をHTMLとして解釈させない
                color: palette.text                                                               // 通常の文字色
                font.family: "monospace"                                                          // 文字を判別しやすい等幅フォント
                wrapMode: Text.WrapAnywhere                                                       // 幅に収まらない場合は任意の位置で折り返す
                readOnly: true                                                                    // 表示専用で編集させない
                selectByMouse: true                                                               // マウスで文字列を選択できるようにする
            }

            // [Created:]の項目名
            Label {
                text: qsTr("Created:")          // 項目名の文字列
                color: palette.placeholderText  // 値より控えめな文字色
            }

            // 鍵の作成日時
            Label {
                Layout.fillWidth: true                     // 残りの幅をすべて使う
                text: root.formatCreated(root.currentKey)  // ロケール形式の作成日時
                textFormat: Text.PlainText                 // HTMLとして解釈させない
            }

            // [Expires:]の項目名
            Label {
                text: qsTr("Expires:")          // 項目名の文字列
                color: palette.placeholderText  // 値より控えめな文字色
            }

            // 鍵の有効期限
            Label {
                Layout.fillWidth: true                     // 残りの幅をすべて使う
                text: root.formatExpires(root.currentKey)  // ロケール形式の有効期限
                textFormat: Text.PlainText                 // HTMLとして解釈させない
            }
        }

        // --- 区切り線 ---
        Rectangle {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            height: 1               // 線の太さ
            opacity: 0.5            // 控えめに表示する
            color: palette.mid      // 線の色
        }

        // --- 警告文 ---
        Label {
            Layout.fillWidth: true      // 親の幅いっぱいに広げる
            // 指紋を確認してから信頼するよう促す警告文
            text: qsTr("Only trust this key if you have verified its fingerprint from a trustworthy source (for example the repository provider's official website). Packages signed with a trusted key can be installed with administrator (root) privileges.")
            textFormat: Text.PlainText  // HTMLとして解釈させない
            color: palette.text         // 通常の文字色
            font.bold: true             // 注意を引くため太字で表示する
            wrapMode: Text.WordWrap     // 幅に収まらない場合は単語単位で折り返す
        }

        // --- 承認記録成功メッセージ ---
        Label {
            Layout.fillWidth: true               // 親の幅いっぱいに広げる
            visible: root.trustedMessage !== ""  // 承認に成功した場合のみ表示する
            text: root.trustedMessage            // 承認成功時のメッセージ
            textFormat: Text.PlainText           // HTMLとして解釈させない
            color: palette.text                  // 通常の文字色
            font.bold: true                      // 注意を引くため太字で表示する
            wrapMode: Text.WordWrap              // 幅に収まらない場合は単語単位で折り返す
        }

        // --- 承認記録失敗メッセージ ---
        Label {
            Layout.fillWidth: true           // 親の幅いっぱいに広げる
            visible: root.trustError !== ""  // 承認に失敗した場合のみ表示する
            text: root.trustError            // 承認失敗時のメッセージ
            textFormat: Text.PlainText       // HTMLとして解釈させない
            color: palette.brightText        // 失敗を目立たせる文字色
            font.bold: true                  // 注意を引くため太字で表示する
            wrapMode: Text.WordWrap          // 幅に収まらない場合は単語単位で折り返す
        }

        // --- ボタン行 ---
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 8              // ボタンの間隔

            // ボタンを右寄せにするための余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [Reject]ボタン (承認成功後は[Close]ボタン)
            Button {
                id: rejectButton  // 拒否ボタンの参照名

                // 信頼成功後は「閉じる」に切り替わる
                text: root.trustedMessage !== "" ? qsTr("Close") : qsTr("Reject")  // 状態に応じたボタンの文字列

                // 鍵を承認せずにダイアログを閉じる
                onClicked: {
                    root.close()
                }
            }

            // [Approve Key]ボタン
            Button {
                visible: root.trustedMessage === ""  // 承認成功後は不要なため非表示にする
                // 鍵が無い場合、処理中の場合、応答待ちの場合は押せないようにする
                enabled: root.currentKey !== null && !PackageController.busy && !root.awaiting
                text: qsTr("Approve Key")            // ボタンの文字列
                highlighted: true                    // 主操作として強調表示する

                // 表示中の鍵の指紋をバックエンドへ送り、承認を要求する
                onClicked: {
                    root.trustError = ""
                    root.awaiting = true
                    var fp = root.currentKey ? String(root.currentKey.fingerprint) : ""
                    // ポルキット認証ダイアログは非同期呼び出し中に表示される
                    PackageController.trustKey(fp)
                }
            }
        }
    }

    // 鍵承認要求の結果の受け口
    Connections {
        target: PackageController  // 鍵承認の結果を通知するPackageController

        /**
         * @brief 鍵承認要求の結果を受け取る
         * @param success trueなら承認記録に成功、falseなら再試行可能な失敗
         * @note 承認待ちでない通知は無視する
         * @note 成功時はtrustedMessageを設定して鍵未インポートの案内を表示し、失敗時はtrustErrorを設定して表示を維持する
         * @note 戻り値は無く、承認待ちでない通知は早期に戻る
         */
        function onKeyTrustFinished(success) {
            // このダイアログからの承認要求でない場合は無視する
            if (!root.awaiting) {
                return
            }

            root.awaiting = false

            // 成功時は再試行時にインポートされる旨を、失敗時は再試行を促す旨を表示する
            if (success) {
                root.trustedMessage = qsTr("Key approved. It will be imported when you retry the operation (for example, refresh the repository again).")
            }
            else {
                // 失敗時はダイアログと鍵表示を維持し、ビジー解除後に再試行できるようにする
                root.trustError = qsTr("The key could not be approved. Wait until the current operation has finished and try again, or reject the key.")
            }
        }
    }
}
