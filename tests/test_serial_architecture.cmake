if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_core "${JH_ROOT}/src/hal/serial/hal_serial.cpp")
set(_port_header
    "${JH_ROOT}/src/hal/debug/jh_serial_port.h")
set(_ports
    "${JH_ROOT}/src/hal/impl/.mock/hal_serial.cpp"
    "${JH_ROOT}/src/hal/impl/esp32/hal_serial.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_serial.cpp"
    "${JH_ROOT}/src/hal/impl/stm32g474/hal_serial.cpp")
set(_stm32_uart
    "${JH_ROOT}/src/hal/impl/stm32g474/port/g474_debug_uart.c")

foreach(_required IN ITEMS "${_core}" "${_port_header}" ${_ports}
                           "${_stm32_uart}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Serial core/port source is missing: ${_required}")
    endif()
endforeach()

file(READ "${_core}" _core_contents)
foreach(_operation IN ITEMS
        hal_serial_begin
        hal_serial_set_flush
        hal_serial_print
        hal_serial_println
        hal_serial_available
        hal_serial_read
        hal_debug_init
        hal_debug_set_muted
        hal_deb_set_prefix
        hal_deb
        hal_derr
        hal_derr_limited
        hal_deb_hex
        hal_debug_loop)
    if(NOT _core_contents MATCHES "${_operation}[ \t\r\n]*\\(")
        message(FATAL_ERROR "Shared serial/debug core lost ${_operation}")
    endif()
endforeach()

foreach(_required_mechanism IN ITEMS
        jh_hal_mutex_create_once
        hal_debug_format_write_deb_prefix
        hal_debug_format_write_error_prefix
        isr_ring_push
        s_error_slots
        hal_net_console_write_from_serial
        jh_serial_port_message_begin
        jh_serial_port_finish_line)
    if(NOT _core_contents MATCHES "${_required_mechanism}")
        message(FATAL_ERROR
            "Shared serial/debug mechanism is missing: ${_required_mechanism}")
    endif()
endforeach()

if(_core_contents MATCHES
   "#[ \t]*include[^\r\n]*(hal_usb|g474_debug_uart|pico/|hardware/)")
    message(FATAL_ERROR "Shared serial/debug core gained transport coupling")
endif()

foreach(_port IN LISTS _ports)
    file(READ "${_port}" _port_contents)
    if(NOT _port_contents MATCHES "jh_serial_port.h")
        message(FATAL_ERROR "Serial transport bypasses the port contract: ${_port}")
    endif()
    foreach(_port_operation IN ITEMS
            jh_serial_port_begin
            jh_serial_port_set_flush
            jh_serial_port_message_begin
            jh_serial_port_write
            jh_serial_port_finish_line
            jh_serial_port_flush
            jh_serial_port_available
            jh_serial_port_read)
        if(NOT _port_contents MATCHES
           "${_port_operation}[ \t\r\n]*\\(")
            message(FATAL_ERROR
                "Serial transport port is incomplete (${_port_operation}): ${_port}")
        endif()
    endforeach()

    if(_port_contents MATCHES
       "hal_debug_format|hal_net_console|hal_debug_loop[ \t\r\n]*\\(|hal_deb[ \t\r\n]*\\(|hal_derr[ \t\r\n]*\\(|hal_error_slot|isr_ring_|s_error_slots|s_rate_limit_cfg")
        message(FATAL_ERROR
            "Target-local serial/debug core logic returned: ${_port}")
    endif()
endforeach()

file(READ "${JH_ROOT}/src/hal/impl/stm32g474/hal_serial.cpp"
     _stm32_port_contents)
file(READ "${_stm32_uart}" _stm32_uart_contents)
if(NOT _stm32_port_contents MATCHES
   "g474_debug_uart_flush[ \t\r\n]*\\(" OR
   NOT _stm32_uart_contents MATCHES "USART_ISR_TC")
    message(FATAL_ERROR
        "STM32 serial flush must wait for physical USART transmission")
endif()

file(GLOB_RECURSE _serial_sources
    "${JH_ROOT}/src/hal/serial/hal_serial.cpp"
    "${JH_ROOT}/src/hal/impl/*/hal_serial.cpp")
list(LENGTH _serial_sources _serial_source_count)
if(NOT _serial_source_count EQUAL 5)
    message(FATAL_ERROR
        "Expected one shared serial core and four target ports; found ${_serial_source_count}")
endif()

file(READ "${JH_ROOT}/CMakeLists.txt" _root_cmake)
file(READ "${JH_ROOT}/stm32_lib/CMakeLists.txt" _stm32_cmake)
if(NOT _root_cmake MATCHES "hal/serial/hal_serial\\.cpp" OR
   NOT _stm32_cmake MATCHES "hal/\\*\\.cpp")
    message(FATAL_ERROR
        "Shared serial/debug core is missing from a source manifest")
endif()
