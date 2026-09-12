if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

file(GLOB_RECURSE _jh_source_files
    "${JH_ROOT}/src/*.c"
    "${JH_ROOT}/src/*.cc"
    "${JH_ROOT}/src/*.cpp"
    "${JH_ROOT}/src/*.h"
    "${JH_ROOT}/src/*.hpp"
    "${JH_ROOT}/tests/*.c"
    "${JH_ROOT}/tests/*.cc"
    "${JH_ROOT}/tests/*.cpp"
    "${JH_ROOT}/tests/*.h"
    "${JH_ROOT}/tests/*.hpp"
    "${JH_ROOT}/tests/*.cmake"
    "${JH_ROOT}/tests/*.py"
    "${JH_ROOT}/examples/*.c"
    "${JH_ROOT}/examples/*.cc"
    "${JH_ROOT}/examples/*.cpp"
    "${JH_ROOT}/examples/*.h"
    "${JH_ROOT}/examples/*.hpp")

set(_allowed_files
    "${JH_ROOT}/src/hal/core/hal_compiler.h"
    "${JH_ROOT}/src/hal/impl/stm32g474/port/atomic_stubs_cm4.c"
    "${JH_ROOT}/tests/test_atomic_abstraction.cmake")

foreach(_source_file IN LISTS _jh_source_files)
    list(FIND _allowed_files "${_source_file}" _allowed_index)
    if(NOT _allowed_index EQUAL -1)
        continue()
    endif()

    file(READ "${_source_file}" _source_text)
    string(REGEX MATCH "__atomic_[A-Za-z0-9_]+|__ATOMIC_[A-Z0-9_]+"
        _raw_atomic "${_source_text}")
    if(_raw_atomic)
        file(RELATIVE_PATH _relative_source "${JH_ROOT}" "${_source_file}")
        message(FATAL_ERROR
            "${_relative_source} bypasses hal_compiler.h with ${_raw_atomic}")
    endif()
endforeach()
