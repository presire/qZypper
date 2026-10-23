/**
 * @file RepoPropertiesPanel.qml
 * @brief 選択中リポジトリの属性を確認・変更するプロパティパネル
 *
 * RepoManagerDrawerContentで使用される
 *
 * 入力 : repo (リポジトリのQVariantMap)
 * 出力 : リポジトリ変更と削除の要求
 * 依存 : PackageController (リポジトリ管理API)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 選択中リポジトリの属性を編集するパネル
Pane {
    id: root  // パネル自身の参照名

    property var repo: null          // 親から受け取る選択中リポジトリ
    property bool hasChanges: false  // 表示値が元のリポジトリと異なるか

    signal applyRequested(var changes)  // 親へ送る変更後のリポジトリ属性

    /**
     * @brief 入力欄からPackageControllerへ渡すリポジトリ変更値を組み立てる
     * @return 変更後の属性を持つオブジェクト (repoが未設定の場合は空オブジェクト)
     */
    function getChanges() {
        // リポジトリが未選択の場合は変更値が無い
        if (!repo) {
            return {}
        }

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

    /**
     * @brief 編集値を元のリポジトリと比較して未保存変更の有無を更新する
     * @note 戻り値は無く、repo未設定時は早期に戻る
     * @note hasChangesを現在の入力内容に合わせて更新する
     */
    function checkChanges() {
        // リポジトリが未選択の場合は比較対象が無いため、変更無しとする
        if (!repo) {
            hasChanges = false
            return
        }

        hasChanges = nameField.text !== (repo.name || "") ||
                     urlField.text !== (repo.rawUrl || repo.url || "") ||
                     enabledCheck.checked !== (repo.enabled || false) ||
                     autoRefreshCheck.checked !== (repo.autoRefresh || false) ||
                     keepPkgCheck.checked !== (repo.keepPackages || false) ||
                     prioritySpin.value !== (repo.priority ?? 99)
    }

    padding: 12  // パネル内側の余白

    // 選択中リポジトリが変わったら、入力欄をその値で埋め直す
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

    // 編集欄全体 (見出し、属性の入力欄、サービス警告を縦に並べる)
    ColumnLayout {
        anchors.fill: parent         // パネル全体を使う
        spacing: 8                   // 各要素の縦方向の間隔
        visible: root.repo !== null  // 選択中リポジトリがある場合だけ編集欄を表示する

        // 見出し
        Label {
            text: qsTr("Properties")  // 見出しの文字列
            font.bold: true           // 太字で表示する
            font.pixelSize: 14        // 文字サイズ
        }

        // 属性の入力欄 (ラベルと入力欄を4列のグリッドに並べる)
        GridLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            columns: 4              // グリッドの列数
            columnSpacing: 16       // 列の間隔
            rowSpacing: 6           // 行の間隔

            // [Name:]ラベル
            Label {
                text: qsTr("Name:")  // ラベルの文字列
            }

            // 名前の入力欄
            TextField {
                id: nameField  // 名前の入力欄の参照名

                Layout.fillWidth: true                         // 残りの幅をすべて使う
                Layout.columnSpan: 3                           // 3列分にまたがる
                enabled: !(root.repo && root.repo.service)     // サービス管理下の名前は手編集させない
                text: root.repo ? (root.repo.name || "") : ""  // 選択中リポジトリの名前

                // 入力内容が変わったので未保存変更の有無を更新する
                onTextChanged: {
                    root.checkChanges()
                }
            }

            // [URL:]ラベル
            Label {
                text: qsTr("URL:")  // ラベルの文字列
            }

            // URLの入力欄
            TextField {
                id: urlField  // URLの入力欄の参照名

                Layout.fillWidth: true                                            // 残りの幅をすべて使う
                Layout.columnSpan: 3                                              // 3列分にまたがる
                enabled: !(root.repo && root.repo.service)                        // サービス管理のURLは編集不可
                text: root.repo ? (root.repo.rawUrl || root.repo.url || "") : ""  // 選択中リポジトリのURL (未展開の値を優先する)

                // 入力内容が変わったので未保存変更の有無を更新する
                onTextChanged: {
                    root.checkChanges()
                }
            }

            // URL展開プレビューの枠 (プレビュー非表示でもグリッドのセル配置が崩れないよう常に表示する)
            Item {
                Layout.fillWidth: true    // 残りの幅をすべて使う
                Layout.columnSpan: 4      // 全列にまたがる

                // プレビュー表示中だけプレビューの高さに合わせる
                implicitHeight: urlPreview.visible ? urlPreview.implicitHeight : 0

                // URLの変数を展開した結果のプレビュー (保存されるのは展開前のURL)
                UrlExpansionPreview {
                    id: urlPreview  // プレビューの参照名

                    rawUrl: urlField.text  // 展開前のURL

                    width: parent.width  // 枠の幅に合わせる
                }
            }

            // [Enabled]チェックボックス
            CheckBox {
                id: enabledCheck  // [Enabled]チェックボックスの参照名

                text: qsTr("Enabled")                                      // チェックボックスの文字列
                checked: root.repo ? (root.repo.enabled || false) : false  // リポジトリが有効かどうか

                // チェック状態が変わったので未保存変更の有無を更新する
                onToggled: {
                    root.checkChanges()
                }
            }

            // [Auto-refresh]チェックボックス
            CheckBox {
                id: autoRefreshCheck  // [Auto-refresh]チェックボックスの参照名

                text: qsTr("Auto-refresh")                                     // チェックボックスの文字列
                checked: root.repo ? (root.repo.autoRefresh || false) : false  // 自動リフレッシュが有効かどうか

                // チェック状態が変わったので未保存変更の有無を更新する
                onToggled: {
                    root.checkChanges()
                }
            }

            // [Keep Packages]チェックボックス
            CheckBox {
                id: keepPkgCheck  // [Keep Packages]チェックボックスの参照名

                text: qsTr("Keep Packages")                                     // チェックボックスの文字列
                checked: root.repo ? (root.repo.keepPackages || false) : false  // ダウンロードしたパッケージを保持するかどうか

                // チェック状態が変わったので未保存変更の有無を更新する
                onToggled: {
                    root.checkChanges()
                }
            }

            // 4列目を埋める空白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [Priority:]ラベル
            Label {
                text: qsTr("Priority:")  // ラベルの文字列
            }

            // 優先度の入力欄
            SpinBox {
                id: prioritySpin  // 優先度の入力欄の参照名

                from: 1                                             // 優先度の最小値
                to: 200                                             // 優先度の最大値
                value: root.repo ? (root.repo.priority ?? 99) : 99  // 選択中リポジトリの優先度 (未設定の場合は既定値の99)

                // 値が変わったので未保存変更の有無を更新する
                onValueChanged: {
                    root.checkChanges()
                }
            }

            // [Type:]ラベル
            Label {
                text: qsTr("Type:")  // ラベルの文字列
            }

            // リポジトリのタイプ (読取専用)
            Label {
                text: root.repo ? (root.repo.type || qsTr("Unknown")) : ""  // 選択中リポジトリのタイプ
                textFormat: Text.PlainText                                  // HTMLとして解釈させない
                color: palette.placeholderText                              // 編集できないことを示す控えめな文字色
            }
        }

        // サービス警告
        Label {
            Layout.fillWidth: true                                           // 親の幅いっぱいに広げる
            visible: root.repo !== null && (root.repo.service || "") !== ""  // サービス管理時だけ上書きの可能性を知らせる
            // 管理元のサービス名を埋め込んだ警告文
            text: qsTr("This repository is managed by service \"%1\". Some properties may be overwritten during service refresh.")
                .arg(root.repo ? (root.repo.service || "") : "")
            textFormat: Text.PlainText                                       // HTMLとして解釈させない
            wrapMode: Text.WordWrap                                          // 幅に収まらない場合は単語単位で折り返す
            color: "#d35400"                                                 // 注意を促す橙色
            font.pixelSize: 13                                               // 文字サイズ
        }

    }

    // リポジトリ未選択時のプレースホルダー
    Label {
        anchors.centerIn: parent           // パネルの中央に表示する
        visible: root.repo === null        // リポジトリ未選択時だけ選択を促す
        text: qsTr("Select a repository")  // 案内文
        color: palette.placeholderText     // プレースホルダーの文字色
    }
}
