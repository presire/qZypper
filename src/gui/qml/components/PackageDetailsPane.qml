/**
 * @file PackageDetailsPane.qml
 * @brief 選択中パッケージの詳細情報を表示するペイン
 *
 * Main.qmlで使用され、PackageTableの選択内容に追従する
 *
 * 入力 : packageDetailsと選択中パッケージの情報
 * 出力 : パッケージ操作と外部URL表示の要求
 * 依存 : PackageController, Util, PackageStatusInfo
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

// パッケージ詳細ペイン (SplitViewの下段に配置され、タブで表示内容を切り替える)
Pane {
    id: detailPane  // 詳細ペイン自身の参照名

    readonly property real minPaneHeight: 200      // 下部詳細ペインに必要な最小高
    readonly property real defaultPaneHeight: 300  // 保存値が無効な場合の復元高
    // 上部のパッケージ一覧の最小高 (200) + SplitViewハンドル (2) を残す
    readonly property real reservedTopHeight: 202  // 上部一覧とハンドルに残す高さ

    // 過去に最小値付近の値が保存されている場合は既定値で補正する
    SplitView.preferredHeight: splitSettings.detailPaneHeight >= minPaneHeight
                               ? splitSettings.detailPaneHeight : defaultPaneHeight
    SplitView.minimumHeight: minPaneHeight  // SplitViewで縮められる最小の高さ
    // 上部一覧とハンドルの高さを残した高さまで広げられる (SplitView未確定の間は制限しない)
    SplitView.maximumHeight: {
        const view = detailPane.SplitView.view
        return view ? Math.max(minPaneHeight, view.height - reservedTopHeight) : Infinity
    }
    padding: 0                              // ペイン内側の余白は設けない

    // 起動時のレイアウト確定前やウィンドウリサイズ時の高さを保存しないよう、
    // ユーザがハンドルをドラッグした時のみ保存する
    onHeightChanged: {
        const view = detailPane.SplitView.view
        if (view && view.resizing) {
            splitSettings.detailPaneHeight = height
        }
    }

    // タブバーとタブの内容を縦に並べる
    ColumnLayout {
        anchors.fill: parent  // ペイン全体を使う
        spacing: 0            // タブバーと内容の間隔は設けない

        // 詳細の種類を切り替えるタブバー
        TabBar {
            id: detailsTabBar  // タブバーの参照名

            Layout.fillWidth: true  // 親の幅いっぱいに広げる

            // [Description]タブ
            TabButton {
                text: qsTr("Description")  // タブの文字列
            }

            // [Technical Data]タブ
            TabButton {
                text: qsTr("Technical Data")  // タブの文字列
            }

            // [Dependencies]タブ
            TabButton {
                text: qsTr("Dependencies")  // タブの文字列
            }

            // [Version]タブ
            TabButton {
                text: qsTr("Version")  // タブの文字列
            }

            // [File List]タブ
            TabButton {
                text: qsTr("File List")  // タブの文字列
            }

            // [Changelog]タブ
            TabButton {
                text: qsTr("Changelog")  // タブの文字列
            }
        }

        // タブごとの内容 (選択中のタブに対応する子のみ表示する)
        StackLayout {
            Layout.fillWidth: true                    // 親の幅いっぱいに広げる
            Layout.fillHeight: true                   // 残りの高さをすべて使う
            currentIndex: detailsTabBar.currentIndex  // 選択中のタブの番号

            // 説明タブ
            ScrollView {
                // パッケージ名、概要、説明文 (メタデータ由来の文字列はエスケープしてからHTMLに埋め込む)
                TextArea {
                    // 名前と概要を見出しにし、説明文の改行を<br/>に置き換えて表示する
                    text: {
                        var d = PackageController.currentPackageDetails
                        if (!d || !d.name) {
                            return ""
                        }

                        return "<h3>" + Util.escapeHtml(d.name) + "</h3>"
                                + "<p>" + Util.escapeHtml(d.summary) + "</p>"
                                + "<hr/>"
                                + "<p>" + Util.escapeHtml(d.description).replace(/\n/g, "<br/>") + "</p>"
                    }
                    textFormat: TextArea.RichText  // HTMLとして表示する
                    wrapMode: TextArea.WordWrap    // 幅に収まらない場合は単語単位で折り返す
                    readOnly: true                 // 表示専用で編集させない
                }
            }

            // 技術データタブ
            ScrollView {
                // 技術データ (URLはリンクとして表示する)
                TextArea {
                    // Util.formatTechData()で整形したHTML (パッケージ未選択時は空)
                    text: {
                        var d = PackageController.currentPackageDetails
                        if (!d || !d.name) {
                            return ""
                        }

                        return Util.formatTechData(d)
                    }
                    textFormat: TextArea.RichText  // HTMLとして表示する
                    wrapMode: TextArea.WordWrap    // 幅に収まらない場合は単語単位で折り返す
                    readOnly: true                 // 表示専用で編集させない

                    // クリックされたリンクを、Util.openSafeUrl()で安全なURLの場合のみ開く
                    onLinkActivated: function(link) {
                        Util.openSafeUrl(link)
                    }

                    // リンク上では指差しカーソル、それ以外は文字入力用のカーソルを表示する
                    HoverHandler {
                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor  // リンク上かどうかで切り替えるカーソル
                    }
                }
            }

            // 依存関係タブ
            ScrollView {
                // 依存関係の一覧
                TextArea {
                    // Util.formatDependencies()で整形したHTML (パッケージ未選択時は空)
                    text: {
                        var d = PackageController.currentPackageDetails
                        if (!d || !d.name) {
                            return ""
                        }

                        return Util.formatDependencies(d)
                    }
                    textFormat: TextArea.RichText  // HTMLとして表示する
                    wrapMode: TextArea.WordWrap    // 幅に収まらない場合は単語単位で折り返す
                    readOnly: true                 // 表示専用で編集させない
                }
            }

            // バージョンタブ
            Pane {
                id: versionPane  // バージョンタブの参照名

                // カラム幅のプロパティ (ドラッグでリサイズ可能)
                readonly property real minColRadio: 40     // ラジオ列の縮小下限
                readonly property real minColVersion: 220  // Version列の縮小下限
                readonly property real minColArch: 120     // Architecture列の縮小下限
                readonly property real minColRepo: 200     // Repository列の縮小下限
                readonly property real minColStatus: 100   // Status列の縮小下限
                property real colRadio: minColRadio        // ラジオ列の現在幅
                property real colVersion: minColVersion    // Version列の現在幅
                property real colArch: minColArch          // Architecture列の現在幅
                property real colStatus: minColStatus      // Status列の現在幅

                // 全列の合計最小幅
                property real verMinRowWidth: colRadio + colVersion + colArch + minColRepo + colStatus

                padding: 0  // ペイン内側の余白は設けない

                // 詳細ペインの高さの保存先 (detailPaneから参照する)
                Settings {
                    id: splitSettings  // 保存設定の参照名

                    property real detailPaneHeight: 300  // 保存する詳細ペイン高

                    category: "splitView"  // 設定ファイル内のグループ名
                }

                // カラムヘッダとバージョンリストを縦に並べる
                ColumnLayout {
                    anchors.fill: parent  // バージョンタブ全体を使う
                    spacing: 0            // ヘッダとリストの間隔は設けない

                    // カラムヘッダ (横スクロール同期)
                    Rectangle {
                        Layout.fillWidth: true  // 親の幅いっぱいに広げる
                        height: 28              // ヘッダの高さ
                        clip: true              // 横スクロールではみ出した部分を描画しない
                        color: palette.button   // ヘッダの背景色

                        // バージョンリストの横スクロールに追従するヘッダ領域
                        Flickable {
                            id: verHeaderFlickable  // ヘッダ領域の参照名

                            anchors.fill: parent                         // ヘッダ全体を使う
                            contentWidth: versionFlickable.contentWidth  // バージョンリストと同じ内容幅
                            contentX: versionFlickable.contentX          // バージョンリストの横スクロール位置に合わせる
                            interactive: false                           // ヘッダ自体はドラッグでスクロールさせない

                            // 各列のヘッダを横に並べる
                            Row {
                                // 表示幅と全列の合計最小幅の大きい方
                                width: Math.max(verHeaderFlickable.width, versionPane.verMinRowWidth)
                                height: 28  // ヘッダの高さ
                                spacing: 0  // 列の間隔は区切り線で表現する

                                // ラジオ列ヘッダ
                                Item {
                                    width: versionPane.colRadio  // ラジオ列の幅
                                    height: parent.height        // ヘッダの高さに合わせる
                                }

                                // 区切り線
                                Rectangle {
                                    width: 1               // 区切り線の太さ
                                    height: parent.height  // ヘッダの高さに合わせる
                                    color: palette.mid     // 区切り線の色
                                }

                                // バージョン列ヘッダ+リサイズハンドル
                                Item {
                                    width: versionPane.colVersion - 1  // Version列の幅 (区切り線の1pxを除く)
                                    height: parent.height              // ヘッダの高さに合わせる

                                    // 列名
                                    Label {
                                        anchors.centerIn: parent  // 列ヘッダの中央に表示する
                                        text: qsTr("Version")     // 列名の文字列
                                        font.bold: true           // 太字で表示する
                                    }

                                    // リサイズハンドル (ホバー中またはドラッグ中は選択色で表示する)
                                    Rectangle {
                                        anchors.right: parent.right  // 列ヘッダの右端に配置する
                                        width: 4                     // ハンドルの幅
                                        height: parent.height        // ヘッダの高さに合わせる
                                        // 操作中のみ見える色
                                        color: vhResize1.hovered || vhResize1.pressed ? palette.highlight : "transparent"

                                        // ドラッグでVersion列の幅を変更する
                                        MouseArea {
                                            id: vhResize1  // リサイズハンドルの参照名

                                            property bool hovered: false   // リサイズハンドルのホバー状態
                                            property real globalStartX: 0  // ドラッグ開始時の画面X座標
                                            property real startW: 0        // ドラッグ開始時のVersion列幅

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
                                            onPressed: function(mouse) {
                                                var p = mapToGlobal(mouse.x, 0)
                                                globalStartX = p.x
                                                startW = versionPane.colVersion
                                            }

                                            // ドラッグ量に応じて列幅を変更する (縮小下限を下回らない)
                                            onPositionChanged: function(mouse) {
                                                if (pressed) {
                                                    var p = mapToGlobal(mouse.x, 0)
                                                    versionPane.colVersion = Math.max(versionPane.minColVersion, startW + p.x - globalStartX)
                                                }
                                            }
                                        }
                                    }
                                }

                                // 区切り線
                                Rectangle {
                                    width: 1               // 区切り線の太さ
                                    height: parent.height  // ヘッダの高さに合わせる
                                    color: palette.mid     // 区切り線の色
                                }

                                // アーキテクチャ列ヘッダ + リサイズハンドル
                                Item {
                                    width: versionPane.colArch - 1  // Architecture列の幅 (区切り線の1pxを除く)
                                    height: parent.height           // ヘッダの高さに合わせる
                                    clip: true                      // 列幅を狭めた場合に列名がはみ出さないようにする

                                    // 列名
                                    Label {
                                        anchors.centerIn: parent                           // 列ヘッダの中央に表示する
                                        width: Math.min(implicitWidth, parent.width - 10)  // 列幅に収まる幅 (左右に余白を残す)
                                        text: qsTr("Architecture")                         // 列名の文字列
                                        font.bold: true                                    // 太字で表示する
                                        horizontalAlignment: Text.AlignHCenter             // 文字列を中央揃えにする
                                        elide: Text.ElideRight                             // 幅に収まらない場合は末尾を省略する
                                    }

                                    // リサイズハンドル (ホバー中またはドラッグ中は選択色で表示する)
                                    Rectangle {
                                        anchors.right: parent.right  // 列ヘッダの右端に配置する
                                        width: 4                     // ハンドルの幅
                                        height: parent.height        // ヘッダの高さに合わせる
                                        // 操作中のみ見える色
                                        color: vhResize2.hovered || vhResize2.pressed ? palette.highlight : "transparent"

                                        // ドラッグでArchitecture列の幅を変更する
                                        MouseArea {
                                            id: vhResize2  // リサイズハンドルの参照名

                                            property bool hovered: false   // リサイズハンドルのホバー状態
                                            property real globalStartX: 0  // ドラッグ開始時の画面X座標
                                            property real startW: 0        // ドラッグ開始時のArchitecture列幅

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
                                            onPressed: function(mouse) {
                                                var p = mapToGlobal(mouse.x, 0)
                                                globalStartX = p.x
                                                startW = versionPane.colArch
                                            }

                                            // ドラッグ量に応じて列幅を変更する (縮小下限を下回らない)
                                            onPositionChanged: function(mouse) {
                                                if (pressed) {
                                                    var p = mapToGlobal(mouse.x, 0)
                                                    versionPane.colArch = Math.max(versionPane.minColArch, startW + p.x - globalStartX)
                                                }
                                            }
                                        }
                                    }
                                }

                                // 区切り線
                                Rectangle {
                                    width: 1               // 区切り線の太さ
                                    height: parent.height  // ヘッダの高さに合わせる
                                    color: palette.mid     // 区切り線の色
                                }

                                // リポジトリ列ヘッダ
                                Item {
                                    // 他の列と区切り線3本を除いた残りの幅 (縮小下限あり)
                                    width: Math.max(versionPane.minColRepo, parent.width - versionPane.colRadio - versionPane.colVersion - versionPane.colArch - versionPane.colStatus - 3)
                                    height: parent.height  // ヘッダの高さに合わせる

                                    // 列名
                                    Label {
                                        anchors.centerIn: parent  // 列ヘッダの中央に表示する
                                        text: qsTr("Repository")  // 列名の文字列
                                        font.bold: true           // 太字で表示する
                                    }
                                }

                                // 区切り線
                                Rectangle {
                                    width: 1               // 区切り線の太さ
                                    height: parent.height  // ヘッダの高さに合わせる
                                    color: palette.mid     // 区切り線の色
                                }

                                // ステータス列ヘッダ
                                Item {
                                    width: versionPane.colStatus - 1  // Status列の幅 (区切り線の1pxを除く)
                                    height: parent.height             // ヘッダの高さに合わせる

                                    // 列名
                                    Label {
                                        anchors.centerIn: parent  // 列ヘッダの中央に表示する
                                        text: qsTr("Status")      // 列名の文字列
                                        font.bold: true           // 太字で表示する
                                    }
                                }
                            }
                        }
                    }

                    // バージョンリスト (横スクロール対応)
                    Flickable {
                        id: versionFlickable  // 横スクロール領域の参照名

                        Layout.fillWidth: true                                          // 親の幅いっぱいに広げる
                        Layout.fillHeight: true                                         // 残りの高さをすべて使う
                        contentWidth: Math.max(width, versionPane.verMinRowWidth)       // 表示幅と全列の合計最小幅の大きい方
                        contentHeight: height                                           // 縦方向はListView側でスクロールする
                        clip: true                                                      // 領域外にはみ出した部分を描画しない
                        flickableDirection: Flickable.HorizontalFlick                   // 横方向のみスクロールする
                        boundsBehavior: Flickable.StopAtBounds                          // 端を越えてスクロールさせない
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }  // 必要な場合のみ横スクロールバーを表示する

                        // 候補バージョンの一覧
                        ListView {
                            id: versionListView  // バージョンリストの参照名

                            // 表示中パッケージの詳細 (変更時に選択を同期する)
                            property var details: PackageController.currentPackageDetails  // 表示中パッケージの詳細
                            property int selectedIndex: -1                                 // 現在選択した候補バージョンの行番号

                            /**
                             * @brief 選択バージョンをC++側へ設定し、非同期更新後に選択位置を同期する
                             * @param idx 選択する候補バージョンの行番号
                             * @param versionData 選択した候補 (version, arch, repoAliasを持つ)
                             * @note 戻り値は無く、同じ行の再選択では早期に戻る
                             * @note PackageController::setPackageVersionを呼び、選択位置を後で同期する
                             */
                            function selectVersion(idx, versionData) {
                                // 同じ行の再選択では何もしない
                                if (selectedIndex === idx) {
                                    return
                                }

                                selectedIndex = idx
                                var d = details

                                // パッケージを表示中の場合は、PackageController::setPackageVersion()で選択した版を設定する
                                if (d && d.name) {
                                    PackageController.setPackageVersion(
                                        d.name,
                                        versionData.version,
                                        versionData.arch,
                                        versionData.repoAlias
                                    )
                                }
                                // 変更に失敗した場合も含め、実際の状態に選択を合わせる
                                Qt.callLater(syncSelection)
                            }

                            /**
                             * @brief 詳細モデルと実際のパッケージ状態から選択行を再計算する
                             * @note 選択 (ラジオボタン) を詳細の状態に合わせる
                             * @note インストール/更新対象なら候補バージョン、それ以外はインストール済みバージョンを選択する
                             */
                            function syncSelection() {
                                var d = details
                                var versions = (d && d.availableVersions) || []
                                var idx = -1
                                var i
                                if (d && d.name) {
                                    var s = d.status
                                    var toInstall = s === PackageStatusInfo.Install || s === PackageStatusInfo.AutoInstall
                                                    || s === PackageStatusInfo.Update || s === PackageStatusInfo.AutoUpdate
                                    // インストール/更新対象の場合は、候補バージョンと一致する行を選ぶ
                                    if (toInstall) {
                                        for (i = 0; i < versions.length; i++) {
                                            var v = versions[i]
                                            if (v.version === d.version && v.arch === d.arch && v.repoAlias === d.repoAlias) {
                                                idx = i
                                                break
                                            }
                                        }
                                    }
                                    else if (d.installedVersion) {
                                        // installed: いずれかのインストール済みとバージョン・アーキテクチャが一致する (バックエンドで判定)
                                        // 複数版インストール時は代表版 (installedVersion) を選ぶ
                                        // 優先順:
                                        //   0. 候補と一致するインストール済みの行 (インストール済みの版を選んで「変更なし」にした場合)
                                        //   1. installedVersionと一致し、候補と同じリポジトリの行
                                        //   2. installedVersionと一致する行
                                        //   3. インストール済みの行
                                        var sameVerIdx = -1
                                        var anyInstIdx = -1
                                        // 優先順0 : 候補と一致するインストール済みの行を探す
                                        for (i = 0; i < versions.length; i++) {
                                            var c = versions[i]
                                            if (c.installed === true && c.version === d.version
                                                    && c.arch === d.arch && c.repoAlias === d.repoAlias) {
                                                idx = i
                                                break
                                            }
                                        }

                                        // 優先順1〜3 : 優先順0で見つからない場合のみ、インストール済みの行を順に調べる
                                        for (i = 0; idx < 0 && i < versions.length; i++) {
                                            // インストール済みでない行は対象外
                                            if (versions[i].installed !== true) {
                                                continue
                                            }

                                            // 最初に見つかったインストール済みの行を優先順3の候補として記録する
                                            if (anyInstIdx < 0) {
                                                anyInstIdx = i
                                            }

                                            // installedVersionと一致しない行は優先順1〜2の対象外
                                            if (versions[i].version !== d.installedVersion) {
                                                continue
                                            }

                                            // 最初に見つかったinstalledVersionと一致する行を優先順2の候補として記録する
                                            if (sameVerIdx < 0) {
                                                sameVerIdx = i
                                            }

                                            // 候補と同じリポジトリの行が見つかった場合は優先順1として確定する
                                            if (versions[i].repoAlias === d.repoAlias) {
                                                idx = i
                                                break
                                            }
                                        }

                                        // 優先順0〜1で見つからない場合は、優先順2、優先順3の順に採用する
                                        if (idx < 0) {
                                            idx = sameVerIdx >= 0 ? sameVerIdx : anyInstIdx
                                        }
                                    }
                                }
                                selectedIndex = idx
                                // 選択行が見えている場合はスクロール位置を動かさない
                                if (idx >= 0) {
                                    positionViewAtIndex(idx, ListView.Contain)
                                }
                            }

                            width: versionFlickable.contentWidth    // 横スクロール領域の内容幅に合わせる
                            height: versionFlickable.height         // 横スクロール領域の高さに合わせる
                            clip: true                              // 領域外にはみ出した部分を描画しない
                            model: details.availableVersions || []  // 表示中パッケージの候補バージョン一覧
                            ScrollBar.vertical: ScrollBar {}        // 縦スクロールバー

                            // モデル差し替えと同じ通知で変わるため、モデル更新後に同期する
                            onDetailsChanged: {
                                Qt.callLater(syncSelection)
                            }

                            // 候補バージョンの1行
                            delegate: ItemDelegate {
                                required property int index      // availableVersions内の行番号
                                required property var modelData  // 現在行のバージョン情報

                                property bool isInstalled: modelData.installed === true            // この候補がインストール済みか
                                property bool isSelected: versionListView.selectedIndex === index  // 現在選択している候補か

                                width: versionListView.width  // リストの幅に合わせる
                                height: 32                    // 行の高さ

                                // 行のクリックでこの候補バージョンを選択する
                                onClicked: {
                                    versionListView.selectVersion(index, modelData)
                                }

                                // 各列のセルを横に並べる
                                Row {
                                    anchors.fill: parent  // 行全体を使う
                                    spacing: 0            // 列の間隔は区切り線で表現する

                                    // ラジオボタン (カスタム描画)
                                    Item {
                                        width: versionPane.colRadio  // ラジオ列の幅
                                        height: parent.height        // 行の高さに合わせる

                                        // ラジオボタンの外枠 (選択中は選択色)
                                        Rectangle {
                                            anchors.centerIn: parent  // セルの中央に配置する
                                            width: 16                 // 外枠の幅
                                            height: 16                // 外枠の高さ
                                            color: "transparent"      // 内側は塗りつぶさない
                                            radius: 8                 // 円形にする
                                            // 選択中は選択色、それ以外は文字色
                                            border.color: isSelected ? palette.highlight : palette.text
                                            border.width: 1.5         // 外枠の太さ

                                            // ラジオボタンの選択マーク
                                            Rectangle {
                                                anchors.centerIn: parent  // 外枠の中央に配置する
                                                width: 8                  // 選択マークの幅
                                                height: 8                 // 選択マークの高さ
                                                visible: isSelected       // 選択中の候補のみ表示する
                                                color: palette.highlight  // 選択色
                                                radius: 4                 // 円形にする
                                            }
                                        }

                                        // ラジオボタンのクリックでこの候補バージョンを選択する
                                        MouseArea {
                                            anchors.fill: parent                // ラジオ列全体で受け付ける
                                            cursorShape: Qt.PointingHandCursor  // クリックできることを示す指差しカーソル

                                            // この候補バージョンを選択する
                                            onClicked: {
                                                versionListView.selectVersion(index, modelData)
                                            }
                                        }
                                    }

                                    // 区切り線
                                    Rectangle {
                                        width: 1               // 区切り線の太さ
                                        height: parent.height  // 行の高さに合わせる
                                        opacity: 0.5           // ヘッダより控えめに表示する
                                        color: palette.mid     // 区切り線の色
                                    }

                                    // バージョン
                                    Item {
                                        width: versionPane.colVersion - 1  // Version列の幅 (区切り線の1pxを除く)
                                        height: parent.height              // 行の高さに合わせる

                                        // バージョン文字列 (インストール済みの候補は太字)
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                            anchors.left: parent.left                      // セルの左端を基準にする
                                            anchors.leftMargin: 4                          // 左側の余白
                                            anchors.right: parent.right                    // セルの右端を基準にする
                                            anchors.rightMargin: 4                         // 右側の余白
                                            text: modelData.version || ""                  // バージョン文字列
                                            textFormat: Text.PlainText                     // HTMLとして解釈させない
                                            font.bold: isInstalled                         // インストール済みの候補は太字で表示する
                                            elide: Text.ElideRight                         // 幅に収まらない場合は末尾を省略する
                                        }
                                    }

                                    // 区切り線
                                    Rectangle {
                                        width: 1               // 区切り線の太さ
                                        height: parent.height  // 行の高さに合わせる
                                        opacity: 0.5           // ヘッダより控えめに表示する
                                        color: palette.mid     // 区切り線の色
                                    }

                                    // アーキテクチャ
                                    Item {
                                        width: versionPane.colArch - 1  // Architecture列の幅 (区切り線の1pxを除く)
                                        height: parent.height           // 行の高さに合わせる

                                        // アーキテクチャ名
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                            anchors.left: parent.left                      // セルの左端を基準にする
                                            anchors.leftMargin: 4                          // 左側の余白
                                            text: modelData.arch || ""                     // アーキテクチャ名
                                            textFormat: Text.PlainText                     // HTMLとして解釈させない
                                            color: palette.placeholderText                 // 補足情報として控えめな文字色
                                        }
                                    }

                                    // 区切り線
                                    Rectangle {
                                        width: 1               // 区切り線の太さ
                                        height: parent.height  // 行の高さに合わせる
                                        opacity: 0.5           // ヘッダより控えめに表示する
                                        color: palette.mid     // 区切り線の色
                                    }

                                    // リポジトリ
                                    Item {
                                        // 他の列と区切り線3本を除いた残りの幅 (縮小下限あり)
                                        width: Math.max(versionPane.minColRepo, parent.width - versionPane.colRadio - versionPane.colVersion - versionPane.colArch - versionPane.colStatus - 3)
                                        height: parent.height  // 行の高さに合わせる

                                        // リポジトリ名
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                            anchors.left: parent.left                      // セルの左端を基準にする
                                            anchors.leftMargin: 4                          // 左側の余白
                                            anchors.right: parent.right                    // セルの右端を基準にする
                                            anchors.rightMargin: 4                         // 右側の余白
                                            text: modelData.repoName || ""                 // リポジトリ名
                                            textFormat: Text.PlainText                     // HTMLとして解釈させない
                                            color: palette.placeholderText                 // 補足情報として控えめな文字色
                                            elide: Text.ElideRight                         // 幅に収まらない場合は末尾を省略する
                                        }
                                    }

                                    // 区切り線
                                    Rectangle {
                                        width: 1               // 区切り線の太さ
                                        height: parent.height  // 行の高さに合わせる
                                        opacity: 0.5           // ヘッダより控えめに表示する
                                        color: palette.mid     // 区切り線の色
                                    }

                                    // ステータス
                                    Item {
                                        width: versionPane.colStatus - 1  // Status列の幅 (区切り線の1pxを除く)
                                        height: parent.height             // 行の高さに合わせる

                                        // インストール済みの候補のみ[Installed]と表示する
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter  // 上下中央に配置する
                                            anchors.left: parent.left                      // セルの左端を基準にする
                                            anchors.leftMargin: 4                          // 左側の余白
                                            text: isInstalled ? qsTr("Installed") : ""     // インストール済みの場合のみ表示する文字列
                                            color: "#4caf50"                               // インストール済みを示す緑色
                                            font.pixelSize: 12                             // 文字サイズ
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ファイル一覧タブ
            ScrollView {
                // パッケージに含まれるファイルの一覧 (1行に1ファイル)
                TextArea {
                    // ファイルパスを改行で連結した文字列 (ファイル一覧が無い場合は空)
                    text: {
                        var d = PackageController.currentPackageDetails
                        return (d && d.fileList) ? d.fileList.join("\n") : ""
                    }
                    textFormat: TextArea.PlainText  // HTMLとして解釈させない
                    font.family: "monospace"        // 等幅フォントで表示する
                    wrapMode: TextArea.NoWrap       // 長いパスも折り返さない
                    readOnly: true                  // 表示専用で編集させない
                }
            }

            // 変更ログタブ
            ScrollView {
                // パッケージの変更ログ (エントリ間は空行で区切る)
                TextArea {
                    // 変更ログの各エントリを空行で連結した文字列 (変更ログが無い場合は空)
                    text: {
                        var d = PackageController.currentPackageDetails
                        return (d && d.changeLog) ? d.changeLog.join("\n\n") : ""
                    }
                    textFormat: TextArea.PlainText  // HTMLとして解釈させない
                    font.family: "monospace"        // 等幅フォントで表示する
                    wrapMode: TextArea.WordWrap     // 幅に収まらない場合は単語単位で折り返す
                    readOnly: true                  // 表示専用で編集させない
                }
            }
        }
    }
}
