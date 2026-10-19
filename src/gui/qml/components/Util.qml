pragma Singleton
/**
 * @file Util.qml
 * @brief 画面横断で使う表示ヘルパーのシングルトン
 *
 * Main.qmlの各ビューからUtil.xxxの形で参照される
 * バイト数の整形、インストール概要モデルの生成、パッケージ詳細HTMLの組み立てを担当する
 *
 * 入力 : なし (各関数の引数で受け取る)
 * 出力 : なし (整形結果の文字列や配列を返すのみ)
 * 依存 : PackageController (packages, pendingChanges, openExternalUrl)
 */
import QtQuick
import org.presire.qzypper.gui

// 画面横断で使う表示ヘルパー (サイズ整形・詳細HTML生成)
QtObject {
    id: util  // シングルトン自身の参照名

    /**
     * @brief バイト数を人間可読な単位へ整形する
     * @param bytes バイト数
     * @return 1024進、小数1桁の文字列 (1024未満は整数)
     */
    function formatBytes(bytes) {
        // 0バイトは単位換算せずにそのまま返す
        if (bytes === 0) {
            return "0 B"
        }

        var units = ["B", "KiB", "MiB", "GiB"]
        var i = 0
        var value = bytes

        while (value >= 1024 && i < units.length - 1) {
            value /= 1024
            i++
        }

        return value.toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    /**
     * @brief サイズ列用にバイト数を整形する
     * @param bytes バイト数
     * @return B/KiB/MiB/GiBのいずれかを付けた文字列
     */
    function formatSize(bytes) {
        // 1KiB未満はバイト単位で表示する
        if (bytes < 1024) {
            return bytes + " B"
        }

        // 1MiB未満はKiB単位で表示する
        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KiB"
        }

        // 1GiB未満はMiB単位で表示する
        if (bytes < 1024 * 1024 * 1024) {
            return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        }

        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    /**
     * @brief 変更予定パッケージからインストール概要モデルを生成する
     * @return category/name/versionを持つオブジェクトの配列
     * @note 戻り値はSummaryViewのmodelに直接バインドされるため、PackageControllerの変更通知で再評価される必要がある
     */
    function buildSummaryModel() {
        // PackageController.packagesを参照してバインディング再評価をトリガー
        var _trigger = PackageController.packages
        var installs = [], updates = [], deletes = []

        // 全プール横断で変更予定パッケージを取得 (キャッシュ済みプロパティ)
        var pkgs = PackageController.pendingChanges
        for (var i = 0; i < pkgs.length; i++) {
            var pkg = pkgs[i]
            var s = pkg.status

            // Install、AutoInstall
            if (s === 1 || s === 2) {
                installs.push({ category: "install", name: pkg.name, version: pkg.version })
            }
            // Update、AutoUpdate
            else if (s === 4 || s === 5) {
                updates.push({ category: "update", name: pkg.name, version: pkg.version })
            }
            // Del、AutoDel (削除対象のため、インストール済みのバージョンを表示する)
            else if (s === 6 || s === 7) {
                deletes.push({ category: "delete", name: pkg.name, version: pkg.installedVersion })
            }
        }

        // カテゴリ順 : インストール -->更新 -->削除
        return installs.concat(updates, deletes)
    }

    /**
     * @brief 技術情報タブに表示するHTMLを生成する
     * @param d PackageDetails (C++のfromVariantMapで組み立てられた詳細情報)
     * @return table要素を含むHTML文字列
     * @note メタデータ由来の値はescapeHtmlを通してから埋め込む
     */
    function formatTechData(d) {
        var html = "<table cellspacing='4'>"
        html += "<tr><td><b>" + qsTr("Version") + ":</b></td><td>" + escapeHtml(d.version) + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Architecture") + ":</b></td><td>" + escapeHtml(d.arch) + "</td></tr>"
        html += "<tr><td><b>" + qsTr("License") + ":</b></td><td>" + escapeHtml(d.license) + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Group") + ":</b></td><td>" + escapeHtml(d.group) + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Vendor") + ":</b></td><td>" + escapeHtml(d.vendor) + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Build Host") + ":</b></td><td>" + escapeHtml(d.buildHost) + "</td></tr>"
        html += "<tr><td><b>" + qsTr("Source RPM") + ":</b></td><td>" + escapeHtml(d.sourceRpm) + "</td></tr>"

        var urlVal = d.url || ""

        // http/httpsのURLのみリンクとして埋め込む (それ以外のスキームは文字列として表示する)
        if (urlVal && isSafeUrl(urlVal)) {
            html += "<tr><td><b>" + qsTr("URL") + ":</b></td><td><a href=\"" + escapeHtml(urlVal) + "\">" + escapeHtml(urlVal) + "</a></td></tr>"
        }
        else {
            html += "<tr><td><b>" + qsTr("URL") + ":</b></td><td>" + escapeHtml(urlVal) + "</td></tr>"
        }

        html += "</table>"

        return html
    }

    /**
     * @brief 依存関係タブに表示するHTMLを生成する
     * @param d PackageDetails (provides/requires/conflicts等の配列を持つ)
     * @return h4とp要素を含むHTML文字列、情報がなければ案内文
     */
    function formatDependencies(d) {
        var html = ""

        // 提供する機能 (Provides)
        if (d.provides && d.provides.length > 0) {
            html += "<h4>" + qsTr("Provides") + "</h4><p>" + d.provides.map(escapeHtml).join("<br/>") + "</p>"
        }

        // 必要とする機能 (Requires)
        if (d.requires && d.requires.length > 0) {
            html += "<h4>" + qsTr("Requires") + "</h4><p>" + d.requires.map(escapeHtml).join("<br/>") + "</p>"
        }

        // 競合する機能 (Conflicts)
        if (d.conflicts && d.conflicts.length > 0) {
            html += "<h4>" + qsTr("Conflicts") + "</h4><p>" + d.conflicts.map(escapeHtml).join("<br/>") + "</p>"
        }

        // 置き換える機能 (Obsoletes)
        if (d.obsoletes && d.obsoletes.length > 0) {
            html += "<h4>" + qsTr("Obsoletes") + "</h4><p>" + d.obsoletes.map(escapeHtml).join("<br/>") + "</p>"
        }

        // 推奨する機能 (Recommends)
        if (d.recommends && d.recommends.length > 0) {
            html += "<h4>" + qsTr("Recommends") + "</h4><p>" + d.recommends.map(escapeHtml).join("<br/>") + "</p>"
        }

        // 提案する機能 (Suggests)
        if (d.suggests && d.suggests.length > 0) {
            html += "<h4>" + qsTr("Suggests") + "</h4><p>" + d.suggests.map(escapeHtml).join("<br/>") + "</p>"
        }

        return html || qsTr("<p>No dependency information</p>")
    }

    /**
     * @brief メタデータ由来の文字列をHTMLへ埋め込む前にエスケープする
     * @param s 対象の文字列
     * @return エスケープ済み文字列 (nullとundefinedは空文字列)
     * @note リポジトリのメタデータは信頼できない入力として扱う
     */
    function escapeHtml(s) {
        // nullとundefinedは空文字列として扱う
        if (s === null || s === undefined) {
            return ""
        }

        return String(s)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#39;")
    }

    /**
     * @brief URLがhttpまたはhttpsスキームかを判定する
     * @param u 判定するURL
     * @return http/httpsならtrue (大文字小文字は区別しない)
     * @note file: やjavascript: などローカル資源やスクリプトを指すスキームを除外する
     */
    function isSafeUrl(u) {
        // nullとundefinedは安全なURLではない
        if (u === null || u === undefined) {
            return false
        }

        return /^https?:\/\/\S/i.test(String(u).trim())
    }

    /**
     * @brief 安全と判定したURLだけを外部ブラウザで開く
     * @param link 開くURL
     * @note PackageController::openExternalUrlを呼び出す
     *       それ以外のスキームは無視する
     */
    function openSafeUrl(link) {
        // http/httpsのURLのみ外部ブラウザで開く
        if (isSafeUrl(link)) {
            PackageController.openExternalUrl(link)
        }
    }
}
