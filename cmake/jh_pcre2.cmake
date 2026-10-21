include_guard(GLOBAL)

# Offline verification only. Preparation belongs to component_manager.py.
function(jh_add_pcre2)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/jh_jhbase.cmake")
    jh_jhbase_dir(JH_VERIFY_JHBASE_DIR)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/jh_verify_pcre2.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_root}/third_party/pcre2_version.conf")
    add_custom_target(jh_verify_pcre2
        COMMAND "${CMAKE_COMMAND}" "-DJH_VERIFY_JHBASE_DIR=${JH_VERIFY_JHBASE_DIR}"
            -P "${_root}/cmake/jh_verify_pcre2.cmake"
        COMMENT "Verifying the clean PCRE2 pin")
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(PCRE2_BUILD_PCRE2_8 ON)
    set(PCRE2_BUILD_PCRE2_16 OFF)
    set(PCRE2_BUILD_PCRE2_32 OFF)
    set(PCRE2_SUPPORT_UNICODE ON)
    set(PCRE2_SUPPORT_JIT OFF)
    set(PCRE2_BUILD_PCRE2GREP OFF)
    set(PCRE2_BUILD_TESTS OFF)
    set(PCRE2_SUPPORT_LIBBZ2 OFF)
    set(PCRE2_SUPPORT_LIBZ OFF)
    set(PCRE2_SUPPORT_LIBREADLINE OFF)
    set(PCRE2_SUPPORT_LIBEDIT OFF)
    set(PCRE2_REBUILD_CHARTABLES OFF)
    set(PCRE2_SHOW_REPORT OFF)
    add_subdirectory("${_source}" "${CMAKE_BINARY_DIR}/pcre2" EXCLUDE_FROM_ALL)
    add_dependencies(pcre2-8-static jh_verify_pcre2)
endfunction()
