include_guard(GLOBAL)

# jh_add_library_package(<library target> <HAL source dir> [ARCHIVE_NAME <name>])
#
# The headers of one static library build next to the archive
# (<build>/include/generated, refreshed on every configure) and its installation: public HAL headers, the
# archive, the generated headers, the link-signature reference and the
# configuration records. A device tree build adds jh_hardware.h, its resolved
# and final JSON and a relocatable jh_hardware_config.cmake. Files the other
# mode left there are removed, so a package never mixes two builds.
function(jh_add_library_package LIBRARY SOURCE_DIR)
    cmake_parse_arguments(PARSE_ARGV 2 _JH_LP "" "ARCHIVE_NAME" "")
    set(_jh_lp_headers
        "${JH_BOARD_GENERATED_DIR}/jh_board_config.h"
        "${JH_BOARD_GENERATED_DIR}/jh_link_contract.h")
    set(_jh_lp_share "${JH_BOARD_GENERATED_DIR}/jh_link_contract_reference.c")
    if(JH_DEVICE_TREE_BUILD)
        list(APPEND _jh_lp_headers "${JH_HARDWARE_HEADER}")
        list(APPEND _jh_lp_share "${JH_HARDWARE_RESOLVED_FILE}" "${JH_HARDWARE_CONFIG_FILE}")
    else()
        list(APPEND _jh_lp_share "${JH_BOARD_GENERATED_DIR}/jh_board_resolved.json")
    endif()

    # The headers are generated during configure, so they are copied then,
    # whether or not the archive needs rebuilding; only a header of the other
    # mode is removed.
    set(_jh_lp_copy "${CMAKE_BINARY_DIR}/include/generated")
    file(REMOVE "${_jh_lp_copy}/jh_board_registry.h")
    if(NOT JH_DEVICE_TREE_BUILD)
        file(REMOVE "${_jh_lp_copy}/jh_hardware.h")
    endif()
    foreach(_jh_lp_header IN LISTS _jh_lp_headers)
        get_filename_component(_jh_lp_name "${_jh_lp_header}" NAME)
        configure_file("${_jh_lp_header}" "${_jh_lp_copy}/${_jh_lp_name}" COPYONLY)
    endforeach()

    install(CODE [[
        foreach(_jh_lp_stale
                include/generated/jh_board_registry.h
                include/generated/jh_hardware.h
                share/JaszczurHAL/generated/jh_board_resolved.json
                share/JaszczurHAL/generated/jh_hardware_resolved.json
                share/JaszczurHAL/generated/jh_hardware_config.json
                share/JaszczurHAL/generated/jh_hardware_config.cmake)
            file(REMOVE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${_jh_lp_stale}")
        endforeach()
    ]])
    if(_JH_LP_ARCHIVE_NAME)
        install(FILES "$<TARGET_FILE:${LIBRARY}>" DESTINATION lib
            RENAME "${_JH_LP_ARCHIVE_NAME}")
    else()
        install(TARGETS ${LIBRARY} ARCHIVE DESTINATION lib)
    endif()
    install(DIRECTORY "${SOURCE_DIR}/" DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "impl/.mock" EXCLUDE)
    install(FILES ${_jh_lp_headers} DESTINATION include/generated)
    install(FILES ${_jh_lp_share} DESTINATION share/JaszczurHAL/generated)
    if(JH_DEVICE_TREE_BUILD)
        install(FILES "${JH_BOARD_GENERATED_DIR}/jh_hardware_package.cmake"
            DESTINATION share/JaszczurHAL/generated
            RENAME jh_hardware_config.cmake)
    endif()
endfunction()
