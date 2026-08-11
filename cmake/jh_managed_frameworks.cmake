include_guard(GLOBAL)

function(jh_managed_framework_include_dirs OUT_INCLUDES)
    set(_jh_third_party_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party")
    set(_jh_cjson_root "${_jh_third_party_root}/cJSON")
    set(_jh_lodepng_root "${_jh_third_party_root}/lodepng")
    set(_jh_jpeg_root "${_jh_third_party_root}/TJpg_Decoder")
    set(_jh_fatfs_root "${_jh_third_party_root}/FatFs/source")
    set(_jh_unity_root "${_jh_third_party_root}/Unity/src")

    set(_jh_required_paths
        "${_jh_cjson_root}/cJSON.h"
        "${_jh_cjson_root}/cJSON_Utils.h"
        "${_jh_lodepng_root}/lodepng.h"
        "${_jh_jpeg_root}/src/tjpgd.h"
        "${_jh_jpeg_root}/src/tjpgdcnf.h"
        "${_jh_fatfs_root}/diskio.h"
        "${_jh_fatfs_root}/ff.c"
        "${_jh_fatfs_root}/ff.h"
        "${_jh_unity_root}/unity.c"
        "${_jh_unity_root}/unity.h"
        "${_jh_unity_root}/unity_internals.h")
    foreach(_jh_required_path IN LISTS _jh_required_paths)
        if(NOT EXISTS "${_jh_required_path}")
            message(FATAL_ERROR
                "Managed framework dependency is missing: "
                "${_jh_required_path}. Run third_party/update_components.sh")
        endif()
    endforeach()

    set(${OUT_INCLUDES}
        "${_jh_cjson_root}"
        "${_jh_lodepng_root}"
        "${_jh_jpeg_root}/src"
        "${_jh_fatfs_root}"
        "${_jh_unity_root}"
        PARENT_SCOPE)
endfunction()

function(jh_managed_framework_configure_sources)
    if(PICO_RISCV AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_property(
            SOURCE
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/codecs/lodepng/lodepng.cpp"
            APPEND PROPERTY COMPILE_OPTIONS -fno-inline)
    endif()
endfunction()
