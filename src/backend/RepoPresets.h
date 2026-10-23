/**
 * @file RepoPresets.h
 * @brief コミュニティリポジトリのプリセットとリポジトリURL変数の展開プレビュー
 *
 * PackageManagerAdaptorの読取専用D-Busメソッド (GetRepoPresets、GetExpandedRepoUrl) から呼び出される
 * ベース製品からopenSUSE TumbleweedかopenSUSE Leap 16かを判定し、対応するサードパーティリポジトリの定義を返す
 * URLの変数 ($releasever等) の展開はlibzyppと同じ規則で行い、ネットワークには接続しない
 *
 * 入力: ベース製品の名前、要約、バージョン、利用者が入力した生のURL
 * 出力: プリセットのQVariantMap一覧、パスワードを伏せた展開後のURL
 * 依存: libzypp (Target、Product、RepoVariablesUrlReplacer)、Qt Core
 */
#ifndef QZYPPER_BACKEND_REPOPRESETS_H
#define QZYPPER_BACKEND_REPOPRESETS_H

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <string>
#include <vector>

// コミュニティリポジトリのプリセットと、URL変数の展開プレビューを扱う名前空間
namespace qZypper {

// リポジトリURL (D-Bus入力) の最大UTF-8バイト数
constexpr int kMaxRepoUrlBytes = 2048;

/**
 * @brief コミュニティリポジトリのプリセット対象ディストリビューション
 */
enum class PresetDistro {
    None,        // 対象外 (Slowroll、SLE、Leap 15などを含む)
    Tumbleweed,  // openSUSE Tumbleweed
    Leap16       // openSUSE Leap 16.x
};

/**
 * @brief コミュニティリポジトリ1件のプリセット
 *
 * 追加は既存のAddRepoFull経路で行うため、このデータ自体は権限を持たない
 */
struct RepoPreset {
    QString id;              // 安定した識別子 (UIの選択用)
    QString name;            // 表示名・リポジトリ名
    QString alias;           // 登録時の既定エイリアス
    QString url;             // 生のURL ($releasever等の変数を含む場合がある)
    int priority = 99;       // 優先度 (小さいほど優先)
    bool autoRefresh = true; // 自動更新するかどうか
    bool enabled = true;     // 追加時に有効にするかどうか
    QString description;     // UIに表示する説明

    /**
     * @brief D-Bus転送用のa{sv}へ変換する
     * @return 各項目を持つマップ (thirdPartyは常にtrue)
     */
    QVariantMap toVariantMap() const;
};

/**
 * @brief ベース製品の名前・要約・バージョンから対象ディストリビューションを判定する
 * @param name 製品名 (例: "Leap")
 * @param summary 製品の要約 (例: "openSUSE Tumbleweed")
 * @param version 製品バージョン (例: "16.0")
 * @return 対象ディストリビューション (Slowroll、SLE、Leap 15はNone)
 */
PresetDistro detectPresetDistro(const QString &name, const QString &summary, const QString &version);

/**
 * @brief ディストリビューションに対応するプリセット一覧を返す
 * @param distro 対象ディストリビューション
 * @return プリセット一覧 (Noneの場合は空)
 */
std::vector<RepoPreset> repoPresetsFor(PresetDistro distro);

/**
 * @brief インストール済みベース製品からプリセット一覧を作る (読取専用、ネットワーク不使用)
 * @return a{sv}のリスト、ベース製品が不明または対象外なら空
 * @note libzyppの例外はログに記録して空リストを返す
 */
QVariantList detectRepoPresets();

/**
 * @brief URL入力の長さと制御文字を検証する
 * @param rawUrl 検証するURL
 * @return 有効な場合はtrue
 */
bool validRepoUrlInput(const QString &rawUrl);

/**
 * @brief URL内のリポジトリ変数 ($releasever等) をlibzyppと同じ規則で展開する
 * @param rawUrl 生のURL
 * @return 資格情報のパスワードを伏せた展開後URL、変数が展開されない場合や解析できない場合は空文字列
 */
QString expandRepoUrlPreview(const QString &rawUrl);

} // namespace qZypper

#endif
