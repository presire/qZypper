/**
 * @file LocksDialog.qml
 * @brief パッケージロック一覧を管理するダイアログ
 *
 * Main.qmlまたはパッケージ詳細から開かれる
 *
 * 入力 : PackageControllerのロックモデル
 * 出力 : ロック追加・削除の要求
 * 依存 : PackageController (D-Busバックエンドのロック管理API)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// パッケージロック管理ダイアログ
// GetLocks結果の一覧表示と、恒久ロックの追加/削除を行う
// Polkit認証はバックエンド側で実施され、拒否時はerrorOccurredで通知される
Dialog {
    id: locksDialog  // ダイアログ自身の参照名

    // コンボの表示値とバックエンド値の対応
    readonly property var kindValues: ["package", "pattern", "patch", "product"]  // 種別の値一覧
    readonly property var matchValues: ["exact", "glob", "regex"]                 // 照合方法の候補 (完全一致、glob、正規表現)

    anchors.centerIn: parent                                      // 親ウィンドウの中央に表示する
    width: Math.min((parent ? parent.width : 800) * 0.92, 920)    // 親幅の92% (最大920px)
    height: Math.min((parent ? parent.height : 600) * 0.85, 640)  // 親の高さの85% (最大640px)
    title: qsTr("Package Locks")                                  // ダイアログのタイトル
    standardButtons: Dialog.Close                                 // [Close]ボタンのみ表示する
    modal: true                                                   // 表示中は背後の画面を操作できないようにする

    // 開くたびにPackageController::loadLocks()でロック一覧を再取得する
    onOpened: {
        PackageController.loadLocks()
    }

    // 説明文、ヘッダ行、ロック一覧、追加フォームを縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 8            // 項目の間隔

        // 恒久ロックの説明文
        Label {
            Layout.fillWidth: true          // 親の幅いっぱいに広げる
            // 説明の文字列
            text: qsTr("Persistent locks keep packages from being installed, updated, or removed by the solver.")
            color: palette.placeholderText  // 補足情報として控えめな文字色
            font.pixelSize: 11              // 文字サイズ
            wrapMode: Text.WordWrap         // 幅に収まらない場合は単語単位で折り返す
        }

        // ---- ヘッダ行 ----
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 6              // 列の間隔

            // [Name]列の見出し
            Label {
                Layout.preferredWidth: 150  // 名前列の幅
                text: qsTr("Name")          // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Type]列の見出し
            Label {
                Layout.preferredWidth: 70  // 種別列の幅
                text: qsTr("Type")         // 見出しの文字列
                font.bold: true            // 太字で表示する
            }

            // [Match]列の見出し
            Label {
                Layout.preferredWidth: 55  // 照合方法列の幅
                text: qsTr("Match")        // 見出しの文字列
                font.bold: true            // 太字で表示する
            }

            // [Repository]列の見出し
            Label {
                Layout.preferredWidth: 100  // リポジトリ列の幅
                text: qsTr("Repository")    // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Arch]列の見出し
            Label {
                Layout.preferredWidth: 60  // アーキテクチャ列の幅
                text: qsTr("Arch")         // 見出しの文字列
                font.bold: true            // 太字で表示する
            }

            // [Version]列の見出し
            Label {
                Layout.preferredWidth: 85  // バージョン列の幅
                text: qsTr("Version")      // 見出しの文字列
                font.bold: true            // 太字で表示する
            }

            // [Comment]列の見出し
            Label {
                Layout.fillWidth: true  // 残りの幅をすべて使う
                text: qsTr("Comment")   // 見出しの文字列
                font.bold: true         // 太字で表示する
            }

            // [Pkg]列 (一致パッケージ数) の見出し
            Label {
                Layout.preferredWidth: 36               // 一致数列の幅
                text: qsTr("Pkg")                       // 見出しの文字列
                font.bold: true                         // 太字で表示する
                horizontalAlignment: Text.AlignHCenter  // 中央揃えにする
            }

            // [Remove]ボタン列の位置を空けるための余白
            Item {
                Layout.preferredWidth: 80  // [Remove]ボタン列の幅
            }
        }

        // ヘッダ行と一覧の区切り線
        MenuSeparator {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
        }

        // ---- ロック一覧 ----
        ListView {
            id: locksList  // ロック一覧の参照名

            Layout.fillWidth: true                                        // 親の幅いっぱいに広げる
            Layout.fillHeight: true                                       // 残りの高さをすべて使う
            Layout.minimumHeight: 100                                     // 一覧の最小の高さ
            clip: true                                                    // 領域外にはみ出した部分を描画しない
            model: PackageController.locks                                // PackageControllerが保持するロック一覧
            boundsBehavior: Flickable.StopAtBounds                        // 端を越えてスクロールさせない
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する

            // ロック1件分の行
            delegate: RowLayout {
                required property var modelData  // ロック1件分のデータ

                // 一覧の幅から縦スクロールバー分 (非表示時は12px) を除いた幅
                width: locksList.width - (locksList.ScrollBar.vertical.visible ? locksList.ScrollBar.vertical.width + 4 : 12)
                spacing: 6  // 列の間隔

                // ロックの名前
                Label {
                    Layout.preferredWidth: 150  // 見出しの列幅に合わせる
                    text: modelData.name || ""  // ロックの名前
                    textFormat: Text.PlainText  // HTMLとして解釈させない
                    elide: Text.ElideRight      // 幅に収まらない場合は末尾を省略する
                }

                // ロックの種別
                Label {
                    Layout.preferredWidth: 70   // 見出しの列幅に合わせる
                    text: modelData.kind || ""  // ロックの種別
                    textFormat: Text.PlainText  // HTMLとして解釈させない
                    elide: Text.ElideRight      // 幅に収まらない場合は末尾を省略する
                }

                // 照合方法
                Label {
                    Layout.preferredWidth: 55    // 見出しの列幅に合わせる
                    text: modelData.match || ""  // 照合方法
                    textFormat: Text.PlainText   // HTMLとして解釈させない
                    elide: Text.ElideRight       // 幅に収まらない場合は末尾を省略する
                }

                // リポジトリの別名
                Label {
                    Layout.preferredWidth: 100       // 見出しの列幅に合わせる
                    text: modelData.repoAlias || ""  // リポジトリの別名
                    textFormat: Text.PlainText       // HTMLとして解釈させない
                    elide: Text.ElideRight           // 幅に収まらない場合は末尾を省略する
                }

                // アーキテクチャ
                Label {
                    Layout.preferredWidth: 60   // 見出しの列幅に合わせる
                    text: modelData.arch || ""  // アーキテクチャ
                    textFormat: Text.PlainText  // HTMLとして解釈させない
                    elide: Text.ElideRight      // 幅に収まらない場合は末尾を省略する
                }

                // バージョン条件
                Label {
                    Layout.preferredWidth: 85      // 見出しの列幅に合わせる
                    text: modelData.version || ""  // バージョン条件
                    textFormat: Text.PlainText     // HTMLとして解釈させない
                    elide: Text.ElideRight         // 幅に収まらない場合は末尾を省略する
                }

                // コメント
                Label {
                    Layout.fillWidth: true          // 残りの幅をすべて使う
                    text: modelData.comment || ""   // ロックのコメント
                    textFormat: Text.PlainText      // HTMLとして解釈させない
                    color: palette.placeholderText  // 補足情報として控えめな文字色
                    elide: Text.ElideRight          // 幅に収まらない場合は末尾を省略する
                }

                // このロックに一致するパッケージ数
                Label {
                    Layout.preferredWidth: 36                                                     // 見出しの列幅に合わせる
                    text: modelData.matchCount !== undefined ? String(modelData.matchCount) : ""  // 一致数 (未設定の場合は空)
                    horizontalAlignment: Text.AlignHCenter                                        // 中央揃えにする
                }

                // [Remove]ボタン
                Button {
                    enabled: !PackageController.busy                                   // 処理中は削除を無効化する
                    activeFocusOnTab: true                                             // [Tab]キーでフォーカスを移動できるようにする
                    text: qsTr("Remove")                                               // ボタンの文字列
                    Accessible.name: qsTr("Remove lock %1").arg(modelData.name || "")  // スクリーンリーダー向けの名前

                    // PackageController::removeLock()でこのロックの削除を要求する
                    onClicked: {
                        PackageController.removeLock(modelData)
                    }
                }
            }
        }

        // 空状態
        Item {
            Layout.fillWidth: true          // 親の幅いっぱいに広げる
            visible: locksList.count === 0  // ロックが1件もない場合のみ表示する

            // ロックがないことを示すメッセージ
            Label {
                anchors.horizontalCenter: parent.horizontalCenter  // 左右中央に配置する
                visible: !PackageController.busy                   // 読み込み中は表示しない
                text: qsTr("No package locks defined")             // メッセージの文字列
                color: palette.placeholderText                     // 補足情報として控えめな文字色
            }
        }

        // ---- 追加フォーム ----
        GroupBox {
            Layout.fillWidth: true   // 親の幅いっぱいに広げる
            title: qsTr("Add Lock")  // グループの見出し

            // 項目名と入力欄を4列で並べる (子の並び順がセルの配置順になる)
            GridLayout {
                anchors.fill: parent  // グループ内側全体を使う
                columns: 4            // 項目名と入力欄を2組並べる4列
                columnSpacing: 8      // 列の間隔
                rowSpacing: 6         // 行の間隔

                // [Name*]の項目名
                Label {
                    text: qsTr("Name*")  // 項目名の文字列
                }

                // ロック名の入力欄
                TextField {
                    id: lockNameField  // ロック名の入力欄の参照名

                    Layout.fillWidth: true                                   // 残りの幅をすべて使う
                    Layout.columnSpan: 3                                     // 残りの3列を使う
                    activeFocusOnTab: true                                   // [Tab]キーでフォーカスを移動できるようにする
                    placeholderText: qsTr("package / pattern / patch name")  // 未入力時に表示する案内
                    Accessible.name: qsTr("Lock name")                       // スクリーンリーダー向けの名前
                }

                // [Type]の項目名
                Label {
                    text: qsTr("Type")  // 項目名の文字列
                }

                // ロック種別の選択欄
                ComboBox {
                    id: lockKindCombo  // ロック種別の選択欄の参照名

                    Layout.columnSpan: 3                                                       // 残りの3列を使う
                    activeFocusOnTab: true                                                     // [Tab]キーでフォーカスを移動できるようにする
                    model: [qsTr("package"), qsTr("pattern"), qsTr("patch"), qsTr("product")]  // 選択肢 (並びはバックエンド値の配列と対応する)
                    Accessible.name: qsTr("Lock entry type")                                   // スクリーンリーダー向けの名前
                }

                // [Match]の項目名
                Label {
                    text: qsTr("Match")  // 項目名の文字列
                }

                // 照合方法の選択欄
                ComboBox {
                    id: lockMatchCombo  // 照合方法の選択欄の参照名

                    Layout.columnSpan: 3                                 // 残りの3列を使う
                    activeFocusOnTab: true                               // [Tab]キーでフォーカスを移動できるようにする
                    model: [qsTr("exact"), qsTr("glob"), qsTr("regex")]  // 選択肢 (並びはバックエンド値の配列と対応する)
                    Accessible.name: qsTr("Lock match mode")             // スクリーンリーダー向けの名前
                }

                // [Repository]の項目名
                Label {
                    text: qsTr("Repository")  // 項目名の文字列
                }

                // リポジトリ別名の入力欄
                TextField {
                    id: lockRepoField  // リポジトリ別名の入力欄の参照名

                    activeFocusOnTab: true                     // [Tab]キーでフォーカスを移動できるようにする
                    placeholderText: qsTr("optional")          // 未入力時に表示する案内
                    Accessible.name: qsTr("Repository alias")  // スクリーンリーダー向けの名前
                }

                // [Architecture]の項目名
                Label {
                    text: qsTr("Architecture")  // 項目名の文字列
                }

                // アーキテクチャの入力欄
                TextField {
                    id: lockArchField  // アーキテクチャの入力欄の参照名

                    activeFocusOnTab: true                 // [Tab]キーでフォーカスを移動できるようにする
                    placeholderText: qsTr("optional")      // 未入力時に表示する案内
                    Accessible.name: qsTr("Architecture")  // スクリーンリーダー向けの名前
                }

                // [Version]の項目名
                Label {
                    text: qsTr("Version")  // 項目名の文字列
                }

                // バージョン条件の入力欄
                TextField {
                    id: lockVersionField  // バージョン条件の入力欄の参照名

                    activeFocusOnTab: true                       // [Tab]キーでフォーカスを移動できるようにする
                    placeholderText: qsTr("optional")            // 未入力時に表示する案内
                    Accessible.name: qsTr("Version constraint")  // スクリーンリーダー向けの名前
                }

                // [Comment]の項目名
                Label {
                    text: qsTr("Comment")  // 項目名の文字列
                }

                // コメントの入力欄
                TextField {
                    id: lockCommentField  // コメントの入力欄の参照名

                    Layout.fillWidth: true                 // 残りの幅をすべて使う
                    activeFocusOnTab: true                 // [Tab]キーでフォーカスを移動できるようにする
                    placeholderText: qsTr("optional")      // 未入力時に表示する案内
                    Accessible.name: qsTr("Lock comment")  // スクリーンリーダー向けの名前
                }

                // [Add Lock]ボタンを右下へ寄せるための余白
                Item {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                    Layout.columnSpan: 3    // 3列分を使う
                }

                // [Add Lock]ボタン
                Button {
                    enabled: lockNameField.text.length > 0 && !PackageController.busy            // 名前が入力済みで処理中でない場合のみ押せる
                    activeFocusOnTab: true                                                       // [Tab]キーでフォーカスを移動できるようにする
                    text: qsTr("Add Lock")                                                       // ボタンの文字列
                    highlighted: true                                                            // 推奨操作として強調表示する
                    Accessible.name: qsTr("Add package lock")                                    // スクリーンリーダー向けの名前
                    Accessible.description: qsTr("Create a persistent lock for the given name")  // スクリーンリーダー向けの説明

                    // 入力内容をまとめ、PackageController::addLock()でロックの追加を要求する
                    // 種別と照合方法は、選択肢の番号からバックエンド値へ変換する
                    onClicked: {
                        PackageController.addLock({
                            name: lockNameField.text,
                            kind: locksDialog.kindValues[lockKindCombo.currentIndex],
                            match: locksDialog.matchValues[lockMatchCombo.currentIndex],
                            repoAlias: lockRepoField.text,
                            arch: lockArchField.text,
                            version: lockVersionField.text,
                            comment: lockCommentField.text
                        })
                    }
                }
            }
        }
    }

    // ロック追加の完了通知を受け取る
    Connections {
        target: PackageController  // ロック操作の結果を通知するPackageController

        /**
         * @brief ロック追加操作の結果を受け取る
         * @param success trueなら追加成功、falseなら失敗
         * @note 追加成功時は入力フォームをクリアして連続追加できるようにする
         */
        function onLockFinished(success) {
            if (success) {
                lockNameField.clear()
                lockRepoField.clear()
                lockArchField.clear()
                lockVersionField.clear()
                lockCommentField.clear()
            }
        }
    }
}
