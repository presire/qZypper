/**
 * @file AddRepoDialog.qml
 * @brief 新しいソフトウェアリポジトリを追加するダイアログ
 *
 * RepoManagerDrawerContentから開かれる
 *
 * 入力 : リポジトリの名前、URL、設定値
 * 出力 : 追加要求
 * 依存 : PackageController (D-Busバックエンドのリポジトリ追加API), UrlTypeSelector
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui
import "../components" as Components

// リポジトリ追加ダイアログ (ステップ1でURLを入力し、ステップ2でプロパティを設定して追加する)
Dialog {
    id: root  // ダイアログ自身の参照名

    property int currentStep: 0        // StackLayoutに表示する入力ステップ
    property bool awaiting: false      // 追加要求の応答待ち
    property string errorMessage: ""   // 追加失敗時のエラー表示
    property var presets: []           // コミュニティリポジトリのプリセット一覧 (対象外のディストリビューションでは空)
    property var selectedPreset: null  // 選択中のプリセット (カスタムURLの場合はnull)
    property string autoName: ""       // 最後に自動生成した名前 (入力欄がこの値のままなら利用者は編集していない)
    property string autoAlias: ""      // 最後に自動生成したエイリアス (入力欄がこの値のままなら利用者は編集していない)

    signal repoAdded()  // 追加成功後に親へリポジトリ再読込を依頼する

    anchors.centerIn: parent           // 親ウィンドウの中央に表示する
    width: 600                         // ダイアログの幅
    height: 540                        // ダイアログの高さ
    title: qsTr("Add Repository")      // ダイアログのタイトル
    standardButtons: Dialog.NoButton   // 標準ボタンは使わず、各ステップの独自ボタンを使う
    modal: true                        // 表示中は背後の画面を操作できないようにする
    // [Esc]キーでのみ閉じる (外側のクリックでは閉じない)
    // 応答待ちの間は閉じない (閉じると失敗理由の表示先が無くなるため)
    closePolicy: awaiting ? Dialog.NoAutoClose : Dialog.CloseOnEscape

    // ダイアログを開くたびに入力状態を初期値へ戻す
    onOpened: {
        currentStep = 0
        awaiting = false
        errorMessage = ""
        urlSelector.clear()
        nameField.text = ""
        aliasField.text = ""
        prioritySpin.value = 99
        autoRefreshCheck.checked = true
        keepPkgCheck.checked = false
        selectedPreset = null
        autoName = ""
        autoAlias = ""
        presetCombo.currentIndex = 0

        // プリセット一覧を取得する (PackageController::loadRepoPresetsを呼び出し、完了はrepoPresetsReadyシグナルで受け取る)
        PackageController.loadRepoPresets()
    }

    /**
     * @brief プリセットの内容を入力欄へ反映する (nullならカスタム入力の初期状態へ戻す)
     * @param preset 選択したプリセットのマップ (url、name、alias、priority、autoRefresh)
     * @note 戻り値は無く、selectedPresetと各入力欄の値を書き換える
     *       URLは展開前の値 ($releasever等を含みうる) をそのまま設定する
     */
    function applyPreset(preset) {
        root.selectedPreset = preset
        root.autoName = ""
        root.autoAlias = ""

        // カスタム入力が選ばれたときは、プリセットで埋めた入力欄を初期状態へ戻す
        if (preset === null) {
            urlSelector.clear()
            nameField.text = ""
            aliasField.text = ""
            prioritySpin.value = 99
            autoRefreshCheck.checked = true
            return
        }

        urlSelector.setUrl(preset.url)
        nameField.text = preset.name
        aliasField.text = preset.alias
        prioritySpin.value = preset.priority
        autoRefreshCheck.checked = preset.autoRefresh
    }

    /**
     * @brief URLからリポジトリ名の既定値を生成する
     * @param url 入力されたURL
     * @return ホスト名、ホストが無いローカルURLはパスの最後の要素 (どちらも得られなければ"repository")
     * @note "dir:///srv/localrepo"は"localrepo"、"https://download.opensuse.org/..."は"download.opensuse.org"になる
     */
    function defaultName(url) {
        // スキーム区切りの後をホスト部 ("/"、"?"、"#"の手前まで) とパス部に分け、パス部からクエリとフラグメントを除く
        var match = url.match(/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\/([^\/?#]*)([^?#]*)/)
        if (!match) {
            return "repository"
        }

        // ホスト部から利用者情報 (user@) とポート番号を除く
        var host = match[1].replace(/^.*@/, "").replace(/:\d*$/, "")
        if (host !== "") {
            return host
        }

        var segments = match[2].split("/").filter(function (segment) { return segment !== "" })
        return segments.length > 0 ? segments[segments.length - 1] : "repository"
    }

    /**
     * @brief エイリアスが未入力または自動生成のままなら、名前から生成し直す
     * @note 名前の英数字・_・-以外を_へ置き換えて生成する
     *       利用者が編集したエイリアスは書き換えない
     */
    function syncAutoAlias() {
        if (aliasField.text !== "" && aliasField.text !== root.autoAlias) {
            return
        }

        aliasField.text = nameField.text.replace(/[^a-zA-Z0-9_-]/g, "_")
        root.autoAlias = aliasField.text
    }

    // ダイアログ本体 (入力ステップを切り替えて表示する)
    contentItem: StackLayout {
        currentIndex: root.currentStep  // 表示する入力ステップ

        // --- ステップ1: URL入力 ---
        ColumnLayout {
            spacing: 16  // 項目の間隔

            // ステップ1の見出し
            Label {
                text: qsTr("Enter the repository URL")  // 見出しの文字列
                font.pixelSize: 14                      // 文字サイズ
                font.bold: true                         // 太字で表示する
            }

            // ステップ1の案内文
            Label {
                Layout.fillWidth: true                                           // 親の幅いっぱいに広げる
                text: qsTr("Select the URL type and enter the repository URL.")  // 案内の文字列
                color: palette.placeholderText                                   // 補足情報として控えめな文字色
                wrapMode: Text.WordWrap                                          // 幅に収まらない場合は単語単位で折り返す
            }

            // コミュニティリポジトリの選択行 (対象ディストリビューションのプリセットがある場合だけ表示する)
            RowLayout {
                Layout.fillWidth: true            // 親の幅いっぱいに広げる
                spacing: 8                        // 項目の間隔
                visible: root.presets.length > 0  // プリセットがあるときだけ表示する

                // [Source:]の項目名
                Label {
                    text: qsTr("Source:")  // 項目名の文字列
                }

                // カスタムURLかコミュニティリポジトリかの選択欄
                ComboBox {
                    id: presetCombo  // プリセット選択欄の参照名

                    Layout.fillWidth: true  // 残りの幅をすべて使う

                    // 先頭はカスタムURL、以降はプリセット名
                    model: [qsTr("Custom URL")].concat(root.presets.map(function (preset) { return preset.name }))

                    // 選択に応じて入力欄へ反映する (0はカスタム)
                    onActivated: function (index) {
                        root.applyPreset(index === 0 ? null : root.presets[index - 1])
                    }
                }
            }

            // 選択したプリセットの説明
            Label {
                Layout.fillWidth: true                 // 親の幅いっぱいに広げる
                visible: root.selectedPreset !== null  // プリセット選択時だけ表示する
                textFormat: Text.PlainText             // HTMLとして解釈させない
                color: palette.placeholderText         // 補足情報として控えめな文字色
                wrapMode: Text.WordWrap                // 幅に収まらない場合は単語単位で折り返す

                // プリセットの説明の文字列
                text: root.selectedPreset ? root.selectedPreset.description : ""
            }

            // サードパーティリポジトリの警告
            Frame {
                Layout.fillWidth: true                 // 親の幅いっぱいに広げる
                visible: root.selectedPreset !== null  // プリセット選択時だけ表示する

                // 警告の文字列
                contentItem: Label {
                    font.bold: true          // 太字で目立たせる
                    wrapMode: Text.WordWrap  // 幅に収まらない場合は単語単位で折り返す

                    // 警告の文字列
                    text: qsTr("Third-party repository: its packages are not provided or reviewed by openSUSE. "
                               + "Add it only if you trust the source. You will be asked to confirm its GPG key.")
                }
            }

            // URL種別の選択とURLの入力欄
            Components.UrlTypeSelector {
                id: urlSelector  // URL入力部品の参照名

                Layout.fillWidth: true  // 親の幅いっぱいに広げる
            }

            // URLの変数を展開した結果のプレビュー (保存されるのは展開前のURL)
            Components.UrlExpansionPreview {
                rawUrl: urlSelector.url  // 展開前のURL

                Layout.fillWidth: true  // 親の幅いっぱいに広げる
            }

            // URLの誤りの説明 ([Next]を押せない理由を示す)
            Label {
                Layout.fillWidth: true                            // 親の幅いっぱいに広げる
                visible: text.length > 0                          // 誤りがある場合のみ表示する
                text: urlSelector.validationError()               // 誤りの説明
                textFormat: Text.PlainText                        // HTMLとして解釈させない
                color: palette.brightText                         // 目立つ文字色
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere       // URLのような長い語も幅に収める
            }

            // 操作ボタンを下側へ寄せるための余白
            Item {
                Layout.fillHeight: true  // 残りの高さをすべて使う
            }

            // 操作ボタンを右寄せで横に並べる
            RowLayout {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる

                // ボタンを右寄せにするための余白
                Item {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                }

                // [Cancel]ボタン
                Button {
                    text: qsTr("Cancel")  // ボタンの文字列

                    // 追加せずにダイアログを閉じる
                    onClicked: {
                        root.close()
                    }
                }

                // [Next]ボタン
                Button {
                    // URLが有効なときだけ次へ進める
                    enabled: urlSelector.isValid()  // URLが有効な場合のみ押せる
                    text: qsTr("Next")              // ボタンの文字列
                    highlighted: true               // 推奨操作として強調表示する

                    // 名前とエイリアスが未入力または自動生成のままなら、現在のURLから既定値を生成し直してステップ2へ進む
                    // ([Back]でURLを直した場合に、誤ったURLから生成した値が残らないようにする)
                    onClicked: {
                        if (nameField.text === "" || nameField.text === root.autoName) {
                            nameField.text = root.defaultName(urlSelector.url)
                            root.autoName = nameField.text
                        }

                        root.syncAutoAlias()
                        root.currentStep = 1
                    }
                }
            }
        }

        // --- ステップ2: プロパティ設定 ---
        ColumnLayout {
            spacing: 12  // 項目の間隔

            // ステップ2の見出し
            Label {
                text: qsTr("Configure repository properties")  // 見出しの文字列
                font.pixelSize: 14                             // 文字サイズ
                font.bold: true                                // 太字で表示する
            }

            // 項目名と入力欄を2列で並べる (子の並び順がセルの配置順になる)
            GridLayout {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる
                columns: 2              // 項目名と入力欄の2列
                columnSpacing: 12       // 列の間隔
                rowSpacing: 8           // 行の間隔

                // [URL:]の項目名
                Label {
                    text: qsTr("URL:")  // 項目名の文字列
                }

                // ステップ1で入力したURL
                Label {
                    Layout.fillWidth: true      // 残りの幅をすべて使う
                    text: urlSelector.url       // 入力済みのURL
                    textFormat: Text.PlainText  // HTMLとして解釈させない
                    color: palette.link         // URLであることを示すリンク色
                    elide: Text.ElideMiddle     // 幅に収まらない場合は中央を省略する
                }

                // [Name:]の項目名
                Label {
                    text: qsTr("Name:")  // 項目名の文字列
                }

                // リポジトリ名の入力欄
                TextField {
                    id: nameField  // 名前入力欄の参照名

                    Layout.fillWidth: true                    // 残りの幅をすべて使う
                    placeholderText: qsTr("Repository name")  // 未入力時に表示する案内

                    // 利用者が名前を編集したら、自動生成のままのエイリアスを名前に追従させる
                    onTextEdited: {
                        root.syncAutoAlias()
                    }
                }

                // [Alias:]の項目名
                Label {
                    text: qsTr("Alias:")  // 項目名の文字列
                }

                // リポジトリのエイリアスの入力欄
                TextField {
                    id: aliasField  // エイリアス入力欄の参照名

                    Layout.fillWidth: true                      // 残りの幅をすべて使う
                    placeholderText: qsTr("Unique identifier")  // 未入力時に表示する案内
                }

                // [Priority:]の項目名
                Label {
                    text: qsTr("Priority:")  // 項目名の文字列
                }

                // 優先度の入力欄
                SpinBox {
                    id: prioritySpin  // 優先度入力欄の参照名

                    from: 1    // 優先度の最小値 (最も高い優先度)
                    to: 200    // 優先度の最大値 (最も低い優先度)
                    value: 99  // 優先度の既定値
                }

                // 項目名の列を空けるための空セル
                Item {
                    width: 1  // 空セルの幅
                }

                // 優先度の値の意味の説明
                Label {
                    // 説明の文字列
                    text: qsTr("Lower values have higher priority (1=highest, 99=default, 200=lowest)")
                    color: palette.placeholderText  // 補足情報として控えめな文字色
                    font.pixelSize: 11              // 文字サイズ
                }
            }

            // オプションのチェックボックスを横に並べる
            RowLayout {
                spacing: 16  // チェックボックスの間隔

                // [Auto-refresh]チェックボックス
                CheckBox {
                    id: autoRefreshCheck  // 自動リフレッシュ設定の参照名

                    text: qsTr("Auto-refresh")  // チェックボックスの文字列
                    checked: true               // 既定で自動リフレッシュを有効にする
                }

                // [Keep Packages]チェックボックス
                CheckBox {
                    id: keepPkgCheck  // パッケージ保持設定の参照名

                    text: qsTr("Keep Packages")  // チェックボックスの文字列
                    checked: false               // 既定ではダウンロードしたパッケージを保持しない
                }
            }

            // 操作ボタンを下側へ寄せるための余白
            Item {
                Layout.fillHeight: true  // 残りの高さをすべて使う
            }

            // 操作ボタンを右寄せで横に並べる
            RowLayout {
                Layout.fillWidth: true  // 親の幅いっぱいに広げる

                // ボタンを右寄せにするための余白
                Item {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                }

                // [Back]ボタン
                Button {
                    enabled: !root.awaiting  // 応答待ちの間は戻れない (失敗理由はこのステップに表示するため)
                    text: qsTr("Back")       // ボタンの文字列

                    // ステップ1へ戻る
                    onClicked: {
                        root.currentStep = 0
                    }
                }

                // [Add]ボタン
                Button {
                    // 追加処理中でなく必須項目が埋まっているときだけ追加できる
                    enabled: !root.awaiting && nameField.text.length > 0 && aliasField.text.length > 0
                    text: qsTr("Add")  // ボタンの文字列
                    highlighted: true  // 推奨操作として強調表示する

                    // 入力内容をまとめ、PackageController::addRepoFull()でリポジトリの追加を要求して応答待ちにする
                    onClicked: {
                        var props = {
                            "url":          urlSelector.url,
                            "name":         nameField.text,
                            "alias":        aliasField.text,
                            "enabled":      true,
                            "autoRefresh":  autoRefreshCheck.checked,
                            "keepPackages": keepPkgCheck.checked,
                            "priority":     prioritySpin.value,
                            // 型の自動検出は未実装のため空にしておく (バックエンド側で決定する)
                            "type":         ""
                        }

                        root.errorMessage = ""
                        root.awaiting = true
                        PackageController.addRepoFull(props)
                    }
                }

                // [Cancel]ボタン
                Button {
                    enabled: !root.awaiting  // 応答待ちの間は閉じられない
                    text: qsTr("Cancel")     // ボタンの文字列

                    // 追加せずにダイアログを閉じる
                    onClicked: {
                        root.close()
                    }
                }
            }

            // 追加失敗時のエラーメッセージ
            Label {
                Layout.fillWidth: true                 // 親の幅いっぱいに広げる
                // エラーがある場合のみ表示する
                visible: root.errorMessage.length > 0  // エラーがある場合のみ表示する
                text: root.errorMessage                // エラーメッセージ
                textFormat: Text.PlainText             // HTMLとして解釈させない
                color: palette.brightText              // 目立つ文字色
                wrapMode: Text.WordWrap                // 幅に収まらない場合は単語単位で折り返す
            }
        }
    }

    // D-Bus追加要求の完了を受け、成功時だけ親へ通知して閉じる
    Connections {
        target: PackageController  // 追加完了を通知するPackageController

        /**
         * @brief コミュニティリポジトリのプリセット一覧を受け取る
         * @param list プリセットのマップ一覧 (対象外のディストリビューションでは空)
         * @note presetsを書き換え、プリセット選択行の表示と選択肢を更新する
         */
        function onRepoPresetsReady(list) {
            root.presets = list
        }

        /**
         * @brief バックエンドとの接続状態の変化を受け取る
         * @note 切断時は応答が破棄されてaddRepoFinishedが届かないため、応答待ちを解除して閉じられるようにする
         */
        function onConnectedChanged() {
            if (root.awaiting && !PackageController.connected) {
                root.awaiting = false
                root.errorMessage = qsTr("Failed to add repository: %1").arg(qsTr("Not connected to the backend."))
            }
        }

        /**
         * @brief リポジトリ追加完了のシグナルを受け取る
         * @param success 追加に成功したか
         * @param errorMessage 失敗理由 (成功時や理由が得られない場合は空)
         * @note 戻り値は無く、追加要求を出していない通知は無視する
         */
        function onAddRepoFinished(success, errorMessage) {
            // このダイアログからの追加要求でない場合は何もしない
            if (!root.awaiting) {
                return
            }

            root.awaiting = false

            // 成功時は親へ通知して閉じ、失敗時はエラーを表示する
            if (success) {
                root.repoAdded()
                root.close()
            }
            else {
                // 理由が得られた場合は汎用メッセージの後ろに付けて表示する
                root.errorMessage = errorMessage
                    ? qsTr("Failed to add repository: %1").arg(errorMessage)
                    : qsTr("Failed to add repository.")
            }
        }
    }
}
