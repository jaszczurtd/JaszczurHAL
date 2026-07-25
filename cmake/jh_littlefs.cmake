include_guard(GLOBAL)

function(jh_littlefs_source_manifest OUT_SOURCES OUT_INCLUDES)
    set(_jh_littlefs_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/littlefs")
    if(NOT EXISTS "${_jh_littlefs_root}/lfs.h" OR
       NOT EXISTS "${_jh_littlefs_root}/LICENSE.md")
        message(FATAL_ERROR
            "Pinned littlefs checkout is missing; run "
            "third_party/update_components.sh")
    endif()

    set(${OUT_SOURCES}
        "${_jh_littlefs_root}/lfs.c"
        "${_jh_littlefs_root}/lfs_util.c"
        PARENT_SCOPE)
    set(${OUT_INCLUDES} "${_jh_littlefs_root}" PARENT_SCOPE)
endfunction()
