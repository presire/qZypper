/**
 * @file UrlTypeSelector.qml
 * @brief リポジトリURLのスキーム選択とURL入力欄をまとめた行
 *
 * AddRepoDialogで使用され、選択したスキームに応じて入力欄の初期値とプレースホルダを切り替える
 *
 * 入力 : なし (呼び出し側はsetUrlで既存URLを渡す)
 * 出力 : url (入力されたURL文字列), currentScheme (選択中のスキーム)
 * 依存 : なし
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// スキームの選択欄とURLの入力欄を横に並べた行
RowLayout {
    id: root  // 行自身の参照名

    property string currentScheme: schemeModel.get(typeCombo.currentIndex).scheme  // 選択中のスキーム

    // 選択可能なスキームの一覧
    // text : 選択欄の表示名、scheme : URLの先頭に付けるスキーム、placeholder : 入力欄の入力例
    readonly property var schemeModel: ListModel {
        // HTTPS
        ListElement {
            text: "HTTPS"                                     // 選択欄の表示名
            scheme: "https"                                   // URLのスキーム
            placeholder: "https://download.opensuse.org/..."  // 入力例
        }

        // HTTP
        ListElement {
            text: "HTTP"                       // 選択欄の表示名
            scheme: "http"                     // URLのスキーム
            placeholder: "http://server/path"  // 入力例
        }

        // FTP
        ListElement {
            text: "FTP"                       // 選択欄の表示名
            scheme: "ftp"                     // URLのスキーム
            placeholder: "ftp://server/path"  // 入力例
        }

        // Windowsファイル共有 (SMB/CIFS)
        ListElement {
            text: "SMB/CIFS"                        // 選択欄の表示名
            scheme: "smb"                           // URLのスキーム
            placeholder: "smb://server/share/path"  // 入力例
        }

        // NFS
        ListElement {
            text: "NFS"                              // 選択欄の表示名
            scheme: "nfs"                            // URLのスキーム
            placeholder: "nfs://server/export/path"  // 入力例
        }

        // CD/DVDドライブ
        ListElement {
            text: "CD/DVD"                          // 選択欄の表示名
            scheme: "cd"                            // URLのスキーム
            placeholder: "cd:///?devices=/dev/sr0"  // 入力例
        }

        // ISOイメージファイル
        ListElement {
            text: "ISO"                                  // 選択欄の表示名
            scheme: "iso"                                // URLのスキーム
            placeholder: "iso:/?iso=/path/to/image.iso"  // 入力例
        }

        // ローカルディレクトリ
        ListElement {
            text: qsTr("Local Directory")            // 選択欄の表示名 (翻訳対象)
            scheme: "dir"                            // URLのスキーム
            placeholder: "dir:///path/to/directory"  // 入力例
        }
    }

    property alias url: urlField.text  // 入力されたURL

    /**
     * @brief 既存のURLを入力欄へ設定し、スキームに合う選択肢へ切り替える
     * @param newUrl 設定するURL
     * @note 戻り値は無く、一致するスキームがあると早期に戻る
     * @note 一致するスキームがなければ選択肢は変更しない
     */
    function setUrl(newUrl) {
        urlField.text = newUrl
        // URLスキームに基づいてComboBoxを更新
        for (var i = 0; i < root.schemeModel.count; i++) {
            var s = root.schemeModel.get(i).scheme + "://"
            if (newUrl.startsWith(s)) {
                typeCombo.currentIndex = i
                return
            }
        }
    }

    /**
     * @brief 入力内容を初期状態 (HTTPS) に戻す
     */
    function clear() {
        typeCombo.currentIndex = 0
        urlField.text = "https://"
    }

    /**
     * @brief URLとして成立しているかを簡易判定する
     * @return スキーム区切りを含み、8文字より長ければtrue
     * @note 実際の到達性は確認せず、入力途中の状態を弾くための判定にとどめる
     */
    function isValid() {
        return urlField.text.length > 8 && urlField.text.indexOf("://") > 0
    }

    spacing: 8  // 選択欄と入力欄の間隔

    // スキームの選択欄
    ComboBox {
        id: typeCombo  // スキームの選択欄の参照名

        Layout.preferredWidth: 160  // 選択欄の幅
        model: root.schemeModel     // 選択可能なスキームの一覧
        textRole: "text"            // 選択欄に表示するロール
        currentIndex: 0             // 初期選択はHTTPS

        // 未入力またはスキームだけが入っている状態なら、選択したスキームで置き換える
        onCurrentIndexChanged: {
            if (urlField.text === "" || urlField.text.match(/^\w+:\/\//)) {
                urlField.text = root.schemeModel.get(currentIndex).scheme + "://"
            }
        }
    }

    // URLの入力欄
    TextField {
        id: urlField  // URLの入力欄の参照名

        Layout.fillWidth: true  // 残りの幅をすべて使う
        text: "https://"        // 初期値 (HTTPSのスキームのみ)
        // 選択中のスキームに対応する入力例
        placeholderText: typeCombo.currentIndex >= 0 ? root.schemeModel.get(typeCombo.currentIndex).placeholder : ""
        selectByMouse: true     // マウスのドラッグで文字列を選択できるようにする
    }
}
