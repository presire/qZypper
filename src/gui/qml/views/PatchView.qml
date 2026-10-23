/**
 * @file PatchView.qml
 * @brief パッチを一覧してインストール対象を選ぶビュー
 *
 * Main.qmlの右ペイン上部のStackLayout内で直接生成され、読み込んだパッチ一覧を表示する
 * チェックでインストール対象と対象外を切り替え、必要なパッチの一括選択も行う
 *
 * 列見出しの境界をドラッグして[Patch]、[Category]、[Severity]、[Summary]の列幅を変更できる
 * 列幅はQtCoreのSettingsに保存し、次回起動時に復元する
 * チェックの切り替えで一覧が読み直されても、縦スクロール位置は維持する
 *
 * 入力 : categoryFilter (表示するカテゴリ、-1ですべて)
 * 出力 : なし
 * 依存 : PackageController (patches, loadPatches(), setPatchStatus(), selectAllNeededPatches(), busy)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// パッチ選択ビュー (右ペイン上部)
Pane {
    id: patchView  // パッチ選択ビュー自身の参照名

    // 選択中のカテゴリ (-1はすべて)、Types.hのPatchCategoryに準拠
    property int categoryFilter: -1  // ComboBoxの選択に応じて更新される

    // 列幅 (見出しのドラッグで変更し、一覧の各行も同じ値を使う)
    property real colPatch: 170     // [Patch]列の幅
    property real colCategory: 100  // [Category]列の幅
    property real colSeverity: 80   // [Severity]列の幅
    property real colSummary: 400   // [Summary]列の幅

    // 列幅の縮小下限と固定幅の列
    readonly property real minColPatch: 80     // [Patch]列の縮小下限
    readonly property real minColCategory: 60  // [Category]列の縮小下限
    readonly property real minColSeverity: 60  // [Severity]列の縮小下限
    readonly property real minColSummary: 120  // [Summary]列の縮小下限
    readonly property real colCheck: 36        // チェックボックス列の幅 (固定)
    readonly property real colFlags: 130       // [Flags]列の幅 (固定)
    readonly property real scrollBarSize: 14   // スクロールバーの太さ (一覧の右端と下端に確保する)

    // 一覧の読み直し時にスクロール位置を維持するための状態
    property bool scrollToTopOnNextUpdate: false  // 復元時に先頭へ戻すか (表示中とカテゴリが異なる一覧の場合のみtrue)
    property bool scrollRestorePending: false     // 位置の復元待ちか (復元前に届いた更新で保存値を上書きしない)
    property real pendingContentY: 0              // 一覧の差し替え直前に保存した縦スクロール位置
    property int displayedCategory: -2            // 表示中の一覧のカテゴリ (-2は未表示)

    // 全列と区切り線5本を合わせた行の最小幅 (表示幅がこれを下回る場合は横スクロールする)
    readonly property real minRowWidth: colCheck + colPatch + colCategory + colSeverity
                                        + colSummary + colFlags + 5

    /**
     * @brief 現在のカテゴリ指定でパッチ一覧を読み直す
     * @note PackageController::loadPatchesを呼び出す
     * @note 先頭へ戻すかどうかは、一覧の受信時にapplyPatchesがカテゴリの変化で判定する
     *       (読み込みに失敗しても先頭へ戻す指定が残らないようにするため)
     */
    function reload() {
        PackageController.loadPatches(categoryFilter)
    }

    /**
     * @brief パッチ一覧のモデルを差し替え、縦スクロール位置を復元する準備をする
     * @note PackageController::patchesの新しい一覧をpatchListのmodelへ代入する
     * @note 復元待ちの間に再度呼ばれた場合は、最初に保存した位置を保持する
     * @note 表示中とカテゴリが異なる一覧の場合は、復元せずに先頭へ戻す
     * @note 実際の復元はQt.callLaterでrestoreScrollを呼び、差し替え後のレイアウト確定後に行う
     */
    function applyPatches() {
        if (!scrollRestorePending) {
            pendingContentY = patchList.contentY
            scrollRestorePending = true
        }

        // PackageController::patchesChangedは、最後に要求したカテゴリ (categoryFilter) の
        // 一覧でのみ発行されるため、カテゴリが変わった一覧は先頭から表示する
        if (categoryFilter !== displayedCategory) {
            scrollToTopOnNextUpdate = true
            displayedCategory = categoryFilter
        }

        patchList.model = PackageController.patches
        Qt.callLater(restoreScroll)
    }

    /**
     * @brief 差し替え前に保存した縦スクロール位置へ一覧を戻す
     * @note scrollToTopOnNextUpdateがtrueの場合は先頭へ戻し、指定を解除する
     * @note 範囲外にならないよう、先頭と末尾の位置に収めてから設定する
     */
    function restoreScroll() {
        // 差し替え直後の行数に合わせて内容の高さを確定させる
        patchList.forceLayout()

        if (scrollToTopOnNextUpdate) {
            patchList.contentY = patchList.originY
            scrollToTopOnNextUpdate = false
        }
        else {
            var minY = patchList.originY
            var maxY = Math.max(minY, patchList.contentHeight - patchList.height + minY)
            patchList.contentY = Math.min(Math.max(pendingContentY, minY), maxY)
        }

        scrollRestorePending = false
    }

    /**
     * @brief バックエンドが返すカテゴリ文字列を表示名へ変換する
     * @param c libzyppのカテゴリ文字列
     * @return 翻訳済みの表示名、未知の値はそのままの文字列
     * @note バックエンドはlibzyppのカテゴリ文字列を返す (security、recommended、optional、feature、documentなど)
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

    // 列見出しの右端に置き、ドラッグで列幅を変更するリサイズハンドル
    // ホバー中またはドラッグ中は選択色で表示する
    component ColumnResizeHandle: Rectangle {
        id: handle  // リサイズハンドル自身の参照名

        required property real columnWidth  // 対象列の現在の幅
        required property real minWidth     // 対象列の縮小下限

        signal resized(real newWidth)  // ドラッグで変更された列幅の通知

        anchors.right: parent.right  // 列見出しの右端に配置する
        width: 4                     // ハンドルの幅
        height: parent.height        // 見出しの高さに合わせる
        // 操作中のみ見える色
        color: handleArea.hovered || handleArea.pressed ? palette.highlight : "transparent"

        // ドラッグで列幅を変更する
        MouseArea {
            id: handleArea  // ドラッグ操作を受け付ける領域の参照名

            property bool hovered: false   // リサイズハンドルのホバー状態
            property real globalStartX: 0  // ドラッグ開始時の画面X座標
            property real startW: 0        // ドラッグ開始時の列幅

            anchors.fill: parent          // ハンドル領域全体で操作を受け付ける
            hoverEnabled: true            // ボタンを押していない状態でもホバーを検出する
            cursorShape: Qt.SplitHCursor  // 列幅を変更できることを示す左右矢印カーソル

            // ホバー開始
            onEntered: {
                hovered = true
            }

            // ホバー終了
            onExited: {
                hovered = false
            }

            // ドラッグ開始時の画面座標と列幅を記録する
            // 列幅の変更に伴いハンドル自体も移動するため、ローカル座標ではなく画面座標を基準にする
            onPressed: function(mouse) {
                var p = mapToGlobal(mouse.x, 0)
                globalStartX = p.x
                startW = handle.columnWidth
            }

            // ドラッグ量に応じて列幅を変更する (縮小下限を下回らない)
            onPositionChanged: function(mouse) {
                if (pressed) {
                    var p = mapToGlobal(mouse.x, 0)
                    handle.resized(Math.max(handle.minWidth, startW + p.x - globalStartX))
                }
            }
        }
    }

    // 列幅の保存先 (ビューの列幅プロパティをそのまま保存・復元する)
    Settings {
        id: patchSettings  // 保存設定の参照名

        property alias colPatch: patchView.colPatch        // 保存する[Patch]列幅
        property alias colCategory: patchView.colCategory  // 保存する[Category]列幅
        property alias colSeverity: patchView.colSeverity  // 保存する[Severity]列幅
        property alias colSummary: patchView.colSummary    // 保存する[Summary]列幅

        category: "patchView"  // 設定ファイル内のグループ名
    }

    // 絞り込み操作、列見出し、パッチ一覧をまとめる領域
    ColumnLayout {
        anchors.fill: parent  // ビュー全体に広げる
        spacing: 8            // 各行の間隔

        // カテゴリの絞り込みと一括選択を行う操作行
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
                // すべて・セキュリティ・推奨・任意・機能・ドキュメント・YaST
                // 表示順はPatchCategoryの値と一致しないため変換は下の対応表で行う
                model: [qsTr("All"), qsTr("Security"), qsTr("Recommended"),
                        qsTr("Optional"), qsTr("Feature"), qsTr("Document"),
                        qsTr("YaST")]
                Accessible.name: qsTr("Patch category filter")  // スクリーンリーダー向けの名前

                // 選択インデックスをカテゴリ値へ変換して一覧を読み直す
                // 位置0はすべて、位置1〜4はSecurity/Recommended/Optional/Feature
                // 位置5はDocument、位置6はYaST
                onActivated: {
                    // PatchCategoryの値へ変換する対応表
                    var categories = [-1, 0, 1, 2, 3, 5, 6]
                    patchView.categoryFilter = categories[currentIndex]
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

            // 一括選択に任意パッチも含める指定
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

                // ソルバーが必要と判断したパッチをまとめてインストール対象にする
                // PackageController::selectAllNeededPatchesを呼び出す
                // 一覧の読み直し時のスクロール位置の維持はConnectionsで行う
                onClicked: {
                    PackageController.selectAllNeededPatches(
                        securityOnlyCheck.checked,
                        includeOptionalCheck.checked
                    )
                }
            }
        }

        // 一覧の列見出し
        // 各列の幅は一覧の各行の列幅と揃え、一覧の横スクロールに追従する
        Rectangle {
            Layout.fillWidth: true                       // 横幅いっぱいに広げる
            Layout.preferredHeight: 28                   // 見出しの高さ
            Layout.rightMargin: patchView.scrollBarSize  // 縦スクロールバーの幅を空けて一覧の幅と揃える
            clip: true                                   // 横スクロールではみ出した部分を描画しない
            color: palette.button                        // 見出しの背景色

            // 一覧の横スクロールに追従する見出し領域
            // 見出し自体はドラッグ操作を受け付けず、位置は一覧のFlickableから受け取る
            Flickable {
                id: headerFlick  // 見出し領域の参照名

                anchors.fill: parent                   // 見出し全体を使う
                contentWidth: patchFlick.contentWidth  // 一覧と同じ内容幅
                contentX: patchFlick.contentX          // 一覧の横スクロール位置に合わせる
                interactive: false                     // 見出し自体はドラッグでスクロールさせない

                // 各列の見出しを横に並べる
                Row {
                    width: Math.max(headerFlick.width, patchView.minRowWidth)  // 表示幅と全列の合計幅の大きい方
                    height: 28                                                 // 見出しの高さ
                    spacing: 0                                                 // 列の間隔は区切り線で表現する

                    // チェックボックス列の見出し位置を空けるための空き領域
                    Item {
                        width: patchView.colCheck  // チェックボックス列の幅
                        height: parent.height      // 見出しの高さに合わせる
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [Patch]列の見出し
                    Item {
                        width: patchView.colPatch  // [Patch]列の幅
                        height: parent.height      // 見出しの高さに合わせる
                        clip: true                 // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.left: parent.left                      // 列の左端を基準にする
                            anchors.leftMargin: 4                          // 一覧の文字位置に合わせる左余白
                            anchors.right: parent.right                    // 列の右端を基準にする
                            anchors.rightMargin: 8                         // リサイズハンドルと重ならないようにする右余白
                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                            text: qsTr("Patch")                            // 列名の文字列
                            font.bold: true                                // 見出しとして太字にする
                            elide: Text.ElideRight                         // 列幅を超える場合は末尾を省略する
                        }

                        // ドラッグで[Patch]列の幅を変更するハンドル
                        ColumnResizeHandle {
                            columnWidth: patchView.colPatch  // ドラッグ開始時に基準とする列幅
                            minWidth: patchView.minColPatch  // 縮小下限

                            // 変更後の列幅を反映する
                            onResized: function(newWidth) {
                                patchView.colPatch = newWidth
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [Category]列の見出し
                    Item {
                        width: patchView.colCategory  // [Category]列の幅
                        height: parent.height         // 見出しの高さに合わせる
                        clip: true                    // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.left: parent.left                      // 列の左端を基準にする
                            anchors.leftMargin: 4                          // 一覧の文字位置に合わせる左余白
                            anchors.right: parent.right                    // 列の右端を基準にする
                            anchors.rightMargin: 8                         // リサイズハンドルと重ならないようにする右余白
                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                            text: qsTr("Category")                         // 列名の文字列
                            font.bold: true                                // 見出しとして太字にする
                            elide: Text.ElideRight                         // 列幅を超える場合は末尾を省略する
                        }

                        // ドラッグで[Category]列の幅を変更するハンドル
                        ColumnResizeHandle {
                            columnWidth: patchView.colCategory  // ドラッグ開始時に基準とする列幅
                            minWidth: patchView.minColCategory  // 縮小下限

                            // 変更後の列幅を反映する
                            onResized: function(newWidth) {
                                patchView.colCategory = newWidth
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [Severity]列の見出し
                    Item {
                        width: patchView.colSeverity  // [Severity]列の幅
                        height: parent.height         // 見出しの高さに合わせる
                        clip: true                    // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.left: parent.left                      // 列の左端を基準にする
                            anchors.leftMargin: 4                          // 一覧の文字位置に合わせる左余白
                            anchors.right: parent.right                    // 列の右端を基準にする
                            anchors.rightMargin: 8                         // リサイズハンドルと重ならないようにする右余白
                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                            text: qsTr("Severity")                         // 列名の文字列
                            font.bold: true                                // 見出しとして太字にする
                            elide: Text.ElideRight                         // 列幅を超える場合は末尾を省略する
                        }

                        // ドラッグで[Severity]列の幅を変更するハンドル
                        ColumnResizeHandle {
                            columnWidth: patchView.colSeverity  // ドラッグ開始時に基準とする列幅
                            minWidth: patchView.minColSeverity  // 縮小下限

                            // 変更後の列幅を反映する
                            onResized: function(newWidth) {
                                patchView.colSeverity = newWidth
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [Summary]列の見出し
                    Item {
                        width: patchView.colSummary  // [Summary]列の幅
                        height: parent.height        // 見出しの高さに合わせる
                        clip: true                   // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.left: parent.left                      // 列の左端を基準にする
                            anchors.leftMargin: 4                          // 一覧の文字位置に合わせる左余白
                            anchors.right: parent.right                    // 列の右端を基準にする
                            anchors.rightMargin: 8                         // リサイズハンドルと重ならないようにする右余白
                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                            text: qsTr("Summary")                          // 列名の文字列
                            font.bold: true                                // 見出しとして太字にする
                            elide: Text.ElideRight                         // 列幅を超える場合は末尾を省略する
                        }

                        // ドラッグで[Summary]列の幅を変更するハンドル
                        ColumnResizeHandle {
                            columnWidth: patchView.colSummary  // ドラッグ開始時に基準とする列幅
                            minWidth: patchView.minColSummary  // 縮小下限

                            // 変更後の列幅を反映する
                            onResized: function(newWidth) {
                                patchView.colSummary = newWidth
                            }
                        }
                    }

                    // 区切り線
                    Rectangle {
                        width: 1               // 区切り線の太さ
                        height: parent.height  // 見出しの高さに合わせる
                        color: palette.mid     // 区切り線の色
                    }

                    // [Flags]列の見出し (固定幅)
                    Item {
                        width: patchView.colFlags  // [Flags]列の幅
                        height: parent.height      // 見出しの高さに合わせる
                        clip: true                 // 列幅を狭めた場合に列名がはみ出さないようにする

                        // 列名
                        Label {
                            anchors.left: parent.left                      // 列の左端を基準にする
                            anchors.leftMargin: 4                          // 一覧の文字位置に合わせる左余白
                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                            text: qsTr("Flags")                            // 列名の文字列
                            font.bold: true                                // 見出しとして太字にする
                        }
                    }
                }
            }
        }

        // パッチ一覧の表示領域
        // 縦スクロールバーを横スクロールに巻き込まれない表示幅の右端へ置くため、固定サイズのItemで包む
        Item {
            id: listArea  // 一覧の表示領域の参照名

            Layout.fillWidth: true     // 横幅いっぱいに広げる
            Layout.fillHeight: true    // 残りの高さをすべて使う
            Layout.minimumHeight: 120  // 狭くなっても確保する最小の高さ

            // パッチ一覧の横スクロール領域
            // 全列の合計幅が表示幅を超えた場合に横スクロールできるようにする
            Flickable {
                id: patchFlick  // 横スクロール領域の参照名

                // 横スクロールバーが必要か (全列の合計幅が表示幅を超える場合)
                readonly property bool needsHBar: patchView.minRowWidth > width  // 横スクロールバーの要否

                anchors.top: parent.top                               // 表示領域の上端を基準にする
                anchors.left: parent.left                             // 表示領域の左端を基準にする
                anchors.right: parent.right                           // 表示領域の右端を基準にする
                anchors.bottom: parent.bottom                         // 表示領域の下端を基準にする
                anchors.rightMargin: patchView.scrollBarSize          // 縦スクロールバーの幅を空ける
                // 横スクロールバーの表示中は、最後の行が隠れないようその高さを空ける
                anchors.bottomMargin: needsHBar ? patchView.scrollBarSize : 0
                contentWidth: Math.max(width, patchView.minRowWidth)  // 表示幅と全列の合計幅の大きい方
                contentHeight: height                                 // 縦方向はListView側でスクロールする
                clip: true                                            // 領域外にはみ出した部分を描画しない
                flickableDirection: Flickable.HorizontalFlick         // 横方向のみスクロールする
                boundsBehavior: Flickable.StopAtBounds                // 端を越えてスクロールさせない

                // 横スクロールバー
                // 一覧の下端に確保した領域へ置き、必要な場合のみ表示する
                ScrollBar.horizontal: ScrollBar {
                    parent: listArea                 // 一覧の表示領域の下端へ置く
                    anchors.left: patchFlick.left    // 横スクロール領域の左端に合わせる
                    anchors.right: patchFlick.right  // 横スクロール領域の右端に合わせる
                    anchors.top: patchFlick.bottom   // 横スクロール領域の直下に置く
                    height: patchView.scrollBarSize  // スクロールバーの太さ
                    policy: ScrollBar.AsNeeded       // 必要な場合のみ表示する
                }

                // パッチを一覧表示する領域
                ListView {
                    id: patchList  // パッチ一覧の参照名

                    width: patchFlick.contentWidth          // 横スクロール領域の内容幅に合わせる
                    height: patchFlick.height               // 横スクロール領域の高さに合わせる
                    clip: true                              // スクロール時に枠外へはみ出した行を描画しない
                    // PackageController::patchesのパッチ一覧
                    // バインドせず、生成完了後とPackageController::patchesChangedの受信時に代入する
                    // (差し替え直前にスクロール位置を保存するため)
                    model: []
                    boundsBehavior: Flickable.StopAtBounds  // 端を越えて引っ張れないようにする

                    // 縦スクロールバー
                    // 内容幅の右端ではなく表示幅の右端へ置き、行が収まらない場合のみ表示する
                    ScrollBar.vertical: ScrollBar {
                        parent: listArea                   // 一覧の表示領域の右端へ置く
                        anchors.top: patchFlick.top        // 横スクロール領域の上端に合わせる
                        anchors.right: listArea.right      // 表示領域の右端に置く
                        anchors.bottom: patchFlick.bottom  // 横スクロール領域の下端に合わせる
                        width: patchView.scrollBarSize     // スクロールバーの太さ
                        policy: ScrollBar.AsNeeded         // 行が収まらない場合のみ表示する
                    }

                    // パッチ1件分の行
                    delegate: Item {
                        id: patchRow  // パッチ1件分の行の参照名

                        required property var modelData  // パッチ1件分のデータ

                        width: patchList.width  // 一覧の幅に合わせる
                        height: 30              // 行の高さ

                        // 各列のセルを見出しと同じ幅で横に並べる
                        Row {
                            width: parent.width    // 行の幅に合わせる
                            height: parent.height  // 行の高さに合わせる
                            spacing: 0             // 列の間隔は区切り線の幅を空けた領域で表現する

                            // チェックでインストール対象=1と対象外=0を切り替えるセル
                            Item {
                                width: patchView.colCheck  // チェックボックス列の幅
                                height: parent.height      // 行の高さに合わせる

                                // パッチをインストール対象にするかどうかの選択
                                CheckBox {
                                    anchors.centerIn: parent                       // セルの中央に配置する
                                    enabled: !PackageController.busy               // PackageController::busyの間は切り替えられないようにする
                                    activeFocusOnTab: true                         // [Tab]キーでフォーカスを移動できるようにする
                                    checked: patchRow.modelData.selected === true  // インストール対象に選ばれているか
                                    // スクリーンリーダー向けの名前
                                    Accessible.name: qsTr("Select patch %1").arg(patchRow.modelData.name)

                                    // チェック状態に応じてパッチの状態を変更する
                                    // PackageController::setPatchStatusを呼び出す
                                    // 一覧の読み直し時のスクロール位置の維持はConnectionsで行う
                                    onToggled: {
                                        PackageController.setPatchStatus(
                                            patchRow.modelData.id,
                                            checked ? 1 : 0
                                        )
                                    }
                                }
                            }

                            // 見出しの区切り線と幅を揃えるための空き領域
                            Item {
                                width: 1               // 区切り線の幅
                                height: parent.height  // 行の高さに合わせる
                            }

                            // パッチ名のセル、ソルバーが必要と判断したものは太字で表示する
                            Item {
                                width: patchView.colPatch  // [Patch]列の幅
                                height: parent.height      // 行の高さに合わせる

                                // パッチ名
                                Label {
                                    anchors.left: parent.left                        // セルの左端を基準にする
                                    anchors.leftMargin: 4                            // 左余白
                                    anchors.right: parent.right                      // セルの右端を基準にする
                                    anchors.rightMargin: 4                           // 右余白
                                    anchors.verticalCenter: parent.verticalCenter    // 上下中央に配置する
                                    text: patchRow.modelData.name || ""              // パッチ名
                                    textFormat: Text.PlainText                       // HTMLとして解釈させない
                                    font.bold: patchRow.modelData.isNeeded === true  // 必要なパッチは太字にする
                                    elide: Text.ElideRight                           // 長い名前は末尾を省略する
                                }
                            }

                            // 見出しの区切り線と幅を揃えるための空き領域
                            Item {
                                width: 1               // 区切り線の幅
                                height: parent.height  // 行の高さに合わせる
                            }

                            // libzyppのカテゴリ文字列を表示名へ変換して表示するセル
                            Item {
                                width: patchView.colCategory  // [Category]列の幅
                                height: parent.height         // 行の高さに合わせる

                                // カテゴリの表示名
                                Label {
                                    anchors.left: parent.left                                  // セルの左端を基準にする
                                    anchors.leftMargin: 4                                      // 左余白
                                    anchors.right: parent.right                                // セルの右端を基準にする
                                    anchors.rightMargin: 4                                     // 右余白
                                    anchors.verticalCenter: parent.verticalCenter              // 上下中央に配置する
                                    text: patchView.categoryText(patchRow.modelData.category)  // カテゴリの表示名
                                    textFormat: Text.PlainText                                 // HTMLとして解釈させない
                                    elide: Text.ElideRight                                     // 長い名前は末尾を省略する
                                }
                            }

                            // 見出しの区切り線と幅を揃えるための空き領域
                            Item {
                                width: 1               // 区切り線の幅
                                height: parent.height  // 行の高さに合わせる
                            }

                            // 重要度のセル、securityやcriticalなどの文字列をそのまま表示する
                            Item {
                                width: patchView.colSeverity  // [Severity]列の幅
                                height: parent.height         // 行の高さに合わせる

                                // 重要度
                                Label {
                                    anchors.left: parent.left                      // セルの左端を基準にする
                                    anchors.leftMargin: 4                          // 左余白
                                    anchors.right: parent.right                    // セルの右端を基準にする
                                    anchors.rightMargin: 4                         // 右余白
                                    anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                    text: patchRow.modelData.severity || ""        // 重要度
                                    textFormat: Text.PlainText                     // HTMLとして解釈させない
                                    elide: Text.ElideRight                         // 長い文字列は末尾を省略する
                                }
                            }

                            // 見出しの区切り線と幅を揃えるための空き領域
                            Item {
                                width: 1               // 区切り線の幅
                                height: parent.height  // 行の高さに合わせる
                            }

                            // パッチの概要のセル
                            Item {
                                width: patchView.colSummary  // [Summary]列の幅
                                height: parent.height        // 行の高さに合わせる

                                // パッチの概要
                                Label {
                                    anchors.left: parent.left                      // セルの左端を基準にする
                                    anchors.leftMargin: 4                          // 左余白
                                    anchors.right: parent.right                    // セルの右端を基準にする
                                    anchors.rightMargin: 4                         // 右余白
                                    anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                    text: patchRow.modelData.summary || ""         // パッチの概要
                                    textFormat: Text.PlainText                     // HTMLとして解釈させない
                                    elide: Text.ElideRight                         // 長い概要は末尾を省略する
                                }
                            }

                            // 見出しの区切り線と幅を揃えるための空き領域
                            Item {
                                width: 1               // 区切り線の幅
                                height: parent.height  // 行の高さに合わせる
                            }

                            // 適用時の注意を示すフラグのセル
                            Item {
                                width: patchView.colFlags  // [Flags]列の幅
                                height: parent.height      // 行の高さに合わせる

                                // フラグを横に並べる領域
                                Row {
                                    anchors.left: parent.left                      // セルの左端を基準にする
                                    anchors.leftMargin: 4                          // 左余白
                                    anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                    spacing: 4                                     // フラグ同士の間隔

                                    // 対話的な応答が必要なパッチ
                                    Label {
                                        visible: patchRow.modelData.interactive === true  // 対話的なパッチの場合のみ表示する
                                        text: qsTr("Interactive")                         // フラグの文字
                                        color: palette.placeholderText                    // 控えめな文字色
                                        font.pixelSize: 10                                // フラグの文字サイズ
                                    }

                                    // 適用後に再起動を推奨するパッチを示すフラグ、目立たせるため赤で表示する
                                    Label {
                                        visible: patchRow.modelData.rebootSuggested === true  // 再起動推奨のパッチの場合のみ表示する
                                        text: qsTr("Reboot")                                  // フラグの文字
                                        color: "#F44336"                                      // 注意を引く赤色
                                        font.pixelSize: 10                                    // フラグの文字サイズ
                                    }

                                    // 適用後にサービスの再起動を推奨するパッチ
                                    Label {
                                        visible: patchRow.modelData.restartSuggested === true  // サービス再起動推奨のパッチの場合のみ表示する
                                        text: qsTr("Restart")                                  // フラグの文字
                                        color: "#FF9800"                                       // 注意を示す橙色
                                        font.pixelSize: 10                                     // フラグの文字サイズ
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }


        // パッチがないときの案内領域、読み込み中に一瞬だけ見えないよう隠す
        Item {
            Layout.fillWidth: true                                     // 横幅いっぱいに広げる
            Layout.fillHeight: true                                    // 残りの高さをすべて使う
            visible: patchList.count === 0 && !PackageController.busy  // パッチがなく、処理中でもない場合のみ表示する

            // パッチがないことを伝える案内表示
            Label {
                anchors.centerIn: parent             // 空き領域の中央に表示する
                text: qsTr("No patches to display")  // 案内メッセージ
                color: palette.placeholderText       // 控えめな文字色
            }
        }
    }

    // PackageController::patchesChangedを受けて一覧のモデルを差し替える
    // 一覧が読み直されるとモデルのリセットで先頭へ戻るため、差し替え直前の縦スクロール位置を保存して復元する
    Connections {
        target: PackageController  // パッチ一覧の変更通知を発行するC++側のシングルトン

        /**
         * @brief パッチ一覧が更新されたときに一覧を差し替える
         * @note applyPatchesでスクロール位置の保存とモデルの代入を行う
         */
        function onPatchesChanged() {
            patchView.applyPatches()
        }
    }

    // 生成時点でPackageController::patchesに読み込み済みの一覧を表示する
    // modelをバインドしていないため、生成完了後に初回の代入を行う
    Component.onCompleted: {
        patchList.model = PackageController.patches
        displayedCategory = categoryFilter
    }
}
