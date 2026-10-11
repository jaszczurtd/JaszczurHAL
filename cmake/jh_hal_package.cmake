include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_hardware_config.cmake")

# jh_link_hal_package(<target> PACKAGE <install prefix> CONFIG_DIR <dir>
#     [ROOT <JaszczurHAL root>] [ASSEMBLY <key>] [VARIANT <id>]
#     [DEFINES <NAME or NAME=VALUE>...])
#
# Links an installed JaszczurHAL package built from a device tree to an
# application target. The application computes the configuration it expects
# from its own CONFIG_DIR with the same generator, before linking, and with
# the ABI its compiler reports for the flags known at configure time
# (CMAKE_C_FLAGS, CMAKE_C_FLAGS_<CONFIG> and the target's options; call this
# after setting them). A package with another configuration digest stops the
# configure with JH-HW-PACKAGE. The application then compiles with the
# package's frozen headers and its own link-signature reference, which checks
# the ABI of the real compile flags, generator expressions included, and
# references the expected signature; the package's jh_board_config.h checks
# the struct layout in every unit that includes HAL headers. A mismatch still
# stops the build before or at linking.
function(jh_link_hal_package TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 _JH_PK ""
        "PACKAGE;CONFIG_DIR;ROOT;ASSEMBLY;VARIANT" "DEFINES")
    if("${_JH_PK_ROOT}" STREQUAL "")
        get_filename_component(_JH_PK_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    endif()
    set(_jh_pk_record "${_JH_PK_PACKAGE}/share/JaszczurHAL/generated/jh_hardware_config.cmake")
    if(NOT EXISTS "${_jh_pk_record}")
        message(FATAL_ERROR
            "[JH-HW-PACKAGE] ${_JH_PK_PACKAGE} is not a device tree package: "
            "${_jh_pk_record} is missing")
    endif()

    jh_generate_hardware_config(
        ROOT "${_JH_PK_ROOT}"
        CONFIG_DIR "${_JH_PK_CONFIG_DIR}"
        ASSEMBLY "${_JH_PK_ASSEMBLY}"
        VARIANT "${_JH_PK_VARIANT}"
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/jh_hal_package/${TARGET}"
        OUTPUT_ROOT "${CMAKE_BINARY_DIR}"
        ABI_LATER
        DEFINES ${_JH_PK_DEFINES})
    jh_finalize_hardware_abi(TARGET ${TARGET})
    set(_jh_pk_expected_config "${JH_CONFIG_SHA256}")
    set(_jh_pk_expected_hardware "${JH_HARDWARE_SHA256}")
    set(_jh_pk_reference "${JH_BOARD_GENERATED_DIR}/jh_link_contract_reference.c")

    include("${_jh_pk_record}")
    if(NOT "${JH_CONFIG_SHA256}" STREQUAL "${_jh_pk_expected_config}")
        if(NOT "${JH_HARDWARE_SHA256}" STREQUAL "${_jh_pk_expected_hardware}")
            set(_jh_pk_what "hardware")
        else()
            set(_jh_pk_what "program configuration, flags or compiler ABI")
        endif()
        message(FATAL_ERROR
            "[JH-HW-PACKAGE] ${_JH_PK_PACKAGE} was built for another "
            "${_jh_pk_what}: package ${JH_CONFIG_SHA256}, "
            "${_JH_PK_CONFIG_DIR} expects ${_jh_pk_expected_config}")
    endif()

    target_sources(${TARGET} PRIVATE "${_jh_pk_reference}")
    target_include_directories(${TARGET} PRIVATE
        ${JH_HAL_PACKAGE_INCLUDE_DIRS} "${_JH_PK_CONFIG_DIR}")
    target_compile_definitions(${TARGET} PRIVATE
        ${JH_TARGET_SELECTOR}=1 "JH_HARDWARE_HEADER=\"${JH_HARDWARE_HEADER}\""
        ${JH_BOARD_COMPILE_DEFINITIONS} ${JH_PROJECT_DEFINITIONS})
    target_link_libraries(${TARGET} PRIVATE "${JH_HAL_PACKAGE_LIBRARY}")
endfunction()
