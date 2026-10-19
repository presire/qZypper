#ifndef QZYPPER_BACKEND_POLKITAUTHORIZATION_H
#define QZYPPER_BACKEND_POLKITAUTHORIZATION_H

#include <QObject>
#include <QMap>
#include <QString>
#include <functional>

namespace qZypper {

/**
 * @brief 完了した認可要求が現在の要求と同じ場合だけ、保持中の識別子を消す
 *
 * 古い要求の完了通知が、後から始まった要求の識別子を消さないようにする
 *
 * @param current 保持中の認可要求の識別子
 * @param finished 完了した認可要求の識別子
 */
inline void clearAuthorizationId(QString &current, const QString &finished)
{
    if (!finished.isEmpty() && current == finished)
        current.clear();
}

/**
 * @class PolkitAuthorization
 * @brief 呼出元の一意バス名を用いた非同期Polkit認可
 */
class PolkitAuthorization : public QObject {

    Q_OBJECT

public:
    /** @brief 認可の完了通知、第1引数はrequest()が返した識別子 */
    using Completion = std::function<void(const QString &id, bool authorized)>; // 認可結果の非同期受信先

    /** @brief Polkit認可器を親オブジェクトへ関連付けて生成する */
    explicit PolkitAuthorization(QObject *parent = nullptr);

    /** @brief 呼出元とアクションに対する非同期認可を開始する */
    QString request(const QString &caller, const QString &action, const Completion &completion);

    /** @brief 指定した認可要求の結果を無効化する */
    void cancel(const QString &id);

private:
    QMap<QString, bool> m_cancelled;  // 完了通知を無視する取消済み要求
};

} // namespace qZypper
#endif // QZYPPER_BACKEND_POLKITAUTHORIZATION_H
