/**
 * @file PatchView.qml
 * @brief パッチを一覧してインストール対象を選ぶビュー
 *
 * Main.qmlの右ペイン上部のStackLayout内で直接生成され、GetPatchesの結果を表示する
 * チェックでInstall/NoInstを切り替え、必要なパッチの一括選択も行う
 *
 * 入力 : categoryFilter (表示するカテゴリ、-1ですべて)
 * 出力 : なし
 * 依存 : PackageController (patches, loadPatches(), setPatchStatus(), selectAllNeededPatches(), busy)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// パッチ選択ビュー (右ペイン上部)
// GetPatches結果を一覧表示し、チェックでInstall/NoInstをトグル、必要パッチの一括選択も行う
Pane {
    id: patchView  // パッチ選択ビュー自身の参照名

    // 選択中のカテゴリ (-1 = すべて)
    // Types.hのPatchCategoryに準拠
    property int categoryFilter: -1  // ComboBoxの選択に応じて切り替わる

    /**
     * @brief 現在のカテゴリ指定でパッチ一覧を読み直す
     * @note PackageController::loadPatchesを呼び出す
     */
    function reload() {
        PackageController.loadPatches(categoryFilter)
    }

    /**
     * @brief バックエンドが返すカテゴリ文字列を表示名へ変換する
     * @param c libzyppのカテゴリ文字列
     * @return 翻訳済みの表示名、未知の値はそのままの文字列
     * @note バックエンドはlibzyppのカテゴリ文字列 (security/recommended/optional/feature/document等) を返す
     */
    function categoryText(c) {
        switch (String(c).toLowerCase()) {
        case "security":    return qsTr("Security")
        case "recommended": return qsTr("Recommended")
        case "optional":    return qsTr("Optional")
        case "feature":     return qsTr("Feature")
        case "document":    return qsTr("Document")
        case "yast":        return "YaST"
        default:            return String(c || "")
        }
    }

    padding: 12  // ビューの外周の余白

    // ツール行・ヘッダ行・パッチ一覧を縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ビュー全体に広げる
        spacing: 8            // 各行の間隔

        // --- ツール行 : カテゴリフィルタ + 一括選択 ---
        RowLayout {
            Layout.fillWidth: true  // 横幅いっぱいに広げる

            // カテゴリ選択の見出し
            Label {
                text: qsTr("Category:")  // 見出しの文字
            }

            // 選択したカテゴリで一覧を読み直す (先頭の「すべて」は-1)
            ComboBox {
                id: categoryCombo  // カテゴリの選択欄の参照名

                Layout.preferredWidth: 150                      // 選択欄の幅
                activeFocusOnTab: true                          // [Tab]キーでフォーカスを移動できるようにする
                // すべて・セキュリティ・推奨・任意・機能 (2番目以降はPatchCategoryの値+1)
                model: [qsTr("All"), qsTr("Security"), qsTr("Recommended"),
                        qsTr("Optional"), qsTr("Feature")]
                Accessible.name: qsTr("Patch category filter")  // スクリーンリーダー向けの名前

                // 選択インデックスをカテゴリ値へ変換して一覧を読み直す
                onActivated: {
                    patchView.categoryFilter = (currentIndex === 0) ? -1 : currentIndex - 1
                    patchView.reload()
                }
            }

            // 残りの幅を埋めて一括選択の部品を右寄せにするスペーサー
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // 一括選択をセキュリティパッチだけに限定する指定
            CheckBox {
                id: securityOnlyCheck  // セキュリティ限定の指定の参照名

                activeFocusOnTab: true                                 // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Security only")                            // チェックボックスの表示名
                Accessible.name: qsTr("Select security patches only")  // スクリーンリーダー向けの名前
            }

            // 一括選択に任意 (optional) パッチも含める指定
            CheckBox {
                id: includeOptionalCheck  // 任意パッチを含める指定の参照名

                activeFocusOnTab: true                             // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Include optional")                     // チェックボックスの表示名
                Accessible.name: qsTr("Include optional patches")  // スクリーンリーダー向けの名前
            }

            // 処理中は一括選択を受け付けない
            Button {
                enabled: !PackageController.busy                    // PackageController::busyの間は押せないようにする
                activeFocusOnTab: true                              // [Tab]キーでフォーカスを移動できるようにする
                text: qsTr("Select All Needed")                     // ボタンの表示名
                Accessible.name: qsTr("Select all needed patches")  // スクリーンリーダー向けの名前
                // スクリーンリーダー向けの説明
                Accessible.description: qsTr("Mark all patches needed by the solver for installation")

                // ソルバが必要と判断したパッチをまとめてインストール対象にする
                // PackageController::selectAllNeededPatchesを呼び出す
                onClicked: {
                    PackageController.selectAllNeededPatches(securityOnlyCheck.checked, includeOptionalCheck.checked)
                }
            }
        }

        // --- ヘッダ行 ---
        // 各列の幅は一覧の行 (delegate) の列幅と揃える
        RowLayout {
            Layout.fillWidth: true  // 横幅いっぱいに広げる
            spacing: 6              // 列の間隔 (一覧の行と同じ)

            // チェックボックス列の見出し位置を空けるための空き領域
            Item {
                Layout.preferredWidth: 24  // チェックボックス列の幅
            }

            // [Patch]列の見出し
            Label {
                Layout.preferredWidth: 170  // パッチ名の列幅
                text: qsTr("Patch")         // 見出しの文字
                font.bold: true             // 見出しとして太字にする
            }

            // [Category]列の見出し
            Label {
                Layout.preferredWidth: 100  // カテゴリの列幅
                text: qsTr("Category")      // 見出しの文字
                font.bold: true             // 見出しとして太字にする
            }

            // [Severity]列の見出し
            Label {
                Layout.preferredWidth: 80  // 重要度の列幅
                text: qsTr("Severity")     // 見出しの文字
                font.bold: true            // 見出しとして太字にする
            }

            // [Summary]列の見出し
            Label {
                Layout.fillWidth: true  // 残りの幅をすべて使う
                text: qsTr("Summary")   // 見出しの文字
                font.bold: true         // 見出しとして太字にする
            }

            // [Flags]列の見出し
            Label {
                Layout.preferredWidth: 130  // フラグの列幅
                text: qsTr("Flags")         // 見出しの文字
                font.bold: true             // 見出しとして太字にする
            }
        }

        // ヘッダ行と一覧の区切り線
        MenuSeparator {
            Layout.fillWidth: true  // 横幅いっぱいに広げる
        }

        // --- パッチ一覧 ---
        ListView {
            id: patchList  // パッチ一覧の参照名

            Layout.fillWidth: true                                        // 横幅いっぱいに広げる
            Layout.fillHeight: true                                       // 残りの高さをすべて使う
            Layout.minimumHeight: 120                                     // 狭くなっても確保する最小の高さ
            clip: true                                                    // スクロール時に枠外へはみ出した行を描画しない
            model: PackageController.patches                              // PackageController::patchesのパッチ一覧
            boundsBehavior: Flickable.StopAtBounds                        // 端を越えて引っ張れないようにする
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 行が収まらない場合のみ縦スクロールバーを表示する

            // パッチ1件分の行
            delegate: RowLayout {
                required property var modelData  // パッチ1件分のデータ

                // スクロールバーの表示中はその幅と隙間を、非表示のときは12pxを差し引いて重なりを避ける
                width: patchList.width - (patchList.ScrollBar.vertical.visible ? patchList.ScrollBar.vertical.width + 4 : 12)
                spacing: 6  // 列の間隔 (ヘッダ行と同じ)

                // チェックでInstall (1) とNoInst (0) を切り替える
                CheckBox {
                    Layout.preferredWidth: 24                                     // チェックボックス列の幅
                    enabled: !PackageController.busy                              // PackageController::busyの間は切り替えられないようにする
                    activeFocusOnTab: true                                        // [Tab]キーでフォーカスを移動できるようにする
                    checked: modelData.selected === true                          // インストール対象に選ばれているか
                    Accessible.name: qsTr("Select patch %1").arg(modelData.name)  // スクリーンリーダー向けの名前

                    // チェック状態に応じてパッチの状態を変更する
                    // PackageController::setPatchStatusを呼び出す
                    onToggled: {
                        PackageController.setPatchStatus(modelData.id, checked ? 1 : 0)
                    }
                }

                // パッチ名 (ソルバが必要と判断したものは太字)
                Label {
                    Layout.preferredWidth: 170              // パッチ名の列幅
                    text: modelData.name || ""              // パッチ名
                    textFormat: Text.PlainText              // HTMLとして解釈させない
                    font.bold: modelData.isNeeded === true  // 必要なパッチは太字にする
                    elide: Text.ElideRight                  // 長い名前は末尾を省略する
                }

                // libzyppのカテゴリ文字列を表示名へ変換して出す
                Label {
                    Layout.preferredWidth: 100                        // カテゴリの列幅
                    text: patchView.categoryText(modelData.category)  // カテゴリの表示名
                    textFormat: Text.PlainText                        // HTMLとして解釈させない
                    elide: Text.ElideRight                            // 長い名前は末尾を省略する
                }

                // 重要度 (security/critical等の文字列をそのまま表示)
                Label {
                    Layout.preferredWidth: 80       // 重要度の列幅
                    text: modelData.severity || ""  // 重要度
                    textFormat: Text.PlainText      // HTMLとして解釈させない
                    elide: Text.ElideRight          // 長い文字列は末尾を省略する
                }

                // パッチの概要
                Label {
                    Layout.fillWidth: true         // 残りの幅をすべて使う
                    text: modelData.summary || ""  // パッチの概要
                    textFormat: Text.PlainText     // HTMLとして解釈させない
                    elide: Text.ElideRight         // 長い概要は末尾を省略する
                }

                // 適用時の注意を表すフラグ表示
                RowLayout {
                    Layout.preferredWidth: 130  // フラグの列幅
                    spacing: 4                  // フラグ同士の間隔

                    // 対話的な応答が必要なパッチ
                    Label {
                        visible: modelData.interactive === true  // 対話的なパッチの場合のみ表示する
                        text: qsTr("Interactive")                // フラグの文字
                        color: palette.placeholderText           // 控えめな文字色
                        font.pixelSize: 10                       // フラグの文字サイズ
                    }

                    // 適用後に再起動を推奨するパッチ (危険度が高いので赤)
                    Label {
                        visible: modelData.rebootSuggested === true  // 再起動推奨のパッチの場合のみ表示する
                        text: qsTr("Reboot")                         // フラグの文字
                        color: "#F44336"                             // 注意を引く赤色
                        font.pixelSize: 10                           // フラグの文字サイズ
                    }

                    // 適用後にサービスの再起動を推奨するパッチ
                    Label {
                        visible: modelData.restartSuggested === true  // サービス再起動推奨のパッチの場合のみ表示する
                        text: qsTr("Restart")                         // フラグの文字
                        color: "#FF9800"                              // 注意を示す橙色
                        font.pixelSize: 10                            // フラグの文字サイズ
                    }
                }
            }
        }

        // 空状態
        // 読み込み中は空表示を出さない (一瞬だけ案内が見えるのを避ける)
        Item {
            Layout.fillWidth: true                                     // 横幅いっぱいに広げる
            Layout.fillHeight: true                                    // 残りの高さをすべて使う
            visible: patchList.count === 0 && !PackageController.busy  // パッチがなく、処理中でもない場合のみ表示する

            // パッチがないことを示す案内表示
            Label {
                anchors.centerIn: parent             // 空き領域の中央に表示する
                text: qsTr("No patches to display")  // 案内メッセージ
                color: palette.placeholderText       // 控えめな文字色
            }
        }
    }
}
