/**
 * @file CommitConfirmDialog.qml
 * @brief 保留中のパッケージ変更を実行する前に内容を確認するダイアログ
 *
 * Main.qmlから開かれ、破壊的な変更の実行前に明示的な確認を求める
 *
 * 入力 : 保留中の変更とディスク使用量
 * 出力 : commitRequestedシグナルまたは実行要求
 * 依存 : PackageController, Util (変更内容とサイズ情報)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// 保留中の変更内容、危険な変更の警告、ディスク使用量を表示し、適用するか確認するダイアログ
Dialog {
    id: confirmDialog  // ダイアログ自身の参照名

    property var summaryModel: []          // 保留中の変更一覧 (カテゴリ、名前、バージョン)
    property var diskUsageModel: []        // マウントポイントごとのディスク使用量
    // バックエンドのGetTransactionSummary結果 (件数と危険な変更の一覧)
    property var transactionSummary: ({})  // 取引サマリー (件数と危険な変更の集計)
    // 概要の取得待ち、取得完了までは[Apply]を押せない
    property bool summaryPending: false    // 概要を取得中かどうか
    property var rejectedLicensePackages: []  // ライセンス拒否により確認対象から除外したパッケージ

    readonly property bool summaryAvailable: transactionSummary.installCount !== undefined  // バックエンド集計を取得済みか
    readonly property int removeCount: transactionSummary.removeCount || 0                  // 削除されるパッケージ数
    readonly property int downgradeCount: transactionSummary.downgradeCount || 0            // ダウングレードされるパッケージ数
    readonly property int vendorChangeCount: transactionSummary.vendorChangeCount || 0      // ベンダーが変更されるパッケージ数
    readonly property int archChangeCount: transactionSummary.archChangeCount || 0          // アーキテクチャが変更されるパッケージ数
    // 危険な変更があるか : 概要が取得できない場合も危険とみなし、明示的な承認を求める
    readonly property bool hasRiskyChanges: !summaryPending &&
                                            (!summaryAvailable || removeCount + downgradeCount + vendorChangeCount + archChangeCount > 0)
    readonly property var riskyEntries: buildRiskyEntries(transactionSummary)               // 危険な変更を表示用の一覧へ展開したもの

    /**
     * @brief 変更前後のバージョンを矢印でつないだ文字列を返す
     * @param e バージョン情報を持つ要素
     * @return 変更前後のバージョン表記
     */
    function versionDetail(e) {
        return (e.oldVersion || "?") + " \u2192 " + (e.newVersion || "?")
    }

    /**
     * @brief summaryの各危険リストを表示用の一覧へ平坦化する
     * @param s バックエンド集計の結果
     * @return 種類、名前、詳細を持つ要素の配列
     */
    function buildRiskyEntries(s) {
        var out = []
        var i

        // 削除されるパッケージ (詳細は削除前のバージョン)
        var list = s.removals || []
        for (i = 0; i < list.length; ++i) {
            out.push({ kindText: qsTr("Remove"), name: list[i].name, detail: list[i].oldVersion || "" })
        }

        // ダウングレードされるパッケージ (詳細は変更前後のバージョン)
        list = s.downgrades || []
        for (i = 0; i < list.length; ++i) {
            out.push({ kindText: qsTr("Downgrade"), name: list[i].name, detail: versionDetail(list[i]) })
        }

        // ベンダーが変更されるパッケージ (詳細は変更前後のベンダー)
        list = s.vendorChanges || []
        for (i = 0; i < list.length; ++i) {
            out.push({ kindText: qsTr("Vendor change"), name: list[i].name,
                       detail: (list[i].oldVendor || "?") + " \u2192 " + (list[i].newVendor || "?") })
        }

        // アーキテクチャが変更されるパッケージ (詳細は変更前後のアーキテクチャ)
        list = s.archChanges || []
        for (i = 0; i < list.length; ++i) {
            out.push({ kindText: qsTr("Architecture change"), name: list[i].name,
                       detail: (list[i].oldArch || "?") + " \u2192 " + (list[i].newArch || "?") })
        }

        return out
    }

    /**
     * @brief 符号付きのバイト数を単位付きの文字列へ整形する
     * @param bytes バイト数 (負値は減少量を表す)
     * @return 先頭に符号を付けたサイズ表記
     */
    function formatSignedBytes(bytes) {
        return (bytes < 0 ? "-" : "+") + formatBytes(Math.abs(bytes))
    }

    /**
     * @brief バイト数を読みやすい単位の文字列へ整形する
     * @param bytes バイト数
     * @return 単位付きのサイズ表記
     */
    function formatBytes(bytes) {
        // 0バイトは単位換算せずにそのまま返す
        if (bytes === 0) {
            return "0 B"
        }

        var units = ["B", "KiB", "MiB", "GiB", "TiB"]
        var i = 0
        var value = Math.abs(bytes)

        // 1024以上の間は1つ上の単位へ換算する
        while (value >= 1024 && i < units.length - 1) {
            value /= 1024
            i++
        }

        return value.toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    anchors.centerIn: parent                     // 親ウィンドウの中央に表示する
    width: Math.min(parent.width * 0.85, 750)    // 親の幅の85% (最大750px)
    height: Math.min(parent.height * 0.85, 680)  // 親の高さの85% (最大680px)
    title: qsTr("Confirm Changes")               // ダイアログのタイトル
    standardButtons: Dialog.NoButton             // 標準ボタンは使わず、独自のボタン行を使う
    modal: true                                  // 表示中は背後の画面を操作できないようにする

    // 開くたびに承認チェックをリセットする (前回の承認を持ち越さない)
    onAboutToShow: {
        riskAckCheck.checked = false
    }

    // 件数サマリ、危険な変更の警告、変更一覧、ディスク使用量、ボタン行を縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ダイアログ内側全体を使う
        spacing: 8            // 項目の間隔

        // 拒否したライセンスにより確認対象から除外されたパッケージを表示する
        Label {
            Layout.fillWidth: true       // ダイアログの内容幅いっぱいに表示する
            Layout.maximumHeight: 90     // 多数の除外候補で確認画面が広がりすぎないようにする
            // ライセンス拒否がある場合だけ、選択が変化した理由を利用者へ示す
            visible: confirmDialog.rejectedLicensePackages.length > 0  // 拒否候補がある場合だけ表示する
            // 翻訳対象の除外理由とパッケージ名を表示する
            text: qsTr("Excluded because the license was declined: %1")
                .arg(confirmDialog.rejectedLicensePackages.join(", "))
            textFormat: Text.PlainText  // パッケージ名をマークアップとして解釈しない
            wrapMode: Text.Wrap         // 長い除外一覧を複数行で読めるようにする
            elide: Text.ElideRight      // 高さ上限を超える一覧を末尾で省略する
            color: palette.text         // 通常の本文色で除外理由を表示する
            ToolTip.visible: excludedHover.hovered  // 見切れたパッケージ名をホバー時に表示する
            ToolTip.text: text                      // ラベルと同じ除外パッケージ名を表示する

            // 見切れた除外パッケージ名のツールチップ表示を制御する
            HoverHandler {
                id: excludedHover  // 除外パッケージ表示のホバー状態
            }
        }

            // 件数とサイズのサマリ
        Label {
            Layout.fillWidth: true                                                // 親の幅いっぱいに広げる
            visible: confirmDialog.transactionSummary.installCount !== undefined  // 取引サマリーがある場合のみ表示する
            // 種類ごとの件数と、ダウンロードサイズ・インストールサイズの増減
            text: qsTr("Install: %1, Update: %2, Remove: %3, Downgrade: %4, Vendor change: %5, Architecture change: %6")
                  .arg(confirmDialog.transactionSummary.installCount || 0)
                  .arg(confirmDialog.transactionSummary.updateCount || 0)
                  .arg(confirmDialog.removeCount)
                  .arg(confirmDialog.downgradeCount)
                  .arg(confirmDialog.vendorChangeCount)
                  .arg(confirmDialog.archChangeCount)
                  + "\n"
                  + qsTr("Download size: %1, Installed size change: %2")
                  .arg(formatBytes(confirmDialog.transactionSummary.downloadSize || 0))
                  .arg(formatSignedBytes(confirmDialog.transactionSummary.installedSizeChange || 0))
            wrapMode: Text.WordWrap                                               // 幅に収まらない場合は単語単位で折り返す
        }

            // 危険な変更の警告、削除、ダウングレード、ベンダー変更、アーキテクチャ変更をまとめる
        Rectangle {
            Layout.fillWidth: true                                          // 親の幅いっぱいに広げる
            Layout.preferredHeight: riskColumn.implicitHeight + 16          // 内容に上下の余白を加えた高さ
            visible: confirmDialog.hasRiskyChanges                          // 危険な変更がある場合のみ表示する
            color: Qt.rgba(0.96, 0.26, 0.21, Theme.darkMode ? 0.18 : 0.10)  // 薄い赤の背景色 (ダークモードでは濃いめ)
            radius: 4                                                       // 角の丸み
            border.color: "#F44336"                                         // 枠線の色 (赤)

            // 警告文、危険な変更の一覧、承認チェックを縦に並べる
            ColumnLayout {
                id: riskColumn  // 警告欄の縦並びの参照名

                anchors.fill: parent  // 警告欄全体を使う
                anchors.margins: 8    // 枠との余白
                spacing: 6            // 項目の間隔

                // 警告文 (概要を取得できなかった場合は別の文言)
                Label {
                    Layout.fillWidth: true   // 親の幅いっぱいに広げる
                    // 概要を取得済みなら危険な変更の確認を、未取得なら危険な変更を否定できない旨を表示する
                    text: confirmDialog.summaryAvailable
                          ? qsTr("Warning: this transaction contains potentially dangerous changes. Review them carefully.")
                          : qsTr("Warning: the transaction summary could not be retrieved. Removals, downgrades or vendor changes cannot be ruled out.")
                    color: "#F44336"         // 警告色 (赤)
                    font.bold: true          // 注意を引くため太字で表示する
                    wrapMode: Text.WordWrap  // 幅に収まらない場合は単語単位で折り返す
                }

                // 危険な変更の一覧
                ListView {
                    id: riskList  // 危険な変更一覧の参照名

                    Layout.fillWidth: true                                        // 親の幅いっぱいに広げる
                    Layout.preferredHeight: Math.min(contentHeight, 140)          // 内容に合わせた高さ (最大140px)
                    visible: count > 0                                            // 項目がある場合のみ表示する
                    clip: true                                                    // 領域外にはみ出した部分を描画しない
                    activeFocusOnTab: true                                        // [Tab]キーでフォーカスを移せるようにする
                    model: confirmDialog.riskyEntries                             // 表示用に展開した危険な変更
                    boundsBehavior: Flickable.StopAtBounds                        // 端を越えてスクロールさせない
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ縦スクロールバーを表示する
                    Accessible.role: Accessible.List                              // スクリーンリーダー向けの役割 (一覧)
                    Accessible.name: qsTr("Dangerous changes")                    // スクリーンリーダー向けの名前

                    // 危険な変更1件分の行 (種類、名前、詳細)
                    delegate: RowLayout {
                        required property var modelData  // 危険な変更の1件 (種類、名前、詳細)

                        // スクロールバー表示中はその幅を除いた幅にする
                        width: riskList.width - (riskList.ScrollBar.vertical.visible ? riskList.ScrollBar.vertical.width + 4 : 0)
                        spacing: 8  // 列の間隔

                        // 変更の種類
                        Label {
                            Layout.preferredWidth: 150  // 列の幅
                            text: modelData.kindText    // 変更の種類 (翻訳済み)
                            font.bold: true             // 太字で表示する
                            elide: Text.ElideRight      // 収まらない場合は末尾を省略する
                        }

                        // パッケージ名
                        Label {
                            Layout.preferredWidth: 180  // 列の幅
                            text: modelData.name || ""  // パッケージ名
                            textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                            elide: Text.ElideRight      // 収まらない場合は末尾を省略する
                        }

                        // 変更の詳細 (バージョン・ベンダー・アーキテクチャの変化)
                        Label {
                            Layout.fillWidth: true          // 残りの幅をすべて使う
                            text: modelData.detail          // 変更の詳細
                            textFormat: Text.PlainText      // 外部由来の文字列をHTMLとして解釈させない
                            color: palette.placeholderText  // 控えめな文字色
                            elide: Text.ElideMiddle         // 収まらない場合は中央を省略する
                        }
                    }
                }

                // 危険な変更を理解した旨の承認チェック ([Apply]ボタンを有効にする条件)
                CheckBox {
                    id: riskAckCheck  // 承認チェックの参照名

                    activeFocusOnTab: true                                                            // [Tab]キーでフォーカスを移せるようにする
                    text: qsTr("I understand the risks of these changes")                             // チェックボックスの文字列
                    Accessible.name: text                                                             // スクリーンリーダー向けの名前 (文字列と同じ)
                    Accessible.description: qsTr("Required before dangerous changes can be applied")  // スクリーンリーダー向けの説明
                }
            }
        }

        // 変更予定パッケージ一覧
        Label {
            text: qsTr("Changes to apply:")  // 一覧の見出し
            font.bold: true                  // 太字で表示する
        }

        // 変更予定のパッケージ一覧
        ListView {
            id: changesList  // 変更一覧の参照名

            Layout.fillWidth: true                  // 親の幅いっぱいに広げる
            Layout.fillHeight: true                 // 残りの高さをすべて使う
            Layout.minimumHeight: 120               // 最小の高さ
            clip: true                              // 領域外にはみ出した部分を描画しない
            model: confirmDialog.summaryModel       // 保留中の変更一覧
            boundsBehavior: Flickable.StopAtBounds  // 端を越えてスクロールさせない

            // 必要な場合のみ表示する縦スクロールバー
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded  // 内容がはみ出す場合のみ表示する
            }

            // 変更予定1件分の行 (記号、名前、バージョン)
            delegate: RowLayout {
                required property var modelData  // 変更予定の1件 (カテゴリ、名前、バージョン)

                // スクロールバー表示中はその幅を除いた幅にする
                width: changesList.width - (changesList.ScrollBar.vertical.visible ? changesList.ScrollBar.vertical.width + 4 : 12)

                    // 変更の種類を表す記号 (インストール、更新、削除)
                Label {
                    Layout.preferredWidth: 24  // 列の幅
                    // カテゴリに対応する記号 (インストールは+、更新は↑、削除は-)
                    text: {
                        var cat = modelData.category

                        if (cat === "install") {
                            return "+"
                        }

                        if (cat === "update") {
                            return "\u2191"
                        }

                        if (cat === "delete") {
                            return "-"
                        }

                        return ""
                    }
                    // カテゴリに対応する色 (インストールは緑、更新は青、削除は赤)
                    color: {
                        var cat = modelData.category

                        if (cat === "install") {
                            return "#4CAF50"
                        }

                        if (cat === "update") {
                            return "#2196F3"
                        }

                        if (cat === "delete") {
                            return "#F44336"
                        }

                        return palette.text
                    }
                    font.bold: true                         // 太字で表示する
                    horizontalAlignment: Text.AlignHCenter  // 列の中央に表示する
                }

                // パッケージ名
                Label {
                    Layout.fillWidth: true      // 残りの幅をすべて使う
                    text: modelData.name        // パッケージ名
                    textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                    elide: Text.ElideRight      // 収まらない場合は末尾を省略する
                }

                // バージョン
                Label {
                    Layout.preferredWidth: 160      // 列の幅
                    text: modelData.version || ""   // バージョン
                    textFormat: Text.PlainText      // 外部由来の文字列をHTMLとして解釈させない
                    color: palette.placeholderText  // 控えめな文字色
                    elide: Text.ElideRight          // 収まらない場合は末尾を省略する
                }
            }
        }

        // 言語サポートの変更を一覧する領域
        // 言語の追加や削除を選んでいない場合は、領域ごと非表示にする
        ColumnLayout {
            Layout.fillWidth: true                                      // 親の幅いっぱいに広げる
            visible: PackageController.pendingLocaleChanges.length > 0  // 言語サポートの変更がある場合のみ表示する
            spacing: 4                                                  // 見出しと一覧の間隔

            // [Language Support]の見出し
            Label {
                text: qsTr("Language Support")  // 見出しの文字列
                color: palette.text             // 通常の文字色
                font.bold: true                 // 見出しとして太字にする
            }

            // 追加または削除する言語の一覧
            // 件数が多い場合は高さを96pxまでに抑え、超えた分はスクロールで表示する
            ListView {
                id: localeChangesList  // 言語サポートの変更一覧の参照名

                Layout.fillWidth: true                               // 親の幅いっぱいに広げる
                Layout.preferredHeight: Math.min(contentHeight, 96)  // 内容の高さに合わせ、最大96pxまでにする
                clip: true                                           // スクロール時に枠外へはみ出した行を描画しない
                model: PackageController.pendingLocaleChanges        // PackageController::pendingLocaleChangesの変更一覧
                boundsBehavior: Flickable.StopAtBounds               // 端を越えて引っ張れないようにする

                // 行が収まらない場合のみ表示する縦スクロールバー
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded  // 必要な場合のみ表示する
                }

                // 言語サポートの変更1件分の行
                delegate: Label {
                    required property var modelData  // 言語サポートの変更1件分のデータ (action、name、code)

                    // スクロールバーの表示中は、その幅と隙間を差し引いて文字との重なりを避ける
                    width: localeChangesList.width - (localeChangesList.ScrollBar.vertical.visible
                                                      ? localeChangesList.ScrollBar.vertical.width + 4 : 0)
                    // 追加か削除かに応じた説明文 (言語名がない場合は言語コードで代用する)
                    text: modelData.action === "add"
                          ? qsTr("Add language support: %1 (%2)").arg(modelData.name || modelData.code || "").arg(modelData.code || "")
                          : qsTr("Remove language support: %1 (%2)").arg(modelData.name || modelData.code || "").arg(modelData.code || "")
                    textFormat: Text.PlainText  // 外部由来の文字列をHTMLとして解釈させない
                    color: palette.text         // 通常の文字色
                    elide: Text.ElideRight      // 収まらない場合は末尾を省略する
                }
            }
        }

        // 区切り線
        Rectangle {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            height: 1               // 線の太さ
            opacity: 0.5            // 控えめに表示する
            color: palette.mid      // 線の色
        }

        // ディスク使用量テーブル
        Label {
            text: qsTr("Disk Usage:")  // テーブルの見出し
            font.bold: true            // 太字で表示する
        }

        // ヘッダ
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 12             // 列の間隔

            // [Mount]列の見出し
            Label {
                Layout.preferredWidth: 150  // 列の幅
                text: qsTr("Mount")         // 見出しの文字列
                font.bold: true             // 太字で表示する
            }

            // [Total]列の見出し
            Label {
                Layout.preferredWidth: 100            // 列の幅
                text: qsTr("Total")                   // 見出しの文字列
                font.bold: true                       // 太字で表示する
                horizontalAlignment: Text.AlignRight  // 右寄せで表示する
            }

            // [Used]列の見出し
            Label {
                Layout.preferredWidth: 100            // 列の幅
                text: qsTr("Used")                    // 見出しの文字列
                font.bold: true                       // 太字で表示する
                horizontalAlignment: Text.AlignRight  // 右寄せで表示する
            }

            // [After Commit]列の見出し
            Label {
                Layout.preferredWidth: 100            // 列の幅
                text: qsTr("After Commit")            // 見出しの文字列
                font.bold: true                       // 太字で表示する
                horizontalAlignment: Text.AlignRight  // 右寄せで表示する
            }

            // [Change]列の見出し
            Label {
                Layout.fillWidth: true                // 残りの幅をすべて使う
                text: qsTr("Change")                  // 見出しの文字列
                font.bold: true                       // 太字で表示する
                horizontalAlignment: Text.AlignRight  // 右寄せで表示する
            }
        }

        // マウントポイントごとのディスク使用量一覧
        ListView {
            id: diskList  // ディスク使用量一覧の参照名

            Layout.fillWidth: true                                // 親の幅いっぱいに広げる
            Layout.preferredHeight: Math.min(contentHeight, 150)  // 内容に合わせた高さ (最大150px)
            Layout.minimumHeight: 40                              // 最小の高さ
            clip: true                                            // 領域外にはみ出した部分を描画しない
            model: confirmDialog.diskUsageModel                   // マウントポイントごとのディスク使用量

            // マウントポイント1件分の行
            delegate: RowLayout {
                required property var modelData  // ディスク使用量の1件 (マウントと容量)

                // コミット後の使用量を計算する
                property real afterCommit: (modelData.usedSize || 0) + (modelData.packageUsage || 0)  // コミット後の使用量
                // マウントの総容量 (0除算を避けるため最低1)
                property real totalSize: modelData.totalSize || 1                                     // 総容量 (最低1)
                // コミット後に残る空き容量
                property real freeAfter: totalSize - afterCommit                                      // コミット後の空き容量
                // 空き容量が10%未満なら警告を出す
                property bool isWarning: freeAfter < totalSize * 0.1                                  // 10%未満で警告

                width: diskList.width  // 一覧の幅に合わせる
                spacing: 12            // 列の間隔

                // マウントポイント
                Label {
                    Layout.preferredWidth: 150        // 列の幅
                    text: modelData.mountPoint || ""  // マウントポイントのパス
                    textFormat: Text.PlainText        // HTMLとして解釈させない
                    elide: Text.ElideRight            // 収まらない場合は末尾を省略する
                }

                // 総容量
                Label {
                    Layout.preferredWidth: 100                   // 列の幅
                    text: formatBytes(modelData.totalSize || 0)  // 単位付きの総容量
                    horizontalAlignment: Text.AlignRight         // 右寄せで表示する
                }

                // 現在の使用量
                Label {
                    Layout.preferredWidth: 100                  // 列の幅
                    text: formatBytes(modelData.usedSize || 0)  // 単位付きの使用量
                    horizontalAlignment: Text.AlignRight        // 右寄せで表示する
                }

                // コミット後の使用量 (空き容量が少ない場合は赤の太字)
                Label {
                    Layout.preferredWidth: 100                   // 列の幅
                    text: formatBytes(afterCommit)               // 単位付きのコミット後使用量
                    color: isWarning ? "#F44336" : palette.text  // 空き容量不足の場合は赤
                    font.bold: isWarning                         // 空き容量不足の場合は太字
                    horizontalAlignment: Text.AlignRight         // 右寄せで表示する
                }

                // 使用量の増減
                Label {
                    Layout.fillWidth: true  // 残りの幅をすべて使う
                    // 増加なら+、減少なら符号なしで単位付きの変化量を表示する
                    text: {
                        var change = modelData.packageUsage || 0
                        var prefix = change >= 0 ? "+" : ""
                        return prefix + formatBytes(Math.abs(change))
                    }
                    // 増加はオレンジ、減少は緑、変化なしは通常の文字色
                    color: {
                        var change = modelData.packageUsage || 0

                        if (change > 0) {
                            return "#FF9800"
                        }

                        if (change < 0) {
                            return "#4CAF50"
                        }

                        return palette.text
                    }
                    horizontalAlignment: Text.AlignRight  // 右寄せで表示する
                }
            }
        }

        // 操作ボタンを並べる行
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            spacing: 8              // ボタンの間隔

            // ボタンを右寄せにするための余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [Cancel]ボタン
            Button {
                activeFocusOnTab: true  // [Tab]キーでフォーカスを移せるようにする
                text: qsTr("Cancel")    // ボタンの文字列
                Accessible.name: text   // スクリーンリーダー向けの名前 (文字列と同じ)

                // 変更を適用せずにダイアログを閉じる
                onClicked: {
                    confirmDialog.reject()
                }
            }

            // [Apply]ボタン
            Button {
                // サマリー取得中は適用を無効化する
                // 危険な変更がある場合は、承認チェックが入るまで押せないようにする
                enabled: PackageController.hasPendingChanges
                         && !confirmDialog.summaryPending
                         && (!confirmDialog.hasRiskyChanges || riskAckCheck.checked)
                activeFocusOnTab: true  // [Tab]キーでフォーカスを移せるようにする
                text: qsTr("Apply")     // ボタンの文字列
                highlighted: true       // 主操作として強調表示する
                Accessible.name: text   // スクリーンリーダー向けの名前 (文字列と同じ)
                // 承認チェックが未入力の場合は、先に危険な変更を確認するよう伝える
                Accessible.description: confirmDialog.hasRiskyChanges && !riskAckCheck.checked
                                        ? qsTr("Confirm the dangerous changes first") : ""

                // 変更の適用を確定してダイアログを閉じる
                onClicked: {
                    confirmDialog.accept()
                }
            }
        }
    }
}
