if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_calendar_source
    "${JH_ROOT}/src/hal/impl/shared/time/jh_calendar.c")
set(_calendar_header
    "${JH_ROOT}/src/hal/impl/shared/time/jh_calendar.h")
set(_public_time "${JH_ROOT}/src/hal/hal_time.cpp")

foreach(_required IN ITEMS
        "${_calendar_source}"
        "${_calendar_header}"
        "${_public_time}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Shared calendar source is missing: ${_required}")
    endif()
endforeach()

set(_consumers
    "${_public_time}"
    "${JH_ROOT}/src/hal/impl/.mock/hal_rtc.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_rtc.cpp"
    "${JH_ROOT}/src/hal/impl/stm32g474/hal_rtc.cpp"
    "${JH_ROOT}/src/hal/impl/shared/drivers/pcf8563/pcf8563.cpp"
    "${JH_ROOT}/src/hal/impl/shared/drivers/ds3231/ds3231.cpp")

foreach(_consumer IN LISTS _consumers)
    file(READ "${_consumer}" _contents)
    if(NOT _contents MATCHES "jh_calendar_[a-z_]+")
        message(FATAL_ERROR
            "Calendar consumer bypasses jh_calendar: ${_consumer}")
    endif()
endforeach()

set(_tools_source "${JH_ROOT}/src/utils/tools.cpp")
file(READ "${_tools_source}" _tools_contents)
set(_tools_wrapper_patterns
    "bool[ \t\r\n]+isDaylightSavingTime\\([^)]*\\)[ \t\r\n]*\\{[ \t\r\n]*return[ \t\r\n]+hal_time_is_daylight_saving_time\\([^;]*;[ \t\r\n]*\\}"
    "void[ \t\r\n]+adjustTime\\([^)]*\\)[ \t\r\n]*\\{[ \t\r\n]*hal_time_adjust_cet_cest\\([^;]*;[ \t\r\n]*\\}"
    "bool[ \t\r\n]+is_time_in_range\\([^)]*\\)[ \t\r\n]*\\{[ \t\r\n]*return[ \t\r\n]+hal_time_is_in_range\\([^;]*;[ \t\r\n]*\\}"
    "void[ \t\r\n]+extract_time\\([^)]*\\)[ \t\r\n]*\\{[ \t\r\n]*hal_time_extract_minutes\\([^;]*;[ \t\r\n]*\\}")
foreach(_pattern IN LISTS _tools_wrapper_patterns)
    if(NOT _tools_contents MATCHES "${_pattern}")
        message(FATAL_ERROR
            "Legacy tools time helper is not a HAL-only wrapper")
    endif()
endforeach()

set(_hal_umbrella "${JH_ROOT}/src/hal/hal.h")
file(READ "${_hal_umbrella}" _hal_umbrella_contents)
if(NOT _hal_umbrella_contents MATCHES
   "#[ \t]*include[ \t]*\"hal_time\\.h\"")
    message(FATAL_ERROR "hal.h does not expose the unconditional time helpers")
endif()
if(_hal_umbrella_contents MATCHES
   "#[ \t]*ifdef[ \t]+HAL_ENABLE_TIME[\t\r\n ]+#[ \t]*include[ \t]*\"hal_time\\.h\"")
    message(FATAL_ERROR
        "hal.h hides unconditional time helpers behind HAL_ENABLE_TIME")
endif()

set(_target_time_sources
    "${JH_ROOT}/src/hal/impl/.mock/hal_time.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_time.cpp"
    "${JH_ROOT}/src/hal/impl/stm32g474/hal_time.cpp")
foreach(_source IN LISTS _target_time_sources)
    file(READ "${_source}" _contents)
    if(_contents MATCHES "hal_time_from_components[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "Target-local hal_time_from_components copy returned: ${_source}")
    endif()
endforeach()

foreach(_consumer IN LISTS _consumers)
    file(READ "${_consumer}" _contents)
    if(_contents MATCHES
       "rtc_is_leap_year|rtc_days_in_month|date2days|daysInMonth|days_before_month|%[ \t]*400u?|%[ \t]*100u?")
        message(FATAL_ERROR
            "Target/driver-local calendar algorithm returned: ${_consumer}")
    endif()
endforeach()
