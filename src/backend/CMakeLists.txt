# ZyppManager静的ライブラリ (Qt D-Busの影響を受けない)
add_library(qzypper-zypp STATIC
    ZyppManager.cpp
    ZyppManager.h
    ZyppCallbackReceiver.cpp
    ZyppCallbackReceiver.h
)

target_include_directories(qzypper-zypp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(qzypper-zypp PUBLIC
    qzypper-common
    Qt6::Core
    PkgConfig::ZYPP
    boost_thread
)

# D-Busバックエンドサービス
set(BACKEND_SOURCES
    main.cpp
    PackageManagerAdaptor.cpp
)

set(BACKEND_HEADERS
    PackageManagerAdaptor.h
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
