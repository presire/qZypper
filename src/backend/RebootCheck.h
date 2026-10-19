#ifndef QZYPPER_BACKEND_REBOOTCHECK_H
#define QZYPPER_BACKEND_REBOOTCHECK_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace qZypper {

/**
 * @brief コミット後の再起動・再起動推奨・サービス再起動の情報
 */
struct RebootInfo {
    bool rebootNeeded = false;      // 再起動が必要 (/run/reboot-neededかrebootSuggested)
    bool restartSuggested = false;  // パッチが再起動を推奨 (restartSuggested)
    QStringList servicesToRestart;  // 削除済みファイルを使用中のSystemdサービスユニット
};

/**
 * @brief 再起動要求フラグが存在するかを検査する
 * @param path 検査対象パス (テスト用に注入可能。通常は"/run/reboot-needed")
 * @return 存在すればtrue
 */
bool rebootFlagPresent(const QString &path);

/**
 * @brief cgroup内容から再起動対象のsystemdユニット名を抽出する
 *
 * cgroup v2 (/proc/<pid>/cgroupの"0::/system.slice/sshd.service"形式) および v1形式 (複数列) の双方で、
 * 最初に現れる.service / .scopeコンポーネントを返す
 *
 * @param cgroup cgroupファイルの内容
 * @return ユニット名、判定不能なら空文字列
 */
QString serviceUnitFromCgroup(const QByteArray &cgroup);

/**
 * @brief 削除済みファイルを使用しているプロセスのsystemdユニットを列挙する
 *
 * /proc/<pid>/mapsに"(deleted)"を含むプロセスを抽出し、/proc/<pid>/cgroupからSystemdサービスユニット名を求める
 *
 * zypper/rpm/snapperなどの外部プロセスは起動しない
 * 上限 (maxProcesses / maxUnits) で有界化し、systemd以外は空を返す
 *
 * @param procRoot /proc相当のルート (テスト用に注入可能)
 * @param maxProcesses 走査するプロセス数の上限
 * @param maxUnits 返すユニット数の上限
 * @return ソート済み・重複除去済みのユニット名一覧
 */
QStringList servicesUsingDeletedFiles(const QString &procRoot,
                                      int maxProcesses = 4000,
                                      int maxUnits = 64);

} // namespace qZypper

#endif // QZYPPER_BACKEND_REBOOTCHECK_H
