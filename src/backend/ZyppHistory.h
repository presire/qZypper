#ifndef QZYPPER_BACKEND_ZYPPHISTORY_H
#define QZYPPER_BACKEND_ZYPPHISTORY_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <string>

namespace qZypper {

/**
 * @brief zypp ヒストリ (/var/log/zypp/history) の 1 行分。
 */
struct HistoryEntry {
    qint64 timestamp = 0;   ///< エポック秒
    QString action;         ///< 操作種別 (install/remove/radd/rremove/command 等)
    QString name;           ///< パッケージ名 (コマンド行では空)
    QString edition;        ///< エディション
    QString arch;           ///< アーキテクチャ
    QString repoAlias;      ///< リポジトリ別名
    QString requestedBy;    ///< 実行主体 (reqby / executedBy)
    QString userdata;       ///< 補足データ
    QString command;        ///< 実行コマンド (command 行のみ)

    /**
     * @brief QVariantMap へ変換する。
     * @return D-Bus 転送用マップ
     */
    QVariantMap toVariantMap() const;
};

/**
 * @brief GetHistory のオプション。
 */
struct HistoryOptions {
    qint64 since = 0;          ///< このエポック秒以降 (0 は制限なし)
    qint64 until = 0;          ///< このエポック秒以前 (0 は制限なし)
    quint32 limit = 1000;      ///< 最大件数
    quint32 offset = 0;        ///< 先頭から読み飛ばす件数
    QStringList actions;       ///< 操作種別フィルタ (空は全種別)
};

/// GetHistory の limit 上限。
constexpr quint32 kMaxHistoryLimit = 100000;

/**
 * @brief 履歴オプションマップを検証し正規化する。
 *
 * since/until は 0 以上 (0 は制限なし)。両方指定時は since <= until。
 * limit は 1..kMaxHistoryLimit。actions は既知の操作種別のみ許可。
 *
 * @param map D-Bus から受け取ったオプションマップ
 * @param out 正規化したオプション (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効であれば true
 */
bool parseHistoryOptions(const QVariantMap &map, HistoryOptions &out, std::string &error);

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPHISTORY_H
