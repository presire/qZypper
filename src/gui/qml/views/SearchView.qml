/**
 * @file SearchView.qml
 * @brief キーワード検索と分類別のパッケージ読み込みを行うビュー
 *
 * Main.qmlのStackLayout内で直接生成されるPaneで、検索条件の入力欄と分類選択を提供する
 * 表示の切替はMain.qmlのfilterTabBarの選択インデックスで行う
 *
 * 検索条件 (マッチ方法、大文字小文字の区別、各検索対象) はQSettingsの[search]グループへ保存する
 *
 * 入力 : なし
 * 出力 : hintRequested() シグナル (-devel / -debug パッケージが非表示であることの案内)
 * 依存 : PackageController (searchPackagesEx(), detectSearchMode(), loadClassification(), searchError, busy, packageProxy)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// [Search]タブで表示するビュー本体
Pane {
    id: searchView  // ビュー自身の参照名

    // マッチ方法の選択欄の並びに対応する検索モード名 (先頭のautoは入力内容から自動判定する)
    readonly property var matchModes: ["auto", "substring", "prefix", "exact", "glob", "regex"]
    // [Auto]選択時の自動判定結果 ([mode]と[query]を持つマップ)、[Auto]以外では空のマップ
    // PackageController::detectSearchModeを入力のたびに呼び出し、判定結果の表示に使う
    readonly property var detectedSearch: matchModeCombo.currentIndex === 0
                                          ? PackageController.detectSearchMode(searchField.text)
                                          : ({})

    signal hintRequested(string message)  // 非表示のパッケージに関する案内の表示要求

    /**
     * @brief 検索モード名を画面表示用の名前へ変換する
     * @param mode 検索モード名 (none / substring / prefix / exact / glob / regex)
     * @return マッチ方法の選択欄と同じ表示名、未知のモード名ではそのままの文字列
     */
    function matchModeLabel(mode) {
        switch (mode) {
        case "none": return qsTr("None")
        case "substring": return qsTr("Contains")
        case "prefix": return qsTr("Starts With")
        case "exact": return qsTr("Exact Match")
        case "glob": return qsTr("Wildcards")
        case "regex": return qsTr("Regular Expression")
        default: return mode
        }
    }

    /**
     * @brief 検索文字列が非表示設定のパッケージを対象にしている場合に案内を要求する
     * @param text 検索欄に入力された文字列
     * @note -develパッケージやデバッグ用パッケージは既定で一覧から隠しているため、検索しても見つからない理由を利用者へ伝える
     *       hintRequestedシグナルを発行し、表示はMain.qml側のInfoToastが担う
     */
    function requestHiddenPackageHints(text) {
        var lower = text.toLowerCase()

        if (lower.indexOf("-devel") >= 0 && PackageController.packageProxy.hideDevel) {
            searchView.hintRequested(qsTr("-devel packages are hidden. Enable \"Show -devel Packages\" in the menu to see them."))
        }

        if ((lower.indexOf("-debuginfo") >= 0 || lower.indexOf("-debugsource") >= 0)
                && PackageController.packageProxy.hideDebug) {
            searchView.hintRequested(qsTr("-debuginfo/-debugsource packages are hidden. Enable \"Show -debuginfo/-debugsource Packages\" in the menu to see them."))
        }
    }

    /**
     * @brief 入力された条件でパッケージを検索する
     * @note 検索対象のチェック状態をビットマスクへ変換し、常にPackageController::searchPackagesExで検索する
     *       [Auto]では入力内容から検索モードを判定し、判定できない場合 (空入力や"="だけの入力) は検索しない
     *       検索後、非表示設定のパッケージを対象にしている場合はhintRequestedシグナルを発行する
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

        // ファイル一覧
        if (cbFileList.checked) {
            flags |= 0x40
        }

        // RPMのRecommends
        if (cbRecommends.checked) {
            flags |= 0x80
        }

        var mode = searchView.matchModes[matchModeCombo.currentIndex]
        var query = searchField.text

        // [Auto]では判定結果のモードと、判定用の接頭辞を除いた問い合わせ文字列で検索する
        // 判定不能 (none) や"="だけの入力 (exactで問い合わせが空) は、全件一致の検索になるのを避けるため実行しない
        if (mode === "auto") {
            var detected = PackageController.detectSearchMode(query)

            if (detected.mode === "none" || (detected.mode === "exact" && detected.query.length === 0)) {
                return
            }

            mode = detected.mode
            query = detected.query
        }

        // 検索結果はパッケージ名昇順で表示
        PackageController.packageProxy.sortColumn = 1
        PackageController.packageProxy.sortAscending = true

        // PackageController::searchPackagesExを呼び出し、結果はpackagesChangedで一覧へ反映される
        // 不正な正規表現などの検索条件のエラーはsearchErrorへ設定される
        PackageController.searchPackagesEx(query, flags, mode, installedOnlyCheck.checked, caseSensitiveCheck.checked)
        searchView.requestHiddenPackageHints(searchField.text)
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
                        searchView.performSearch()
                    }
                }

                // 検索の実行ボタン
                Button {
                    text: qsTr("Search")  // ボタンの表示名

                    // 検索を実行する
                    onClicked: {
                        searchView.performSearch()
                    }
                }
            }

            // マッチ方法の指定
            // 左ペインは狭いため、マッチ方法・判定結果・チェックボックスを別の行に分ける
            RowLayout {
                Layout.fillWidth: true  // 横幅いっぱいに広げる

                // マッチ方法の見出し
                Label {
                    text: qsTr("Match:")  // 見出しの文字
                }

                // マッチ方法の選択 (searchView.matchModesと同じ並び)
                ComboBox {
                    id: matchModeCombo  // マッチ方法の選択欄の参照名

                    Layout.fillWidth: true  // 残りの幅をすべて使う
                    // 自動判定・部分一致・前方一致・完全一致・ワイルドカード・正規表現
                    model: [qsTr("Auto"), qsTr("Contains"), qsTr("Starts With"),
                            qsTr("Exact Match"), qsTr("Wildcards"), qsTr("Regular Expression")]
                    Accessible.name: qsTr("Search match mode")  // スクリーンリーダー向けの名前
                    // スクリーンリーダー向けの説明
                    Accessible.description: qsTr("How the query text is matched against package data")
                }
            }

            // [Auto]での自動判定結果
            // [Auto]を選択していて、検索欄に入力がある場合のみ表示する
            Label {
                Layout.fillWidth: true  // 横幅いっぱいに広げる
                // [Auto]選択中かつ入力がある場合のみ表示する
                visible: matchModeCombo.currentIndex === 0 && searchField.text.trim().length > 0
                opacity: 0.7  // 補足情報のため控えめに表示する
                // 判定したマッチ方法の表示名
                text: qsTr("Detected: %1").arg(searchView.matchModeLabel(searchView.detectedSearch.mode))
                textFormat: Text.PlainText  // HTMLとして解釈させない
                elide: Text.ElideRight      // 幅を超える場合は末尾を省略する
            }

            // 大文字小文字の区別とインストール済み限定の指定
            // 幅が足りない場合は折り返して全項目を操作できるようにする
            Flow {
                Layout.fillWidth: true  // 横幅いっぱいに広げる
                spacing: 8              // チェックボックス同士の間隔

                // 大文字と小文字を区別して検索する
                CheckBox {
                    id: caseSensitiveCheck  // 大文字小文字の区別の指定の参照名

                    text: qsTr("Case sensitive")                    // チェックボックスの表示名
                    Accessible.name: qsTr("Case sensitive search")  // スクリーンリーダー向けの名前
                }

                // インストール済みパッケージだけに絞る
                CheckBox {
                    id: installedOnlyCheck  // インストール済み限定の指定の参照名

                    text: qsTr("Installed only")                      // チェックボックスの表示名
                    Accessible.name: qsTr("Installed packages only")  // スクリーンリーダー向けの名前
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

                    // RPMの[Recommends]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbRecommends  // [Recommends]のチェックボックスの参照名

                        text: qsTr("RPM \"Recommends\" (&O)")  // チェックボックスの表示名
                    }

                    // [ファイル一覧]を検索対象にするチェックボックス
                    ElidedCheckBox {
                        id: cbFileList  // [ファイル一覧]のチェックボックスの参照名

                        text: qsTr("File List (&L)")  // チェックボックスの表示名
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

    // 検索条件の永続化 (次回起動時も同じ条件で検索できるようにする)
    Settings {
        property alias matchModeIndex: matchModeCombo.currentIndex  // 保存するマッチ方法の選択位置
        property alias caseSensitive: caseSensitiveCheck.checked    // 保存する大文字小文字の区別
        property alias searchName: cbName.checked                   // 保存する[名前]の検索対象指定
        property alias searchKeywords: cbKeyword.checked            // 保存する[キーワード]の検索対象指定
        property alias searchSummary: cbSummary.checked             // 保存する[概要]の検索対象指定
        property alias searchDescription: cbDesc.checked            // 保存する[説明]の検索対象指定
        property alias searchProvides: cbProvides.checked           // 保存する[Provides]の検索対象指定
        property alias searchRequires: cbRequires.checked           // 保存する[Requires]の検索対象指定
        property alias searchRecommends: cbRecommends.checked       // 保存する[Recommends]の検索対象指定
        property alias searchFileList: cbFileList.checked           // 保存する[ファイル一覧]の検索対象指定

        category: "search"  // 設定ファイル内のグループ名
    }
}
