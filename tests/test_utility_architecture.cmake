if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_removed_tools_source "${JH_ROOT}/src/utils/tools.cpp")
if(EXISTS "${_removed_tools_source}")
    message(FATAL_ERROR "Legacy tools.cpp returned")
endif()

set(_utility_sources
    "hal/analog/hal_adc_utils.cpp"
    "hal/codecs/hal_image.cpp"
    "hal/core/hal_math.cpp"
    "hal/core/hal_text.cpp"
    "hal/core/jh_endian.cpp"
    "hal/display/hal_pixel.cpp"
    "hal/gps/hal_gps_nmea_utils.cpp"
    "hal/network/hal_network_utils.cpp"
    "hal/system/hal_periodic_random.cpp"
    "hal/temperature/hal_ntc.cpp")

set(_utility_headers
    "hal/analog/hal_adc_utils.h"
    "hal/codecs/hal_image.h"
    "hal/core/hal_array.h"
    "hal/core/hal_math.h"
    "hal/core/hal_text.h"
    "hal/core/jh_endian.h"
    "hal/display/hal_pixel.h"
    "hal/gps/hal_gps_nmea_utils.h"
    "hal/network/hal_network_utils.h"
    "hal/system/hal_periodic_random.h"
    "hal/temperature/hal_ntc.h"
    "hal/time/hal_time.h")

file(READ "${JH_ROOT}/CMakeLists.txt" _host_manifest)
file(READ "${JH_ROOT}/scripts/build_esp_idf.py" _esp_manifest)
file(READ "${JH_ROOT}/cmake/jh_rp_hal_sources.cmake" _rp_manifest)
file(READ "${JH_ROOT}/stm32_lib/jh_stm32g474_firmware.cmake" _stm_manifest)
file(READ "${JH_ROOT}/src/hal/hal.h" _hal_umbrella)

foreach(_source IN LISTS _utility_sources)
    if(NOT EXISTS "${JH_ROOT}/src/${_source}")
        message(FATAL_ERROR "Thematic utility source is missing: ${_source}")
    endif()
    if(NOT _host_manifest MATCHES "${_source}")
        message(FATAL_ERROR "Host source inventory omits ${_source}")
    endif()
endforeach()

foreach(_source IN LISTS _utility_sources)
    if(_source STREQUAL "hal/gps/hal_gps_nmea_utils.cpp")
        continue()
    endif()
    if(NOT _esp_manifest MATCHES "src/${_source}")
        message(FATAL_ERROR "ESP-IDF source inventory omits ${_source}")
    endif()
endforeach()

if(NOT _rp_manifest MATCHES
   "file\\(GLOB_RECURSE _common_sources CONFIGURE_DEPENDS")
    message(FATAL_ERROR "RP source inventory no longer collects thematic HAL sources")
endif()
if(NOT _stm_manifest MATCHES
   "file\\(GLOB_RECURSE _hal_common CONFIGURE_DEPENDS")
    message(FATAL_ERROR "STM32 source inventory no longer collects thematic HAL sources")
endif()

foreach(_header IN LISTS _utility_headers)
    if(NOT EXISTS "${JH_ROOT}/src/${_header}")
        message(FATAL_ERROR "Thematic utility header is missing: ${_header}")
    endif()
    if(NOT _hal_umbrella MATCHES "${_header}")
        message(FATAL_ERROR "hal.h does not expose ${_header}")
    endif()
endforeach()

file(READ "${JH_ROOT}/src/hal/serial/hal_serial.h" _serial_header)
foreach(_debug_name IN ITEMS
        hal_debug_init_default
        hal_debug_set_module_prefix
        deb
        derr)
    if(NOT _serial_header MATCHES "${_debug_name}")
        message(FATAL_ERROR "Public debug API lost ${_debug_name}")
    endif()
endforeach()

file(READ "${JH_ROOT}/src/utils/tools_api.h" _tools_api_header)
if(_tools_api_header MATCHES
   "[A-Za-z_][A-Za-z0-9_]*[ \\t\\r\\n]*\\([^)]*\\)[ \\t\\r\\n]*;")
    message(FATAL_ERROR "tools_api.h must remain an include-only aggregator")
endif()
