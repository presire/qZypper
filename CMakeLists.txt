cmake_minimum_required(VERSION 3.21)

project(qZypper VERSION 0.1.0 LANGUAGES CXX)

# Default install prefix to /usr (standard system location for packaged software)
# NOTE: Must be AFTER project() — CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT
# is only set by the project() command.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt6
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Quick
    QuickControls2
    DBus
    LinguistTools
    Svg
)

qt_policy(SET QTP0001 NEW)

# QTP0004: SUSE 16+でのみ有効化
if(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" _os_version_id REGEX "^VERSION_ID=")
    if(_os_version_id MATCHES "VERSION_ID=\"?([0-9]+)")
        set(_suse_major "${CMAKE_MATCH_1}")
        if(_suse_major GREATER_EQUAL 16)
            qt_policy(SET QTP0004 NEW)
        endif()
    endif()
endif()

# libzypp
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZYPP REQUIRED IMPORTED_TARGET libzypp)


# Polkit-Qt6
find_package(PolkitQt6-1 REQUIRED)

# サブディレクトリ
add_subdirectory(src/common)
add_subdirectory(src/backend)
add_subdirectory(src/gui)

# インストール設定
include(GNUInstallDirs)

# テンプレートからファイルを生成 (CMAKE_INSTALL_FULL_*変数を展開)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.presire.qzypper.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/dbus/org.presire.qzypper.service
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm-post-install.sh.in
    ${CMAKE_CURRENT_BINARY_DIR}/packaging/rpm-post-install.sh
    @ONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/desktop/org.presire.qzypper.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/desktop/org.presire.qzypper.desktop
    @ONLY
)

# SELinuxポリシーモジュール
option(ENABLE_SELINUX "Build and install SELinux policy module" ON)
if(ENABLE_SELINUX)
    add_subdirectory(selinux)
endif()

install(FILES dbus/org.presire.qzypper.conf
    DESTINATION /usr/share/dbus-1/system.d
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dbus/org.presire.qzypper.service
    DESTINATION /usr/share/dbus-1/system-services
)
install(FILES polkit/org.presire.qzypper.policy
    DESTINATION /usr/share/polkit-1/actions
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/desktop/org.presire.qzypper.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# SELinux: インストール後にポリシーロードとファイルラベル付与を実行
# cmake configure時にSELinuxツールが見つからなくても、install時に検出して実行する
set(_SELINUX_TE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/selinux/qzypper.te")
set(_SELINUX_FC_GEN "${CMAKE_CURRENT_BINARY_DIR}/selinux/qzypper.fc")
set(_SELINUX_PP_DEST "/usr/share/selinux/packages/qzypper.pp")
set(_SELINUX_BIN "${CMAKE_INSTALL_FULL_BINDIR}/qzypper")
set(_SELINUX_BACKEND "${CMAKE_INSTALL_FULL_LIBEXECDIR}/qzypper-backend")

install(CODE "
    # Check if running as root
    execute_process(COMMAND id -u OUTPUT_VARIABLE _UID OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _UID STREQUAL \"0\")
        return()
    endif()

    # Check if SELinux is enabled
    execute_process(COMMAND selinuxenabled RESULT_VARIABLE _SE OUTPUT_QUIET ERROR_QUIET)
    if(NOT _SE EQUAL 0)
        return()
    endif()

    # Build .pp from source if not already installed by selinux/ subdirectory
    if(NOT EXISTS \"${_SELINUX_PP_DEST}\")
        find_program(_CHECKMODULE checkmodule)
        find_program(_SEMODULE_PKG semodule_package)
        if(_CHECKMODULE AND _SEMODULE_PKG AND EXISTS \"${_SELINUX_TE_SRC}\")
            message(STATUS \"SELinux: Building policy module from source...\")
            execute_process(
                COMMAND \${_CHECKMODULE} -M -m
                        -o /tmp/qzypper_selinux.mod \"${_SELINUX_TE_SRC}\"
                RESULT_VARIABLE _R ERROR_VARIABLE _ERR)
            if(_R EQUAL 0)
                if(EXISTS \"${_SELINUX_FC_GEN}\")
                    execute_process(
                        COMMAND \${_SEMODULE_PKG}
                                -o \"${_SELINUX_PP_DEST}\"
                                -m /tmp/qzypper_selinux.mod
                                -f \"${_SELINUX_FC_GEN}\"
                        RESULT_VARIABLE _R ERROR_VARIABLE _ERR)
                else()
                    execute_process(
                        COMMAND \${_SEMODULE_PKG}
                                -o \"${_SELINUX_PP_DEST}\"
                                -m /tmp/qzypper_selinux.mod
                        RESULT_VARIABLE _R ERROR_VARIABLE _ERR)
                endif()
                if(_R EQUAL 0)
                    message(STATUS \"SELinux: Policy module built successfully\")
                else()
                    message(WARNING \"SELinux: semodule_package failed: \${_ERR}\")
                endif()
            else()
                message(WARNING \"SELinux: checkmodule failed: \${_ERR}\")
            endif()
            file(REMOVE /tmp/qzypper_selinux.mod)
        endif()
    endif()

    # Load policy module
    if(EXISTS \"${_SELINUX_PP_DEST}\")
        find_program(_SEMODULE semodule)
        if(_SEMODULE)
            message(STATUS \"SELinux: Loading policy module...\")
            execute_process(
                COMMAND \${_SEMODULE} -i \"${_SELINUX_PP_DEST}\"
                RESULT_VARIABLE _R ERROR_VARIABLE _ERR)
            if(_R EQUAL 0)
                message(STATUS \"SELinux: Policy module loaded successfully\")
            else()
                message(WARNING \"SELinux: Failed to load policy: \${_ERR}\")
            endif()
        endif()
    endif()

    # Relabel binaries
    find_program(_RESTORECON restorecon)
    if(_RESTORECON)
        message(STATUS \"SELinux: Relabeling binaries...\")
        execute_process(COMMAND \${_RESTORECON} -v \"${_SELINUX_BIN}\"
                        OUTPUT_VARIABLE _O1 ERROR_QUIET)
        execute_process(COMMAND \${_RESTORECON} -v \"${_SELINUX_BACKEND}\"
                        OUTPUT_VARIABLE _O2 ERROR_QUIET)
        if(_O1)
            message(STATUS \"  \${_O1}\")
        endif()
        if(_O2)
            message(STATUS \"  \${_O2}\")
        endif()
        message(STATUS \"SELinux: Relabeling complete\")
    endif()
")

# CPack RPM パッケージ設定
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_NAME "qzypper")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "GUI package manager for openSUSE — successor to YaST Software/Repository Management")
set(CPACK_PACKAGE_DESCRIPTION
    "qZypper is a Qt 6 / QML based graphical package management tool for openSUSE Leap 16 and SLE 16. It integrates software management and repository management using libzypp, with D-Bus privilege separation and Polkit authentication.")
set(CPACK_PACKAGE_VENDOR "presire")
set(CPACK_PACKAGE_CONTACT "presire")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/presire/qZypper")

set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "System/Packages")
set(CPACK_RPM_PACKAGE_URL "https://github.com/presire/qZypper")
set(CPACK_RPM_PACKAGE_AUTOPROV ON)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

# 明示的な依存関係 (自動検出で漏れるランタイム依存)
set(CPACK_RPM_PACKAGE_REQUIRES "dbus-1, polkit")
set(CPACK_RPM_PACKAGE_SUGGESTS "selinux-policy")

# SELinux post-install / post-uninstall スクリプト
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/packaging/rpm-post-install.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm-post-uninstall.sh")

include(CPack)
