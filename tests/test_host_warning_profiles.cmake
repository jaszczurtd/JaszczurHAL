if(NOT JH_ROOT)
    get_filename_component(JH_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

include("${JH_ROOT}/cmake/jh_host_warnings.cmake")

jh_host_warning_options(_msvc MSVC ERRORS)
if(NOT "${_msvc}" STREQUAL "/W4;/permissive-;/WX")
    message(FATAL_ERROR "Unexpected MSVC warning profile: ${_msvc}")
endif()

jh_host_warning_options(_gnu GNU ERRORS)
if(NOT "${_gnu}" STREQUAL "-Wall;-Wextra;-Werror")
    message(FATAL_ERROR "Unexpected GNU warning profile: ${_gnu}")
endif()

jh_host_warning_options(_clang Clang)
if(NOT "${_clang}" STREQUAL "-Wall;-Wextra")
    message(FATAL_ERROR "Unexpected Clang warning profile: ${_clang}")
endif()

jh_host_warning_options(_unknown Unknown ERRORS)
if(_unknown)
    message(FATAL_ERROR "Unknown compiler received warning flags: ${_unknown}")
endif()
