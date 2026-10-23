find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

foreach(test IN ITEMS CommitOutcome OperationState PromptRegistry Validation Locks History
                     SearchOptions TransactionSummary CommitInteraction SnapshotInfo RebootCheck
                     LicenseConfirmation PackageDetails RepoPresets)
    add_executable(test-${test} tst_${test}.cpp)
    target_link_libraries(test-${test} PRIVATE qzypper-zypp Qt6::Test)
    add_test(NAME ${test} COMMAND test-${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endforeach()

target_compile_definitions(test-History PRIVATE
    QZYPPER_HISTORY_SAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/data/zypp-history.sample")

add_executable(test-ZyppSmoke tst_ZyppSmoke.cpp ../src/backend/ZyppExecutor.cpp)
target_link_libraries(test-ZyppSmoke PRIVATE qzypper-zypp Qt6::Test Qt6::DBus)
add_test(NAME ZyppSmoke COMMAND test-ZyppSmoke)
set_tests_properties(ZyppSmoke PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP" TIMEOUT 60)

# 一時ルートでのZyppManager結合テスト (root / ネットワーク不要)
add_executable(test-ZyppTempRoot tst_ZyppTempRoot.cpp)
target_link_libraries(test-ZyppTempRoot PRIVATE qzypper-zypp Qt6::Test)
add_test(NAME ZyppTempRoot COMMAND test-ZyppTempRoot)
set_tests_properties(ZyppTempRoot PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP" TIMEOUT 120
    ENVIRONMENT "ZYPPTMPDIR=/tmp")

add_executable(test-ZyppExecutor tst_ZyppExecutor.cpp ../src/backend/ZyppExecutor.cpp)
target_link_libraries(test-ZyppExecutor PRIVATE qzypper-zypp Qt6::Test Qt6::DBus)
add_test(NAME ZyppExecutor COMMAND test-ZyppExecutor)
set_tests_properties(ZyppExecutor PROPERTIES TIMEOUT 30)

# GUIモデルのロジックテスト (root / バックエンド不要)
add_executable(test-PackageListModel tst_PackageListModel.cpp
    ../src/gui/models/PackageListModel.cpp
    ../src/gui/models/PackageFilterProxyModel.cpp)
target_include_directories(test-PackageListModel PRIVATE ../src/gui/models)
target_link_libraries(test-PackageListModel PRIVATE Qt6::Test)
add_test(NAME PackageListModel COMMAND test-PackageListModel)
set_tests_properties(PackageListModel PROPERTIES TIMEOUT 30)

# リポジトリファイルのプレビュー読み込み (通常ファイル判定 / サイズ上限、root不要)
add_executable(test-RepoFilePreview tst_RepoFilePreview.cpp
    ../src/gui/controllers/RepoFilePreview.cpp)
target_include_directories(test-RepoFilePreview PRIVATE ../src/gui/controllers)
target_link_libraries(test-RepoFilePreview PRIVATE Qt6::Test)
add_test(NAME RepoFilePreview COMMAND test-RepoFilePreview)
set_tests_properties(RepoFilePreview PROPERTIES TIMEOUT 30)

# ローカルRPM / .repo 解析 / GPGプロンプト (root不要)
foreach(test IN ITEMS RepoFileParser LocalRpm KeyTrustPrompt)
    add_executable(test-${test} tst_${test}.cpp)
    target_link_libraries(test-${test} PRIVATE qzypper-zypp Qt6::Test)
    add_test(NAME ${test} COMMAND test-${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endforeach()

# 一括操作とサブパッケージ選択 (一時ルート、root / ネットワーク不要)
add_executable(test-SelectionParity tst_SelectionParity.cpp)
target_link_libraries(test-SelectionParity PRIVATE qzypper-zypp Qt6::Test)
add_test(NAME SelectionParity COMMAND test-SelectionParity)
set_tests_properties(SelectionParity PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP" TIMEOUT 120
    ENVIRONMENT "ZYPPTMPDIR=/tmp")

# 複数版パッケージの版単位選択 (一時ルート、root / ネットワーク不要)
add_executable(test-MultiversionSelection tst_MultiversionSelection.cpp)
target_link_libraries(test-MultiversionSelection PRIVATE qzypper-zypp Qt6::Test)
add_test(NAME MultiversionSelection COMMAND test-MultiversionSelection)
set_tests_properties(MultiversionSelection PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP" TIMEOUT 120
    ENVIRONMENT "ZYPPTMPDIR=/tmp")

# ソルバー設定のシャットダウン時リセット (一時ルート、root / ネットワーク不要)
add_executable(test-SolverSettingsReset tst_SolverSettingsReset.cpp)
target_link_libraries(test-SolverSettingsReset PRIVATE qzypper-zypp Qt6::Test)
add_test(NAME SolverSettingsReset COMMAND test-SolverSettingsReset)
set_tests_properties(SolverSettingsReset PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP" TIMEOUT 120
    ENVIRONMENT "ZYPPTMPDIR=/tmp")

# 自動更新対象の必要時リフレッシュ (一時ルート、root / ネットワーク不要)
add_executable(test-RefreshIfNeeded tst_RefreshIfNeeded.cpp)
target_link_libraries(test-RefreshIfNeeded PRIVATE qzypper-zypp Qt6::Test)
add_test(NAME RefreshIfNeeded COMMAND test-RefreshIfNeeded)
set_tests_properties(RefreshIfNeeded PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP" TIMEOUT 120
    ENVIRONMENT "ZYPPTMPDIR=/tmp")

# 検索モードの自動判定 (純関数、root不要)
add_executable(test-SearchModeDetect tst_SearchModeDetect.cpp ../src/gui/controllers/SearchModeDetect.cpp)
target_include_directories(test-SearchModeDetect PRIVATE ../src/gui/controllers)
target_link_libraries(test-SearchModeDetect PRIVATE Qt6::Test)
add_test(NAME SearchModeDetect COMMAND test-SearchModeDetect)
set_tests_properties(SearchModeDetect PROPERTIES TIMEOUT 30)

# EULAを持つ一時プールでの承認・拒否 (root / ネットワーク不要)
pkg_check_modules(LIBSOLV REQUIRED IMPORTED_TARGET libsolv)
add_executable(test-LicenseSelection tst_LicenseSelection.cpp)
target_link_libraries(test-LicenseSelection PRIVATE qzypper-zypp Qt6::Test PkgConfig::LIBSOLV)
add_test(NAME LicenseSelection COMMAND test-LicenseSelection)
set_tests_properties(LicenseSelection PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP" TIMEOUT 120
    ENVIRONMENT "ZYPPTMPDIR=/tmp")

# 製品情報・パッチカテゴリ・プラグインサービスの信頼検査 (root不要)
foreach(test IN ITEMS ProductInfo PatchCategory PluginServiceTrust)
    add_executable(test-${test} tst_${test}.cpp)
    target_link_libraries(test-${test} PRIVATE qzypper-zypp Qt6::Test)
    add_test(NAME ${test} COMMAND test-${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endforeach()
