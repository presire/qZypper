#include "RebootCheck.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace qZypper {

namespace {
// /proc/<pid>/maps の読み取り上限 (壊れた巨大ファイルでも有界にする)
constexpr qint64 kMaxMapsBytes = 4 * 1024 * 1024;
// /proc/<pid>/cgroup の読み取り上限
constexpr qint64 kMaxCgroupBytes = 64 * 1024;
} // namespace

/**
 * @brief 再起動要求フラグの存在を検査する。
 * @param path 検査対象パス
 * @return 存在すれば true
 */
bool rebootFlagPresent(const QString &path)
{
    if (path.isEmpty())
        return false;
    return QFileInfo::exists(path);
}

/**
 * @brief cgroup 内容から systemd ユニット名を抽出する。
 * @param cgroup cgroup ファイルの内容
 * @return ユニット名 (判定不能なら空)
 */
QString serviceUnitFromCgroup(const QByteArray &cgroup)
{
    const QList<QByteArray> lines = cgroup.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        // "0::/system.slice/sshd.service" 形式。最後の ':' 以降がパス。
        const int sep = line.lastIndexOf(':');
        const QByteArray path = (sep >= 0) ? line.mid(sep + 1) : line;
        const QList<QByteArray> components = path.split('/');
        for (const QByteArray &component : components) {
            if (component.endsWith(".service") || component.endsWith(".scope")) {
                const QString unit = QString::fromUtf8(component);
                if (unit == QStringLiteral("init.scope"))
                    continue;
                return unit;
            }
        }
    }
    return QString();
}

/**
 * @brief 削除済みファイルを使用中の systemd ユニットを列挙する。
 * @param procRoot /proc 相当のルート
 * @param maxProcesses 走査するプロセス数の上限
 * @param maxUnits 返すユニット数の上限
 * @return ソート済みユニット名一覧
 */
QStringList servicesUsingDeletedFiles(const QString &procRoot, int maxProcesses, int maxUnits)
{
    QStringList result;
    if (maxProcesses <= 0 || maxUnits <= 0)
        return result;

    QDir proc(procRoot);
    if (!proc.exists())
        return result;

    QSet<QString> units;
    const QStringList entries =
        proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    int scanned = 0;
    for (const QString &name : entries) {
        if (scanned >= maxProcesses || units.size() >= maxUnits)
            break;
        bool isPid = false;
        name.toUInt(&isPid);
        if (!isPid)
            continue;
        ++scanned;

        const QString procDir = proc.filePath(name);
        QFile maps(procDir + QStringLiteral("/maps"));
        if (!maps.open(QIODevice::ReadOnly))
            continue;
        const QByteArray mapsData = maps.read(kMaxMapsBytes);
        maps.close();
        if (!mapsData.contains(" (deleted)"))
            continue;

        QFile cgroup(procDir + QStringLiteral("/cgroup"));
        if (!cgroup.open(QIODevice::ReadOnly))
            continue;
        const QByteArray cgroupData = cgroup.read(kMaxCgroupBytes);
        cgroup.close();

        const QString unit = serviceUnitFromCgroup(cgroupData);
        if (!unit.isEmpty())
            units.insert(unit);
    }

    result = QStringList(units.begin(), units.end());
    result.sort();
    if (result.size() > maxUnits)
        result = result.mid(0, maxUnits);
    return result;
}

} // namespace qZypper
