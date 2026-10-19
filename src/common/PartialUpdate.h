#ifndef QZYPPER_COMMON_PARTIALUPDATE_H
#define QZYPPER_COMMON_PARTIALUPDATE_H

#include <QString>
#include <QVariantMap>
#include <QMetaType>
#include <cmath>
#include <limits>
#include <optional>

namespace qZypper {

/**
 * @brief D-Bus部分更新の値を型検証して取り出すヘルパー
 */
namespace PartialUpdate {

/**
 * @brief キーが存在すればboolとして取り出す
 * @param map 入力マップ
 * @param key キー
 * @param out 取り出し先 (キーが無ければ変更しない)
 * @param err 型不一致時のエラー
 * @return 型が正しい (またはキー無し) 場合はtrue
 */
inline bool takeBool(const QVariantMap &map, const char *key,
                     std::optional<bool> &out, QString &err)
{
    const auto it = map.constFind(QLatin1String(key));
    if (it == map.constEnd())
        return true;
    if (it->typeId() != QMetaType::Bool) {
        err = QStringLiteral("Property '%1' must be a boolean").arg(QLatin1String(key));
        return false;
    }
    out = it->toBool();
    return true;
}

/**
 * @brief キーが存在すれば整数として取り出す (文字列・非整数・int範囲外の浮動小数は拒否)
 * @param map 入力マップ
 * @param key キー
 * @param out 取り出し先 (キーが無ければ変更しない)
 * @param err 型不一致時のエラー
 * @return 型が正しい (またはキー無し) 場合はtrue
 */
inline bool takeInt(const QVariantMap &map, const char *key,
                    std::optional<qlonglong> &out, QString &err)
{
    const auto it = map.constFind(QLatin1String(key));
    if (it == map.constEnd())
        return true;
    switch (it->typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::UChar:
        out = it->toLongLong();
        return true;
    case QMetaType::ULongLong:
        if (it->toULongLong() > static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()))
            break;
        out = it->toLongLong();
        return true;
    case QMetaType::Double: {
        // QMLの数値はdoubleで届くことがあるため、整数値で表せる範囲のみ受け付ける
        const double value = it->toDouble();
        if (!std::isfinite(value) || std::trunc(value) != value
            || value < static_cast<double>(std::numeric_limits<int>::min())
            || value > static_cast<double>(std::numeric_limits<int>::max()))
            break;
        out = static_cast<qlonglong>(value);
        return true;
    }
    default:
        break;
    }
    err = QStringLiteral("Property '%1' must be an integer").arg(QLatin1String(key));
    return false;
}

/**
 * @brief キーが存在すれば文字列として取り出す
 * @param map 入力マップ
 * @param key キー
 * @param out 取り出し先 (キーが無ければ変更しない)
 * @param err 型不一致時のエラー
 * @return 型が正しい (またはキー無し) 場合はtrue
 */
inline bool takeString(const QVariantMap &map, const char *key,
                       std::optional<QString> &out, QString &err)
{
    const auto it = map.constFind(QLatin1String(key));
    if (it == map.constEnd())
        return true;
    if (it->typeId() != QMetaType::QString) {
        err = QStringLiteral("Property '%1' must be a string").arg(QLatin1String(key));
        return false;
    }
    out = it->toString();
    return true;
}

} // namespace PartialUpdate

} // namespace qZypper

#endif // QZYPPER_COMMON_PARTIALUPDATE_H
