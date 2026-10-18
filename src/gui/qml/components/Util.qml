pragma Singleton
import QtQuick
import org.presire.qzypper.gui

// 画面横断で使う表示ヘルパー (サイズ整形・詳細 HTML 生成)。
QtObject {
    id: util

    // バイト数を人間可読な単位へ整形する (1024 進、小数 1 桁)。
    function formatBytes(bytes) {
        if (bytes === 0) return "0 B"
        var units = ["B", "KiB", "MiB", "GiB"]
        var i = 0
        var value = bytes
        while (value >= 1024 && i < units.length - 1) {
            value /= 1024
            i++
        }
        return value.toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    // サイズ列用の整形 (1024 未満は整数バイト、それ以上は小数 1 桁)。
    function formatSize(bytes) {
        if (bytes < 1024)            return bytes + " B"
        if (bytes < 1024 * 1024)     return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    // 変更予定パッケージからインストール概要モデルを生成する。
    function buildSummaryModel() {
        // PackageController.packages を参照してバインディング再評価をトリガー
        var _trigger = PackageController.packages
        var installs = [], updates = [], deletes = []
        // 全プール横断で変更予定パッケージを取得 (キャッシュ済みプロパティ)
        var pkgs = PackageController.pendingChanges
        for (var i = 0; i < pkgs.length; i++) {
            var pkg = pkgs[i]
            var s = pkg.status
            if (s === 1 || s === 2) {  // Install, AutoInstall
                installs.push({ category: "install", name: pkg.name, version: pkg.version })
            } else if (s === 4 || s === 5) {  // Update, AutoUpdate
                updates.push({ category: "update", name: pkg.name, version: pkg.version })
            } else if (s === 6 || s === 7) {  // Del, AutoDel
                deletes.push({ category: "delete", name: pkg.name, version: pkg.installedVersion })
            }
        }
        // カテゴリ順: インストール --> 更新 --> 削除
        return installs.concat(updates, deletes)
    }

    // 技術情報タブ用の HTML を生成する。
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
        if (urlVal && isSafeUrl(urlVal))
            html += "<tr><td><b>" + qsTr("URL") + ":</b></td><td><a href=\"" + escapeHtml(urlVal) + "\">" + escapeHtml(urlVal) + "</a></td></tr>"
        else
            html += "<tr><td><b>" + qsTr("URL") + ":</b></td><td>" + escapeHtml(urlVal) + "</td></tr>"
        html += "</table>"
        return html
    }

    // 依存関係タブ用の HTML を生成する。
    function formatDependencies(d) {
        var html = ""
        if (d.provides && d.provides.length > 0)
            html += "<h4>" + qsTr("Provides") + "</h4><p>" + d.provides.map(escapeHtml).join("<br/>") + "</p>"
        if (d.requires && d.requires.length > 0)
            html += "<h4>" + qsTr("Requires") + "</h4><p>" + d.requires.map(escapeHtml).join("<br/>") + "</p>"
        if (d.conflicts && d.conflicts.length > 0)
            html += "<h4>" + qsTr("Conflicts") + "</h4><p>" + d.conflicts.map(escapeHtml).join("<br/>") + "</p>"
        if (d.obsoletes && d.obsoletes.length > 0)
            html += "<h4>" + qsTr("Obsoletes") + "</h4><p>" + d.obsoletes.map(escapeHtml).join("<br/>") + "</p>"
        if (d.recommends && d.recommends.length > 0)
            html += "<h4>" + qsTr("Recommends") + "</h4><p>" + d.recommends.map(escapeHtml).join("<br/>") + "</p>"
        if (d.suggests && d.suggests.length > 0)
            html += "<h4>" + qsTr("Suggests") + "</h4><p>" + d.suggests.map(escapeHtml).join("<br/>") + "</p>"
        return html || qsTr("<p>No dependency information</p>")
    }

    // 信頼できないメタデータを RichText に埋め込む前の HTML エスケープ。
    // null/undefined は空文字列として扱う。
    function escapeHtml(s) {
        if (s === null || s === undefined) return ""
        return String(s)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#39;")
    }

    // http/https スキームのみ許可 (大文字小文字は区別しない)。
    // file:, javascript:, smb:, data: などそれ以外のスキームは false を返す。
    function isSafeUrl(u) {
        if (u === null || u === undefined) return false
        return /^https?:\/\/\S/i.test(String(u).trim())
    }

    // リンク開きは http/https に限定し、それ以外のスキームは無視する。
    function openSafeUrl(link) {
        if (isSafeUrl(link))
            PackageController.openExternalUrl(link)
    }
}
