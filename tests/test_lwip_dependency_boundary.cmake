set(_lwip "${JH_ROOT}/third_party/lwip")

if(NOT EXISTS "${_lwip}/src/include/lwip/init.h" OR
   NOT EXISTS "${_lwip}/src/core/init.c")
    message(FATAL_ERROR
        "Pinned third_party/lwip checkout is missing")
endif()

set(_makefsdata
    "${_lwip}/src/apps/http/makefsdata/makefsdata.c")
if(NOT EXISTS "${_makefsdata}")
    message(FATAL_ERROR "Pinned lwIP makefsdata source is missing")
endif()

file(READ "${JH_ROOT}/cmake/jh_cyw43_driver.cmake" _cmake_helper)
if(NOT _cmake_helper MATCHES "third_party/lwip")
    message(FATAL_ERROR "STM32 lwIP source selection bypasses the pinned boundary")
endif()
if(NOT _cmake_helper MATCHES "src/core/init\\.c")
    message(FATAL_ERROR "STM32 lwIP sources are not staged explicitly")
endif()
