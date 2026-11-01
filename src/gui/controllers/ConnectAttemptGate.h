/**
 * @file ConnectAttemptGate.h
 * @brief バックエンド接続試行の世代管理を行うヘッダーオンリークラスの宣言
 *
 * 接続試行ごとに一意なIDを発行し、最新の試行だけが完了を確定できるようにする
 * D-Busには依存せず、試行の開始・無効化・完了判定だけを扱う
 *
 * DBusClientが接続開始時と切断時に利用する
 *
 * 入力 : 接続試行の開始・完了・無効化の要求
 * 出力 : 試行IDと完了可否
 * 依存 : Qt Core
 */
#ifndef QZYPPER_GUI_CONTROLLERS_CONNECTATTEMPTGATE_H
#define QZYPPER_GUI_CONTROLLERS_CONNECTATTEMPTGATE_H

#include <QtGlobal>

namespace qZypper {

/**
 * @class ConnectAttemptGate
 * @brief 接続試行の開始と完了を世代IDで対応付けるゲート
 *
 * begin()で試行IDを発行し、finish()へ同じIDを渡した時だけ完了を許可する
 * invalidate()で進行中の試行を破棄すると、その試行のfinish()はfalseを返す
 */
class ConnectAttemptGate
{
public:
    /**
     * @brief 新しい接続試行を開始する
     * @return 試行ID (既に試行中の場合は0)
     * @note 開始時に試行中の状態と現在のIDを更新する
     */
    quint64 begin()
    {
        // 同時に複数の接続試行を進めない
        if (m_active)
            return 0;
        m_active = true;
        ++m_nextId;
        m_currentId = m_nextId;
        return m_currentId;
    }

    /**
     * @brief 進行中の試行を無効化する (複数回呼んでも安全)
     * @note 試行中の状態と現在のIDを消去する
     */
    void invalidate()
    {
        m_active = false;
        m_currentId = 0;
    }

    /**
     * @brief 試行の完了を確定する
     * @param id begin()が返した試行ID
     * @return 最新の試行として完了できた場合はtrue
     * @note 完了できた場合は試行中の状態と現在のIDを消去する
     */
    bool finish(quint64 id)
    {
        // 無効化済みや古い試行の応答で現在の接続状態を変更しない
        if (!m_active || id == 0 || id != m_currentId)
            return false;
        m_active = false;
        m_currentId = 0;
        return true;
    }

    /**
     * @brief 試行中かどうかを返す
     * @return 試行中の場合はtrue
     */
    bool active() const { return m_active; }

private:
    bool m_active = false;        // 試行中か
    quint64 m_currentId = 0;      // 進行中の試行ID (0は非試行)
    quint64 m_nextId = 0;         // 最後に発行した試行ID
};

} // namespace qZypper

#endif // QZYPPER_GUI_CONTROLLERS_CONNECTATTEMPTGATE_H
