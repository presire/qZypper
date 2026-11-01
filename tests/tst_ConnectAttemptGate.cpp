/**
 * @file tst_ConnectAttemptGate.cpp
 * @brief ConnectAttemptGateの試行IDと完了判定を検証する
 *
 * 開始→完了、二重開始の拒否、無効化後の旧試行の拒否、再開始後の新旧試行の判定、
 * 二重無効化の安全性を確認する
 * バックエンドやD-Busには依存せず、root権限も不要
 *
 * 入力 : なし
 * 出力 : QTestの合否
 * 依存 : Qt Test、ConnectAttemptGate
 */
#include <QtTest>
#include "ConnectAttemptGate.h"

using namespace qZypper;

/**
 * @class ConnectAttemptGateTest
 * @brief ConnectAttemptGateの状態遷移を検証する
 */
class ConnectAttemptGateTest : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 有効な試行を同じIDで完了し、試行中の状態を解除できることを検証する */
    void finishesActiveAttempt();
    /** @brief 二重開始を拒否して元の試行を完了できることを検証する */
    void rejectsSecondBegin();
    /** @brief 無効化後の遅い完了が受理されないことを検証する */
    void rejectsFinishAfterInvalidate();
    /** @brief 再開始後の旧応答が新しい試行を終了させないことを検証する */
    void rejectsOldAttemptAfterRestart();
    /** @brief invalidate()を二度呼んでも状態が壊れないことを検証する */
    void allowsRepeatedInvalidate();
};

void ConnectAttemptGateTest::finishesActiveAttempt()
{
    ConnectAttemptGate gate;

    // 実行 : 試行を開始して同じIDで完了する
    const quint64 id = gate.begin();
    const bool finished = gate.finish(id);

    // 確認 : IDが発行され、完了できて試行中ではなくなる
    QVERIFY(id != 0);
    QVERIFY(finished);
    QVERIFY(!gate.active());
}

void ConnectAttemptGateTest::rejectsSecondBegin()
{
    ConnectAttemptGate gate;

    // 実行 : 試行中にもう一度開始する
    const quint64 first = gate.begin();
    const quint64 second = gate.begin();

    // 確認 : 2回目は0を返し、最初の試行は有効なまま
    QVERIFY(first != 0);
    QCOMPARE(second, quint64(0));
    QVERIFY(gate.active());
    QVERIFY(gate.finish(first));
}

void ConnectAttemptGateTest::rejectsFinishAfterInvalidate()
{
    ConnectAttemptGate gate;

    // 実行 : 開始後に無効化し、旧IDで完了を試みる
    const quint64 id = gate.begin();
    gate.invalidate();

    // 確認 : 旧試行は完了できず、試行中でもない
    QVERIFY(!gate.finish(id));
    QVERIFY(!gate.active());
}

void ConnectAttemptGateTest::rejectsOldAttemptAfterRestart()
{
    ConnectAttemptGate gate;

    // 実行 : 旧試行を無効化して新試行を開始し、両方のIDで完了を試みる
    const quint64 oldId = gate.begin();
    gate.invalidate();
    const quint64 newId = gate.begin();
    const bool oldFinished = gate.finish(oldId);
    const bool stillActive = gate.active();
    const bool newFinished = gate.finish(newId);

    // 確認 : 旧試行は拒否され、新試行だけが完了できる
    QVERIFY(oldId != 0);
    QVERIFY(newId != 0);
    QVERIFY(oldId != newId);
    QVERIFY(!oldFinished);
    QVERIFY(stillActive);
    QVERIFY(newFinished);
    QVERIFY(!gate.active());
}

void ConnectAttemptGateTest::allowsRepeatedInvalidate()
{
    ConnectAttemptGate gate;

    // 実行 : 試行中でない状態で二度無効化する
    gate.invalidate();
    gate.invalidate();

    // 確認 : 試行中にならず、その後の開始も通常どおり成立する
    QVERIFY(!gate.active());
    const quint64 id = gate.begin();
    QVERIFY(id != 0);
    QVERIFY(gate.finish(id));
}

QTEST_GUILESS_MAIN(ConnectAttemptGateTest)

#include "tst_ConnectAttemptGate.moc"
