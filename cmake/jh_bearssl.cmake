include_guard(GLOBAL)

function(jh_bearssl_source_manifest OUT_SOURCES OUT_INCLUDES)
    set(_jh_bearssl_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/BearSSL")
    if(NOT EXISTS "${_jh_bearssl_root}/inc/bearssl.h" OR
       NOT EXISTS "${_jh_bearssl_root}/LICENSE.txt")
        message(FATAL_ERROR
            "Pinned BearSSL checkout is missing; run "
            "third_party/update_components.sh")
    endif()

    # The checkout is pinned and verified before configuration. Avoid
    # CONFIGURE_DEPENDS: ESP-IDF also evaluates component requirements in
    # script mode, and a permanent VERIFY_GLOBS edge would make the
    # post-build `ninja -n` freshness contract report false pending work.
    file(GLOB_RECURSE _jh_bearssl_upstream_sources
        "${_jh_bearssl_root}/src/*.c")
    if(NOT _jh_bearssl_upstream_sources)
        message(FATAL_ERROR "Pinned BearSSL source set is empty")
    endif()

    set(${OUT_SOURCES} ${_jh_bearssl_upstream_sources} PARENT_SCOPE)
    set(${OUT_INCLUDES}
        "${_jh_bearssl_root}/inc"
        "${_jh_bearssl_root}/src"
        PARENT_SCOPE)
endfunction()

function(jh_add_bearssl_source_library TARGET_NAME)
    jh_bearssl_source_manifest(
        _jh_bearssl_upstream_sources
        _jh_bearssl_include_dirs)
    add_library(${TARGET_NAME} STATIC ${_jh_bearssl_upstream_sources})
    target_include_directories(${TARGET_NAME} PUBLIC
        ${_jh_bearssl_include_dirs})
    set_target_properties(${TARGET_NAME} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON)
    target_compile_options(${TARGET_NAME} PRIVATE
        -ffunction-sections
        -fdata-sections)
    if(JH_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        # BearSSL implements C polymorphism through prefix-compatible vtables
        # and uses native unaligned loads on architectures that support them.
        # Clang diagnoses both deliberate upstream optimizations as UB.
        target_compile_options(${TARGET_NAME} PRIVATE
            -fno-sanitize=function,alignment)
    endif()
endfunction()
