#ifndef QZYPPER_BACKEND_LOCALRPM_H
#define QZYPPER_BACKEND_LOCALRPM_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <sys/types.h>

namespace qZypper {

/**
 * @brief RPM本体の最大許可サイズ (2[GiB])
 */
constexpr std::uintmax_t kMaxLocalRpmBytes = static_cast<std::uintmax_t>(2) * 1024 * 1024 * 1024;

/**
 * @brief 1回の要求で受け付けるRPM数の上限
 */
constexpr std::size_t kMaxLocalRpmCount = 64;

/**
 * @brief 取消しでコピーを中断した時にcopyLocalRpmToが返す理由
 */
constexpr const char kLocalRpmCopyCancelled[] = "copy cancelled";

/**
 * @class LocalRpmFile
 * @brief 検証済みローカルRPMファイル
 *
 * O_NOFOLLOWで開いたファイル記述子を検証からコピー完了まで保持し、statと実体の食い違い (TOCTOU) を防ぐ
 * ムーブ専用
 */
class LocalRpmFile {
public:
    LocalRpmFile() = default;
    ~LocalRpmFile();
    LocalRpmFile(const LocalRpmFile &) = delete;
    LocalRpmFile &operator=(const LocalRpmFile &) = delete;
    LocalRpmFile(LocalRpmFile &&other) noexcept;
    LocalRpmFile &operator=(LocalRpmFile &&other) noexcept;

    int fd = -1;                   // O_NOFOLLOWで開いた記述子
    std::string path;              // 呼び出し側が指定したパス (表示用)
    std::string filename;          // basename (拡張子検証済み)
    std::uintmax_t size = 0;       // fstatで取得したサイズ
};

/**
 * @brief パス文字列の構文検査 (絶対パス・'..'禁止・制御文字禁止・.rpm拡張子)
 * @param path 検証対象パス
 * @param err 失敗時の理由
 * @return 有効時true
 */
bool validateLocalRpmPath(const std::string &path, std::string &err);

/**
 * @brief fstatの結果から呼び出し元が読めるか判定する
 *
 * 世界可読 (S_IROTH) か、所有者が呼び出し元と一致する場合のみ許可する
 * バックエンドはrootとして動作するため、root専用ファイルを非特権ユーザに読ませないための検査
 *
 * @param mode ファイルモード
 * @param fileUid ファイル所有者uid
 * @param callerUid 呼び出し元uid
 * @return 読める場合はtrue
 */
bool isCallerReadable(mode_t mode, uid_t fileUid, uid_t callerUid);

/**
 * @brief パスを安全に開いて検証し、LocalRpmFileを返す
 * @param path 検証対象パス
 * @param callerUid 呼び出し元uid
 * @param maxBytes 許可最大サイズ
 * @param out 成功時の出力先
 * @param err 失敗時の理由
 * @return 成功時はtrue
 */
bool openLocalRpm(const std::string &path, uid_t callerUid, std::uintmax_t maxBytes, LocalRpmFile &out, std::string &err);

/**
 * @brief 呼び出し元 (UI) がユーザ権限で開いて渡した記述子を検証して取り込む
 *
 * ファイルのオープンは呼び出し元の権限で行われるため、rootバックエンドがパスを開く場合のアクセス権・symlink・FIFOの問題を回避できる
 * 記述子は複製して保持する (元の記述子の所有権は移らない)
 *
 * @param fd D-Busで受け取った記述子
 * @param fileName 表示・配置用ファイル名 (basename、.rpm拡張子必須)
 * @param maxBytes 許可最大サイズ
 * @param out 成功時の出力先
 * @param err 失敗時の理由
 * @return 成功時はtrue
 */
bool adoptLocalRpmFd(int fd, const std::string &fileName, std::uintmax_t maxBytes, LocalRpmFile &out, std::string &err);

/**
 * @brief 検証済み記述子から宛先へ安全にコピーする
 * @param src コピー元 (openLocalRpmで検証済み)
 * @param destPath 宛先パス (O_EXCLで新規作成)
 * @param maxBytes 許可最大サイズ
 * @param err 失敗時の理由 (取消し時はkLocalRpmCopyCancelled)
 * @param cancel 取消し要求フラグ (nullptrの場合は確認しない)
 * @param heartbeat 読み込みごとの進捗通知 (空の場合は呼ばない)
 * @return 成功時はtrue
 */
bool copyLocalRpmTo(const LocalRpmFile &src,
                    const std::string &destPath,
                    std::uintmax_t maxBytes,
                    std::string &err,
                    const std::atomic<bool> *cancel = nullptr,
                    const std::function<void()> &heartbeat = {});

} // namespace qZypper

#endif // QZYPPER_BACKEND_LOCALRPM_H
