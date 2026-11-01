# Valgrind settings of 'ctest -T memcheck' for the host test builds: the root
# project and the jh host CLI (tools/jh). Host-only dynamic analysis (NOT
# firmware - Valgrind cannot run on MCU targets). Run locally or in CI with:
#
#     ctest --test-dir .build/host -T memcheck --output-on-failure
#
# Include this before include(CTest), which bakes the variables into
# DartConfiguration.tcl. --error-exitcode=1 makes Valgrind fail the individual
# test (and therefore the whole ctest run) on a real defect, so the CI memcheck
# job goes red instead of silently passing.
find_program(MEMORYCHECK_COMMAND NAMES valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS
    "--leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --track-origins=yes --fair-sched=yes --error-exitcode=1"
    CACHE STRING "Flags passed to Valgrind by 'ctest -T memcheck'")
set(MEMORYCHECK_SUPPRESSIONS_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../tests/valgrind.supp"
    CACHE FILEPATH "Valgrind suppressions for known third-party/system noise")
