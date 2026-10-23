/**
 * @file AddServiceDialog.qml
 * @brief リポジトリサービスを追加するダイアログ
 *
 * RepoManagerDrawerContentから開かれる
 *
 * 入力 : サービスのURLと別名
 * 出力 : サービス追加要求
 * 依存 : PackageController (D-Busバックエンドのサービス管理API)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// サービス (repoindex.xml) のURLとエイリアスを入力して追加するダイアログ
Dialog {
    id: root  // ダイアログ自身の参照名

    property bool awaiting: false     // 追加要求の応答待ち
    property string errorMessage: ""  // 追加失敗時のエラー表示

    signal serviceAdded()  // サービス追加要求

    anchors.centerIn: parent           // 親ウィンドウの中央に表示する
    width: 450                         // ダイアログの幅
    height: 285                        // プラグインサービスの注記を含む高さ
    title: qsTr("Add Service")         // ダイアログのタイトル
    standardButtons: Dialog.NoButton   // 標準ボタンは使わず、独自の[Cancel]・[Add]ボタンを使う
    modal: true                        // 表示中は背後の画面を操作できないようにする
    closePolicy: Dialog.CloseOnEscape  // [Esc]キーでのみ閉じる (外側のクリックでは閉じない)

    // ダイアログを開くたびに入力状態を初期値へ戻す
    onOpened: {
        awaiting = false
        errorMessage = ""
        urlField.text = "https://"
        aliasField.text = ""
        urlField.forceActiveFocus()
    }

    // ダイアログ本体 (案内、入力欄、操作ボタン、エラー表示)
    contentItem: ColumnLayout {
        spacing: 12  // 項目の間隔

        // 入力内容の案内
        Label {
            text: qsTr("Enter the service URL and alias")  // 案内の文字列
            font.pixelSize: 14                             // 文字サイズ
            font.bold: true                                // 太字で表示する
        }

        // プラグインサービスはURL指定で追加できないことの注記
        Label {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            opacity: 0.8            // 注記を控えめに表示する
            text: qsTr("Plugin services are provided by installed packages and cannot be added here; use zypper for those.")
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        // 項目名と入力欄を2列で並べる
        GridLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる
            columns: 2              // 項目名と入力欄の2列
            columnSpacing: 12       // 列の間隔
            rowSpacing: 8           // 行の間隔

            // [URL:]の項目名
            Label {
                text: qsTr("URL:")  // 項目名の文字列
            }

            // サービスURLの入力欄
            TextField {
                id: urlField  // URL入力欄の参照名

                Layout.fillWidth: true                                     // 残りの幅をすべて使う
                placeholderText: "https://example.com/repo/repoindex.xml"  // 未入力時に表示する入力例
                selectByMouse: true                                        // マウスで文字列を選択できるようにする
            }

            // [Alias:]の項目名
            Label {
                text: qsTr("Alias:")  // 項目名の文字列
            }

            // サービスのエイリアスの入力欄
            TextField {
                id: aliasField  // エイリアス入力欄の参照名

                Layout.fillWidth: true                      // 残りの幅をすべて使う
                placeholderText: qsTr("Unique identifier")  // 未入力時に表示する案内
            }
        }

        // 操作ボタンを下側へ寄せるための余白
        Item {
            Layout.fillHeight: true  // 残りの高さをすべて使う
        }

        // 操作ボタンを右寄せで横に並べる
        RowLayout {
            Layout.fillWidth: true  // 親の幅いっぱいに広げる

            // ボタンを右寄せにするための余白
            Item {
                Layout.fillWidth: true  // 残りの幅をすべて使う
            }

            // [Cancel]ボタン
            Button {
                text: qsTr("Cancel")  // ボタンの文字列

                // 追加せずにダイアログを閉じる
                onClicked: {
                    root.close()
                }
            }

            // [Add]ボタン
            Button {
                // 追加処理中でなくURLとエイリアスが入力済みのときだけ追加できる
                enabled: !root.awaiting && urlField.text.length > 8 && aliasField.text.length > 0
                text: qsTr("Add")  // ボタンの文字列
                highlighted: true  // 推奨操作として強調表示する

                // PackageController::addService()でサービスの追加を要求し、応答待ちにする
                onClicked: {
                    root.errorMessage = ""
                    root.awaiting = true
                    PackageController.addService(urlField.text, aliasField.text)
                }
            }
        }

        // 追加失敗時のエラーメッセージ
        Label {
            Layout.fillWidth: true                 // 親の幅いっぱいに広げる
            // エラーがある場合のみ表示する
            visible: root.errorMessage.length > 0  // エラーがある場合のみ表示する
            text: root.errorMessage                // エラーメッセージ
            textFormat: Text.PlainText             // HTMLとして解釈させない
            color: palette.brightText              // 目立つ文字色
            wrapMode: Text.WordWrap                // 幅に収まらない場合は単語単位で折り返す
        }
    }

    // D-Busサービス追加要求の完了を受け、成功時だけ親へ通知して閉じる
    Connections {
        target: PackageController  // 追加完了を通知するPackageController

        /**
         * @brief サービス追加完了のシグナルを受け取る
         * @param success 追加に成功したか
         * @note 戻り値は無く、追加要求を出していない通知は無視する
         */
        function onAddServiceFinished(success) {
            // このダイアログからの追加要求でない場合は何もしない
            if (!root.awaiting) {
                return
            }

            root.awaiting = false

            // 成功時は親へ通知して閉じ、失敗時はエラーを表示する
            if (success) {
                root.serviceAdded()
                root.close()
            }
            else {
                root.errorMessage = qsTr("Failed to add service.")
            }
        }
    }
}
