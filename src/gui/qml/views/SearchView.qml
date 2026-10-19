/**
 * @file SearchView.qml
 * @brief キーワード検索と分類別のパッケージ読み込みを行うビュー
 *
 * Main.qmlのStackLayout内で直接生成されるPaneで、検索条件の入力欄と分類選択を提供する
 * 表示の切替はMain.qmlのfilterTabBarの選択インデックスで行う
 *
 * 入力 : なし
 * 出力 : なし
 * 依存 : PackageController (searchPackages(), searchPackagesEx(), loadClassification(), searchError, busy)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// [Search]タブで表示するビュー本体
Pane {
    /**
     * @brief 入力された条件でパッケージを検索する
     * @note 検索対象のチェック状態をビットマスクへ変換し、マッチ方法かインストール済み限定が既定と異なる場合だけ拡張検索を使用する
     *       PackageController::searchPackages / searchPackagesExを呼び出す
     */
    function performSearch() {
        // 検索対象のチェック状態をビットマスクへ変換する
        var flags = 0

        // 名前
        if (cbName.checked) {
            flags |= 0x01
        }

        // キーワード
        if (cbKeyword.checked) {
            flags |= 0x02
        }

        // 概要
        if (cbSummary.checked) {
            flags |= 0x04
        }

        // 説明
        if (cbDesc.checked) {
            flags |= 0x08
        }

        // RPMのRequires
        if (cbRequires.checked) {
            flags |= 0x10
        }

        // RPMのProvides
        if (cbProvides.checked) {
            flags |= 0x20
        }

        // 検索結果はパッケージ名昇順で表示
        PackageController.packageProxy.sortColumn = 1
        PackageController.packageProxy.sortAscending = true

        // マッチモード / installed-onlyが既定と違う場合は拡張検索を使用する
        var match = ["substring", "exact", "glob", "regex"][matchModeCombo.currentIndex]
        if (match !== "substring" || installedOnlyCheck.checked) {
            PackageController.searchPackagesEx(searchField.text, flags, match, installedOnlyCheck.checked)
        }
        else {
            PackageController.searchPackages(searchField.text, flags)
        }
    }

    padding: 12  // ビューの外周の余白

    // 画面が狭いときのために全体をスクロール可能にする
    ScrollView {
        id: searchScroll  // スクロール領域の参照名

        anchors.fill: parent          // ビュー全体に広げる
        contentWidth: availableWidth  // 横スクロールさせず、幅は表示領域に合わせる
        clip: true                    // スクロール時に枠外へはみ出した部品を描画しない

        // 検索条件の各部品を縦に並べる
        ColumnLayout {
            width: searchScroll.availableWidth  // スクロール領域の表示幅に合わせる
            spacing: 8                          // 各行の間隔

            // 検索入力
            RowLayout {
                Layout.fillWidth: true  // 横幅いっぱいに広げる

                // 検索語の入力欄 ([Enter]キーでも検索する)
                TextField {
                    id: searchField  // 検索語の入力欄の参照名

                    Layout.fillWidth: true                       // 残りの幅をすべて使う
                    Layout.minimumWidth: 120                     // 狭くなっても確保する最小幅
                    placeholderText: qsTr("Search packages...")  // 未入力時の案内文字
                    Accessible.name: qsTr("Search packages")     // スクリーンリーダー向けの名前

                    // [Enter]キーで検索を実行する
                    onAccepted: {
                        performSearch()
                    }
                }

                // 検索の実行ボタン
                Button {
                    text: qsTr("Search")  // ボタンの表示名

                    // 検索を実行する
                    onClicked: {
                        performSearch()
                    }
                }
            }

            // マッチ方法とインストール済み限定の指定
            RowLayout {
                Layout.fillWidth: true  // 横幅いっぱいに広げる

                // マッチ方法の見出し
                Label {
                    text: qsTr("Match:")  // 見出しの文字
                }

                // マッチ方法の選択 (performSearch()内の文字列配列と同じ並び)
                ComboBox {
                    id: matchModeCombo  // マッチ方法の選択欄の参照名

                    model: [qsTr("Substring"), qsTr("Exact"), qsTr("Glob"), qsTr("Regex")]  // 部分一致・完全一致・グロブ・正規表現
                    Accessible.name: qsTr("Search match mode")                              // スクリーンリーダー向けの名前
                    // スクリーンリーダー向けの説明
                    Accessible.description: qsTr("How the query text is matched against package data")
                }

                // インストール済みパッケージだけに絞る
                CheckBox {
                    id: installedOnlyCheck  // インストール済み限定の指定の参照名

                    text: qsTr("Installed only")                      // チェックボックスの表示名
                    Accessible.name: qsTr("Installed packages only")  // スクリーンリーダー向けの名前
                }

                // 残りの幅を埋めて各部品を左寄せにするスペーサー
                Item {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                }
            }

            // 検索対象チェックボックス
            GroupBox {
                Layout.fillWidth: true          // 横幅いっぱいに広げる
                title: qsTr("Search Criteria")  // グループの見出し

                // 検索対象のチェックボックスを縦に並べる
                ColumnLayout {
                    anchors.fill: parent  // グループの中身全体に広げる

                    // 長いラベルでも幅を超えないよう省略表示するチェックボックス
                    component ElidedCheckBox : CheckBox {
                        Layout.fillWidth: true  // 横幅いっぱいに広げる

                        // ラベル部分 (省略表示に対応させるため置き換える)
                        contentItem: Label {
                            leftPadding: parent.indicator.width + parent.spacing  // チェック欄の幅と間隔だけ右へずらす
                            text: parent.text                                     // チェックボックスに設定した表示名
                            textFormat: Text.PlainText                            // HTMLとして解釈させない
                            verticalAlignment: Text.AlignVCenter                  // 高さ方向の中央に配置する
                            elide: Text.ElideRight                                // 長いラベルは末尾を省略する
                        }
                    }

                    // [名前]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbName  // [名前]のチェックボックスの参照名

                        text: qsTr("Name (&E)")  // チェックボックスの表示名
                        checked: true            // 既定で検索対象にする
                    }

                    // [キーワード]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbKeyword  // [キーワード]のチェックボックスの参照名

                        text: qsTr("Keywords (&K)")  // チェックボックスの表示名
                        checked: true                // 既定で検索対象にする
                    }

                    // [概要]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbSummary  // [概要]のチェックボックスの参照名

                        text: qsTr("Summary (&M)")  // チェックボックスの表示名
                        checked: true               // 既定で検索対象にする
                    }

                    // [説明]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbDesc  // [説明]のチェックボックスの参照名

                        text: qsTr("Description (&I)")  // チェックボックスの表示名
                    }

                    // RPMの[Provides]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbProvides  // [Provides]のチェックボックスの参照名

                        text: qsTr("RPM \"Provides\" (&R)")  // チェックボックスの表示名
                    }

                    // RPMの[Requires]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbRequires  // [Requires]のチェックボックスの参照名

                        text: qsTr("RPM \"Requires\" (&Q)")  // チェックボックスの表示名
                    }
                }
            }

            // クライアント側エラー表示 (正規表現エラーなど)
            // 入力中は空文字列が入るため、その間は行ごと隠す
            Label {
                Layout.fillWidth: true                             // 横幅いっぱいに広げる
                visible: PackageController.searchError.length > 0  // エラーがある場合のみ表示する
                text: PackageController.searchError                // PackageController::searchErrorのエラーメッセージ
                textFormat: Text.PlainText                         // HTMLとして解釈させない
                color: "#F44336"                                   // エラーを示す赤色
                wrapMode: Text.WordWrap                            // 長いメッセージは単語単位で折り返す
            }

            // 分類別パッケージ読み込み
            RowLayout {
                Layout.fillWidth: true  // 横幅いっぱいに広げる

                // 分類の見出し
                Label {
                    text: qsTr("Classification:")  // 見出しの文字
                }

                // 分類の選択 (Loadボタン内の文字列配列と同じ並び)
                ComboBox {
                    id: classificationCombo  // 分類の選択欄の参照名

                    Layout.preferredWidth: 180                // 選択欄の幅
                    model: [qsTr("Recommended"),              // 推奨・提案・孤立・不要・マルチバージョン・取り下げ・取り下げ (インストール済み)・PTF・ロック
                            qsTr("Suggested"),
                            qsTr("Orphaned"),
                            qsTr("Unneeded"),
                            qsTr("Multiversion"),
                            qsTr("Retracted"),
                            qsTr("Retracted (installed)"),
                            qsTr("PTF"),
                            qsTr("Locked")]
                    Accessible.name: qsTr("Package classification")  // スクリーンリーダー向けの名前
                    Accessible.description: qsTr("Load packages of the selected classification into the list")  // スクリーンリーダー向けの説明
                }

                // 読み込み中は多重実行を防ぐため無効化する
                Button {
                    enabled: !PackageController.busy                          // PackageController::busyの間は押せないようにする
                    text: qsTr("Load")                                        // ボタンの表示名
                    Accessible.name: qsTr("Load packages by classification")  // スクリーンリーダー向けの名前

                    // 選択した分類のパッケージを一覧へ読み込む
                    // PackageController::loadClassificationを呼び出し、結果はpackagesChangedで一覧へ反映される
                    onClicked: {
                        // 選択欄の並びに対応するバックエンド側の分類名
                        var cls = ["recommended", "suggested", "orphaned", "unneeded", "multiversion",
                                  "retracted", "retractedInstalled", "ptf", "locked"][classificationCombo.currentIndex]
                        // 分類結果はパッケージ名昇順で見せる
                        PackageController.packageProxy.sortColumn = 1
                        PackageController.packageProxy.sortAscending = true
                        PackageController.loadClassification(cls)
                    }
                }

                // 残りの幅を埋めて各部品を左寄せにするスペーサー
                Item {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                }
            }
        }
    }
}
