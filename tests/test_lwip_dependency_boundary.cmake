set(_lwip "${JH_ROOT}/src/hal/impl/shared/frameworks/lwip")

file(GLOB_RECURSE _implicit_sources "${_lwip}/vendor/*.c")
if(_implicit_sources)
    message(FATAL_ERROR
        "Vendored lwIP .c files would be compiled implicitly by Arduino: ${_implicit_sources}")
endif()

set(_makefsdata
    "${_lwip}/vendor/src/apps/http/makefsdata/makefsdata.c.upstream")
if(NOT EXISTS "${_makefsdata}")
    message(FATAL_ERROR "Pinned lwIP makefsdata source is missing")
endif()

file(READ "${JH_ROOT}/cmake/jh_cyw43_driver.cmake" _cmake_helper)
if(NOT _cmake_helper MATCHES "vendor/src/core/init\\.c\\.upstream")
    message(FATAL_ERROR "STM32 lwIP source selection bypasses the pinned boundary")
endif()
if(NOT _cmake_helper MATCHES "configure_file\\(.*COPYONLY")
    message(FATAL_ERROR "STM32 lwIP sources are not staged explicitly")
endif()
