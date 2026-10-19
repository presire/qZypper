/**
 * @file InfoToast.qml
 * @brief 画面下部に短時間だけ表示する情報通知トースト
 *
 * Main.qmlから利用され、リポジトリ操作などの完了を控えめに知らせる
 *
 * 入力 : show()の呼び出しで受け取る通知テキスト
 * 出力 : なし
 * 依存 : PackageController (呼び出し元が渡すテキストのみ)
 */
import QtQuick
import QtQuick.Controls

// 短命の通知なので操作をブロックせず、Timerで自動的に消す
Rectangle {
    id: infoToast  // トースト自身の参照名

    /**
     * @brief トーストにテキストを設定して表示し、自動消去タイマーを開始する
     * @param text 表示する通知テキスト
     * @note 連続呼び出し時はrestart()で表示時間を延長する
     */
    function show(text) {
        infoToastLabel.text = text
        infoToast.visible = true
        infoToastTimer.restart()
    }

    anchors.horizontalCenter: parent.horizontalCenter   // 画面の左右中央に配置する
    anchors.bottom: parent.bottom                       // 画面の下端を基準にする
    anchors.bottomMargin: 64                            // 画面下端のバーと重ならない距離
    implicitWidth: infoToastLabel.implicitWidth + 20    // 本文の幅に左右の余白を加えた幅
    implicitHeight: infoToastLabel.implicitHeight + 20  // 本文の高さに上下の余白を加えた高さ
    z: 10000                                            // ダイアログより手前に出す
    visible: false                                      // show()が呼ばれるまで非表示
    color: palette.toolTipBase                          // ツールチップと同じ背景色
    radius: 4                                           // 角の丸み
    border.color: palette.mid                           // 枠線の色

    // 通知本文 (メタデータ由来の文字列を渡されるためHTML解釈させない)
    Label {
        id: infoToastLabel  // 通知本文の参照名

        anchors.centerIn: parent    // トーストの中央に表示する
        textFormat: Text.PlainText  // HTMLとして解釈させない
        color: palette.toolTipText  // ツールチップと同じ文字色
    }

    // 表示から一定時間で自動的に隠す
    Timer {
        id: infoToastTimer  // 自動消去タイマの参照名

        interval: 4000  // 表示しておく時間 (ミリ秒)

        // 表示時間が過ぎたのでトーストを隠す
        onTriggered: {
            infoToast.visible = false
        }
    }
}
