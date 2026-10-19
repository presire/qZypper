#ifndef QZYPPER_BACKEND_REPOPOLICY_H
#define QZYPPER_BACKEND_REPOPOLICY_H

#include <set>
#include <string>
#include <zypp/RepoInfo.h>
#include <zypp/ServiceInfo.h>
#include <zypp-core/Pathname.h>

namespace qZypper {

    /**
    * @namespace qZypper::RepoPolicy
    * @brief リポジトリ・サービスの入力検証とセキュリティポリシー検査
    *
    * D-Bus経由の入力値 (URL・エイリアス・表示名・優先度) の検証と、
    * サービスやミラーリスト由来のリポジトリデータがURL/INI/GPG検証を迂回するのを防ぐ検査を提供する
    *
    * 全ての関数は状態を持たず、libzyppのプールやRepoManagerにはアクセスしない
    */
    namespace RepoPolicy {
        // 入力値の検証

        /**
        * @brief リポジトリ/サービスURLを検証する
        *
        * 空URL・制御文字を拒否し、zypp::Urlで構文解析する
        * スキームは許可リスト (http、https、ftp、tftp、file、dir、hd、iso、cd、dvd、nfs、nfs4、smb、cifs) のみ許可し、特にpluginを拒否する
        *
        * @param url 検証対象URL
        * @param err 失敗時のエラー詳細 (呼び出し側がエラーメッセージに利用する)
        * @param allowIsoNesting isoスキームのurlクエリパラメータの再帰検証を許可するか
        * @return 有効な場合はtrue
        */
        bool validateUrl(const std::string &url, std::string &err, bool allowIsoNesting = true);

        /**
        * @brief リポジトリ/サービスのエイリアスを検証する
        * @param alias 検証対象エイリアス
        * @param err 失敗時のエラー詳細
        * @return 有効な場合はtrue
        */
        bool validateAlias(const std::string &alias, std::string &err);

        /**
        * @brief リポジトリ/サービスの表示名を検証する
        * @param name 検証対象の表示名 (APIが空を許す箇所では空も有効)
        * @param err 失敗時のエラー詳細
        * @return 有効な場合はtrue
        */
        bool validateName(const std::string &name, std::string &err);

        /**
        * @brief リポジトリ優先度を検証する
        * @param priority 検証対象の優先度 (有効範囲は1～200)
        * @param err 失敗時のエラー詳細
        * @return 有効な場合はtrue
        */
        bool validatePriority(long long priority, std::string &err);

        // リポジトリのポリシー検査

        /**
        * @brief リポジトリポリシーを検査する
        *
        * サービスやミラーリスト由来のリポジトリデータがURL/INI/GPG検証を迂回するのを防ぐ
        * 制御文字・不正なURL・不審なミラーリスト/GPG鍵URLを拒否する
        * エラー文にはエイリアス・理由・スキームのみを含め、完全なURLは含めない
        *
        * @param repo 検査対象リポジトリ
        * @param err 失敗時のエラー詳細
        * @return ポリシーに準拠している場合はtrue
        */
        bool checkRepoPolicy(const zypp::RepoInfo &repo, std::string &err);

        /**
        * @brief リポジトリの生のGPG設定が弱体化されているか検査する
        *
        * getRawGpgChecksの三値 (g、r、p) のいずれかが確定的にfalseの場合にtrueを返す
        * (例 : gpgcheck=1 repo_gpgcheck=0 pkg_gpgcheck=0)
        *
        * 実効ブール値ではAllowUnsignedを検出できないため生値を見る
        *
        * @param repo 検査対象リポジトリ
        * @return 弱体化されている場合はtrue
        */
        bool rawGpgChecksWeakened(const zypp::RepoInfo &repo);

        /**
        * @brief ミラーリスト由来の全エンドポイントURLを検証する
        *
        * repoOrigins() (MirroredOriginSet) を走査し、validateUrlで検査する
        * ネットワークI/Oを伴う場合があるため、オフライン必須の箇所では呼ばない
        *
        * @param repo 検査対象リポジトリ
        * @param err 失敗時のエラー詳細 (エイリアスとスキームのみ)
        * @return 全てのエンドポイントが有効な場合はtrue
        */
        bool checkRepoOrigins(const zypp::RepoInfo &repo, std::string &err);

        // サービスのポリシー検査

        /**
        * @brief サービス情報を検査する
        *
        * リモートのrepoindex由来のエイリアス/名前に含まれる制御文字、不正なURL、PLUGIN型、repoStatesキーのINIメタ文字を拒否する
        *
        * @param svc 検査対象サービス
        * @param err 失敗時のエラー詳細
        * @return ポリシーに準拠している場合はtrue
        */
        bool checkServicePolicy(const zypp::ServiceInfo &svc, std::string &err);

        /**
        * @brief ディスク上の.serviceファイルがメモリ上のサービスと一致するか検査する
        *
        * 単一セクション・セクション名とエイリアスの一致・全てのurl/type値の一致を要求する
        * 改行注入によるtype=plugin行やurl=行の差し込みを検出する
        *
        * @param path .serviceファイルのパス
        * @param svc メモリ上のサービス
        * @return 一致する場合はtrue
        */
        bool serviceFileMatches(const zypp::Pathname &path, const zypp::ServiceInfo &svc);

        /**
        * @brief 既知リポジトリパス直下の通常ファイルを列挙する
        * @return フルパス文字列の集合
        */
        std::set<std::string> snapshotRepoFiles();
    } // namespace RepoPolicy
} // namespace qZypper

#endif // QZYPPER_BACKEND_REPOPOLICY_H
