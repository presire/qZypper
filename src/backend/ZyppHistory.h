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
    qint64 timestamp = 0;   // エポック秒
    QString action;         // 操作種別 (install/remove/radd/rremove/command等)
    QString name;           // パッケージ名 (コマンド行では空)
    QString edition;        // エディション
    QString arch;           // アーキテクチャ
    QString repoAlias;      // リポジトリエイリアス
    QString requestedBy;    // 実行主体 (reqby / executedBy)
    QString userdata;       // 補足データ
    QString command;        // 実行コマンド (command行のみ)
    QString url;            // リポジトリURL (radd : 追加時、url : 変更後)
    QString oldUrl;         // 変更前のリポジトリURL (url行のみ、グループ取得時に直前の既知URLから補完)
    QString oldAlias;       // 変更前のリポジトリエイリアス (エイリアス変更行のみ)

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
    qint64 since = 0;          // このエポック秒以降 (0は制限なし、D-Bus入力のミリ秒はparseHistoryOptionsで秒へ変換済み)
    qint64 until = 0;          // このエポック秒以前 (0は制限なし、D-Bus入力のミリ秒はparseHistoryOptionsで秒へ変換済み)
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

constexpr quint32 kDefaultHistoryGroupLimit = 100;  // GetHistoryGroupsの既定グループ件数
constexpr quint32 kMaxHistoryGroupLimit = 1000;     // GetHistoryGroups 1回で返すグループ数の上限
constexpr int kMaxHistoryTextBytes = 256;           // GetHistoryGroupsの検索文字列のUTF-8上限バイト数

/**
 * @brief D-Busで受け取ったエポックミリ秒をエポック秒へ変換する (単位変換はここだけで行う)
 *
 * GUIはJavaScriptのDate.getTime() (ミリ秒) をsince/untilとして送るため、D-Bus契約はミリ秒とする
 * 0は制限なしのまま、正の値は秒へ切り捨てる
 * 切り捨てて0になる1から999ミリ秒は1秒に切り上げ、制限なしと取り違えない
 *
 * @param milliseconds エポックミリ秒 (0以上)
 * @return エポック秒
 */
constexpr qint64 epochMillisecondsToSeconds(qint64 milliseconds)
{
    // 0以下は制限なしを表すため、変換せずに0を返す
    if (milliseconds <= 0) {
        return 0;
    }

    const qint64 seconds = milliseconds / 1000;
    return seconds > 0 ? seconds : 1;
}

/**
 * @brief 履歴オプションマップを検証し正規化する
 *
 * since/untilは0以上のエポックミリ秒 (0は制限なし)、HistoryOptionsにはエポック秒へ変換して格納する
 * 両方指定時はsince <= until
 * limitは1..kMaxHistoryLimit
 * offset + limitはkMaxHistoryWindow以下
 *
 * actionsは既知の操作種別のみ許可
 *
 * @param map D-Busから受け取ったオプションマップ
 * @param out 正規化したオプション (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効な場合はtrue
 */
bool parseHistoryOptions(const QVariantMap &map, HistoryOptions &out, std::string &error);

/**
 * @brief 履歴グループ (1つのcommand行とその配下イベント)
 *
 * 履歴ファイルをファイル順に走査し、command行をグループ境界として構成する
 * command行より前のイベントは、artificial=trueの仮想グループへまとめる
 */
struct HistoryGroup {
    quint64 seq = 0;                   // ファイル内でのグループ出現番号 (0始まり、空グループも数える)
    qint64 timestamp = 0;              // command行の時刻 (仮想グループは先頭イベントの時刻)
    QString command;                   // 実行コマンド (仮想グループは"<unknown>")
    QString requestedBy;               // 実行主体
    QString userdata;                  // 補足データ
    bool artificial = false;           // true : 履歴の先頭が欠けていたため作った仮想グループ
    quint32 eventCount = 0;            // フィルタ後の配下イベント総数
    quint32 installCount = 0;          // フィルタ後のinstall件数
    quint32 removeCount = 0;           // フィルタ後のremove件数
    std::vector<HistoryEntry> events;  // 返却するイベント (ファイル順、上限で切り詰め)

    /**
     * @brief 切り詰めによって返却しなかったイベント数
     * @return eventCountから返却済み件数を引いた値
     */
    quint32 hiddenCount() const
    {
        // 返却数がeventCount以上のときは符号なし整数の引き算がアンダーフローするため、0を返す
        return eventCount > events.size() ? eventCount - static_cast<quint32>(events.size()) : 0;
    }

    /**
     * @brief QVariantMapへ変換する
     * @return D-Bus転送用マップ (eventsはa{sv}のリスト)
     */
    QVariantMap toVariantMap() const;
};

/**
 * @brief GetHistoryGroupsのオプション
 */
struct HistoryGroupOptions {
    HistoryOptions filter;                      // since/until (エポック秒) とactions、limit/offsetは使わない
    QString text;                               // 配下イベントへの部分一致 (大文字小文字無視、空は絞り込みなし)
    quint32 offset = 0;                         // 新しい順で読み飛ばすグループ数
    quint32 limit = kDefaultHistoryGroupLimit;  // 返却する最大グループ数
};

/**
 * @brief 履歴グループ取得の資源上限
 */
struct HistoryGroupLimits {
    quint64 maxBytes = kMaxHistoryBytes;   // ファイルサイズ上限
    quint64 maxEntries = kMaxHistoryScan;  // 走査エントリ数上限
    quint32 maxEventsPerGroup = 500;       // 1グループで返すイベント数の上限
    quint32 maxTotalEvents = 5000;         // 1回の応答で返すイベント総数の上限
};

/**
 * @brief 履歴グループ取得の結果
 */
struct HistoryGroupResult {
    std::vector<HistoryGroup> groups;  // 新しい順のグループ (offset/limit適用後)
    quint64 totalGroups = 0;           // フィルタ後に残った全グループ数 (offset/limit適用前)
    quint32 offset = 0;                // 適用したoffset
    quint32 limit = 0;                 // 適用したlimit
    quint64 hiddenEvents = 0;          // 上限で返却しなかったイベント総数
    bool truncated = false;            // true : 1件以上のイベントを上限で返却していない
    bool hasMore = false;              // true : offset + 返却数の後ろにまだグループがある

    /**
     * @brief QVariantMapへ変換する
     * @return D-Bus転送用マップ (groupsはa{sv}のリスト)
     */
    QVariantMap toVariantMap() const;
};

/**
 * @brief 履歴グループのオプションマップを検証し正規化する
 *
 * since、until、actionsはparseHistoryOptionsと同じ規則 (sinceとuntilはエポックミリ秒)
 * limitは1からkMaxHistoryGroupLimit、offsetは0からkMaxHistoryWindowの範囲で、quint32へ縮める前に検査する
 * textはUTF-8で256バイト以下の文字列
 * actionsのcommandはグループ境界のため配下イベントの絞り込みには使えない (指定すると一致なし)
 *
 * @param map D-Busから受け取ったオプションマップ
 * @param out 正規化したオプション (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効であればtrue
 */
bool parseHistoryGroupOptions(const QVariantMap &map, HistoryGroupOptions &out, std::string &error);

/**
 * @brief 履歴ファイルを資源上限付きで読み込む
 *
 * ファイルサイズがlimits.maxBytesを超えた場合、または走査エントリ数がlimits.maxEntriesに達した場合は、部分結果を返さずに失敗する
 * cancelが立つとエントリ境界で読み込みを打ち切り、失敗を返す
 *
 * @param path 履歴ファイルのパス
 * @param options 検証済みの履歴オプション
 * @param cancel キャンセルフラグ
 * @param heartbeat 走査の進捗通知 (一定件数ごとに呼ぶ)
 * @param out 結果 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @param limits 資源上限
 * @return 成功した場合はtrue
 */
bool readHistoryFile(const std::string &path, const HistoryOptions &options,
                     const std::atomic<bool> &cancel, const std::function<void()> &heartbeat,
                     std::vector<HistoryEntry> &out, std::string &error,
                     const HistoryReadLimits &limits = HistoryReadLimits());

/**
 * @brief 履歴ファイルをcommand単位のグループにして読み込む
 *
 * アルゴリズム:
 * 1. ファイル順に全行を読む (アクション/期間の絞り込みでcommand行を落とさないため、読み込み時は絞り込まない)
 * 2. command行をグループ境界とし、最初のcommandより前のイベントは仮想グループ ("<unknown>"、artificial=true) へ入れる
 * 3. 絞り込み (期間、アクション、テキスト) を配下イベントにだけ適用し、空になったグループを捨てる
 * 4. グループを新しい順 (時刻降順、同時刻はファイルで後ろが先) に並べる
 * 5. offset/limitをグループ単位で適用する
 *
 * 2回に分けて走査し、1回目はグループの要約だけ、2回目は返却対象グループのイベントだけを保持するため、メモリは返却量と要約数で決まる
 * 返却するイベントが上限を超える場合は、グループのtruncatedとhiddenCount、結果のtruncatedとhiddenEventsで明示する
 *
 * @param path 履歴ファイルのパス
 * @param options 検証済みのオプション
 * @param cancel キャンセルフラグ
 * @param heartbeat 走査の進捗通知
 * @param out 結果 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @param limits 資源上限
 * @return 成功ならtrue
 */
bool readHistoryGroups(const std::string &path, const HistoryGroupOptions &options,
                       const std::atomic<bool> &cancel, const std::function<void()> &heartbeat,
                       HistoryGroupResult &out, std::string &error,
                       const HistoryGroupLimits &limits = HistoryGroupLimits());

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPHISTORY_H
