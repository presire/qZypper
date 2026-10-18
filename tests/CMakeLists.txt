find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

foreach(test IN ITEMS CommitOutcome OperationState PromptRegistry Validation Locks History
                     SearchOptions TransactionSummary CommitInteraction SnapshotInfo RebootCheck)
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

add_executable(test-ZyppExecutor tst_ZyppExecutor.cpp ../src/backend/ZyppExecutor.cpp)
target_link_libraries(test-ZyppExecutor PRIVATE qzypper-zypp Qt6::Test Qt6::DBus)
add_test(NAME ZyppExecutor COMMAND test-ZyppExecutor)
set_tests_properties(ZyppExecutor PROPERTIES TIMEOUT 30)

# GUI モデルのロジックテスト (root / バックエンド不要)
add_executable(test-PackageListModel tst_PackageListModel.cpp
    ../src/gui/models/PackageListModel.cpp
    ../src/gui/models/PackageFilterProxyModel.cpp)
target_include_directories(test-PackageListModel PRIVATE ../src/gui/models)
target_link_libraries(test-PackageListModel PRIVATE Qt6::Test)
add_test(NAME PackageListModel COMMAND test-PackageListModel)
set_tests_properties(PackageListModel PROPERTIES TIMEOUT 30)

# ローカル RPM / .repo 解析 / GPG プロンプト (root 不要)
foreach(test IN ITEMS RepoFileParser LocalRpm KeyTrustPrompt)
    add_executable(test-${test} tst_${test}.cpp)
    target_link_libraries(test-${test} PRIVATE qzypper-zypp Qt6::Test)
    add_test(NAME ${test} COMMAND test-${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endforeach()
