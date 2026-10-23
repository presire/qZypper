/**
 * @file RepoPresets.cpp
 * @brief コミュニティリポジトリのプリセット定義とURL変数の展開プレビューの実装
 *
 * PackageManagerAdaptor (AdaptorQueries.cpp) から呼び出される
 * ベース製品の取得とURL変数の展開にlibzyppを使用し、ネットワークには接続しない
 *
 * 入力: ベース製品の情報、利用者が入力した生のURL
 * 出力: プリセットのQVariantMap一覧、パスワードを伏せた展開後のURL
 * 依存: libzypp (ZYpp、Target、Product、Url、RepoVariablesUrlReplacer)、ZyppLogging
 */
#include "RepoPresets.h"

#include <zypp/Product.h>
#include <zypp/Url.h>
#include <zypp/ZYpp.h>
#include <zypp/ZYppFactory.h>
#include <zypp/Target.h>
#include <zypp/base/Exception.h>
#include <zypp/repo/RepoVariables.h>

#include "ZyppLogging.h"

// コミュニティリポジトリのプリセットと、URL変数の展開プレビューを扱う名前空間
namespace qZypper {
namespace {

/**
 * @brief 共通属性を持つプリセットを作る
 * @param id 識別子
 * @param name 表示名
 * @param alias 既定エイリアス
 * @param url 生のURL
 * @param priority 優先度
 * @param description 説明
 * @return プリセット (自動更新・有効)
 */
RepoPreset makePreset(const char *id, const char *name, const char *alias, const char *url,
                      int priority, const char *description)
{
    RepoPreset preset;
    preset.id = QString::fromLatin1(id);
    preset.name = QString::fromLatin1(name);
    preset.alias = QString::fromLatin1(alias);
    preset.url = QString::fromLatin1(url);
    preset.priority = priority;
    preset.autoRefresh = true;
    preset.enabled = true;
    preset.description = QString::fromLatin1(description);
    return preset;
}

// 優先度の方針 (libzyppの既定は99、小さいほど優先)
//  - Packman系はディストリビューションと同名のコーデック関連パッケージ (ffmpeg等) を置き換える目的で使うため90とする
//    ディストリビューション標準 (99) より優先されるが、Myrlynの80～85ほど他の追加リポジトリに勝つ設定にはしない
//  - VideoLAN、libdvdcss、Open H.264、nVidiaは、ディストリビューションに無いパッケージ、
//    またはバージョン比較で選ばれるべきパッケージ (ドライバ) を提供するため既定値の99とし、標準パッケージを勝手に置き換えない

// 以降の説明文はUIに表示するため、英語で記述する (翻訳の対象にはしていない)

// Packmanの説明文
const char kPackmanDescription[] =
    "Multimedia codecs and extra applications (e.g. a full-featured ffmpeg). Packages from this repository may replace distribution packages.";
// Packman Essentialsの説明文
const char kEssentialsDescription[] =
    "Only the multimedia libraries and codecs from Packman (a subset of the Packman repository).";
// VideoLANの説明文
const char kVideoLanDescription[] =
    "VLC-related packages and libdvdcss from the VideoLAN project.";
// LibDvdCssの説明文
const char kLibDvdCssDescription[] =
    "libdvdcss for playing encrypted DVDs (opensuse-guide.org).";
// Open H.264の説明文
const char kOpenH264Description[] =
    "Cisco's Open H.264 codec binaries for openSUSE.";
// nVidiaの説明文
const char kNvidiaDescription[] =
    "Proprietary NVIDIA graphics drivers (nVidia Corporation). Requires a supported NVIDIA GPU.";

/**
 * @brief 展開結果から資格情報のパスワードを伏せたURL文字列を作る
 * @param url 展開後のURL
 * @return パスワード部を"***"へ置き換えた文字列
 * @note urlは値渡しのため、呼び出し元のURLは変更されない
 *       プレビューが画面やログに残っても資格情報が漏れないようにするための処理
 */
std::string maskedUrlString(zypp::Url url)
{
    // パスワードを含むURLだけを伏せ、含まないURLはそのまま返す
    if (!url.getPassword().empty()) {
        url.setPassword("***");
    }
    return url.asString();
}

} // namespace

/**
 * @brief D-Bus転送用のa{sv}へ変換する
 * @return 識別子、名前、URL、優先度などを持つマップ (thirdPartyは常にtrue)
 */
QVariantMap RepoPreset::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), id);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("alias"), alias);
    map.insert(QStringLiteral("url"), url);
    map.insert(QStringLiteral("priority"), priority);
    map.insert(QStringLiteral("autoRefresh"), autoRefresh);
    map.insert(QStringLiteral("enabled"), enabled);
    map.insert(QStringLiteral("thirdParty"), true);
    map.insert(QStringLiteral("description"), description);
    return map;
}

/**
 * @brief ベース製品から対象ディストリビューションを判定する (MyrlynのCommunityRepos::findBestMatch相当)
 * @param name 製品名
 * @param summary 製品の要約
 * @param version 製品バージョン
 * @return 対象ディストリビューション
 */
PresetDistro detectPresetDistro(const QString &name, const QString &summary, const QString &version)
{
    // SlowrollはTumbleweedの要約を含むため、Tumbleweedの判定より先に対象外とする
    if (summary.contains(QLatin1String("Slowroll"), Qt::CaseInsensitive) ||
        name.contains(QLatin1String("Slowroll"), Qt::CaseInsensitive)) {
        return PresetDistro::None;
    }

    if (summary.contains(QLatin1String("Tumbleweed"), Qt::CaseInsensitive) ||
        name.contains(QLatin1String("Tumbleweed"), Qt::CaseInsensitive)) {
        return PresetDistro::Tumbleweed;
    }

    // SLE (SUSE Linux Enterprise) は名前に"Leap"を含まないが、念のため要約でも対象外とする
    if (summary.startsWith(QLatin1String("SUSE Linux Enterprise"), Qt::CaseInsensitive)) {
        return PresetDistro::None;
    }

    if (name.contains(QLatin1String("Leap"), Qt::CaseInsensitive) && version.startsWith(QLatin1String("16."))) {
        return PresetDistro::Leap16;
    }

    return PresetDistro::None;
}

/**
 * @brief ディストリビューションごとのプリセットを返す
 *
 * 2026年10月時点で各URLのrepodata/repomd.xmlがHTTP 200で取得できることを確認済み
 * Leap 16のlibdvdcss (opensuse-guide.org) はHTTP 404のため除外
 * Leap 16のOpen H.264はLeap 16の標準リポジトリ索引 (RIS) が提供するため除外
 * Open H.264 (Tumbleweed) はMyrlynのURLをhttpからhttpsへ変更した (https対応を確認済み)
 *
 * @param distro 対象ディストリビューション
 * @return プリセット一覧
 */
std::vector<RepoPreset> repoPresetsFor(PresetDistro distro)
{
    std::vector<RepoPreset> presets;

    // Leap 16のURLは$releasever (例: 16.0) を含む生のURLのまま返し、追加時にlibzyppが展開する
    switch (distro) {
    case PresetDistro::Tumbleweed:
        presets.push_back(makePreset("packman", "Packman", "packman-repo",
            "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/", 90, kPackmanDescription));
        presets.push_back(makePreset("packman-essentials", "Packman Essentials", "packman-essentials-repo",
            "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/Essentials/", 90, kEssentialsDescription));
        presets.push_back(makePreset("videolan", "VideoLAN", "video-lan-repo",
            "https://download.videolan.org/SuSE/Tumbleweed/", 99, kVideoLanDescription));
        presets.push_back(makePreset("libdvdcss", "LibDvdCss", "libdvdcss-repo",
            "https://opensuse-guide.org/repo/openSUSE_Tumbleweed/", 99, kLibDvdCssDescription));
        presets.push_back(makePreset("openh264", "Open H.264 Codec", "openh264-repo",
            "https://codecs.opensuse.org/openh264/openSUSE_Tumbleweed/", 99, kOpenH264Description));
        presets.push_back(makePreset("nvidia", "nVidia", "nvidia-repo",
            "https://download.nvidia.com/opensuse/tumbleweed/", 99, kNvidiaDescription));
        break;
    case PresetDistro::Leap16:
        presets.push_back(makePreset("packman", "Packman", "packman-repo",
            "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Leap_$releasever/", 90, kPackmanDescription));
        presets.push_back(makePreset("packman-essentials", "Packman Essentials", "packman-essentials-repo",
            "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Leap_$releasever/Essentials/", 90, kEssentialsDescription));
        presets.push_back(makePreset("videolan", "VideoLAN", "video-lan-repo",
            "https://download.videolan.org/SuSE/$releasever/", 99, kVideoLanDescription));
        presets.push_back(makePreset("nvidia", "nVidia", "nvidia-repo",
            "https://download.nvidia.com/opensuse/leap/$releasever/", 99, kNvidiaDescription));
        break;
    case PresetDistro::None:
        break;
    }

    return presets;
}

/**
 * @brief インストール済みベース製品からプリセット一覧を返す
 * @return a{sv}のリスト (ベース製品が不明または対象外のときは空)
 * @note libzyppの例外 (ターゲット未初期化など) はログに記録し、プリセットなしとして空リストを返す
 */
QVariantList detectRepoPresets()
{
    QVariantList list;

    try {
        // ZYpp::targetからインストール済みのベース製品を取得し、ディストリビューションを判定する
        const zypp::Product::constPtr product = zypp::getZYpp()->target()->baseProduct();

        // ベース製品が無い環境 (コンテナなど) ではプリセットを提示しない
        if (!product) {
            return list;
        }

        const PresetDistro distro = detectPresetDistro(QString::fromStdString(product->name()),
                                                       QString::fromStdString(product->summary()),
                                                       QString::fromStdString(product->edition().version()));

        for (const RepoPreset &preset : repoPresetsFor(distro)) {
            list.append(preset.toVariantMap());
        }
    }
    catch (const zypp::Exception &e) {
        // ベース製品を取得できなくてもアプリは動作できるため、警告のログだけ残してプリセットなしとする
        qCWarning(lcZypp) << "Failed to detect the base product:" << e.asUserString().c_str();
    }

    return list;
}

/**
 * @brief URL入力の長さと制御文字を検証する
 * @param rawUrl 検証するURL
 * @return 有効な場合はtrue
 * @note 過大な入力や制御文字を含む入力をlibzyppのURL解析へ渡さないための検証
 */
bool validRepoUrlInput(const QString &rawUrl)
{
    // 過大な入力による負荷を防ぐため、UTF-8換算の長さで制限する
    if (rawUrl.toUtf8().size() > kMaxRepoUrlBytes) {
        return false;
    }

    // 改行などの制御文字は、ログや表示の改ざんにつながるため拒否する
    for (const QChar character : rawUrl) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
            return false;
        }
    }

    return true;
}

/**
 * @brief リポジトリ変数を展開したURLをパスワードを伏せて返す (プレビュー用)
 * @param rawUrl 生のURL
 * @return 展開後のURL、変数が展開されない場合や解析できない場合は空文字列
 * @note ネットワークには接続せず、保存されるURLは変更しない (表示専用)
 */
QString expandRepoUrlPreview(const QString &rawUrl)
{
    // 不正な入力、空のURL、変数 ($) を含まないURLは展開しても変化しないため、空文字列を返す
    if (!validRepoUrlInput(rawUrl) || rawUrl.isEmpty() || !rawUrl.contains(QLatin1Char('$'))) {
        return {};
    }

    try {
        const zypp::Url original(rawUrl.toStdString());

        // libzyppの追加時と同じ規則 (RepoVariablesUrlReplacer) で変数を展開する
        const zypp::Url expanded = zypp::repo::RepoVariablesUrlReplacer()(original);

        // 展開しても変化しないURLはプレビューを出さないため、空文字列を返す
        if (expanded.asString() == original.asString()) {
            return {};
        }

        return QString::fromStdString(maskedUrlString(expanded));
    }
    catch (const zypp::Exception &) {
        // 解析できないURLはプレビュー対象外として空文字列を返す (追加時の検証はRepoPolicyが行う)
        return {};
    }
}

} // namespace qZypper
