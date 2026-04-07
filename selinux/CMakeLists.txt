# SELinux policy module build configuration for qZypper

########################################
# Tool detection
########################################

find_program(CHECKMODULE checkmodule)
find_program(SEMODULE_PACKAGE semodule_package)
find_program(SEMODULE semodule)
find_program(RESTORECON restorecon)

if(NOT CHECKMODULE OR NOT SEMODULE_PACKAGE)
    message(WARNING "SELinux policy tools not found (checkmodule, semodule_package).")
    message(WARNING "SELinux policy module will not be built.")
    message(WARNING "To install tools on openSUSE: zypper install selinux-policy-devel")
    return()
endif()

########################################
# Policy module configuration
########################################

set(POLICY_NAME qzypper)
set(POLICY_VERSION 1.0.0)

message(STATUS "Building SELinux policy module: ${POLICY_NAME} (version ${POLICY_VERSION})")

# Generate .fc from .fc.in template (substitutes @CMAKE_INSTALL_FULL_BINDIR@ etc.)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/${POLICY_NAME}.fc.in
    ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.fc
    @ONLY
)

########################################
# Build pipeline: .te -> .mod -> .pp
########################################

# Step 1: Compile .te (Type Enforcement) to .mod (module object)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.mod
    COMMAND ${CHECKMODULE} -M -m
            -o ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.mod
            ${CMAKE_CURRENT_SOURCE_DIR}/${POLICY_NAME}.te
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/${POLICY_NAME}.te
    COMMENT "Compiling SELinux Type Enforcement policy: ${POLICY_NAME}.te -> ${POLICY_NAME}.mod"
    VERBATIM
)

# Step 2: Package .mod + .fc (File Contexts) + .if (Interface) into .pp (policy package)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.pp
    COMMAND ${SEMODULE_PACKAGE}
            -o ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.pp
            -m ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.mod
            -f ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.fc
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.mod
        ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.fc
        ${CMAKE_CURRENT_SOURCE_DIR}/${POLICY_NAME}.if
    COMMENT "Packaging SELinux policy module: ${POLICY_NAME}.pp"
    VERBATIM
)

# Build target
add_custom_target(selinux-policy ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.pp
)

########################################
# Installation
########################################

# Install .pp to standard SELinux packages directory
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.pp
        DESTINATION /usr/share/selinux/packages
        COMPONENT selinux
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

# Install source files for reference (optional, useful for administrators)
install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/${POLICY_NAME}.te
        ${CMAKE_CURRENT_BINARY_DIR}/${POLICY_NAME}.fc
        ${CMAKE_CURRENT_SOURCE_DIR}/${POLICY_NAME}.if
        ${CMAKE_CURRENT_SOURCE_DIR}/${POLICY_NAME}.fc.in
        DESTINATION /usr/share/doc/${PROJECT_NAME}/selinux
        COMPONENT selinux
        OPTIONAL)

########################################
# Post-installation: Load module and relabel files
########################################

if(SEMODULE AND RESTORECON)
    install(CODE "
        set(POLICY_PP \"/usr/share/selinux/packages/${POLICY_NAME}.pp\")

        message(STATUS \"\")
        message(STATUS \"========================================\")
        message(STATUS \"SELinux Policy Module Installation\")
        message(STATUS \"========================================\")
        message(STATUS \"Module: ${POLICY_NAME}.pp\")
        message(STATUS \"Location: \${POLICY_PP}\")

        # Check if running as root (required for semodule)
        execute_process(
            COMMAND id -u
            OUTPUT_VARIABLE CURRENT_UID
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(NOT CURRENT_UID STREQUAL \"0\")
            message(WARNING \"\")
            message(WARNING \"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\")
            message(WARNING \"  NOT running as root (uid=\${CURRENT_UID})\")
            message(WARNING \"  semodule requires root privileges.\")
            message(WARNING \"  SELinux module will NOT be loaded.\")
            message(WARNING \"  Please run: sudo make install\")
            message(WARNING \"  Or manually: sudo semodule -i \${POLICY_PP}\")
            message(WARNING \"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\")
            message(WARNING \"\")
        else()
            # Load the policy module into the system
            message(STATUS \"Running: ${SEMODULE} -i \${POLICY_PP}\")

            execute_process(
                COMMAND ${SEMODULE} -i \${POLICY_PP}
                RESULT_VARIABLE SEMODULE_RESULT
                OUTPUT_VARIABLE SEMODULE_OUTPUT
                ERROR_VARIABLE SEMODULE_ERROR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_STRIP_TRAILING_WHITESPACE
            )

            if(NOT SEMODULE_RESULT EQUAL 0)
                message(WARNING \"\")
                message(WARNING \"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\")
                message(WARNING \"  FAILED to install SELinux module: ${POLICY_NAME}\")
                message(WARNING \"  Exit code: \${SEMODULE_RESULT}\")
                message(WARNING \"  Error: \${SEMODULE_ERROR}\")
                message(WARNING \"  Please run manually:\")
                message(WARNING \"    sudo semodule -i \${POLICY_PP}\")
                message(WARNING \"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\")
                message(WARNING \"\")
            else()
                message(STATUS \"SELinux module '${POLICY_NAME}' installed successfully\")

                # Relabel installed binaries
                message(STATUS \"Relabeling qZypper binaries...\")

                set(_RESTORECON_BIN \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/qzypper\")
                set(_RESTORECON_DBUS \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBEXECDIR}/qzypper-backend\")

                message(STATUS \"Running: ${RESTORECON} -R -v \${_RESTORECON_BIN}\")
                execute_process(
                    COMMAND ${RESTORECON} -R -v \${_RESTORECON_BIN}
                    OUTPUT_VARIABLE RESTORECON_OUTPUT1
                    ERROR_VARIABLE RESTORECON_ERROR1
                    RESULT_VARIABLE RESTORECON_RESULT1
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE
                )

                if(RESTORECON_RESULT1 EQUAL 0)
                    if(RESTORECON_OUTPUT1)
                        message(STATUS \"  \${RESTORECON_OUTPUT1}\")
                    else()
                        message(STATUS \"  \${_RESTORECON_BIN}: label already correct\")
                    endif()
                else()
                    message(WARNING \"  restorecon failed for \${_RESTORECON_BIN}: \${RESTORECON_ERROR1}\")
                endif()

                message(STATUS \"Running: ${RESTORECON} -R -v \${_RESTORECON_DBUS}\")
                execute_process(
                    COMMAND ${RESTORECON} -R -v \${_RESTORECON_DBUS}
                    OUTPUT_VARIABLE RESTORECON_OUTPUT2
                    ERROR_VARIABLE RESTORECON_ERROR2
                    RESULT_VARIABLE RESTORECON_RESULT2
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE
                )

                if(RESTORECON_RESULT2 EQUAL 0)
                    if(RESTORECON_OUTPUT2)
                        message(STATUS \"  \${RESTORECON_OUTPUT2}\")
                    else()
                        message(STATUS \"  \${_RESTORECON_DBUS}: label already correct\")
                    endif()
                else()
                    message(WARNING \"  restorecon failed for \${_RESTORECON_DBUS}: \${RESTORECON_ERROR2}\")
                endif()

                message(STATUS \"\")
                message(STATUS \"SELinux setup completed successfully\")
                message(STATUS \"To verify: semodule -l | grep ${POLICY_NAME}\")
                message(STATUS \"To check labels: ls -Z \${_RESTORECON_BIN} \${_RESTORECON_DBUS}\")
            endif()
        endif()
        message(STATUS \"========================================\")
        message(STATUS \"\")
    ")
else()
    message(STATUS "SELinux tools (semodule, restorecon) not found at build time.")
    message(STATUS "Policy module will be installed but not automatically loaded.")
    message(STATUS "After installation, manually run:")
    message(STATUS "  sudo semodule -i /usr/share/selinux/packages/${POLICY_NAME}.pp")
    message(STATUS "  sudo restorecon -R -v ${CMAKE_INSTALL_FULL_BINDIR}/qzypper ${CMAKE_INSTALL_FULL_LIBEXECDIR}/qzypper-backend")
endif()

########################################
# Uninstallation helper
########################################

# Note: CMake doesn't provide built-in uninstall target, but we can document the process
message(STATUS "To uninstall SELinux policy module later:")
message(STATUS "  sudo semodule -r ${POLICY_NAME}")
