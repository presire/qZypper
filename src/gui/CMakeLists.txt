set(GUI_SOURCES
    main.cpp
    controllers/PackageController.cpp
    controllers/PackageControllerQueries.cpp
    controllers/PackageControllerTransactions.cpp
    controllers/PackageControllerRepos.cpp
    controllers/DBusClient.cpp
    controllers/RepoFilePreview.cpp
    controllers/SearchModeDetect.cpp
    models/PackageListModel.cpp
    models/PackageFilterProxyModel.cpp
)

set(GUI_HEADERS
    controllers/PackageController.h
    controllers/DBusClient.h
    controllers/RepoFilePreview.h
    controllers/SearchModeDetect.h
    models/PackageListModel.h
    models/PackageFilterProxyModel.h
)

qt_add_executable(qzypper ${GUI_SOURCES} ${GUI_HEADERS})

target_compile_definitions(qzypper PRIVATE
    APP_VERSION="${CMAKE_PROJECT_VERSION}"
)

# QMLシングルトン (pragma Singletonだけでは生成qmldirにsingleton行が出力されないため明示する)
# qt_add_qml_module()より前に設定する必要がある
set_source_files_properties(
    qml/Theme.qml
    qml/PackageStatusInfo.qml
    qml/components/Util.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

# QMLモジュール
# NO_IMPORT_SCAN: 共有ライブラリビルド (SUSE 16) での静的プラグインリンク警告を抑制
# QMLプラグインは実行時に動的ロードされるため静的リンク不要
qt_add_qml_module(qzypper
    URI org.presire.qzypper.gui
    VERSION 1.0
    NO_IMPORT_SCAN
    QML_FILES
        qml/Theme.qml
        qml/PackageStatusInfo.qml
        qml/Main.qml
        qml/dialogs/RepoManagerDrawerContent.qml
        qml/dialogs/AddRepoDialog.qml
        qml/dialogs/AddServiceDialog.qml
        qml/dialogs/AboutQtDialog.qml
        qml/dialogs/CommitConfirmDialog.qml
        qml/dialogs/KeyTrustDialog.qml
        qml/dialogs/CommitProgressDialog.qml
        qml/dialogs/AboutqZypperDialog.qml
        qml/dialogs/LocksDialog.qml
        qml/dialogs/HistoryDialog.qml
        qml/dialogs/SolverTestcaseDialog.qml
        qml/dialogs/DistUpgradeDialog.qml
        qml/components/RepoTable.qml
        qml/components/RepoPropertiesPanel.qml
        qml/components/UrlTypeSelector.qml
        qml/components/UrlExpansionPreview.qml
        qml/components/StatusIcon.qml
        qml/components/StatusFilterPanel.qml
        qml/components/Util.qml
        qml/components/NavDrawer.qml
        qml/components/InfoToast.qml
        qml/components/RefreshOverlay.qml
        qml/components/PackageTable.qml
        qml/components/PackageDetailsPane.qml
        qml/views/SearchView.qml
        qml/views/SummaryView.qml
        qml/views/RepositoryView.qml
        qml/views/PatternView.qml
        qml/views/PatchView.qml
        qml/views/UpdatesView.qml
        qml/views/RpmGroupsView.qml
        qml/views/LanguagesView.qml
        qml/views/ProductView.qml
        qml/dialogs/InstallationResultDialog.qml
        qml/dialogs/ConflictDialog.qml
        qml/dialogs/UpdateInfoDialog.qml
        qml/dialogs/StartupDialog.qml
        qml/dialogs/PromptDialog.qml
        qml/dialogs/PackageLockDialog.qml
        qml/dialogs/SolverWaitDialog.qml
        qml/dialogs/PackageLoadWaitDialog.qml
        qml/dialogs/SessionEndedDialog.qml
    RESOURCES
        icons/Qt.png
        icons/qZypper@64.png
        icons/qZypper@128.png
        icons/qZypper@256.png
        icons/qZypper@512.png
        icons/qZypper@1024.png
        icons/status/no-inst.svg
        icons/status/install.svg
        icons/status/auto-install.svg
        icons/status/keep.svg
        icons/status/update.svg
        icons/status/auto-update.svg
        icons/status/delete.svg
        icons/status/auto-delete.svg
        icons/status/taboo.svg
        icons/status/protected.svg
        icons/menu/update-all.svg
        icons/menu/dist-upgrade.svg
        icons/menu/install-rpm.svg
        icons/menu/history.svg
)

# アプリケーションアイコン (QMLモジュールとは別に登録し、起動時から利用可能にする)
qt_add_resources(qzypper "app_icons"
    PREFIX "/icons"
    FILES
        icons/qZypper@64.png
        icons/qZypper@128.png
        icons/qZypper@256.png
        icons/qZypper@512.png
        icons/qZypper@1024.png
)

target_include_directories(qzypper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/models
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(qzypper PRIVATE
    qzypper-common
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Svg
)


# 翻訳ファイル
set(TS_FILES
    ${CMAKE_SOURCE_DIR}/translations/qZypper_ja.ts
)
qt_add_translations(qzypper
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/translations"
    LUPDATE_OPTIONS -no-obsolete
)

include(GNUInstallDirs)
install(TARGETS qzypper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# アプリケーションアイコン (hicolorテーマに各サイズをインストール)
foreach(ICON_SIZE 64 128 256 512 1024)
    install(FILES ${CMAKE_SOURCE_DIR}/src/gui/icons/qZypper@${ICON_SIZE}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps
        RENAME qZypper.png
    )
endforeach()
