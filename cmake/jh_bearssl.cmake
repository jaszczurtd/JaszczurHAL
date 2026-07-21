include_guard(GLOBAL)

function(jh_add_bearssl_source_library TARGET_NAME)
    set(_jh_bearssl_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/impl/shared/frameworks/BearSSL")
    set(_jh_bearssl_vendor "${_jh_bearssl_root}/vendor")
    if(NOT EXISTS "${_jh_bearssl_vendor}/inc/bearssl.h" OR
       NOT EXISTS "${_jh_bearssl_root}/LICENSE.txt")
        message(FATAL_ERROR "Pinned BearSSL source or license is missing")
    endif()

    file(GLOB_RECURSE _jh_bearssl_upstream_sources CONFIGURE_DEPENDS
        "${_jh_bearssl_vendor}/src/*.c.upstream")
    if(NOT _jh_bearssl_upstream_sources)
        message(FATAL_ERROR "Pinned BearSSL source set is empty")
    endif()

    set(_jh_bearssl_generated_sources)
    foreach(_jh_source IN LISTS _jh_bearssl_upstream_sources)
        file(RELATIVE_PATH _jh_relative "${_jh_bearssl_vendor}/src" "${_jh_source}")
        string(REGEX REPLACE "\\.upstream$" "" _jh_relative "${_jh_relative}")
        set(_jh_generated
            "${CMAKE_CURRENT_BINARY_DIR}/jh_bearssl/${TARGET_NAME}/${_jh_relative}")
        get_filename_component(_jh_generated_dir "${_jh_generated}" DIRECTORY)
        file(MAKE_DIRECTORY "${_jh_generated_dir}")
        configure_file("${_jh_source}" "${_jh_generated}" COPYONLY)
        list(APPEND _jh_bearssl_generated_sources "${_jh_generated}")
    endforeach()

    add_library(${TARGET_NAME} STATIC ${_jh_bearssl_generated_sources})
    target_include_directories(${TARGET_NAME} PUBLIC
        "${_jh_bearssl_vendor}/inc")
    target_include_directories(${TARGET_NAME} PRIVATE
        "${_jh_bearssl_vendor}/src")
    set_target_properties(${TARGET_NAME} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON)
    target_compile_options(${TARGET_NAME} PRIVATE
        -ffunction-sections
        -fdata-sections
        -w)
endfunction()
