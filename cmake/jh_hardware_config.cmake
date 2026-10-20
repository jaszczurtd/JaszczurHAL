include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_python.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/jh_project_config.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/jh_project_features.cmake")
# Empty keyword values, such as ASSEMBLY "" or VARIANT "", are empty strings;
# every reader below tests them with STREQUAL "".
if(POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()

# A configuration directory with device_tree.json builds from its assemblies
# through scripts/generate_hardware_config.py; without the file the build
# keeps the board path. Only the presence of the file selects the mode.

# Variables jh_generate_hardware_config() hands to its caller, beside the
# project configuration of jh_read_project_config().
set(JH_HARDWARE_CONFIG_VARIABLES
    JH_RESOLVED_TARGET JH_RESOLVED_BOARD JH_BOARD_PROVIDER JH_BOARD_RECIPE
    JH_BOARD_COMPONENTS JH_BOARD_COMPILE_DEFINITIONS
    JH_BOARD_EXPECTED_FLASH_BYTES JH_BOARD_REQUESTED_FEATURES
    JH_BOARD_RESOLVED_FEATURES JH_BOARD_RESOLVED_FEATURES_DIGEST
    JH_BOARD_FEATURES JH_BOARD_FEATURE_HASH JH_BOARD_CONTRACT_SYMBOL
    IDF_TARGET PICO_PLATFORM PICO_BOARD
    JH_HARDWARE_HEADER JH_HARDWARE_RESOLVED_FILE JH_HARDWARE_CONFIG_FILE
    JH_HARDWARE_SHA256 JH_CONFIG_SHA256 JH_ASSEMBLY JH_PROJECT_VARIANT
    JH_PROJECT_TUNABLES JH_HARDWARE_ABI JH_TARGET_SELECTOR)

# jh_device_tree_build(<out-var> <config dir>)
#
# TRUE when the directory holds device_tree.json. The build reconfigures when
# the file appears or disappears, so a stale mode never survives.
function(jh_device_tree_build OUT_VAR CONFIG_DIR)
    set(_jh_dt FALSE)
    if(NOT "${CONFIG_DIR}" STREQUAL "")
        jh_config_glob(_jh_dt_pattern "${CONFIG_DIR}" device_tree.json)
        file(GLOB _jh_dt_file CONFIGURE_DEPENDS "${_jh_dt_pattern}")
        if(_jh_dt_file)
            set(_jh_dt TRUE)
        endif()
    endif()
    set(${OUT_VAR} ${_jh_dt} PARENT_SCOPE)
endfunction()

function(_jh_hardware_generator)
    jh_find_python(_jh_hw_python)
    execute_process(
        COMMAND "${_jh_hw_python}" "${JH_HW_ROOT}/scripts/generate_hardware_config.py"
            ${ARGN}
        RESULT_VARIABLE _jh_hw_result
        ERROR_VARIABLE _jh_hw_error)
    if(NOT _jh_hw_result EQUAL 0)
        string(STRIP "${_jh_hw_error}" _jh_hw_error)
        message(FATAL_ERROR "Hardware configuration failed: ${_jh_hw_error}")
    endif()
endfunction()

# jh_generate_hardware_config(ROOT <JaszczurHAL root> CONFIG_DIR <dir>
#     OUTPUT_DIR <dir> [OUTPUT_ROOT <dir>] [TARGET <id>] [ASSEMBLY <key>]
#     [VARIANT <id>] [ALL_FEATURES] [ABI_LATER]
#     [DEFINES <NAME or NAME=VALUE>...])
#
# Resolves the assembly into jh_hardware.h, reads the project configuration
# with it exactly as the compiler does, then finalizes jh_board_config.h, the
# link signature and the CMake records in OUTPUT_DIR. TARGET only narrows the
# assembly choice. DEFINES are the -D values of a static library build;
# ALL_FEATURES adds every feature of the resolved target, which still needs
# its hardware in the assembly. ABI_LATER: the build calls
# jh_finalize_hardware_abi() once its compiler is known; until then the
# record comes from a finalize in OUTPUT_DIR/pre-project, so the files the
# build compiles are written only once, in their final form.
#
# Sets in the caller's scope what jh_read_project_config() sets,
# JH_REQUESTED_FEATURES, JH_RESOLVED_FEATURES, JH_BOARD_GENERATED_DIR,
# JH_HARDWARE_DEFINITION (the compile definition naming jh_hardware.h for the
# hook) and the record variables in JH_HARDWARE_CONFIG_VARIABLES.
function(jh_generate_hardware_config)
    cmake_parse_arguments(PARSE_ARGV 0 _JH_HC "ALL_FEATURES;ABI_LATER"
        "ROOT;CONFIG_DIR;OUTPUT_DIR;OUTPUT_ROOT;TARGET;ASSEMBLY;VARIANT" "DEFINES")
    foreach(_jh_hc_required ROOT CONFIG_DIR OUTPUT_DIR)
        if("${_JH_HC_${_jh_hc_required}}" STREQUAL "")
            message(FATAL_ERROR "jh_generate_hardware_config requires ${_jh_hc_required}")
        endif()
    endforeach()
    set(JH_HW_ROOT "${_JH_HC_ROOT}")
    set(_jh_hc_out "${_JH_HC_OUTPUT_DIR}")
    set(_jh_hc_root_args "")
    if(NOT "${_JH_HC_OUTPUT_ROOT}" STREQUAL "")
        set(_jh_hc_root_args --output-root "${_JH_HC_OUTPUT_ROOT}")
    endif()

    set(_jh_hc_resolve resolve --config-dir "${_JH_HC_CONFIG_DIR}")
    if(NOT "${_JH_HC_TARGET}" STREQUAL "")
        list(APPEND _jh_hc_resolve --target "${_JH_HC_TARGET}")
    endif()
    if(NOT "${_JH_HC_ASSEMBLY}" STREQUAL "")
        list(APPEND _jh_hc_resolve --assembly "${_JH_HC_ASSEMBLY}")
    endif()
    _jh_hardware_generator(${_jh_hc_resolve}
        --output-dir "${_jh_hc_out}" ${_jh_hc_root_args})
    file(READ "${_jh_hc_out}/jh_hardware_resolved.json" _jh_hc_model)
    string(JSON _jh_hc_target GET "${_jh_hc_model}" selection target)
    if(_JH_HC_ALL_FEATURES)
        jh_all_features_for_target(_jh_hc_all "${_jh_hc_target}" FEATURES_ONLY)
        list(APPEND _JH_HC_DEFINES ${_jh_hc_all})
    endif()
    jh_merge_library_defines(_JH_HC_DEFINES ${_JH_HC_DEFINES})

    jh_read_project_config(
        ROOT "${_JH_HC_ROOT}"
        CONFIG_DIR "${_JH_HC_CONFIG_DIR}"
        TARGET "${_jh_hc_target}"
        VARIANT "${_JH_HC_VARIANT}"
        HARDWARE_HEADER "${_jh_hc_out}/jh_hardware.h"
        OUTPUT_DIR "${_jh_hc_out}/project"
        DEFINES ${_JH_HC_DEFINES})
    jh_resolve_feature_defines(
        JH_REQUESTED_FEATURES JH_RESOLVED_FEATURES ${JH_PROJECT_FEATURES})

    set(_jh_hc_finalize finalize
        --hardware "${_jh_hc_out}/jh_hardware_resolved.json"
        --hardware-header "${_jh_hc_out}/jh_hardware.h"
        --config-dir "${_JH_HC_CONFIG_DIR}")
    if(NOT "${_JH_HC_VARIANT}" STREQUAL "")
        list(APPEND _jh_hc_finalize --variant "${_JH_HC_VARIANT}")
    endif()
    foreach(_jh_hc_define IN LISTS _JH_HC_DEFINES)
        list(APPEND _jh_hc_finalize "--define=${_jh_hc_define}")
    endforeach()
    set(JH_HARDWARE_FINALIZE_ARGUMENTS ${_jh_hc_finalize}
        --output-dir "${_jh_hc_out}" ${_jh_hc_root_args})
    set(JH_HARDWARE_ROOT "${_JH_HC_ROOT}")
    set(_jh_hc_record_dir "${_jh_hc_out}")
    if(_JH_HC_ABI_LATER)
        set(_jh_hc_record_dir "${_jh_hc_out}/pre-project")
    endif()
    _jh_hardware_generator(${_jh_hc_finalize}
        --output-dir "${_jh_hc_record_dir}" ${_jh_hc_root_args})
    include("${_jh_hc_record_dir}/jh_hardware_config.cmake")

    # The generator and CMake resolve the same features; any difference
    # would compile one set and sign another.
    set(_jh_hc_generated ${JH_BOARD_RESOLVED_FEATURES})
    set(_jh_hc_resolved ${JH_RESOLVED_FEATURES})
    list(SORT _jh_hc_generated)
    list(SORT _jh_hc_resolved)
    if(NOT "${_jh_hc_generated}" STREQUAL "${_jh_hc_resolved}")
        message(FATAL_ERROR
            "[JH-HW-FEATURE] the hardware generator resolved "
            "'${JH_BOARD_RESOLVED_FEATURES}', CMake '${JH_RESOLVED_FEATURES}'")
    endif()

    # Every file the generation read, and new registry files, reconfigure
    # the build; a removed device_tree.json does too.
    file(STRINGS "${_jh_hc_record_dir}/generation.d" _jh_hc_inputs)
    list(REMOVE_DUPLICATES _jh_hc_inputs)
    file(GLOB_RECURSE _jh_hc_new_inputs CONFIGURE_DEPENDS ${JH_BOARD_INPUT_GLOBS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${_jh_hc_inputs} ${_jh_hc_new_inputs})

    set(JH_BOARD_GENERATED_DIR "${_jh_hc_out}")
    set(JH_HARDWARE_DEFINITION "JH_HARDWARE_HEADER=\"${JH_HARDWARE_HEADER}\"")
    _jh_project_config_export(
        JH_REQUESTED_FEATURES JH_RESOLVED_FEATURES JH_BOARD_GENERATED_DIR
        JH_HARDWARE_DEFINITION JH_HARDWARE_FINALIZE_ARGUMENTS JH_HARDWARE_ROOT
        ${JH_HARDWARE_CONFIG_VARIABLES})
endfunction()

# jh_finalize_hardware_abi([TARGET <target>] [FLAGS <compile flags>...])
#
# Finalizes the configuration of jh_generate_hardware_config() again with the
# ABI the project's C compiler reports for the flags known at configure time:
# CMAKE_C_FLAGS, CMAKE_C_FLAGS_<CONFIG>, the directory's compile options, the
# TARGET's compile options and FLAGS. The ABI (float calling convention, FPU,
# architecture, type sizes) and the struct layout (packing, enum size) then
# join JH_CONFIG_SHA256 and the link signature. Generator expressions cannot
# be evaluated here; the generated link-signature sources check the ABI and
# jh_board_config.h the layout when they compile with the real flags, so any
# difference still stops the build before linking. Runs after project();
# refreshes the record variables in the caller's scope.
function(jh_finalize_hardware_abi)
    cmake_parse_arguments(PARSE_ARGV 0 _JH_FA "" "TARGET" "FLAGS")
    if(NOT JH_HARDWARE_FINALIZE_ARGUMENTS OR NOT CMAKE_C_COMPILER)
        message(FATAL_ERROR
            "jh_finalize_hardware_abi() needs jh_generate_hardware_config() and project()")
    endif()
    get_property(_jh_fa_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_jh_fa_multi)
        message(FATAL_ERROR
            "[JH-HW-GENERATION] a device tree build signs one configuration; use a "
            "single-configuration generator with CMAKE_BUILD_TYPE")
    endif()
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _jh_fa_config)
    separate_arguments(_jh_fa_flags UNIX_COMMAND
        "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${_jh_fa_config}}")
    get_directory_property(_jh_fa_options COMPILE_OPTIONS)
    if(_JH_FA_TARGET)
        # A target's options start with the directory's, as CMake copies them.
        get_target_property(_jh_fa_options ${_JH_FA_TARGET} COMPILE_OPTIONS)
        if(NOT _jh_fa_options)
            set(_jh_fa_options "")
        endif()
    endif()
    list(FILTER _jh_fa_options EXCLUDE REGEX "\\$<")
    set(_jh_fa_command ${JH_HARDWARE_FINALIZE_ARGUMENTS} "--compiler=${CMAKE_C_COMPILER}")
    if(CMAKE_C_COMPILER_TARGET)
        list(APPEND _jh_fa_command "--compiler-flag=--target=${CMAKE_C_COMPILER_TARGET}")
    endif()
    foreach(_jh_fa_flag IN LISTS _jh_fa_flags _jh_fa_options _JH_FA_FLAGS)
        list(APPEND _jh_fa_command "--compiler-flag=${_jh_fa_flag}")
    endforeach()
    set(JH_HW_ROOT "${JH_HARDWARE_ROOT}")
    _jh_hardware_generator(${_jh_fa_command})
    include("${JH_BOARD_GENERATED_DIR}/jh_hardware_config.cmake")
    foreach(_jh_fa_variable IN LISTS JH_HARDWARE_CONFIG_VARIABLES)
        set(${_jh_fa_variable} "${${_jh_fa_variable}}" PARENT_SCOPE)
    endforeach()
endfunction()

# jh_settle_build_mode(<device tree TRUE/FALSE> <config dir>
#     [BOARD_VARIABLE <cache variable>])
#
# Clears what the other mode left in the cache and refuses a selector the
# caller set for the wrong mode. A device tree selects its board through the
# assembly, so a board the board path resolved on an earlier configure is
# dropped and one the caller set is an error. Without the file an assembly a
# device tree build used is dropped and one the caller set is an error.
function(jh_settle_build_mode DEVICE_TREE CONFIG_DIR)
    cmake_parse_arguments(PARSE_ARGV 2 _JH_SM "" "BOARD_VARIABLE" "")
    set(_jh_sm_board "${_JH_SM_BOARD_VARIABLE}")
    if(DEVICE_TREE)
        if(NOT _jh_sm_board OR "$CACHE{${_jh_sm_board}}" STREQUAL "")
            return()
        endif()
        if("$CACHE{${_jh_sm_board}}" STREQUAL "$CACHE{_JH_BOARD_FROM_BUILD}")
            unset(${_jh_sm_board} CACHE)
            unset(${_jh_sm_board} PARENT_SCOPE)
            return()
        endif()
        message(FATAL_ERROR
            "[JH-HW-BOARD] ${CONFIG_DIR}/device_tree.json selects the board through "
            "its assemblies; remove ${_jh_sm_board}=$CACHE{${_jh_sm_board}} and choose "
            "JH_ASSEMBLY")
    endif()
    if("$CACHE{JH_ASSEMBLY}" STREQUAL "")
        return()
    endif()
    if(DEFINED CACHE{_JH_ASSEMBLY_FROM_DEVICE_TREE} AND
       "$CACHE{JH_ASSEMBLY}" STREQUAL "$CACHE{_JH_ASSEMBLY_FROM_DEVICE_TREE}")
        unset(JH_ASSEMBLY CACHE)
        unset(JH_ASSEMBLY PARENT_SCOPE)
        return()
    endif()
    message(FATAL_ERROR
        "[JH-HW-ASSEMBLY] JH_ASSEMBLY=$CACHE{JH_ASSEMBLY} needs "
        "${CONFIG_DIR}/device_tree.json")
endfunction()

# jh_remember_build_mode(<device tree TRUE/FALSE> [BOARD <resolved board>])
#
# Marks the selector this configure resolved for itself, so the next
# configure in the other mode can tell it from a caller's choice.
function(jh_remember_build_mode DEVICE_TREE)
    cmake_parse_arguments(PARSE_ARGV 1 _JH_RM "" "BOARD" "")
    if(DEVICE_TREE)
        set(_JH_ASSEMBLY_FROM_DEVICE_TREE "$CACHE{JH_ASSEMBLY}" CACHE INTERNAL
            "Assembly selector of the last device tree configure")
        unset(_JH_BOARD_FROM_BUILD CACHE)
    else()
        set(_JH_BOARD_FROM_BUILD "${_JH_RM_BOARD}" CACHE INTERNAL
            "Board the board path resolved on the last configure")
        unset(_JH_ASSEMBLY_FROM_DEVICE_TREE CACHE)
    endif()
endfunction()

# jh_configure_device_tree_library(<JaszczurHAL root>)
#
# A static library configured from HAL_PROJECT_CONFIG_DIR/device_tree.json,
# without application sources: JH_ASSEMBLY and JH_VARIANT select as in a
# firmware build, JH_TARGET only narrows, EXTRA_HAL_DEFINES and
# JH_ENABLE_ALL_FEATURES add requests. No runner preset is added. A macro, so
# the results land in the library project's scope.
macro(jh_configure_device_tree_library _jh_dtl_root)
    file(REMOVE_RECURSE
        "${CMAKE_BINARY_DIR}/generated/boards" "${CMAKE_BINARY_DIR}/generated/project")
    set(_jh_dtl_all "")
    if(JH_ENABLE_ALL_FEATURES)
        set(_jh_dtl_all ALL_FEATURES)
    endif()
    jh_generate_hardware_config(
        ROOT "${_jh_dtl_root}"
        CONFIG_DIR "${HAL_PROJECT_CONFIG_DIR}"
        TARGET "${JH_TARGET}"
        ASSEMBLY "${JH_ASSEMBLY}"
        VARIANT "${JH_VARIANT}"
        OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/hardware"
        OUTPUT_ROOT "${CMAKE_BINARY_DIR}"
        ${_jh_dtl_all}
        ABI_LATER
        DEFINES ${EXTRA_HAL_DEFINES})
    # The library compiles with the definitions the configuration was read
    # with: the -D requests and the variant's.
    set(EXTRA_HAL_DEFINES ${JH_PROJECT_DEFINITIONS})
    set(JH_TARGET "${JH_RESOLVED_TARGET}")
    set(JH_BOARD "${JH_RESOLVED_BOARD}")
    jh_remember_build_mode(TRUE)
endmacro()
