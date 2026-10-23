#include <QDebug>
#include <QUrl>
#include <QThread>
#include <memory>
#include "PackageController.h"
#include "DBusClient.h"
#include "RepoFilePreview.h"

namespace qZypper {

// -- リポジトリ管理 --

/**
 * @brief リポジトリ一覧を読み込む (非同期)
 */
void PackageController::loadRepos()
{
    // 読み取り系は所有者だけに許可されるため、Initializeの完了前は呼ばない
    if (!m_connected)
        return;
    m_dbus->getRepos([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_repos = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit reposChanged();
        qDebug() << "Loaded" << m_repos.size() << "repositories";
    });
}

/**
 * @brief リポジトリを非同期で追加する
 *
 * 完了後、addRepoFinishedシグナルで成功可否を通知する
 *
 * @param url リポジトリURL
 * @param name リポジトリ名
 */
void PackageController::addRepo(const QString &url, const QString &name)
{
    if (!m_connected) {
        emit addRepoFinished(false);
        return;
    }
    if (m_busy) {
        emit addRepoFinished(false);
        return;
    }

    m_dbus->addRepo(url, name, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toMap().value(QStringLiteral("success")).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
        emit addRepoFinished(success);
    });
}

/**
 * @brief 全プロパティを指定してリポジトリを非同期で追加する
 *
 * 完了後、addRepoFinishedシグナルで成功可否を通知する
 *
 * @param properties リポジトリプロパティ
 */
void PackageController::addRepoFull(const QVariantMap &properties)
{
    if (!m_connected) {
        emit addRepoFinished(false);
        return;
    }
    if (m_busy) {
        emit addRepoFinished(false);
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Adding repository..."));

    m_dbus->addRepoFull(properties,
                        [this, properties](bool ok, const QString &, const QString &, const QVariantList &args) {
        const QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        const bool success = result.value(QStringLiteral("success")).toBool();

        if (success) {
            loadRepos();
            setStatusMessage(tr("Repository added: %1").arg(properties.value(QStringLiteral("name")).toString()));
        } else {
            setStatusMessage(tr("Failed to add repository"));
            emit errorOccurred(m_dbus->lastError());
        }

        setBusy(false);
        emit addRepoFinished(success);
    });
}

/**
 * @brief リポジトリURLの変数展開プレビューを要求する (非同期、読取専用)
 *
 * 失敗時や展開不要時はexpandedUrlが空のまま通知する (プレビューはエラー表示しない)
 * 結果は必ずイベントループ経由で通知するため、呼び出し側は戻り値の要求番号を保存してから応答を受け取れる
 * 要求番号は全呼び出しで一意なので、呼び出し側は最新の要求番号以外の応答を破棄できる
 *
 * @param rawUrl 展開前のURL
 * @return この要求の要求番号 (結果シグナルへそのまま返す)
 * @note 完了はexpandedRepoUrlReadyシグナルで通知する
 */
int PackageController::requestExpandedRepoUrl(const QString &rawUrl)
{
    const int requestId = ++m_expandedUrlRequest;

    // 未接続・空・変数を含まないURLは展開結果が変わらないため、バックエンドを呼ばずに空で通知する
    // シグナルを即時に発行すると呼び出し側が要求番号を保存する前に応答が届くため、QueuedConnectionで遅延させる
    if (!m_connected || rawUrl.isEmpty() || !rawUrl.contains(QLatin1Char('$'))) {
        QMetaObject::invokeMethod(this, [this, requestId, rawUrl]() {
            emit expandedRepoUrlReady(requestId, rawUrl, QString());
        }, Qt::QueuedConnection);
        return requestId;
    }

    // DBusClient::getExpandedRepoUrlを呼び出し、完了は非同期コールバックで受け取ってexpandedRepoUrlReadyで通知する
    // 失敗時はプレビューを出さないだけなので、エラー通知はせず空文字列で返す
    m_dbus->getExpandedRepoUrl(rawUrl, [this, requestId, rawUrl](bool ok, const QString &, const QString &,
                                                                 const QVariantList &args) {
        emit expandedRepoUrlReady(requestId, rawUrl,
                                  ok && !args.isEmpty() ? args.at(0).toString() : QString());
    });
    return requestId;
}

/**
 * @brief コミュニティリポジトリのプリセット一覧を読み込む (非同期)
 *
 * 未対応のディストリビューションや旧バックエンドでは空リストを通知する
 *
 * @note 完了はrepoPresetsReadyシグナルで通知する
 */
void PackageController::loadRepoPresets()
{
    // バックエンド未接続の間は呼び出せないため、プリセットなしとして即座に通知する
    if (!m_connected) {
        emit repoPresetsReady(QVariantList());
        return;
    }

    // DBusClient::getRepoPresetsを呼び出し、完了は非同期コールバックで受け取ってrepoPresetsReadyで通知する
    // 旧バックエンドなどでメソッドが無い場合も、エラー表示はせず空リストとして扱う
    m_dbus->getRepoPresets([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        emit repoPresetsReady(ok && !args.isEmpty() ? args.at(0).toList() : QVariantList());
    });
}

/**
 * @brief リポジトリを非同期で削除する
 * @param alias 削除対象のリポジトリエイリアス
 */
void PackageController::removeRepo(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->removeRepo(alias, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief リポジトリの有効/無効を非同期で切り替える
 * @param alias 対象のリポジトリエイリアス
 * @param enabled 有効にする場合はtrue
 */
void PackageController::setRepoEnabled(const QString &alias, bool enabled)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->setRepoEnabled(alias, enabled, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief リポジトリのプロパティを非同期で変更する
 * @param alias 対象のリポジトリエイリアス
 * @param properties 変更するプロパティ
 */
void PackageController::modifyRepo(const QString &alias, const QVariantMap &properties)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->modifyRepo(alias, properties, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief 全リポジトリの非同期リフレッシュを実行する
 *
 * 完了時にrefreshFinishedシグナルが発行される
 */
void PackageController::refreshReposAsync()
{
    if (!m_connected) return;
    if (m_busy) return;

    m_retryAction = [this]() { refreshReposAsync(); };
    setBusy(true);
    setStatusMessage(tr("Refreshing repositories..."));

    m_dbus->refreshReposAsync([this](bool ok, const QString &errorName, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        const bool cancelled = !ok && errorName == QLatin1String("org.presire.qzypper.Error.Cancelled");

        if (success) {
            setStatusMessage(tr("Repository refresh completed"));
            loadRepos();
        } else if (cancelled) {
            setStatusMessage(tr("Refresh cancelled"));
        } else {
            setStatusMessage(tr("Refresh failed"));
            emit errorOccurred(m_dbus->lastError());
        }
        setBusy(false);
        emit refreshFinished(success);
    });
}

/**
 * @brief 期限切れの場合のみリポジトリをリフレッシュする (非同期)
 *
 * 完了時にrefreshFinishedシグナルを発行する
 * 成功時はリポジトリ一覧を再読込する
 * 更新可能数の再取得は保留フラグに積み、解放時に1回だけ実行する
 */
void PackageController::refreshReposIfNeeded()
{
    if (!m_connected) {
        return;
    }
    if (m_busy) {
        return;
    }

    m_retryAction = [this]() { refreshReposIfNeeded(); };
    setBusy(true);
    setStatusMessage(tr("Refreshing repositories..."));

    m_dbus->refreshReposIfNeeded([this](bool ok, const QString &errorName, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        const bool cancelled = !ok && errorName == QLatin1String("org.presire.qzypper.Error.Cancelled");

        if (success) {
            setStatusMessage(tr("Repository refresh completed"));
            loadRepos();
        } else if (cancelled) {
            setStatusMessage(tr("Refresh cancelled"));
        } else {
            setStatusMessage(tr("Refresh failed"));
            emit errorOccurred(m_dbus->lastError());
        }
        // 解放時に保留分を1回だけ処理させるため、明示的な再取得は行わない
        m_updateCountPending = true;
        setBusy(false);
        emit refreshFinished(success);
    });
}

/**
 * @brief リフレッシュ操作をキャンセルする
 */
void PackageController::cancelRefresh()
{
    m_dbus->cancelOperation();
    setStatusMessage(tr("Cancelling... Please wait"));
}

/**
 * @brief 指定リポジトリを非同期でリフレッシュする
 * @param alias リフレッシュ対象のリポジトリエイリアス
 */
void PackageController::refreshSingleRepo(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Refreshing repository: %1").arg(alias));

    m_dbus->refreshSingleRepoAsync(alias, [this](bool ok, const QString &errorName, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        const bool cancelled = !ok && errorName == QLatin1String("org.presire.qzypper.Error.Cancelled");

        if (success) {
            setStatusMessage(tr("Repository refresh completed"));
            loadRepos();
        } else if (cancelled) {
            setStatusMessage(tr("Refresh cancelled"));
        } else {
            setStatusMessage(tr("Refresh failed"));
            emit errorOccurred(m_dbus->lastError());
        }
        setBusy(false);
        emit refreshFinished(success);
    });
}

// -- 署名鍵 --

/**
 * @brief GPG署名鍵の指紋を非同期で承認する (鍵の取り込みは操作の再試行時)
 *
 * 完了後、keyTrustFinishedシグナルで成功可否を通知する
 *
 * @param fingerprint 鍵フィンガープリント
 */
void PackageController::trustKey(const QString &fingerprint)
{
    if (!m_connected) {
        emit keyTrustFinished(false);
        return;
    }
    if (m_busy) {
        emit keyTrustFinished(false);
        return;
    }

    m_dbus->trustKey(fingerprint, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            setStatusMessage(tr("Signing key approved. It will be imported when you retry the operation."));
        else
            emit errorOccurred(m_dbus->lastError());
        emit keyTrustFinished(success);
    });
}

// -- サービス管理 --

/**
 * @brief サービス一覧を読み込む (非同期)
 */
void PackageController::loadServices()
{
    // 読み取り系は所有者だけに許可されるため、Initializeの完了前は呼ばない
    if (!m_connected)
        return;
    m_dbus->getServices([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_services = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit servicesChanged();
    });
}

/**
 * @brief サービスを非同期で追加する
 *
 * 完了後、addServiceFinishedシグナルで成功可否を通知する
 *
 * @param url サービスURL
 * @param alias サービスエイリアス
 */
void PackageController::addService(const QString &url, const QString &alias)
{
    if (!m_connected) {
        emit addServiceFinished(false);
        return;
    }
    if (m_busy) {
        emit addServiceFinished(false);
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Adding service..."));

    m_dbus->addService(url, alias, [this, alias](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();

        if (success) {
            loadServices();
            loadRepos();
            setStatusMessage(tr("Service added: %1").arg(alias));
        } else {
            setStatusMessage(tr("Failed to add service"));
            emit errorOccurred(m_dbus->lastError());
        }

        setBusy(false);
        emit addServiceFinished(success);
    });
}

/**
 * @brief サービスを非同期で削除する
 * @param alias 削除対象のサービスエイリアス
 */
void PackageController::removeService(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->removeService(alias, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success) {
            loadServices();
            loadRepos();
        } else {
            emit errorOccurred(m_dbus->lastError());
        }
    });
}

/**
 * @brief サービスのプロパティを非同期で変更する
 * @param alias 対象のサービスエイリアス
 * @param properties 変更するプロパティ
 */
void PackageController::modifyService(const QString &alias, const QVariantMap &properties)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->modifyService(alias, properties, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadServices();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief サービスを非同期でリフレッシュする
 * @param alias リフレッシュ対象のサービスエイリアス
 */
void PackageController::refreshService(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Refreshing service: %1").arg(alias));

    m_dbus->refreshServiceAsync(alias, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success) {
            loadServices();
            loadRepos();
            setStatusMessage(tr("Service refreshed"));
        } else {
            setStatusMessage(tr("Failed to refresh service"));
            emit errorOccurred(m_dbus->lastError());
        }
        setBusy(false);
        emit refreshFinished(success);
    });
}

// -- リポジトリファイルのインポート --

/**
 * @brief .repoファイルを読み込み、バックエンドで解析してプレビューを通知する
 *
 * ファイルはユーザー権限でUI側が読み込み、内容のみをバックエンドへ渡す (rootプロセスにユーザーが指定したパスを開かせないため)
 *
 * @param fileUrl 選択された.repoファイルのURL
 */
void PackageController::previewRepoFile(const QUrl &fileUrl)
{
    if (!m_connected) return;

    constexpr qint64 kMaxRepoFileSize = 64 * 1024;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const quint64 request = ++m_repoPreviewRequest;

    /**
     * @struct ReadOutcome
     * @brief 読み込みスレッドからGUIスレッドへ渡す読み込み結果
     */
    struct ReadOutcome {
        bool ok = false;        // 読み込みに成功したか
        QByteArray content;     // 読み込んだファイル内容
        QString error;          // 失敗時のエラーメッセージ
    };

    auto outcome = std::make_shared<ReadOutcome>();

    // 通常ファイルでも、FUSEやネットワーク上ではopen/readが長時間ブロックし得るため、読み込みは専用スレッドで行い、結果だけをUIスレッドへ戻す
    // ブロックしたままのスレッドは中断できないが、GUIと以降の要求は妨げない
    //
    // 読み込みスレッドはcontrollerに触れず、finishedのqueued接続で結果を渡す (controller破棄時は接続が切れるため、破棄後に呼ばれない)
    QThread *reader = QThread::create([path, outcome]() {
        outcome->ok = readRepoFileForPreview(path, kMaxRepoFileSize, outcome->content, outcome->error);
    });
    connect(reader, &QThread::finished, this, [this, request, outcome]() {
        onRepoFileRead(request, outcome->ok, outcome->content, outcome->error);
    });
    connect(reader, &QThread::finished, reader, &QObject::deleteLater);
    reader->start();
}

/**
 * @brief previewRepoFileの読み込み結果を受け取り、バックエンドで解析する (UIスレッド)
 * @param request 読み込み要求の番号 (最新の要求以外の結果は破棄する)
 * @param readOk 読み込み成否
 * @param contentBytes 読み込んだ内容
 * @param readError 失敗時の理由
 */
void PackageController::onRepoFileRead(quint64 request, bool readOk, const QByteArray &contentBytes,
                                       const QString &readError)
{
    if (request != m_repoPreviewRequest || !m_connected)
        return;
    if (!readOk) {
        emit repoFilePreviewReady(false, {}, QString(), readError);
        return;
    }
    const QString content = QString::fromUtf8(contentBytes);

    m_dbus->parseRepoFile(content, [this, request, content](bool ok, const QString &, const QString &message,
                                                            const QVariantList &args) {
        // 解析待ちの間に別のファイルが選ばれた場合や切断した場合は、古い結果を表示しない
        if (request != m_repoPreviewRequest || !m_connected)
            return;
        const QVariantList repos = (ok && !args.isEmpty()) ? args.at(0).toList() : QVariantList();
        if (!ok)
            emit repoFilePreviewReady(false, {}, content, message);
        else if (repos.isEmpty())
            emit repoFilePreviewReady(false, {}, content, tr("No valid repository definitions were found"));
        else
            emit repoFilePreviewReady(true, repos, content, QString());
    });
}

/**
 * @brief プレビュー済みの.repo内容を取り込み、リポジトリを追加する
 * @param content previewRepoFileで読み込んだ.repoの内容
 */
void PackageController::importRepoFile(const QString &content)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Importing repositories..."));

    m_dbus->importRepoFile(content, [this](bool ok, const QString &, const QString &message,
                                           const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!ok) {
            result[QStringLiteral("success")] = false;
            result[QStringLiteral("errorMessage")] = message;
        }
        setBusy(false);
        if (!result.value(QStringLiteral("added")).toList().isEmpty())
            loadRepos();
        setStatusMessage(result.value(QStringLiteral("success")).toBool()
                             ? tr("Repositories imported")
                             : tr("Repository import failed"));
        emit repoFileImported(result);
    });
}

} // namespace qZypper
