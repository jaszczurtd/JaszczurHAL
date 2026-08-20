# JaszczurHAL native Pico SDK firmware recipe.
#
# The dispatcher imports Pico SDK before project() and provides:
# JH_RP_TARGET_DEFINE, PICO_PLATFORM, PICO_BOARD and the selected board profile.

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Generate compile_commands.json for VS Code" FORCE)

set(JH_PICOTOOL_EXECUTABLE "" CACHE FILEPATH
    "Verified picotool executable used by Pico SDK post-processing")
if(NOT JH_PICOTOOL_EXECUTABLE)
    set(_jh_managed_picotool_name "picotool")
    if(CMAKE_HOST_WIN32)
        set(_jh_managed_picotool_name "picotool.exe")
    endif()
    set(_jh_managed_picotool
        "${JH_ROOT}/.build/tools/picotool/${_jh_managed_picotool_name}")
    if(EXISTS "${_jh_managed_picotool}")
        set(JH_PICOTOOL_EXECUTABLE "${_jh_managed_picotool}"
            CACHE FILEPATH
            "Verified picotool executable used by Pico SDK post-processing"
            FORCE)
    endif()
endif()
if(JH_PICOTOOL_EXECUTABLE AND NOT TARGET picotool)
    if(NOT EXISTS "${JH_PICOTOOL_EXECUTABLE}")
        message(FATAL_ERROR
            "JH_PICOTOOL_EXECUTABLE does not exist: "
            "${JH_PICOTOOL_EXECUTABLE}")
    endif()
    add_executable(picotool IMPORTED GLOBAL)
    set_target_properties(picotool PROPERTIES
        IMPORTED_LOCATION "${JH_PICOTOOL_EXECUTABLE}")
endif()

set(JH_EXTRA_INCLUDES "" CACHE STRING
    "Extra include directories for native RP firmware")
set(JH_LINK_LIBRARIES "" CACHE STRING
    "Extra libraries for native RP firmware")
set(HAL_PROJECT_CONFIG_DIR "${JH_PROJECT_DIR}")
set(EXTRA_HAL_DEFINES ${JH_EXTRA_DEFINES})
set(JH_RP_BOARD_DEFINES ${JH_BOARD_COMPILE_DEFINITIONS})

include("${JH_ROOT}/cmake/jh_rp_native_sdk.cmake")

jh_resolve_project_sources(_sources)
add_executable(firmware ${_sources})
target_sources(firmware PRIVATE
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_reference.c")
target_include_directories(firmware PRIVATE
    "${JH_PROJECT_DIR}"
    "${JH_BOARD_GENERATED_DIR}"
    ${JH_EXTRA_INCLUDES}
)
if(JH_LINK_LIBRARIES)
    target_link_libraries(firmware PRIVATE ${JH_LINK_LIBRARIES})
endif()
jh_add_rp_native_firmware(firmware)
target_sources(JaszczurHAL PRIVATE
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_definition.c")
target_include_directories(JaszczurHAL PUBLIC "${JH_BOARD_GENERATED_DIR}")

set(_jh_out_dir "${JH_ARTIFACT_DIR}")
add_custom_command(TARGET firmware POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_jh_out_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:firmware>" "${_jh_out_dir}/firmware.elf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/firmware.bin"
            "${_jh_out_dir}/firmware.bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/firmware.uf2"
            "${_jh_out_dir}/firmware.uf2"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/firmware.hex"
            "${_jh_out_dir}/firmware.hex"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/firmware.elf.map"
            "${_jh_out_dir}/firmware.map"
    VERBATIM
)
if(_jh_native_ota)
    add_custom_command(TARGET firmware POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_BINARY_DIR}/firmware.ota"
                "${_jh_out_dir}/firmware.ota"
        VERBATIM)
endif()

add_custom_target(firmware_debug DEPENDS firmware)
add_custom_target(firmware_upload DEPENDS firmware)
add_custom_target(firmware_compile_db DEPENDS firmware)

message(STATUS
    "jh-firmware[${JH_TARGET}]: platform=${PICO_PLATFORM} "
    "board=${PICO_BOARD} sources=${JH_PROJECT_DIR}")
