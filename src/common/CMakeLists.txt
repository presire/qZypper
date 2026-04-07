add_library(qzypper-common INTERFACE)

target_include_directories(qzypper-common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(qzypper-common INTERFACE
    Qt6::Core
)
