# ZyppManager静的ライブラリ (Qt D-Busの影響を受けない)
add_library(qzypper-zypp STATIC
    InputValidation.cpp
    ZyppManager.cpp
    ZyppManager.h
    ZyppCallbackReceiver.cpp
    ZyppCallbackReceiver.h
    CommitOutcome.cpp CommitOutcome.h
    CommitInteraction.cpp CommitInteraction.h
    SnapshotInfo.cpp SnapshotInfo.h
    RebootCheck.cpp RebootCheck.h
    OperationState.cpp OperationState.h
    PromptRegistry.cpp PromptRegistry.h
    ZyppSession.cpp
    ZyppLocks.cpp ZyppLocks.h
    ZyppHistory.cpp ZyppHistory.h
    ZyppClassification.cpp
    ZyppVerify.cpp
    SearchOptions.cpp SearchOptions.h
    TransactionSummary.cpp TransactionSummary.h
    ZyppSearch.cpp
    ZyppUpgrade.cpp
    ZyppPatches.cpp
    ZyppLocalRpm.cpp
    KeyTrustPrompt.cpp KeyTrustPrompt.h
    LocalRpm.cpp LocalRpm.h
    RepoFileParser.cpp RepoFileParser.h
)

target_include_directories(qzypper-zypp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(qzypper-zypp PUBLIC
    qzypper-common
    Qt6::Core
    PkgConfig::ZYPP
    Boost::thread
)

# D-Busバックエンドサービス
set(BACKEND_SOURCES
    main.cpp
    PackageManagerAdaptor.cpp
    AdaptorExecution.cpp
    AdaptorSession.cpp
    AdaptorQueries.cpp
    AdaptorSelection.cpp
    AdaptorRepositories.cpp
    AdaptorCommit.cpp
    AdaptorMaintenance.cpp
    AdaptorUpgrade.cpp
    AdaptorImport.cpp
    ZyppExecutor.cpp
    PolkitAuthorization.cpp
)

set(BACKEND_HEADERS
    PackageManagerAdaptor.h
    ZyppExecutor.h
    PolkitAuthorization.h
)

add_executable(qzypper-backend ${BACKEND_SOURCES} ${BACKEND_HEADERS})

target_compile_definitions(qzypper-backend PRIVATE
    APP_VERSION="${CMAKE_PROJECT_VERSION}"
)

target_link_libraries(qzypper-backend PRIVATE
    qzypper-zypp
    Qt6::Core
    Qt6::DBus
)

include(GNUInstallDirs)
install(TARGETS qzypper-backend
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
)
