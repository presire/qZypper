/**
 * @file tst_PatchCategory.cpp
 * @brief パッチカテゴリ列挙値とD-Bus境界判定の単体テスト
 *
 * Document/YaST追加後も既存の列挙値と有効範囲が変わらないことを検証する
 *
 * 入力: なし
 * 出力: テスト結果 (標準出力)
 * 依存: Types.h、Qt Test
 */

#include <QtTest>
#include "Types.h"

using namespace qZypper;

/**
 * @class PatchCategoryTest
 * @brief パッチカテゴリ列挙値とD-Bus境界判定を検証する
 */
class PatchCategoryTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 既存の列挙値が変更されていないことを検証する */
    void keepsExistingValues()
    {
        QCOMPARE(static_cast<int>(PatchCategory::Security), 0);
        QCOMPARE(static_cast<int>(PatchCategory::Recommended), 1);
        QCOMPARE(static_cast<int>(PatchCategory::Optional), 2);
        QCOMPARE(static_cast<int>(PatchCategory::Feature), 3);
        QCOMPARE(static_cast<int>(PatchCategory::All), 4);
    }

    /** @brief 追加したDocument/YaSTの値を検証する */
    void addsDocumentAndYaST()
    {
        QCOMPARE(static_cast<int>(PatchCategory::Document), 5);
        QCOMPARE(static_cast<int>(PatchCategory::YaST), 6);
    }

    /** @brief 有効なカテゴリ指定 (-1と0～6) を受け付けることを検証する */
    void acceptsValidRange()
    {
        QVERIFY(isValidPatchCategory(-1));
        for (int value = 0; value <= 6; ++value) {
            QVERIFY(isValidPatchCategory(value));
        }
    }

    /** @brief 範囲外のカテゴリ指定を拒否することを検証する */
    void rejectsOutOfRange()
    {
        QVERIFY(!isValidPatchCategory(-2));
        QVERIFY(!isValidPatchCategory(7));
        QVERIFY(!isValidPatchCategory(100));
    }
};

QTEST_GUILESS_MAIN(PatchCategoryTest)
#include "tst_PatchCategory.moc"
