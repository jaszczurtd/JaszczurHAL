include_guard(GLOBAL)

function(jh_sx126x_source_manifest OUT_SOURCES OUT_INCLUDES)
    set(_jh_sx126x_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/sx126x_driver")
    foreach(_jh_sx126x_required IN ITEMS
            LICENSE.txt
            src/sx126x.c
            src/sx126x.h
            src/sx126x_driver_version.c
            src/sx126x_driver_version.h
            src/sx126x_hal.h
            src/sx126x_status.h)
        if(NOT EXISTS "${_jh_sx126x_root}/${_jh_sx126x_required}")
            message(FATAL_ERROR
                "Pinned SX126x driver checkout is missing; run "
                "third_party/update_components.sh")
        endif()
    endforeach()

    set(${OUT_SOURCES}
        "${_jh_sx126x_root}/src/sx126x.c"
        "${_jh_sx126x_root}/src/sx126x_driver_version.c"
        PARENT_SCOPE)
    set(${OUT_INCLUDES} "${_jh_sx126x_root}/src" PARENT_SCOPE)
endfunction()
