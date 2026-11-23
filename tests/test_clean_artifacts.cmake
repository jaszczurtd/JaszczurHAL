# runalltests.sh --clean-artifacts removes what the gate and the test projects
# leave, and nothing later runs reuse. It runs on a copy of the script in a
# scratch repository with a scratch cache, so the real trees stay untouched.
# Arguments: JH_ROOT (this checkout), JH_TEST_BINARY_DIR (scratch parent).
if(NOT IS_DIRECTORY "${JH_ROOT}" OR NOT IS_DIRECTORY "${JH_TEST_BINARY_DIR}")
    message(FATAL_ERROR "JH_ROOT and JH_TEST_BINARY_DIR are required")
endif()
find_program(_git NAMES git REQUIRED)
find_program(_bash NAMES bash REQUIRED)

set(_work "${JH_TEST_BINARY_DIR}/clean_artifacts")
set(_repo "${_work}/repo")
set(_cache "${_work}/cache")
set(_gates "${_cache}/jaszczurhal/commit-gate")
file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_repo}")
file(COPY "${JH_ROOT}/runalltests.sh" DESTINATION "${_repo}" USE_SOURCE_PERMISSIONS)
execute_process(COMMAND "${_git}" init -q "${_repo}" RESULT_VARIABLE _code)
if(NOT _code EQUAL 0)
    message(FATAL_ERROR "git init failed: ${_code}")
endif()

set(_removed
    "${_repo}/.build/gate/host/CMakeCache.txt"
    "${_repo}/.build/examples/01_core_runtime/firmware.elf"
    "${_repo}/.build/tests/ram-code/probe.o"
    "${_repo}/.build/python-cache/probe.pyc"
    "${_repo}/.build/compiler-cache/host.log"
    "${_repo}/tests/fixtures/esp32_gamepad/.build/esp-idf/build.log"
    "${_repo}/tests/hardware/esp32s3_ota/.build/audit/build.log"
    # The repository copy of this checkout is gone.
    "${_gates}/aaaaaaaaaaaa/.build/gate/host/CMakeCache.txt"
    # Not a checkout at all.
    "${_gates}/bbbbbbbbbbbb/runalltests.sh")
set(_kept
    "${_repo}/.build/tools/cppcheck/cppcheck"
    "${_repo}/.build/host-tools/linux/0123/bin/jh"
    "${_repo}/.build/host/CMakeCache.txt"
    "${_repo}/tests/fixtures/esp32_gamepad/main/app.c"
    # Checkouts of existing repository copies, by absolute and relative gitdir.
    "${_gates}/cccccccccccc/.build/gate/host/CMakeCache.txt"
    "${_gates}/dddddddddddd/.build/gate/host/CMakeCache.txt"
    "${_cache}/jaszczurhal/ccache/probe")
foreach(_file IN LISTS _removed _kept)
    file(WRITE "${_file}" "probe\n")
endforeach()
file(WRITE "${_gates}/aaaaaaaaaaaa/.git" "gitdir: ${_work}/deleted copy/.git/worktrees/aaaaaaaaaaaa\n")
file(MAKE_DIRECTORY "${_work}/copy/.git/worktrees/cccccccccccc")
file(WRITE "${_gates}/cccccccccccc/.git" "gitdir: ${_work}/copy/.git/worktrees/cccccccccccc\n")
file(MAKE_DIRECTORY "${_work}/relative copy/.git/worktrees/dddddddddddd")
file(WRITE "${_gates}/dddddddddddd/.git"
    "gitdir: ../../../../relative copy/.git/worktrees/dddddddddddd\n")
# A worktree registration of the scratch repository whose checkout is gone.
set(_stale "${_repo}/.git/worktrees/stale")
file(WRITE "${_stale}/gitdir" "${_work}/deleted checkout/.git\n")

function(run_clean expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "XDG_CACHE_HOME=${_cache}"
            "${_bash}" "${_repo}/runalltests.sh" ${ARGN}
        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _code EQUAL expected)
        message(FATAL_ERROR "runalltests.sh ${ARGN}: expected exit ${expected}, got ${_code}\n${_out}\n${_err}")
    endif()
endfunction()

# Combined with stages it refuses before removing anything.
run_clean(1 --clean-artifacts --stage host)
foreach(_file IN LISTS _removed _kept)
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "A refused --clean-artifacts removed ${_file}")
    endif()
endforeach()

run_clean(0 --clean-artifacts)
foreach(_file IN LISTS _removed)
    if(EXISTS "${_file}")
        message(FATAL_ERROR "--clean-artifacts left ${_file}")
    endif()
endforeach()
foreach(_file IN LISTS _kept)
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "--clean-artifacts removed ${_file}, which later runs reuse")
    endif()
endforeach()
foreach(_dir IN ITEMS "${_gates}/aaaaaaaaaaaa" "${_gates}/bbbbbbbbbbbb"
        "${_repo}/tests/fixtures/esp32_gamepad/.build" "${_stale}")
    if(EXISTS "${_dir}")
        message(FATAL_ERROR "--clean-artifacts left ${_dir}")
    endif()
endforeach()
file(REMOVE_RECURSE "${_work}")
