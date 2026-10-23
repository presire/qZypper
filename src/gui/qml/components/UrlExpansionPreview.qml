/**
 * @file UrlExpansionPreview.qml
 * @brief リポジトリURLの変数($releasever等)を展開した結果を表示する補足ラベル
 *
 * AddRepoDialogとRepoPropertiesPanelのURL入力欄の下に配置される
 * rawUrlが変わるたびに300ms待ってからバックエンドへ展開を問い合わせる (入力中の連続呼び出しを避ける)
 * 要求番号を保持し、最新の要求以外の応答 (入力がさらに変わった後に届いた古い応答) は破棄する
 * 展開結果がrawUrlと異なる場合だけ表示する
 * 保存されるのは常に展開前のURLであり、このラベルは表示専用である
 *
 * 入力 : rawUrl (展開前のURL)
 * 出力 : expandedUrl (展開後のURL、パスワードは伏せられる)
 * 依存 : PackageController (requestExpandedRepoUrlとexpandedRepoUrlReady)
 */
import QtQuick
import QtQuick.Controls
import org.presire.qzypper.gui

// 展開後URLの表示ラベル (展開結果がrawUrlと同じ場合と、未取得の間は非表示)
Label {
    id: root  // ラベル自身の参照名

    property string rawUrl: ""       // 展開前のURL (呼び出し側が入力欄の値を渡す)
    property string expandedUrl: ""  // 直近に取得した展開後のURL
    property string expandedFor: ""  // expandedUrlの展開元URL (入力が変わった後の古い結果を隠すために保持する)
    property int latestRequest: -1   // 最新の要求番号 (-1は有効な要求なし)

    // 現在の入力に対する展開結果があり、かつ展開前と異なる場合だけ表示する
    visible: root.expandedUrl.length > 0 && root.expandedFor === root.rawUrl && root.expandedUrl !== root.rawUrl

    textFormat: Text.PlainText      // HTMLとして解釈させない
    color: palette.placeholderText  // 補足情報として控えめな文字色
    wrapMode: Text.WrapAnywhere     // URLは単語区切りがないため幅に合わせて折り返す
    font.pixelSize: 11              // 文字サイズ

    // 展開結果の表示文字列
    text: qsTr("Expanded URL: %1").arg(root.expandedUrl)

    // 入力が変わったので進行中の要求を無効にし、変数を含む場合だけ再問い合わせを予約する
    onRawUrlChanged: {
        root.latestRequest = -1

        // 変数($)を含むURLだけが展開の対象となるため、入力が止まってから問い合わせる
        if (root.rawUrl.indexOf("$") >= 0) {
            debounceTimer.restart()
        }
        else {
            debounceTimer.stop()
            root.expandedUrl = ""
            root.expandedFor = ""
        }
    }

    // 入力が止まってから展開を問い合わせるタイマー
    Timer {
        id: debounceTimer  // デバウンスタイマーの参照名

        interval: 300  // 最後の入力から問い合わせまでの待ち時間 (ミリ秒)

        // 最新の入力で展開を要求し、返された要求番号を保持する
        // PackageController::requestExpandedRepoUrlを呼び出し、完了はexpandedRepoUrlReadyシグナルで受け取る
        onTriggered: {
            root.latestRequest = PackageController.requestExpandedRepoUrl(root.rawUrl)
        }
    }

    // バックエンドからの展開結果を受け取る
    Connections {
        target: PackageController  // 展開結果を通知するPackageController

        /**
         * @brief URL展開結果のシグナルを受け取る
         * @param requestId 応答の要求番号
         * @param sourceUrl 展開前のURL
         * @param expanded 展開後のURL (展開なし、失敗時は空)
         * @note 最新の要求番号と入力に一致しない古い応答は無視し、一致する応答だけexpandedUrlとexpandedForを更新する
         */
        function onExpandedRepoUrlReady(requestId, sourceUrl, expanded) {
            // 入力がさらに変わった後に届いた古い応答は、表示を乱さないよう破棄する
            if (requestId !== root.latestRequest || sourceUrl !== root.rawUrl) {
                return
            }

            root.expandedUrl = expanded
            root.expandedFor = sourceUrl
        }
    }
}
