include_guard(GLOBAL)

# Build trees inside this repository belong below a .build directory, so a
# stray build/, build_test/ or cmake-build-*/ is refused before anything is
# generated. Trees outside the repository (consumer projects) are not checked.

set(_JH_BUILD_GUARD_REPO "${CMAKE_CURRENT_LIST_DIR}/..")

# Set OUT_VAR to TRUE when BINARY_DIR is outside REPO_DIR or below a .build
# directory inside it.
function(jh_build_dir_is_managed REPO_DIR BINARY_DIR OUT_VAR)
    get_filename_component(_repo "${REPO_DIR}" REALPATH)
    get_filename_component(_binary "${BINARY_DIR}" REALPATH)
    file(RELATIVE_PATH _relative "${_repo}" "${_binary}")
    if(IS_ABSOLUTE "${_relative}" OR _relative STREQUAL ".."
       OR _relative MATCHES "^\\.\\./")
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    elseif("/${_relative}/" MATCHES "/\\.build/")
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    else()
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(jh_require_managed_build_dir)
    jh_build_dir_is_managed(
        "${_JH_BUILD_GUARD_REPO}" "${CMAKE_BINARY_DIR}" _jh_managed)
    if(NOT _jh_managed)
        get_filename_component(_repo "${_JH_BUILD_GUARD_REPO}" REALPATH)
        message(FATAL_ERROR
            "[JH-BUILD-DIR] ${CMAKE_BINARY_DIR} is inside JaszczurHAL but not "
            "below .build/. Configure into ${_repo}/.build/<name> instead, "
            "for example: cmake -S . -B .build/host, and delete the stray "
            "directory.")
    endif()
endfunction()
