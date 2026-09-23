include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_version_pins.cmake")

# Helpers binding the native RP build to the pinned Pico SDK and picotool.
#
# Native RP builds use exactly the Pico SDK and picotool named in
# third_party/pico_sdk_version.conf and third_party/picotool_version.conf.
# Neither tool reports a mismatch by itself: the SDK builds from whatever
# PICO_SDK_PATH holds, and an imported picotool target skips the SDK's
# version check. A checkout or executable left behind by a pin bump therefore
# fails the configure here instead of producing a different image.

# jh_rp_check_pico_sdk_version()
# Compare the imported SDK (PICO_SDK_VERSION_STRING) with the pinned version.
function(jh_rp_check_pico_sdk_version)
    jh_read_version_pin("${JH_ROOT}/third_party/pico_sdk_version.conf"
        PICO_SDK_VERSION _jh_pinned)
    if(NOT "${PICO_SDK_VERSION_STRING}" STREQUAL "${_jh_pinned}")
        message(FATAL_ERROR
            "Pico SDK ${PICO_SDK_VERSION_STRING} at ${PICO_SDK_PATH} does not "
            "match the pinned ${_jh_pinned}. Run "
            "${JH_ROOT}/scripts/ensure_pico_sdk.sh to update the checkout.")
    endif()
endfunction()

# jh_rp_remove_stale_flash_region()
# Pico SDK 2.2 and older wrote pico_flash_region.ld into the top build
# directory, which is also the linker's working directory. GNU ld searches the
# working directory for INCLUDE files before any -L path, so a copy left there
# by an older SDK shadows both the SDK's own region and jh_rp_set_flash_region()
# in a build directory reused across the SDK upgrade. SDK 2.3 writes the file
# below pico_standard_link instead.
function(jh_rp_remove_stale_flash_region)
    set(_jh_stale "${CMAKE_BINARY_DIR}/pico_flash_region.ld")
    if(EXISTS "${_jh_stale}")
        file(REMOVE "${_jh_stale}")
        message(STATUS "Removed ${_jh_stale} left by an older Pico SDK")
    endif()
endfunction()

# jh_rp_import_picotool()
# Import JH_PICOTOOL_EXECUTABLE (or the managed build) as the SDK's picotool
# target after checking that it reports the pinned version.
function(jh_rp_import_picotool)
    set(JH_PICOTOOL_EXECUTABLE "" CACHE FILEPATH
        "Verified picotool executable used by Pico SDK post-processing")
    set(_jh_picotool "${JH_PICOTOOL_EXECUTABLE}")
    if(NOT _jh_picotool)
        set(_jh_name "picotool")
        if(CMAKE_HOST_WIN32)
            set(_jh_name "picotool.exe")
        endif()
        set(_jh_managed "${JH_ROOT}/.build/tools/picotool/${_jh_name}")
        if(EXISTS "${_jh_managed}")
            set(_jh_picotool "${_jh_managed}")
            set(JH_PICOTOOL_EXECUTABLE "${_jh_managed}" CACHE FILEPATH
                "Verified picotool executable used by Pico SDK post-processing"
                FORCE)
        endif()
    endif()
    if(NOT _jh_picotool OR TARGET picotool)
        return()
    endif()
    if(NOT EXISTS "${_jh_picotool}")
        message(FATAL_ERROR
            "JH_PICOTOOL_EXECUTABLE does not exist: ${_jh_picotool}")
    endif()
    jh_read_version_pin("${JH_ROOT}/third_party/picotool_version.conf"
        PICOTOOL_VERSION _jh_pinned)
    execute_process(
        COMMAND "${_jh_picotool}" version
        RESULT_VARIABLE _jh_result
        OUTPUT_VARIABLE _jh_output
        ERROR_VARIABLE _jh_output)
    string(REGEX MATCH "picotool v([0-9]+\\.[0-9]+\\.[0-9]+)" _jh_match
        "${_jh_output}")
    if(NOT _jh_result EQUAL 0 OR NOT "${CMAKE_MATCH_1}" STREQUAL "${_jh_pinned}")
        string(STRIP "${_jh_output}" _jh_output)
        message(FATAL_ERROR
            "picotool at ${_jh_picotool} reports '${_jh_output}', but "
            "${_jh_pinned} is pinned. Run "
            "${JH_ROOT}/scripts/ensure_picotool.sh to rebuild it.")
    endif()
    add_executable(picotool IMPORTED GLOBAL)
    set_target_properties(picotool PROPERTIES
        IMPORTED_LOCATION "${_jh_picotool}")
endfunction()

# jh_rp_set_flash_region(TARGET OFFSET LENGTH)
# The SDK reads the FLASH memory region from pico_flash_region.ld
# (memory_flash.incl in pico_standard_link). A per-target copy on the linker
# script override path moves that region into the reserved slice and leaves
# every SDK script untouched.
function(jh_rp_set_flash_region TARGET_NAME OFFSET LENGTH)
    set(_jh_flash_memory
        "${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/script_include/memory_flash.incl")
    set(_jh_includes_region FALSE)
    if(EXISTS "${_jh_flash_memory}")
        file(STRINGS "${_jh_flash_memory}" _jh_region_include
            REGEX "INCLUDE[ \t]+\"pico_flash_region\\.ld\"")
        if(_jh_region_include)
            set(_jh_includes_region TRUE)
        endif()
    endif()
    if(NOT _jh_includes_region)
        message(FATAL_ERROR
            "Pico SDK ${PICO_SDK_VERSION_STRING} no longer takes the FLASH "
            "region from pico_flash_region.ld (${_jh_flash_memory}); the "
            "EEPROM/LittleFS/OTA flash reservation cannot be applied.")
    endif()
    set(_jh_region_dir "${CMAKE_CURRENT_BINARY_DIR}/jh_flash_region/${TARGET_NAME}")
    file(CONFIGURE OUTPUT "${_jh_region_dir}/pico_flash_region.ld"
        CONTENT "FLASH(rx) : ORIGIN = 0x10000000 + ${OFFSET}, LENGTH = ${LENGTH}\n")
    pico_add_linker_script_override_path("${TARGET_NAME}" "${_jh_region_dir}"
        FILES pico_flash_region.ld)
endfunction()

# jh_rp_check_flash_range(TARGET OFFSET LENGTH)
# Fail the build when the target's UF2 does not start at the region origin or
# leaves the region, i.e. when the region override did not reach the linker.
function(jh_rp_check_flash_range TARGET_NAME OFFSET LENGTH)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    math(EXPR _jh_start "0x10000000 + ${OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR _jh_end "0x10000000 + ${OFFSET} + ${LENGTH}" OUTPUT_FORMAT HEXADECIMAL)
    add_custom_command(TARGET "${TARGET_NAME}" POST_BUILD
        COMMAND "${Python3_EXECUTABLE}"
            "${JH_ROOT}/scripts/rp_ota_artifacts.py"
            check-range
            --uf2 "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.uf2"
            --start "${_jh_start}"
            --end "${_jh_end}"
        VERBATIM)
endfunction()
