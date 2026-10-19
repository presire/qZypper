#ifndef QZYPPER_COMMON_PATTERNPATCHINFO_H
#define QZYPPER_COMMON_PATTERNPATCHINFO_H

#include <QString>
#include <QVariantMap>
#include <QMetaType>

namespace qZypper {

/**
 * @brief パターン情報
 */
struct PatternInfo {
    QString name;                                       // パターン名
    QString summary;                                    // 概要
    QString description;                                // 説明文
    QString category;                                   // カテゴリ
    QString icon;                                       // アイコンパス
    int status = 0;                                     // パッケージ状態
    int order = 0;                                      // 表示順序

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return パターン属性を持つマップ
     */
    QVariantMap toVariantMap() const {
        return {
            {"name",        name},
            {"summary",     summary},
            {"description", description},
            {"category",    category},
            {"icon",        icon},
            {"status",      status},
            {"order",       order}
        };
    }
};

/**
 * @brief パッチ情報
 */
struct PatchInfo {
    QString name;                                       // パッチ名
    QString summary;                                    // 概要
    QString category;                                   // カテゴリ (security, recommended等)
    QString severity;                                   // 深刻度
    QString version;                                    // バージョン
    int status = 0;                                     // パッケージ状態
    bool interactive = false;                           // 対話操作が必要か
    QString id;                                         // 安定識別子: "name-edition.arch@repo"
    bool isNeeded = false;                              // 適用が必要か
    bool isRelevant = false;                            // 対象システムに関連するか
    bool isSatisfied = false;                           // 既に満たされているか
    bool rebootSuggested = false;                       // 再起動推奨
    bool restartSuggested = false;                      // サービス再起動推奨
    bool selected = false;                              // 適用が選択されているか

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return パッチ属性を持つマップ
     */
    QVariantMap toVariantMap() const {
        return {
            {"name",             name},
            {"summary",          summary},
            {"category",         category},
            {"severity",         severity},
            {"version",          version},
            {"status",           status},
            {"interactive",      interactive},
            {"id",               id},
            {"isNeeded",         isNeeded},
            {"isRelevant",       isRelevant},
            {"isSatisfied",      isSatisfied},
            {"rebootSuggested",  rebootSuggested},
            {"restartSuggested", restartSuggested},
            {"selected",         selected}
        };
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::PatternInfo)
Q_DECLARE_METATYPE(qZypper::PatchInfo)

#endif // QZYPPER_COMMON_PATTERNPATCHINFO_H
