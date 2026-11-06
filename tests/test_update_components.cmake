# third_party/update_components.sh refreshes security/sbom.cdx.json after a
# successful synchronization, rewriting it only when its content changes, and
# scripts/install_host_tools.sh, which every Linux CI job runs, keeps the
# committed SBOM. A fake interpreter stands in for the component manager and
# the SBOM generator; the paths contain spaces on purpose.
foreach(_required JH_ROOT WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

find_program(BASH bash REQUIRED)
find_program(TOUCH touch REQUIRED)

file(REMOVE_RECURSE "${WORK_DIR}")
set(repo "${WORK_DIR}/repo")
set(sbom "${repo}/security/sbom.cdx.json")
set(log "${WORK_DIR}/python.log")
set(fake_python "${WORK_DIR}/fake python")
set(manager "${repo}/scripts/component_manager.py all --repo-root ${repo}")
set(generator "${repo}/scripts/generate_sbom.py --output ")
set(old_mtime 1000000000)

file(COPY "${JH_ROOT}/third_party/update_components.sh"
    DESTINATION "${repo}/third_party")
file(MAKE_DIRECTORY "${repo}/security")

function(write_executable path text)
    file(WRITE "${path}" "${text}")
    file(CHMOD "${path}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endfunction()

# Logs its arguments. The component manager exits with FAKE_MANAGER_STATUS;
# the SBOM generator writes a fixed SBOM to --output or exits with
# FAKE_SBOM_STATUS.
write_executable("${fake_python}" [=[#!/bin/sh
printf '%s\n' "$*" >> "${FAKE_LOG}"
case "$1" in
    */scripts/component_manager.py) exit "${FAKE_MANAGER_STATUS}" ;;
    */scripts/generate_sbom.py)
        [ "${FAKE_SBOM_STATUS}" -eq 0 ] || exit "${FAKE_SBOM_STATUS}"
        [ "$2" = --output ] || exit 64
        printf 'generated\n' > "$3"
        echo "Wrote $3"
        ;;
    *) exit 65 ;;
esac
]=])

# Puts content into the SBOM (or removes it for MISSING) with an old mtime.
function(set_sbom content)
    if(content STREQUAL "MISSING")
        file(REMOVE "${sbom}")
        return()
    endif()
    file(WRITE "${sbom}" "${content}")
    execute_process(COMMAND "${TOUCH}" -d "@${old_mtime}" "${sbom}"
        RESULT_VARIABLE _touched)
    if(NOT _touched EQUAL 0)
        message(FATAL_ERROR "cannot set the SBOM mtime")
    endif()
endfunction()

# Runs the launcher; sets status, output and calls (one log line per call).
function(run_launcher)
    cmake_parse_arguments(RUN "" "MANAGER_STATUS;SBOM_STATUS" "ARGS" ${ARGN})
    if(NOT DEFINED RUN_MANAGER_STATUS)
        set(RUN_MANAGER_STATUS 0)
    endif()
    if(NOT DEFINED RUN_SBOM_STATUS)
        set(RUN_SBOM_STATUS 0)
    endif()
    file(REMOVE "${log}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "JH_COMPONENT_PYTHON=${fake_python}"
            "FAKE_LOG=${log}"
            "FAKE_MANAGER_STATUS=${RUN_MANAGER_STATUS}"
            "FAKE_SBOM_STATUS=${RUN_SBOM_STATUS}"
            "${BASH}" "${repo}/third_party/update_components.sh" ${RUN_ARGS}
        INPUT_FILE /dev/null
        RESULT_VARIABLE _status
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    set(_calls "")
    if(EXISTS "${log}")
        file(STRINGS "${log}" _calls)
    endif()
    set(status "${_status}" PARENT_SCOPE)
    set(output "${_out}${_err}" PARENT_SCOPE)
    set(calls "${_calls}" PARENT_SCOPE)
endfunction()

function(fail what)
    message(FATAL_ERROR "update_components.sh: ${what}\n"
        "status: ${status}\ncalls:\n${calls}\noutput:\n${output}")
endfunction()

# The launcher ran the component manager with arguments args, and then the SBOM
# generator exactly when refresh is TRUE.
function(expect_calls args refresh)
    list(LENGTH calls _count)
    if(_count EQUAL 0)
        fail("the component manager was not called")
    endif()
    list(GET calls 0 _first)
    if(NOT _first STREQUAL "${manager}${args}")
        fail("component manager called as '${_first}', expected '${manager}${args}'")
    endif()
    if(refresh)
        if(NOT _count EQUAL 2)
            fail("expected the component manager and then the SBOM generator")
        endif()
        list(GET calls 1 _second)
        string(FIND "${_second}" "${generator}" _at)
        if(NOT _at EQUAL 0)
            fail("SBOM generator called as '${_second}'")
        endif()
    elseif(NOT _count EQUAL 1)
        fail("expected only the component manager")
    endif()
endfunction()

function(expect_sbom content rewritten)
    file(READ "${sbom}" _actual)
    if(NOT _actual STREQUAL content)
        fail("SBOM holds '${_actual}', expected '${content}'")
    endif()
    file(TIMESTAMP "${sbom}" _mtime "%s" UTC)
    if(rewritten AND _mtime STREQUAL "${old_mtime}")
        fail("SBOM was not rewritten")
    elseif(NOT rewritten AND NOT _mtime STREQUAL "${old_mtime}")
        fail("SBOM was rewritten")
    endif()
endfunction()

# A stale SBOM is replaced; other arguments reach the component manager.
set_sbom("old\n")
run_launcher(ARGS --force)
if(NOT status EQUAL 0 OR NOT output MATCHES "Refreshed security/sbom.cdx.json")
    fail("stale SBOM was not refreshed")
endif()
expect_calls(" --force" TRUE)
expect_sbom("generated\n" TRUE)

# A current SBOM keeps its bytes and its mtime.
set_sbom("generated\n")
run_launcher()
if(NOT status EQUAL 0 OR NOT output MATCHES "SBOM is current")
    fail("current SBOM was not reported")
endif()
expect_calls("" TRUE)
expect_sbom("generated\n" FALSE)

# A missing SBOM is created.
set_sbom(MISSING)
run_launcher()
if(NOT status EQUAL 0)
    fail("missing SBOM was not created")
endif()
expect_sbom("generated\n" TRUE)

# Verification, help, prefixes argparse accepts, another repository root and
# --no-sbom never touch the SBOM; --no-sbom does not reach the component
# manager.
foreach(_case
        "--verify-only" "--verify" "--v" "--help" "-h" "--he"
        "--repo-root|${WORK_DIR}/other" "--no-sbom|--force")
    string(REPLACE "|" ";" _args "${_case}")
    list(FILTER _args EXCLUDE REGEX "^--no-sbom$")
    list(JOIN _args " " _expected)
    set_sbom("old\n")
    string(REPLACE "|" ";" _given "${_case}")
    run_launcher(ARGS ${_given})
    if(NOT status EQUAL 0)
        fail("${_case} failed")
    endif()
    expect_calls(" ${_expected}" FALSE)
    expect_sbom("old\n" FALSE)
endforeach()

# A failed synchronization keeps its exit status and leaves the SBOM alone.
set_sbom("old\n")
run_launcher(MANAGER_STATUS 3)
if(NOT status EQUAL 3)
    fail("component manager status 3 became ${status}")
endif()
expect_calls("" FALSE)
expect_sbom("old\n" FALSE)

# A failed SBOM generation fails the launcher and keeps the old file.
set_sbom("old\n")
run_launcher(SBOM_STATUS 5)
if(status EQUAL 0 OR NOT output MATCHES "the SBOM refresh failed")
    fail("failed SBOM generation was not reported")
endif()
expect_calls("" TRUE)
expect_sbom("old\n" FALSE)

# install_host_tools.sh (runmefirst.sh, Linux CI) passes --no-sbom. sudo and
# curl are stubs, and both scanners are installed at their pins, so the script
# installs nothing and reaches the launcher.
set(tools "${WORK_DIR}/host tools")
set(fake_bin "${WORK_DIR}/bin")
set(home "${WORK_DIR}/home")
set(osv_dir "${WORK_DIR}/osv")
set(launcher_log "${WORK_DIR}/launcher.log")
file(COPY "${JH_ROOT}/scripts/install_host_tools.sh" "${JH_ROOT}/scripts/scanner_pins.sh"
    DESTINATION "${tools}/scripts")
file(COPY "${JH_ROOT}/third_party/osv_scanner_version.conf"
    "${JH_ROOT}/third_party/cve_bin_tool_version.conf"
    DESTINATION "${tools}/third_party")
write_executable("${tools}/third_party/update_components.sh"
    "#!/bin/sh\nprintf '[%s]\\n' \"$*\" >> \"\${FAKE_LOG}\"\n")
write_executable("${tools}/scripts/run_sanitizer_fuzz.sh" "#!/bin/sh\n")
write_executable("${fake_bin}/sudo" "#!/bin/sh\n")
write_executable("${fake_bin}/curl" "#!/bin/sh\nexit 1\n")
file(STRINGS "${JH_ROOT}/third_party/osv_scanner_version.conf" _osv
    REGEX "^OSV_SCANNER_VERSION=")
file(STRINGS "${JH_ROOT}/third_party/cve_bin_tool_version.conf" _cve
    REGEX "^CVE_BIN_TOOL_VERSION=")
string(REPLACE "OSV_SCANNER_VERSION=" "" _osv "${_osv}")
string(REPLACE "CVE_BIN_TOOL_VERSION=" "" _cve "${_cve}")
write_executable("${osv_dir}/osv-scanner"
    "#!/bin/sh\necho 'osv-scanner version: ${_osv}'\n")
write_executable("${home}/.local/bin/cve-bin-tool" "#!/bin/sh\necho '${_cve}'\n")
# The source-archive check runs the scanner's own Python on a script from stdin.
write_executable("${home}/.local/bin/python" "#!/bin/sh\ncat >/dev/null\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${fake_bin}:/usr/bin:/bin"
        "HOME=${home}"
        "JH_OSV_SCANNER_DIR=${osv_dir}"
        "FAKE_LOG=${launcher_log}"
        "${BASH}" "${tools}/scripts/install_host_tools.sh"
    INPUT_FILE /dev/null
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE _err)
string(APPEND output "${_err}")
set(calls "")
if(EXISTS "${launcher_log}")
    file(STRINGS "${launcher_log}" calls)
endif()
# The final tool check may fail on this host; only the launcher call counts.
if(NOT calls STREQUAL "[--no-sbom]")
    message(FATAL_ERROR "install_host_tools.sh: update_components.sh called as "
        "'${calls}', expected '[--no-sbom]'\noutput:\n${output}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "update_components.sh SBOM refresh: OK")
