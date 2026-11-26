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

# A device tree build signs its configuration with the ABI of the SDK flags.
if(JH_DEVICE_TREE_BUILD)
    jh_finalize_hardware_abi()
endif()

set(JH_EXTRA_INCLUDES "" CACHE STRING
    "Extra include directories for native RP firmware")
set(JH_LINK_LIBRARIES "" CACHE STRING
    "Extra libraries for native RP firmware")
set(HAL_PROJECT_CONFIG_DIR "${JH_PROJECT_DIR}")
set(EXTRA_HAL_DEFINES ${JH_PROJECT_DEFINITIONS})
set(JH_RP_BOARD_DEFINES ${JH_BOARD_COMPILE_DEFINITIONS})

include("${JH_ROOT}/cmake/jh_rp_pico_sdk.cmake")

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
# HAL_RAM_CODE_SOURCES: project files whose code runs from SRAM.
include("${JH_ROOT}/cmake/jh_ram_code.cmake")
jh_ram_code_from_project(_jh_ram_code_sources
    BASE_DIR "${JH_PROJECT_DIR}" SOURCES ${_sources})
jh_add_rp_pico_firmware(firmware RAM_CODE_SOURCES ${_jh_ram_code_sources})
target_sources(JaszczurHAL PRIVATE
    "${JH_BOARD_GENERATED_DIR}/jh_link_contract_definition.c")
target_include_directories(JaszczurHAL PUBLIC "${JH_BOARD_GENERATED_DIR}")

set(_jh_out_dir "${JH_ARTIFACT_DIR}")
# Post-build steps run in this binary directory. On Windows they share one
# cmd.exe line of at most 8191 characters with the link, so name the outputs
# relative to it and copy them with one command.
set(_jh_outputs firmware.bin firmware.uf2 firmware.hex)
if(_jh_rp_ota)
    list(APPEND _jh_outputs firmware.ota)
endif()
add_custom_command(TARGET firmware POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_jh_out_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:firmware>" ${_jh_outputs} "${_jh_out_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            firmware.elf.map "${_jh_out_dir}/firmware.map"
    VERBATIM
)

add_custom_target(firmware_debug DEPENDS firmware)
add_custom_target(firmware_upload DEPENDS firmware)
add_custom_target(firmware_compile_db DEPENDS firmware)

message(STATUS
    "jh-firmware[${JH_TARGET}]: platform=${PICO_PLATFORM} "
    "board=${PICO_BOARD} sources=${JH_PROJECT_DIR}")
