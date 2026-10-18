#ifndef QZYPPER_BACKEND_POLKITAUTHORIZATION_H
#define QZYPPER_BACKEND_POLKITAUTHORIZATION_H

#include <QObject>
#include <QMap>
#include <functional>

namespace qZypper {

/** @brief 呼出元の一意バス名を用いた非同期Polkit認可。 */
class PolkitAuthorization : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void(bool)>;
    explicit PolkitAuthorization(QObject *parent = nullptr);
    QString request(const QString &caller, const QString &action, const Completion &completion);
    void cancel(const QString &id);
private:
    QMap<QString, bool> m_cancelled;
};

} // namespace qZypper
#endif // QZYPPER_BACKEND_POLKITAUTHORIZATION_H
