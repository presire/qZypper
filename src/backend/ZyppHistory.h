#ifndef QZYPPER_BACKEND_ZYPPHISTORY_H
#define QZYPPER_BACKEND_ZYPPHISTORY_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

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

/// GetHistory が保持する offset + limit の上限 (メモリ使用量の安全弁)。
constexpr quint32 kMaxHistoryWindow = 1000000;

/// 履歴読み込み時にファイルから走査する最大エントリ数 (読み込み量の安全弁)。
/// 超過時は部分結果を返さずエラーにする。
constexpr quint32 kMaxHistoryScan = 1000000;

/// 読み込みを許可する履歴ファイルの最大サイズ (バイト)。
/// logrotate (size 10M) の想定を十分に上回る値とし、超過時はエラーにする。
constexpr quint64 kMaxHistoryBytes = 64ULL * 1024 * 1024;

/**
 * @brief 履歴ファイル読み込みの資源上限。
 */
struct HistoryReadLimits {
    quint64 maxBytes = kMaxHistoryBytes;  ///< ファイルサイズ上限
    quint64 maxEntries = kMaxHistoryScan; ///< 走査エントリ数上限
};

/**
 * @brief 履歴読み込み中のメモリを抑える有界コレクタ。
 *
 * ファイル順 (古い→新しい、同時刻は出現順) で add() された HistoryEntry のうち、
 * 最終順序 (新しい順、同時刻は出現順) で上位 offset + limit 件のみを保持する。
 * 内部は単調増加する出現番号 (seq) によるタイブレーク付きの固定長ヒープで、
 * 保持件数が offset + limit を超えることはない。最終順序は従来の
 * std::stable_sort (timestamp 降順、同時刻はファイル出現順) と一致する。
 */
class BoundedHistoryCollector
{
public:
    /**
     * @brief コンストラクタ。
     * @param offset 最終順序の先頭から読み飛ばす件数
     * @param limit 読み飛ばし後に返却する最大件数
     */
    BoundedHistoryCollector(quint32 offset, quint32 limit);

    /**
     * @brief エントリを 1 件追加する。
     * @param entry 追加するエントリ (呼び出し順がファイル出現順)
     */
    void add(const HistoryEntry &entry);

    /**
     * @brief 現在保持しているエントリ数。
     * @return 保持件数 (capacity() 以下)
     */
    std::size_t size() const { return m_kept.size(); }

    /**
     * @brief 保持上限 (offset + limit)。
     * @return 上限件数
     */
    std::size_t capacity() const { return m_capacity; }

    /**
     * @brief 最終順序で offset 件読み飛ばした結果を返す。
     * @return 返却対象のエントリ一覧 (最大 limit 件)
     */
    std::vector<HistoryEntry> take() const;

private:
    /// 保持要素 (seq は出現順タイブレーク用)。
    struct Item {
        HistoryEntry entry;
        quint64 seq = 0;
    };

    /**
     * @brief 最終順序で a が b より前 (新しい) なら true。
     * @param a 比較対象
     * @param b 比較対象
     * @return a が先に来る場合 true
     */
    static bool newerThan(const Item &a, const Item &b);

    /// ヒープ先頭が最終順序で最下位 (最初に捨てる候補) になる比較子。
    struct HeapCompare {
        bool operator()(const Item &a, const Item &b) const { return newerThan(a, b); }
    };

    std::size_t m_capacity;   ///< 保持上限
    quint32 m_offset;         ///< 先頭から読み飛ばす件数
    quint64 m_seq = 0;        ///< 単調増加する出現番号
    std::vector<Item> m_kept; ///< 固定長ヒープ
};

/**
 * @brief 履歴オプションマップを検証し正規化する。
 *
 * since/until は 0 以上 (0 は制限なし)。両方指定時は since <= until。
 * limit は 1..kMaxHistoryLimit。offset + limit は kMaxHistoryWindow 以下。
 * actions は既知の操作種別のみ許可。
 *
 * @param map D-Bus から受け取ったオプションマップ
 * @param out 正規化したオプション (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効であれば true
 */
bool parseHistoryOptions(const QVariantMap &map, HistoryOptions &out, std::string &error);

/**
 * @brief 履歴ファイルを資源上限付きで読み込む。
 *
 * ファイルサイズが limits.maxBytes を超える場合、または走査エントリ数が
 * limits.maxEntries に達した場合は、部分結果を返さずに失敗する。
 * cancel が立つとエントリ境界で読み込みを打ち切り、失敗を返す。
 *
 * @param path 履歴ファイルのパス
 * @param options 検証済みの履歴オプション
 * @param cancel キャンセルフラグ
 * @param heartbeat 走査の進捗通知 (一定件数ごとに呼ぶ)
 * @param out 結果 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @param limits 資源上限
 * @return 成功なら true
 */
bool readHistoryFile(const std::string &path, const HistoryOptions &options,
                     const std::atomic<bool> &cancel, const std::function<void()> &heartbeat,
                     std::vector<HistoryEntry> &out, std::string &error,
                     const HistoryReadLimits &limits = HistoryReadLimits());

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPHISTORY_H
