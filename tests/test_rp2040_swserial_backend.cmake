if(NOT DEFINED JH_ROOT)
    message(FATAL_ERROR "JH_ROOT is required")
endif()

set(_rp2040_impl "${JH_ROOT}/src/hal/impl/rp2040/hal_swserial.cpp")
set(_pio_program
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/swserial/swserial.pio.h")
set(_pio_source
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/swserial/swserial.pio")
set(_shared_impl
    "${JH_ROOT}/src/hal/serial/swserial/hal_swserial.cpp")

if(NOT EXISTS "${_rp2040_impl}")
    message(FATAL_ERROR
        "RP2040 SoftwareSerial backend is missing: ${_rp2040_impl}")
endif()

file(READ "${_rp2040_impl}" _rp2040_source)

# The RP2040 implementation must drive PIO through the Pico SDK directly.
if(NOT _rp2040_source MATCHES
       "#[ \t]*include[ \t]*[<\"]hardware/pio\\.h[>\"]")
    message(FATAL_ERROR
        "RP2040 SoftwareSerial must use the native Pico SDK hardware/pio.h")
endif()

if(NOT _rp2040_source MATCHES
       "#[ \t]*include[ \t]*[<\"]hardware/dma\\.h[>\"]")
    message(FATAL_ERROR
        "RP2040 SoftwareSerial must use native Pico SDK DMA for RX buffering")
endif()

if(NOT _rp2040_source MATCHES
       "#[ \t]*include[ \t]*[<\"]drivers/swserial/swserial\\.pio\\.h[>\"]")
    message(FATAL_ERROR
        "RP2040 SoftwareSerial must include the committed PIO program header")
endif()

foreach(_pio_file IN ITEMS "${_pio_program}" "${_pio_source}")
    if(NOT EXISTS "${_pio_file}")
        message(FATAL_ERROR
            "RP2040 SoftwareSerial PIO file is missing: ${_pio_file}")
    endif()
endforeach()

file(READ "${_pio_program}" _pio_header_source)
file(READ "${_pio_source}" _pio_asm_source)
foreach(_direction IN ITEMS rx tx)
    if(NOT _pio_header_source MATCHES "jh_swserial_${_direction}_program" OR
       NOT _pio_asm_source MATCHES "\\.program[ \t]+jh_swserial_${_direction}")
        message(FATAL_ERROR
            "RP2040 SoftwareSerial is missing its ${_direction} PIO program")
    endif()
endforeach()

if(NOT _rp2040_source MATCHES "pio_add_program[ \t\r\n]*\\(" OR
   NOT _rp2040_source MATCHES "pio_sm_[A-Za-z0-9_]+[ \t\r\n]*\\(")
    message(FATAL_ERROR
        "RP2040 SoftwareSerial does not configure a native PIO state machine")
endif()

if(NOT _rp2040_source MATCHES "dma_channel_configure[ \t\r\n]*\\(")
    message(FATAL_ERROR
        "RP2040 SoftwareSerial does not configure native DMA RX buffering")
endif()

# Sampling a complete byte from a GPIO callback was the source of nearly one
# millisecond high-priority ISR stalls at 9600 baud.  Arduino serial wrappers
# are also forbidden here: this backend must remain Pico-SDK-native.
foreach(_blocking_primitive IN ITEMS
        hal_gpio_attach_interrupt
        hal_delay_us
        busy_wait_us
        sleep_us
        hal_critical_section_enter
        critical_section_enter_blocking
        save_and_disable_interrupts
        taskENTER_CRITICAL
        portENTER_CRITICAL)
    if(_rp2040_source MATCHES
       "${_blocking_primitive}[ \t\r\n]*\\(")
        message(FATAL_ERROR
            "RP2040 SoftwareSerial contains blocking primitive: "
            "${_blocking_primitive}")
    endif()
endforeach()

foreach(_arduino_header IN ITEMS SoftwareSerial.h SerialPIO.h Arduino.h)
    string(FIND "${_rp2040_source}" "${_arduino_header}" _header_at)
    if(NOT _header_at EQUAL -1)
        message(FATAL_ERROR
            "RP2040 SoftwareSerial contains forbidden Arduino dependency: "
            "${_arduino_header}")
    endif()
endforeach()

# Also reject the wrapper classes if they were forward-declared or exposed by
# another include instead of their usual headers.
if(_rp2040_source MATCHES
   "(SoftwareSerial|SerialPIO)[ \t\r\n*&]+[A-Za-z_][A-Za-z0-9_]*[ \t\r\n]*[;({=]" OR
   _rp2040_source MATCHES "(SoftwareSerial|SerialPIO)[ \t\r\n]*\\(")
    message(FATAL_ERROR
        "RP2040 SoftwareSerial must not use Arduino serial wrapper classes")
endif()

if(NOT EXISTS "${_shared_impl}")
    message(FATAL_ERROR
        "Shared SoftwareSerial backend is missing: ${_shared_impl}")
endif()

file(READ "${_shared_impl}" _shared_source)

# Arduino library builds discover implementation files recursively, so CMake
# source-list filtering alone is insufficient: the portable GPIO driver must
# guard itself out for the complete RP family.
if(NOT _shared_source MATCHES
       "#[ \t]*if[^\r\n]*![ \t]*HAL_TARGET_IS_RP")
    message(FATAL_ERROR
        "Shared GPIO SoftwareSerial must be excluded for HAL_TARGET_IS_RP")
endif()
