set(GUI_SOURCES
    main.cpp
    controllers/PackageController.cpp
    controllers/DBusClient.cpp
)

set(GUI_HEADERS
    controllers/PackageController.h
    controllers/DBusClient.h
)

qt_add_executable(qzypper ${GUI_SOURCES} ${GUI_HEADERS})

target_compile_definitions(qzypper PRIVATE
    APP_VERSION="${CMAKE_PROJECT_VERSION}"
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
        qml/Main.qml
        qml/dialogs/RepoManagerDrawerContent.qml
        qml/dialogs/AddRepoDialog.qml
        qml/dialogs/AddServiceDialog.qml
        qml/dialogs/AboutQtDialog.qml
        qml/dialogs/CommitConfirmDialog.qml
        qml/dialogs/CommitProgressDialog.qml
        qml/dialogs/AboutqZypperDialog.qml
        qml/components/RepoTable.qml
        qml/components/RepoPropertiesPanel.qml
        qml/components/UrlTypeSelector.qml
    RESOURCES
        icons/Qt.png
        icons/qZypper@64.png
        icons/qZypper@128.png
        icons/qZypper@256.png
        icons/qZypper@512.png
        icons/qZypper@1024.png
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

# Polkit-Qt6 認証 (GUI側)
target_link_libraries(qzypper PRIVATE PolkitQt6-1::Core)


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

# アプリケーションアイコン (hicolor テーマに各サイズをインストール)
foreach(ICON_SIZE 64 128 256 512 1024)
    install(FILES ${CMAKE_SOURCE_DIR}/src/gui/icons/qZypper@${ICON_SIZE}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps
        RENAME qZypper.png
    )
endforeach()
