/**
 * @file StatusIcon.qml
 * @brief パッケージステータスを表す小さなアイコン
 *
 * PackageTableの行とPackageDetailsPaneで使用され、ステータス名はツールチップで補う
 *
 * 入力 : status (パッケージステータス値), iconSize (表示サイズ), showToolTip (ツールチップの有無)
 * 出力 : なし
 * 依存 : PackageStatusInfo (アイコンパスと表示名の取得)
 */
import QtQuick
import QtQuick.Controls
import org.presire.qzypper.gui

// パッケージステータスのアイコン (ホバーでステータス名をツールチップ表示)
Image {
    id: root  // アイコン自身の参照名

    property int status: PackageStatusInfo.NoInst  // 表示するステータス値
    property int iconSize: 16                      // アイコンの一辺のピクセル数
    // 行全体にツールチップがある場合などはfalseにする
    property bool showToolTip: true                // ツールチップを表示するか

    width: iconSize                                     // アイコンの幅
    height: iconSize                                    // アイコンの高さ
    source: PackageStatusInfo.iconSource(status)        // PackageStatusInfo.iconSource()が返すステータス別のアイコン
    sourceSize.width: iconSize                          // 表示サイズで画像を読み込み、拡大縮小によるぼやけを防ぐ
    sourceSize.height: iconSize                         // 表示サイズで画像を読み込む (高さ)
    fillMode: Image.PreserveAspectFit                   // 縦横比を保って収める
    smooth: true                                        // 拡大縮小時になめらかに描画する
    Accessible.role: Accessible.Graphic                 // スクリーンリーダー向けの役割 (画像)
    Accessible.name: PackageStatusInfo.label(status)    // スクリーンリーダー向けの名前 (ステータス名)
    ToolTip.visible: root.showToolTip && hover.hovered  // ツールチップが有効で、ホバー中の場合のみ表示する
    ToolTip.delay: 500                                  // カーソルが通り過ぎただけでは出さない
    ToolTip.text: PackageStatusInfo.label(root.status)  // ステータス名

    // showToolTipがfalseのときはホバー判定自体を止める
    HoverHandler {
        id: hover  // ホバー判定の参照名

        enabled: root.showToolTip  // ツールチップを表示する場合のみホバーを検出する
    }
}
