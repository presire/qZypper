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
 * @brief zyppヒストリ (/var/log/zypp/history) の1行分
 */
struct HistoryEntry {
    qint64 timestamp = 0;   // エポックタイム
    QString action;         // 操作種別 (install/remove/radd/rremove/command等)
    QString name;           // パッケージ名 (コマンド行では空)
    QString edition;        // エディション
    QString arch;           // アーキテクチャ
    QString repoAlias;      // リポジトリ別名
    QString requestedBy;    // 実行主体 (reqby / executedBy)
    QString userdata;       // 補足データ
    QString command;        // 実行コマンド (command行のみ)

    /**
     * @brief QVariantMapへ変換する
     * @return D-Bus転送用マップ
     */
    QVariantMap toVariantMap() const;
};

/**
 * @brief GetHistoryのオプション
 */
struct HistoryOptions {
    qint64 since = 0;          // このエポック秒以降 (0は制限なし)
    qint64 until = 0;          // このエポック秒以前 (0は制限なし)
    quint32 limit = 1000;      // 最大件数
    quint32 offset = 0;        // 先頭から読み飛ばす件数
    QStringList actions;       // 操作種別フィルタ (空は全種別)
};

/// GetHistoryのlimit上限
constexpr quint32 kMaxHistoryLimit = 100000;

/// GetHistoryが保持するoffset + limitの上限 (メモリ使用量の安全弁)
constexpr quint32 kMaxHistoryWindow = 1000000;

/// 履歴読み込み時にファイルから走査する最大エントリ数 (読み込み量の安全弁)
/// 超過時は部分結果を返さずエラーにする
constexpr quint32 kMaxHistoryScan = 1000000;

/// 読み込みを許可する履歴ファイルの最大サイズ (バイト)
/// logrotate (size 10M) の想定を十分に上回る値とし、超過時はエラーにする
constexpr quint64 kMaxHistoryBytes = 64ULL * 1024 * 1024;

/**
 * @brief 履歴ファイル読み込みの資源上限
 */
struct HistoryReadLimits {
    quint64 maxBytes = kMaxHistoryBytes;   // ファイルサイズ上限
    quint64 maxEntries = kMaxHistoryScan;  // 走査エントリ数上限
};

/**
 * @class BoundedHistoryCollector
 * @brief 履歴読み込み中のメモリを抑える有界コレクタ
 *
 * ファイル順 (古い→新しい、同時刻は出現順) でadd()されたHistoryEntryのうち、
 * 最終順序 (新しい順、同時刻は出現順) で上位offset + limit件のみを保持する
 *
 * 内部は単調増加する出現番号 (seq) によるタイブレーク付きの固定長ヒープで、保持件数がoffset + limitを超えることはない
 *
 * 最終順序は従来のstd::stable_sort (timestamp降順、同時刻はファイル出現順) と一致する
 */
class BoundedHistoryCollector
{
public:
    /**
     * @brief コンストラクタ
     * @param offset 最終順序の先頭から読み飛ばす件数
     * @param limit 読み飛ばし後に返却する最大件数
     */
    BoundedHistoryCollector(quint32 offset, quint32 limit);

    /**
     * @brief エントリを1件追加する
     * @param entry 追加するエントリ (呼び出し順がファイル出現順)
     */
    void add(const HistoryEntry &entry);

    /**
     * @brief 現在保持しているエントリ数
     * @return 保持件数 (capacity()以下)
     */
    std::size_t size() const { return m_kept.size(); }

    /**
     * @brief 保持上限 (offset + limit)
     * @return 上限件数
     */
    std::size_t capacity() const { return m_capacity; }

    /**
     * @brief 最終順序でoffset件読み飛ばした結果を返す
     * @return 返却対象のエントリ一覧 (最大limit件)
     */
    std::vector<HistoryEntry> take() const;

private:
    /**
     * @struct Item
     * @brief ヒープに保持する要素
     */
    struct Item {
        HistoryEntry entry;     // 履歴エントリ本体
        quint64 seq = 0;        // 出現順 (同時刻のエントリを並べるタイブレーク用)
    };

    /**
     * @brief 最終順序でaがbより前 (新しい) ならtrue
     * @param a 比較対象
     * @param b 比較対象
     * @return aが先に来る場合true
     */
    static bool newerThan(const Item &a, const Item &b);

    /**
     * @struct HeapCompare
     * @brief ヒープ先頭が最終順序で最下位 (最初に捨てる候補) になる比較子
     */
    struct HeapCompare {
        /**
         * @brief 最終順序の下位要素がヒープ先頭になるよう比較する
         * @param a 比較する要素
         * @param b 比較する要素
         * @return aが最終順序でbより前ならtrue
         */
        bool operator()(const Item &a, const Item &b) const { return newerThan(a, b); }
    };

    std::size_t m_capacity;    // 保持上限
    quint32 m_offset;          // 先頭から読み飛ばす件数
    quint64 m_seq = 0;         // 単調増加する出現番号
    std::vector<Item> m_kept;  // 固定長ヒープ
};

/**
 * @brief 履歴オプションマップを検証し正規化する
 *
 * since/untilは0以上 (0は制限なし)
 * 両方指定時はsince <= until
 * limitは1..kMaxHistoryLimit
 * offset + limitはkMaxHistoryWindow以下
 *
 * actionsは既知の操作種別のみ許可
 *
 * @param map D-Busから受け取ったオプションマップ
 * @param out 正規化したオプション (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効であればtrue
 */
bool parseHistoryOptions(const QVariantMap &map, HistoryOptions &out, std::string &error);

/**
 * @brief 履歴ファイルを資源上限付きで読み込む
 *
 * ファイルサイズがlimits.maxBytesを超える場合、または、走査エントリ数がlimits.maxEntriesに達した場合は、部分結果を返さずに失敗する
 * cancelが立つとエントリ境界で読み込みを打ち切り、失敗を返す
 *
 * @param path 履歴ファイルのパス
 * @param options 検証済みの履歴オプション
 * @param cancel キャンセルフラグ
 * @param heartbeat 走査の進捗通知 (一定件数ごとに呼ぶ)
 * @param out 結果 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @param limits 資源上限
 * @return 成功ならtrue
 */
bool readHistoryFile(const std::string &path, const HistoryOptions &options,
                     const std::atomic<bool> &cancel, const std::function<void()> &heartbeat,
                     std::vector<HistoryEntry> &out, std::string &error,
                     const HistoryReadLimits &limits = HistoryReadLimits());

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPHISTORY_H
